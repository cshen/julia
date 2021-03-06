/*
  array constructors and primitives
*/
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "julia.h"

// array constructors ---------------------------------------------------------

static jl_array_t *_new_array(jl_type_t *atype,
                              uint32_t ndims, size_t *dims)
{
    size_t i, tot, nel=1;
    int isunboxed=0, elsz;
    void *data;
    jl_array_t *a;

    for(i=0; i < ndims; i++) {
        nel *= dims[i];
    }
    jl_type_t *el_type = (jl_type_t*)jl_tparam0(atype);

    isunboxed = jl_is_bits_type(el_type);
    if (isunboxed) {
        elsz = jl_bitstype_nbits(el_type)/8;
        tot = elsz * nel;
        if (elsz == 1) {
            // hidden 0 terminator for all byte arrays
            tot++;
        }
    }
    else {
        elsz = sizeof(void*);
        tot = sizeof(void*) * nel;
    }

    int ndimwords = (ndims > 2 ? (ndims-2) : 0);
#ifndef __LP64__
    // on 32-bit, ndimwords must be odd to preserve 8-byte alignment
    ndimwords += (~ndimwords)&1;
#endif
    if (tot <= ARRAY_INLINE_NBYTES) {
        a = allocobj(sizeof(jl_array_t) + tot + (ndimwords-1)*sizeof(size_t));
        a->type = atype;
        data = (&a->_space[0] + ndimwords*sizeof(size_t));
        if (tot > 0 && !isunboxed) {
            memset(data, 0, tot);
        }
    }
    else {
        a = allocobj(sizeof(jl_array_t) + (ndimwords-1)*sizeof(size_t));
        JL_GC_PUSH(&a);
        a->type = atype;
        // temporarily initialize to make gc-safe
        a->data = NULL;
        a->length = 0;
        a->reshaped = 0;
        data = allocb(tot);
        if (!isunboxed)
            memset(data, 0, tot);
        JL_GC_POP();
    }

    a->data = data;
    if (elsz == 1) ((char*)data)[tot-1] = '\0';
    a->length = nel;
    a->ndims = ndims;
    a->reshaped = 0;
    a->elsize = elsz;
    if (ndims == 1) {
        a->nrows = nel;
        a->maxsize = nel;
        a->offset = 0;
    }
    else {
        size_t *adims = &a->nrows;
        for(i=0; i < ndims; i++)
            adims[i] = dims[i];
    }
    
    return a;
}

jl_array_t *jl_reshape_array(jl_type_t *atype, jl_array_t *data,
                             jl_tuple_t *dims)
{
    size_t i;
    jl_array_t *a;
    size_t ndims = dims->length;

    int ndimwords = (ndims > 2 ? (ndims-2) : 0);
#ifndef __LP64__
    // on 32-bit, ndimwords must be odd to preserve 8-byte alignment
    ndimwords += (~ndimwords)&1;
#endif
    a = allocobj(sizeof(jl_array_t) + ndimwords*sizeof(size_t));
    a->type = atype;
    *((jl_array_t**)(&a->_space[0] + ndimwords*sizeof(size_t))) = data;
    a->data = data->data;
    a->length = data->length;
    a->elsize = data->elsize;
    a->ndims = ndims;
    a->reshaped = 1;

    if (ndims == 1) {
        a->nrows = a->length;
        a->maxsize = a->length;
        a->offset = 0;
    }
    else {
        size_t *adims = &a->nrows;
        for(i=0; i < ndims; i++) {
            adims[i] = jl_unbox_long(jl_tupleref(dims, i));
        }
    }
    
    return a;
}

jl_array_t *jl_new_array_(jl_type_t *atype, uint32_t ndims, size_t *dims)
{
    return _new_array(atype, ndims, dims);
}

jl_array_t *jl_new_array(jl_type_t *atype, jl_tuple_t *dims)
{
    size_t ndims = dims->length;
    size_t *adims = alloca(ndims*sizeof(size_t));
    size_t i;
    for(i=0; i < ndims; i++)
        adims[i] = jl_unbox_long(jl_tupleref(dims,i));
    return _new_array(atype, ndims, adims);
}

jl_array_t *jl_alloc_array_1d(jl_type_t *atype, size_t nr)
{
    return _new_array(atype, 1, &nr);
}

jl_array_t *jl_alloc_array_2d(jl_type_t *atype, size_t nr, size_t nc)
{
    size_t d[2] = {nr, nc};
    return _new_array(atype, 2, &d[0]);
}

jl_array_t *jl_alloc_array_3d(jl_type_t *atype, size_t nr, size_t nc, size_t z)
{
    size_t d[3] = {nr, nc, z};
    return _new_array(atype, 3, &d[0]);
}

jl_array_t *jl_pchar_to_array(char *str, size_t len)
{
    jl_array_t *a = jl_alloc_array_1d(jl_array_uint8_type, len);
    memcpy(a->data, str, len);
    return a;
}

jl_value_t *jl_array_to_string(jl_array_t *a)
{
    // TODO: check type of array?
    jl_struct_type_t* string_type = u8_isvalid(a->data, a->length) == 1 ? // ASCII
        jl_ascii_string_type : jl_utf8_string_type;
    return jl_apply((jl_function_t*)string_type, (jl_value_t**)&a, 1);
}

jl_value_t *jl_pchar_to_string(char *str, size_t len)
{
    jl_array_t *a = jl_pchar_to_array(str, len);
    JL_GC_PUSH(&a);
    jl_value_t *s = jl_array_to_string(a);
    JL_GC_POP();
    return s;
}

jl_value_t *jl_cstr_to_string(char *str)
{
    return jl_pchar_to_string(str, strlen(str));
}

jl_array_t *jl_alloc_cell_1d(size_t n)
{
    return jl_alloc_array_1d(jl_array_any_type, n);
}

// array primitives -----------------------------------------------------------

JL_CALLABLE(jl_f_arraylen)
{
    JL_NARGS(arraylen, 1, 1);
    JL_TYPECHK(arraylen, array, args[0]);
    return jl_box_long(((jl_array_t*)args[0])->length);
}

JL_CALLABLE(jl_f_arraysize)
{
    JL_TYPECHK(arraysize, array, args[0]);
    jl_array_t *a = (jl_array_t*)args[0];
    size_t nd = jl_array_ndims(a);
    if (nargs == 2) {
        JL_TYPECHK(arraysize, long, args[1]);
        int dno = jl_unbox_long(args[1]);
        if (dno < 1)
            jl_error("arraysize: dimension out of range");
        if (dno > nd)
            return jl_box_long(1);
        return jl_box_long((&a->nrows)[dno-1]);
    }
    else {
        JL_NARGS(arraysize, 1, 1);
    }
    jl_tuple_t *d = jl_alloc_tuple(nd);
    JL_GC_PUSH(&d);
    size_t i;
    for(i=0; i < nd; i++)
        jl_tupleset(d, i, jl_box_long(jl_array_dim(a,i)));
    JL_GC_POP();
    return (jl_value_t*)d;
}

static jl_value_t *new_scalar(jl_bits_type_t *bt)
{
    size_t nb = jl_bitstype_nbits(bt)/8;
    jl_value_t *v = 
        (jl_value_t*)allocobj((NWORDS(LLT_ALIGN(nb,sizeof(void*)))+1)*
                              sizeof(void*));
    v->type = (jl_type_t*)bt;
    return v;
}

typedef struct {
    int64_t a;
    int64_t b;
} bits128_t;

jl_value_t *jl_arrayref(jl_array_t *a, size_t i)
{
    jl_type_t *el_type = (jl_type_t*)jl_tparam0(jl_typeof(a));
    jl_value_t *elt;
    if (jl_is_bits_type(el_type)) {
        if (el_type == (jl_type_t*)jl_bool_type) {
            if (((int8_t*)a->data)[i] != 0)
                return jl_true;
            return jl_false;
        }
        elt = new_scalar((jl_bits_type_t*)el_type);
        size_t nb = a->elsize;
        switch (nb) {
        case 1:
            *(int8_t*)jl_bits_data(elt)  = ((int8_t*)a->data)[i];  break;
        case 2:
            *(int16_t*)jl_bits_data(elt) = ((int16_t*)a->data)[i]; break;
        case 4:
            *(int32_t*)jl_bits_data(elt) = ((int32_t*)a->data)[i]; break;
        case 8:
            *(int64_t*)jl_bits_data(elt) = ((int64_t*)a->data)[i]; break;
        case 16:
            *(bits128_t*)jl_bits_data(elt) = ((bits128_t*)a->data)[i]; break;
        default:
            memcpy(jl_bits_data(elt), &((char*)a->data)[i*nb], nb);
        }
    }
    else {
        elt = ((jl_value_t**)a->data)[i];
        if (elt == NULL) {
            jl_undef_ref_error();
        }
    }
    return elt;
}

JL_CALLABLE(jl_f_arrayref)
{
    JL_NARGS(arrayref, 2, 2);
    JL_TYPECHK(arrayref, array, args[0]);
    JL_TYPECHK(arrayref, long, args[1]);
    jl_array_t *a = (jl_array_t*)args[0];
    size_t i = jl_unbox_long(args[1])-1;
    if (i >= a->length) {
        jl_errorf("ref array[%d]: index out of range", i+1);
    }
    return jl_arrayref(a, i);
}

void jl_arrayset(jl_array_t *a, size_t i, jl_value_t *rhs)
{
    jl_value_t *el_type = jl_tparam0(jl_typeof(a));
    if (el_type != (jl_value_t*)jl_any_type) {
        if (!jl_subtype(rhs, el_type, 1))
            jl_type_error("arrayset", el_type, rhs);
    }
    if (jl_is_bits_type(el_type)) {
        size_t nb = a->elsize;
        switch (nb) {
        case 1:
            ((int8_t*)a->data)[i]  = *(int8_t*)jl_bits_data(rhs);  break;
        case 2:
            ((int16_t*)a->data)[i] = *(int16_t*)jl_bits_data(rhs); break;
        case 4:
            ((int32_t*)a->data)[i] = *(int32_t*)jl_bits_data(rhs); break;
        case 8:
            ((int64_t*)a->data)[i] = *(int64_t*)jl_bits_data(rhs); break;
        case 16:
            ((bits128_t*)a->data)[i] = *(bits128_t*)jl_bits_data(rhs); break;
        default:
            memcpy(&((char*)a->data)[i*nb], jl_bits_data(rhs), nb);
        }
    }
    else {
        ((jl_value_t**)a->data)[i] = rhs;
    }
}

JL_CALLABLE(jl_f_arrayset)
{
    JL_NARGS(arrayset, 3, 3);
    JL_TYPECHK(arrayset, array, args[0]);
    JL_TYPECHK(arrayset, long, args[1]);
    jl_array_t *b = (jl_array_t*)args[0];
    size_t i = jl_unbox_long(args[1])-1;
    if (i >= b->length) {
        jl_errorf("assign array[%d]: index out of range", i+1);
    }
    jl_arrayset(b, i, args[2]);
    return args[0];
}

static void *array_new_buffer(jl_array_t *a, size_t newlen)
{
    size_t nbytes = newlen * a->elsize;
    if (a->elsize == 1) {
        nbytes++;
    }
    int isunboxed = jl_is_bits_type(jl_tparam0(jl_typeof(a)));
    char *newdata = allocb(nbytes);
    if (!isunboxed)
        memset(newdata, 0, nbytes);
    if (a->elsize == 1) newdata[nbytes-1] = '\0';
    return newdata;
}

void jl_array_grow_end(jl_array_t *a, size_t inc)
{
    // optimized for the case of only growing and shrinking at the end
    size_t alen = a->length;
    if ((alen + inc) > a->maxsize - a->offset) {
        size_t newlen = a->maxsize==0 ? (inc<4?4:inc) : a->maxsize*2;
        while ((alen + inc) > newlen - a->offset)
            newlen *= 2;
        char *newdata = array_new_buffer(a, newlen);
        size_t es = a->elsize;
        newdata += (a->offset*es);
        size_t anb = alen*es;
        memcpy(newdata, (char*)a->data, anb);
        if (es == 1) {
            memset(newdata + anb, 0, (newlen-a->offset-alen)*es);
        }
        a->maxsize = newlen;
        a->data = newdata;
    }
    a->length += inc; a->nrows += inc;
}

void jl_array_del_end(jl_array_t *a, size_t dec)
{
    if (dec > a->length)
        jl_error("array_del_end: index out of range");
    memset((char*)a->data + (a->length-dec)*a->elsize, 0, dec*a->elsize);
    a->length -= dec; a->nrows -= dec;
}

void jl_array_grow_beg(jl_array_t *a, size_t inc)
{
    // designed to handle the case of growing and shrinking at both ends
    if (inc == 0)
        return;
    size_t es = a->elsize;
    size_t nb = inc*es;
    if (a->offset >= inc) {
        a->data = (char*)a->data - nb;
        a->offset -= inc;
    }
    else {
        size_t alen = a->length;
        size_t anb = alen*es;
        char *newdata;
        if (inc > (a->maxsize-alen)/2 - (a->maxsize-alen)/20) {
            size_t newlen = a->maxsize==0 ? 2*inc : a->maxsize*2;
            while (alen+2*inc > newlen-a->offset)
                newlen *= 2;
            newdata = array_new_buffer(a, newlen);
            size_t center = (newlen - (alen + inc))/2;
            newdata += (center*es);
            a->maxsize = newlen;
            a->offset = center;
        }
        else {
            size_t center = (a->maxsize - (alen + inc))/2;
            newdata = (char*)a->data - es*a->offset + es*center;
            a->offset = center;
        }
        memmove(&newdata[nb], a->data, anb);
        a->data = newdata;
    }
    a->length += inc; a->nrows += inc;
}

void jl_array_del_beg(jl_array_t *a, size_t dec)
{
    if (dec == 0)
        return;
    if (dec > a->length)
        jl_error("array_del_beg: index out of range");
    size_t es = a->elsize;
    size_t nb = dec*es;
    memset(a->data, 0, nb);
    size_t offset = a->offset;
    offset += dec;
    a->data = (char*)a->data + nb;
    a->length -= dec; a->nrows -= dec;

    // make sure offset doesn't grow forever due to deleting at beginning
    // and growing at end
    size_t newoffs = offset;
    if (offset >= 13*a->maxsize/20) {
        newoffs = 17*(a->maxsize - a->length)/100;
    }
#ifdef __LP64__
    while (newoffs > (size_t)((uint32_t)-1)) {
        newoffs = newoffs/2;
    }
#endif
    if (newoffs != offset) {
        size_t anb = a->length*es;
        size_t delta = (offset - newoffs)*es;
        a->data = (char*)a->data - delta;
        memmove(a->data, (char*)a->data + delta, anb);
    }
    a->offset = newoffs;
}

void jl_cell_1d_push(jl_array_t *a, jl_value_t *item)
{
    assert(jl_typeis(a, jl_array_any_type));
    jl_array_grow_end(a, 1);
    jl_cellset(a, a->length-1, item);
}

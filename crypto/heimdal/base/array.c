/*
 * Copyright (c) 2010 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "baselocl.h"

/*
 *
 */

struct heim_array_data {
    size_t len;
    heim_object_t *val;
};

static void
array_dealloc(heim_object_t ptr)
{
    heim_array_t array = ptr;
    size_t n;
    for (n = 0; n < array->len; n++)
	heim_release(array->val[n]);
    free(array->val);
}

struct heim_type_data array_object = {
    HEIM_TID_ARRAY,
    "dict-object",
    NULL,
    array_dealloc,
    NULL,
    NULL,
    NULL
};

/**
 * Allocate an array
 *
 * @return A new allocated array, free with heim_release()
 */

heim_array_t
heim_array_create(void)
{
    heim_array_t array;

    array = _heim_alloc_object(&array_object, sizeof(*array));
    if (array == NULL)
	return NULL;

    array->val = NULL;
    array->len = 0;

    return array;
}

/**
 * Get type id of an dict
 *
 * @return the type id
 */

heim_tid_t
heim_array_get_type_id(void)
{
    return HEIM_TID_ARRAY;
}

/**
 * Append object to array
 *
 * @param array array to add too
 * @param object the object to add
 *
 * @return zero if added, errno otherwise
 */

int
heim_array_append_value(heim_array_t array, heim_object_t object)
{
    heim_object_t *ptr;

    ptr = realloc(array->val, (array->len + 1) * sizeof(array->val[0]));
    if (ptr == NULL)
	return ENOMEM;
    array->val = ptr;
    array->val[array->len++] = heim_retain(object);

    return 0;
}

/**
 * Iterate over all objects in array
 *
 * @param array array to iterate over
 * @param fn function to call on each object
 * @param ctx context passed to fn
 */

void
heim_array_iterate_f(heim_array_t array, heim_array_iterator_f_t fn, void *ctx)
{
    size_t n;
    for (n = 0; n < array->len; n++)
	fn(array->val[n], ctx);
}

#ifdef __BLOCKS__
/**
 * Iterate over all objects in array
 *
 * @param array array to iterate over
 * @param fn block to call on each object
 */

void
heim_array_iterate(heim_array_t array, void (^fn)(heim_object_t))
{
    size_t n;
    for (n = 0; n < array->len; n++)
	fn(array->val[n]);
}
#endif

/**
 * Get length of array
 *
 * @param array array to get length of
 *
 * @return length of array
 */

size_t
heim_array_get_length(heim_array_t array)
{
    return array->len;
}

/**
 * Copy value of array
 *
 * @param array array copy object from
 * @param idx index of object, 0 based, must be smaller then
 *        heim_array_get_length()
 *
 * @return a retained copy of the object
 */

heim_object_t
heim_array_copy_value(heim_array_t array, size_t idx)
{
    if (idx >= array->len)
	heim_abort("index too large");
    return heim_retain(array->val[idx]);
}

/**
 * Delete value at idx
 *
 * @param array the array to modify
 * @param idx the key to delete
 */

void
heim_array_delete_value(heim_array_t array, size_t idx)
{
    heim_object_t obj;
    if (idx >= array->len)
	heim_abort("index too large");
    obj = array->val[idx];

    array->len--;

    if (idx < array->len)
	memmove(&array->val[idx], &array->val[idx + 1],
		(array->len - idx) * sizeof(array->val[0]));

    heim_release(obj);
}

#ifdef __BLOCKS__
/**
 * Get value at idx
 *
 * @param array the array to modify
 * @param idx the key to delete
 */

void
heim_array_filter(heim_array_t array, int (^block)(heim_object_t))
{
    size_t n = 0;

    while (n < array->len) {
	if (block(array->val[n])) {
	    heim_array_delete_value(array, n);
	} else {
	    n++;
	}
    }
}

#endif /* __BLOCKS__ */

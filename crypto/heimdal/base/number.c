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

static void
number_dealloc(void *ptr)
{
}

static int
number_cmp(void *a, void *b)
{
    int na, nb;

    if (heim_base_is_tagged_object(a))
	na = heim_base_tagged_object_value(a);
    else
	na = *(int *)a;

    if (heim_base_is_tagged_object(b))
	nb = heim_base_tagged_object_value(b);
    else
	nb = *(int *)b;

    return na - nb;
}

static unsigned long
number_hash(void *ptr)
{
    if (heim_base_is_tagged_object(ptr))
	return heim_base_tagged_object_value(ptr);
    return (unsigned long)*(int *)ptr;
}

struct heim_type_data _heim_number_object = {
    HEIM_TID_NUMBER,
    "number-object",
    NULL,
    number_dealloc,
    NULL,
    number_cmp,
    number_hash
};

/**
 * Create a number object
 *
 * @param the number to contain in the object
 *
 * @return a number object
 */

heim_number_t
heim_number_create(int number)
{
    heim_number_t n;

    if (number < 0xffffff && number >= 0)
	return heim_base_make_tagged_object(number, HEIM_TID_NUMBER);

    n = _heim_alloc_object(&_heim_number_object, sizeof(int));
    if (n)
	*((int *)n) = number;
    return n;
}

/**
 * Return the type ID of number objects
 *
 * @return type id of number objects
 */

heim_tid_t
heim_number_get_type_id(void)
{
    return HEIM_TID_NUMBER;
}

/**
 * Get the int value of the content
 *
 * @param number the number object to get the value from
 *
 * @return an int
 */

int
heim_number_get_int(heim_number_t number)
{
    if (heim_base_is_tagged_object(number))
	return heim_base_tagged_object_value(number);
    return *(int *)number;
}

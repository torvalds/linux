/*
 * Copyright (c) 1997-2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "der_locl.h"

RCSID("$Id$");

size_t
_heim_len_unsigned (unsigned val)
{
    size_t ret = 0;
    int last_val_gt_128;

    do {
	++ret;
	last_val_gt_128 = (val >= 128);
	val /= 256;
    } while (val);

    if(last_val_gt_128)
	ret++;

    return ret;
}

size_t
_heim_len_int (int val)
{
    unsigned char q;
    size_t ret = 0;

    if (val >= 0) {
	do {
	    q = val % 256;
	    ret++;
	    val /= 256;
	} while(val);
	if(q >= 128)
	    ret++;
    } else {
	val = ~val;
	do {
	    q = ~(val % 256);
	    ret++;
	    val /= 256;
	} while(val);
	if(q < 128)
	    ret++;
    }
    return ret;
}

static size_t
len_oid (const heim_oid *oid)
{
    size_t ret = 1;
    size_t n;

    for (n = 2; n < oid->length; ++n) {
	unsigned u = oid->components[n];

	do {
	    ++ret;
	    u /= 128;
	} while(u > 0);
    }
    return ret;
}

size_t
der_length_len (size_t len)
{
    if (len < 128)
	return 1;
    else {
	int ret = 0;
	do {
	    ++ret;
	    len /= 256;
	} while (len);
	return ret + 1;
    }
}

size_t
der_length_tag(unsigned int tag)
{
    size_t len = 0;

    if(tag <= 30)
	return 1;
    while(tag) {
	tag /= 128;
	len++;
    }
    return len + 1;
}

size_t
der_length_integer (const int *data)
{
    return _heim_len_int (*data);
}

size_t
der_length_unsigned (const unsigned *data)
{
    return _heim_len_unsigned(*data);
}

size_t
der_length_enumerated (const unsigned *data)
{
  return _heim_len_int (*data);
}

size_t
der_length_general_string (const heim_general_string *data)
{
    return strlen(*data);
}

size_t
der_length_utf8string (const heim_utf8_string *data)
{
    return strlen(*data);
}

size_t
der_length_printable_string (const heim_printable_string *data)
{
    return data->length;
}

size_t
der_length_ia5_string (const heim_ia5_string *data)
{
    return data->length;
}

size_t
der_length_bmp_string (const heim_bmp_string *data)
{
    return data->length * 2;
}

size_t
der_length_universal_string (const heim_universal_string *data)
{
    return data->length * 4;
}

size_t
der_length_visible_string (const heim_visible_string *data)
{
    return strlen(*data);
}

size_t
der_length_octet_string (const heim_octet_string *k)
{
    return k->length;
}

size_t
der_length_heim_integer (const heim_integer *k)
{
    if (k->length == 0)
	return 1;
    if (k->negative)
	return k->length + (((~(((unsigned char *)k->data)[0])) & 0x80) ? 0 : 1);
    else
	return k->length + ((((unsigned char *)k->data)[0] & 0x80) ? 1 : 0);
}

size_t
der_length_oid (const heim_oid *k)
{
    return len_oid (k);
}

size_t
der_length_generalized_time (const time_t *t)
{
    heim_octet_string k;
    size_t ret;

    _heim_time2generalizedtime (*t, &k, 1);
    ret = k.length;
    free(k.data);
    return ret;
}

size_t
der_length_utctime (const time_t *t)
{
    heim_octet_string k;
    size_t ret;

    _heim_time2generalizedtime (*t, &k, 0);
    ret = k.length;
    free(k.data);
    return ret;
}

size_t
der_length_boolean (const int *k)
{
    return 1;
}

size_t
der_length_bit_string (const heim_bit_string *k)
{
    return (k->length + 7) / 8 + 1;
}

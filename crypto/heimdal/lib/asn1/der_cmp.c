/*
 * Copyright (c) 2003-2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
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

int
der_heim_oid_cmp(const heim_oid *p, const heim_oid *q)
{
    if (p->length != q->length)
	return p->length - q->length;
    return memcmp(p->components,
		  q->components,
		  p->length * sizeof(*p->components));
}

int
der_heim_octet_string_cmp(const heim_octet_string *p,
			  const heim_octet_string *q)
{
    if (p->length != q->length)
	return p->length - q->length;
    return memcmp(p->data, q->data, p->length);
}

int
der_printable_string_cmp(const heim_printable_string *p,
			 const heim_printable_string *q)
{
    return der_heim_octet_string_cmp(p, q);
}

int
der_ia5_string_cmp(const heim_ia5_string *p,
		   const heim_ia5_string *q)
{
    return der_heim_octet_string_cmp(p, q);
}

int
der_heim_bit_string_cmp(const heim_bit_string *p,
			const heim_bit_string *q)
{
    int i, r1, r2;
    if (p->length != q->length)
	return p->length - q->length;
    i = memcmp(p->data, q->data, p->length / 8);
    if (i)
	return i;
    if ((p->length % 8) == 0)
	return 0;
    i = (p->length / 8);
    r1 = ((unsigned char *)p->data)[i];
    r2 = ((unsigned char *)q->data)[i];
    i = 8 - (p->length % 8);
    r1 = r1 >> i;
    r2 = r2 >> i;
    return r1 - r2;
}

int
der_heim_integer_cmp(const heim_integer *p,
		     const heim_integer *q)
{
    if (p->negative != q->negative)
	return q->negative - p->negative;
    if (p->length != q->length)
	return p->length - q->length;
    return memcmp(p->data, q->data, p->length);
}

int
der_heim_bmp_string_cmp(const heim_bmp_string *p, const heim_bmp_string *q)
{
    if (p->length != q->length)
	return p->length - q->length;
    return memcmp(p->data, q->data, q->length * sizeof(q->data[0]));
}

int
der_heim_universal_string_cmp(const heim_universal_string *p,
			      const heim_universal_string *q)
{
    if (p->length != q->length)
	return p->length - q->length;
    return memcmp(p->data, q->data, q->length * sizeof(q->data[0]));
}

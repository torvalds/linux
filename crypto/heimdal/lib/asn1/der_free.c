/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

void
der_free_general_string (heim_general_string *str)
{
    free(*str);
    *str = NULL;
}

void
der_free_integer (int *i)
{
    *i = 0;
}

void
der_free_unsigned (unsigned *u)
{
    *u = 0;
}

void
der_free_generalized_time(time_t *t)
{
    *t = 0;
}

void
der_free_utctime(time_t *t)
{
    *t = 0;
}


void
der_free_utf8string (heim_utf8_string *str)
{
    free(*str);
    *str = NULL;
}

void
der_free_printable_string (heim_printable_string *str)
{
    der_free_octet_string(str);
}

void
der_free_ia5_string (heim_ia5_string *str)
{
    der_free_octet_string(str);
}

void
der_free_bmp_string (heim_bmp_string *k)
{
    free(k->data);
    k->data = NULL;
    k->length = 0;
}

void
der_free_universal_string (heim_universal_string *k)
{
    free(k->data);
    k->data = NULL;
    k->length = 0;
}

void
der_free_visible_string (heim_visible_string *str)
{
    free(*str);
    *str = NULL;
}

void
der_free_octet_string (heim_octet_string *k)
{
    free(k->data);
    k->data = NULL;
    k->length = 0;
}

void
der_free_heim_integer (heim_integer *k)
{
    free(k->data);
    k->data = NULL;
    k->length = 0;
}

void
der_free_oid (heim_oid *k)
{
    free(k->components);
    k->components = NULL;
    k->length = 0;
}

void
der_free_bit_string (heim_bit_string *k)
{
    free(k->data);
    k->data = NULL;
    k->length = 0;
}

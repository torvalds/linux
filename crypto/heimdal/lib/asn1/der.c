/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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
#include <com_err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getarg.h>
#include <err.h>

RCSID("$Id$");


static const char *class_names[] = {
    "UNIV",			/* 0 */
    "APPL",			/* 1 */
    "CONTEXT",			/* 2 */
    "PRIVATE"			/* 3 */
};

static const char *type_names[] = {
    "PRIM",			/* 0 */
    "CONS"			/* 1 */
};

static const char *tag_names[] = {
    "EndOfContent",		/* 0 */
    "Boolean",			/* 1 */
    "Integer",			/* 2 */
    "BitString",		/* 3 */
    "OctetString",		/* 4 */
    "Null",			/* 5 */
    "ObjectID",			/* 6 */
    NULL,			/* 7 */
    NULL,			/* 8 */
    NULL,			/* 9 */
    "Enumerated",		/* 10 */
    NULL,			/* 11 */
    NULL,			/* 12 */
    NULL,			/* 13 */
    NULL,			/* 14 */
    NULL,			/* 15 */
    "Sequence",			/* 16 */
    "Set",			/* 17 */
    NULL,			/* 18 */
    "PrintableString",		/* 19 */
    NULL,			/* 20 */
    NULL,			/* 21 */
    "IA5String",		/* 22 */
    "UTCTime",			/* 23 */
    "GeneralizedTime",		/* 24 */
    NULL,			/* 25 */
    "VisibleString",		/* 26 */
    "GeneralString",		/* 27 */
    NULL,			/* 28 */
    NULL,			/* 29 */
    "BMPString"			/* 30 */
};

static int
get_type(const char *name, const char *list[], unsigned len)
{
    unsigned i;
    for (i = 0; i < len; i++)
	if (list[i] && strcasecmp(list[i], name) == 0)
	    return i;
    return -1;
}

#define SIZEOF_ARRAY(a) (sizeof((a))/sizeof((a)[0]))

const char *
der_get_class_name(unsigned num)
{
    if (num >= SIZEOF_ARRAY(class_names))
	return NULL;
    return class_names[num];
}

int
der_get_class_num(const char *name)
{
    return get_type(name, class_names, SIZEOF_ARRAY(class_names));
}

const char *
der_get_type_name(unsigned num)
{
    if (num >= SIZEOF_ARRAY(type_names))
	return NULL;
    return type_names[num];
}

int
der_get_type_num(const char *name)
{
    return get_type(name, type_names, SIZEOF_ARRAY(type_names));
}

const char *
der_get_tag_name(unsigned num)
{
    if (num >= SIZEOF_ARRAY(tag_names))
	return NULL;
    return tag_names[num];
}

int
der_get_tag_num(const char *name)
{
    return get_type(name, tag_names, SIZEOF_ARRAY(tag_names));
}

/*
 * Copyright (c) 2005 Kungliga Tekniska Högskolan
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
#include <hex.h>

RCSID("$Id$");

int
der_parse_hex_heim_integer (const char *p, heim_integer *data)
{
    ssize_t len;

    data->length = 0;
    data->negative = 0;
    data->data = NULL;

    if (*p == '-') {
	p++;
	data->negative = 1;
    }

    len = strlen(p);
    if (len <= 0) {
	data->data = NULL;
	data->length = 0;
	return EINVAL;
    }

    data->length = (len / 2) + 1;
    data->data = malloc(data->length);
    if (data->data == NULL) {
	data->length = 0;
	return ENOMEM;
    }

    len = hex_decode(p, data->data, data->length);
    if (len < 0) {
	free(data->data);
	data->data = NULL;
	data->length = 0;
	return EINVAL;
    }

    {
	unsigned char *q = data->data;
	while(len > 0 && *q == 0) {
	    q++;
	    len--;
	}
	data->length = len;
	memmove(data->data, q, len);
    }
    return 0;
}

int
der_print_hex_heim_integer (const heim_integer *data, char **p)
{
    ssize_t len;
    char *q;

    len = hex_encode(data->data, data->length, p);
    if (len < 0)
	return ENOMEM;

    if (data->negative) {
	len = asprintf(&q, "-%s", *p);
	free(*p);
	if (len < 0)
	    return ENOMEM;
	*p = q;
    }
    return 0;
}

int
der_print_heim_oid (const heim_oid *oid, char delim, char **str)
{
    struct rk_strpool *p = NULL;
    size_t i;

    if (oid->length == 0)
	return EINVAL;

    for (i = 0; i < oid->length ; i++) {
	p = rk_strpoolprintf(p, "%d", oid->components[i]);
	if (p && i < oid->length - 1)
	    p = rk_strpoolprintf(p, "%c", delim);
	if (p == NULL) {
	    *str = NULL;
	    return ENOMEM;
	}
    }

    *str = rk_strpoolcollect(p);
    if (*str == NULL)
	return ENOMEM;
    return 0;
}

int
der_parse_heim_oid (const char *str, const char *sep, heim_oid *data)
{
    char *s, *w, *brkt, *endptr;
    unsigned int *c;
    long l;

    data->length = 0;
    data->components = NULL;

    if (sep == NULL)
	sep = ".";

    s = strdup(str);

    for (w = strtok_r(s, sep, &brkt);
	 w != NULL;
	 w = strtok_r(NULL, sep, &brkt)) {

	c = realloc(data->components,
		    (data->length + 1) * sizeof(data->components[0]));
	if (c == NULL) {
	    der_free_oid(data);
	    free(s);
	    return ENOMEM;
	}
	data->components = c;

	l = strtol(w, &endptr, 10);
	if (*endptr != '\0' || l < 0 || l > INT_MAX) {
	    der_free_oid(data);
	    free(s);
	    return EINVAL;
	}
	data->components[data->length++] = l;
    }
    free(s);
    return 0;
}

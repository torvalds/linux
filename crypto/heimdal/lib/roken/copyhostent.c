/*
 * Copyright (c) 1999 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "roken.h"

/*
 * return a malloced copy of `h'
 */

ROKEN_LIB_FUNCTION struct hostent * ROKEN_LIB_CALL
copyhostent (const struct hostent *h)
{
    struct hostent *res;
    char **p;
    int i, n;

    res = malloc (sizeof (*res));
    if (res == NULL)
	return NULL;
    res->h_name      = NULL;
    res->h_aliases   = NULL;
    res->h_addrtype  = h->h_addrtype;
    res->h_length    = h->h_length;
    res->h_addr_list = NULL;
    res->h_name = strdup (h->h_name);
    if (res->h_name == NULL) {
	freehostent (res);
	return NULL;
    }
    for (n = 0, p = h->h_aliases; *p != NULL; ++p)
	++n;
    res->h_aliases = malloc ((n + 1) * sizeof(*res->h_aliases));
    if (res->h_aliases == NULL) {
	freehostent (res);
	return NULL;
    }
    for (i = 0; i < n + 1; ++i)
	res->h_aliases[i] = NULL;
    for (i = 0; i < n; ++i) {
	res->h_aliases[i] = strdup (h->h_aliases[i]);
	if (res->h_aliases[i] == NULL) {
	    freehostent (res);
	    return NULL;
	}
    }

    for (n = 0, p = h->h_addr_list; *p != NULL; ++p)
	++n;
    res->h_addr_list = malloc ((n + 1) * sizeof(*res->h_addr_list));
    if (res->h_addr_list == NULL) {
	freehostent (res);
	return NULL;
    }
    for (i = 0; i < n + 1; ++i) {
	res->h_addr_list[i] = NULL;
    }
    for (i = 0; i < n; ++i) {
	res->h_addr_list[i] = malloc (h->h_length);
	if (res->h_addr_list[i] == NULL) {
	    freehostent (res);
	    return NULL;
	}
	memcpy (res->h_addr_list[i], h->h_addr_list[i], h->h_length);
    }
    return res;
}


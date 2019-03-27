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

#include <config.h>

#include <stdarg.h>
#include <stdlib.h>
#include "roken.h"

struct rk_strpool {
    char *str;
    size_t len;
};

/*
 *
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_strpoolfree(struct rk_strpool *p)
{
    if (p->str) {
	free(p->str);
	p->str = NULL;
    }
    free(p);
}

/*
 *
 */

ROKEN_LIB_FUNCTION struct rk_strpool * ROKEN_LIB_CALL
rk_strpoolprintf(struct rk_strpool *p, const char *fmt, ...)
{
    va_list ap;
    char *str, *str2;
    int len;

    if (p == NULL) {
	p = malloc(sizeof(*p));
	if (p == NULL)
	    return NULL;
	p->str = NULL;
	p->len = 0;
    }
    va_start(ap, fmt);
    len = vasprintf(&str, fmt, ap);
    va_end(ap);
    if (str == NULL) {
	rk_strpoolfree(p);
	return NULL;
    }
    str2 = realloc(p->str, len + p->len + 1);
    if (str2 == NULL) {
	rk_strpoolfree(p);
	return NULL;
    }
    p->str = str2;
    memcpy(p->str + p->len, str, len + 1);
    p->len += len;
    free(str);
    return p;
}

/*
 *
 */

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
rk_strpoolcollect(struct rk_strpool *p)
{
    char *str;
    if (p == NULL)
	return strdup("");
    str = p->str;
    p->str = NULL;
    free(p);
    return str;
}

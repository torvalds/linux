/*
 * Copyright (c) 1995, 1996, 1997 Kungliga Tekniska Högskolan
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

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
roken_concat (char *s, size_t len, ...)
{
    int ret;
    va_list args;

    va_start(args, len);
    ret = roken_vconcat (s, len, args);
    va_end(args);
    return ret;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
roken_vconcat (char *s, size_t len, va_list args)
{
    const char *a;

    while ((a = va_arg(args, const char*))) {
	size_t n = strlen (a);

	if (n >= len)
	    return -1;
	memcpy (s, a, n);
	s += n;
	len -= n;
    }
    *s = '\0';
    return 0;
}

ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
roken_vmconcat (char **s, size_t max_len, va_list args)
{
    const char *a;
    char *p, *q;
    size_t len = 0;
    *s = NULL;
    p = malloc(1);
    if(p == NULL)
	return 0;
    len = 1;
    while ((a = va_arg(args, const char*))) {
	size_t n = strlen (a);

	if(max_len && len + n > max_len){
	    free(p);
	    return 0;
	}
	q = realloc(p, len + n);
	if(q == NULL){
	    free(p);
	    return 0;
	}
	p = q;
	memcpy (p + len - 1, a, n);
	len += n;
    }
    p[len - 1] = '\0';
    *s = p;
    return len;
}

ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
roken_mconcat (char **s, size_t max_len, ...)
{
    size_t ret;
    va_list args;

    va_start(args, max_len);
    ret = roken_vmconcat (s, max_len, args);
    va_end(args);
    return ret;
}

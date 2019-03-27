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

#include <stdlib.h>
#include <string.h>

/*
 * This is the easy way out, use putenv to implement setenv. We might
 * leak some memory but that is ok since we are usally about to exec
 * anyway.
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
setenv(const char *var, const char *val, int rewrite)
{
#ifndef _WIN32
    char *t = NULL;

    if (!rewrite && getenv(var) != 0)
	return 0;

    if (asprintf (&t, "%s=%s", var, val) < 0 || t == NULL)
	return -1;

    if (putenv(t) == 0)
	return 0;
    else
	return -1;
#else  /* Win32 */
    char dummy[8];

    if (!rewrite && GetEnvironmentVariable(var, dummy, sizeof(dummy)/sizeof(char)) != 0)
	return 0;

    if (SetEnvironmentVariable(var, val) == 0)
	return -1;
    else
	return 0;
#endif
}

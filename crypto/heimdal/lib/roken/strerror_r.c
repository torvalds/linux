/*
 * Copyright (c) 1995, 1996, 1997, 1998 Kungliga Tekniska Högskolan
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

#if (!defined(HAVE_STRERROR_R) && !defined(strerror_r)) || (!defined(STRERROR_R_PROTO_COMPATIBLE) && defined(HAVE_STRERROR_R))

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "roken.h"

#ifdef _MSC_VER

int ROKEN_LIB_FUNCTION
rk_strerror_r(int eno, char * strerrbuf, size_t buflen)
{
    errno_t err;

    err = strerror_s(strerrbuf, buflen, eno);
    if (err != 0) {
        int code;
        code = sprintf_s(strerrbuf, buflen, "Error % occurred.", eno);
        err = ((code != 0)? errno : 0);
    }

    return err;
}

#else  /* _MSC_VER */

int ROKEN_LIB_FUNCTION
rk_strerror_r(int eno, char *strerrbuf, size_t buflen)
{
    /* Assume is the linux broken strerror_r (returns the a buffer (char *) if the input buffer wasn't use */
#ifdef HAVE_STRERROR_R
    const char *str;
    str = strerror_r(eno, strerrbuf, buflen);
    if (str != strerrbuf)
	if (strlcpy(strerrbuf, str, buflen) >= buflen)
	    return ERANGE;
    return 0;
#else
    int ret;
    ret = strlcpy(strerrbuf, strerror(eno), buflen);
    if (ret > buflen)
	return ERANGE;
    return 0;
#endif
}

#endif  /* !_MSC_VER */

#endif

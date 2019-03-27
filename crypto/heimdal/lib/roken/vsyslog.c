/*
 * Copyright (c) 1995 - 2000 Kungliga Tekniska Högskolan
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

#ifndef HAVE_VSYSLOG

#include <stdio.h>
#include <syslog.h>
#include <stdarg.h>

#include "roken.h"

/*
 * the theory behind this is that we might be trying to call vsyslog
 * when there's no memory left, and we should try to be as useful as
 * possible.  And the format string should say something about what's
 * failing.
 */

static void
simple_vsyslog(int pri, const char *fmt, va_list ap)
{
    syslog (pri, "%s", fmt);
}

/*
 * do like syslog but with a `va_list'
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
vsyslog(int pri, const char *fmt, va_list ap)
{
    char *fmt2;
    const char *p;
    char *p2;
    int ret;
    int saved_errno = errno;
    int fmt_len  = strlen (fmt);
    int fmt2_len = fmt_len;
    char *buf;

    fmt2 = malloc (fmt_len + 1);
    if (fmt2 == NULL) {
	simple_vsyslog (pri, fmt, ap);
	return;
    }

    for (p = fmt, p2 = fmt2; *p != '\0'; ++p) {
	if (p[0] == '%' && p[1] == 'm') {
	    const char *e = strerror (saved_errno);
	    int e_len = strlen (e);
	    char *tmp;
	    int pos;

	    pos = p2 - fmt2;
	    fmt2_len += e_len - 2;
	    tmp = realloc (fmt2, fmt2_len + 1);
	    if (tmp == NULL) {
		free (fmt2);
		simple_vsyslog (pri, fmt, ap);
		return;
	    }
	    fmt2 = tmp;
	    p2   = fmt2 + pos;
	    memmove (p2, e, e_len);
	    p2 += e_len;
	    ++p;
	} else
	    *p2++ = *p;
    }
    *p2 = '\0';

    ret = vasprintf (&buf, fmt2, ap);
    free (fmt2);
    if (ret < 0 || buf == NULL) {
	simple_vsyslog (pri, fmt, ap);
	return;
    }
    syslog (pri, "%s", buf);
    free (buf);
}
#endif

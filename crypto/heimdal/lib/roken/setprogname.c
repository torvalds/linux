/*
 * Copyright (c) 1995-2004 Kungliga Tekniska Högskolan
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

#ifndef HAVE___PROGNAME
extern const char *__progname;
#endif

#ifndef HAVE_SETPROGNAME

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
setprogname(const char *argv0)
{

#ifndef HAVE___PROGNAME

    const char *p;
    if(argv0 == NULL)
	return;
    p = strrchr(argv0, '/');

#ifdef BACKSLASH_PATH_DELIM
    {
        const char * pb;

        pb = strrchr((p != NULL)? p : argv0, '\\');
        if (pb != NULL)
            p = pb;
    }
#endif

    if(p == NULL)
	p = argv0;
    else
	p++;

#ifdef _WIN32
    {
        char * fn = strdup(p);
        char * ext;

        strlwr(fn);
        ext = strrchr(fn, '.');
        if (ext != NULL && !strcmp(ext, ".exe"))
            *ext = '\0';

        __progname = fn;
    }
#else

    __progname = p;

#endif

#endif  /* HAVE___PROGNAME */
}

#endif /* HAVE_SETPROGNAME */

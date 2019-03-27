/*
 * Copyright (c) 1998, 1999 Kungliga Tekniska Högskolan
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
#include <string.h>
#include <errno.h>
#include "roken.h"

enum { initial = 10, increment = 5 };

static char **
sub (char **argv, int i, int argc, va_list *ap)
{
    do {
	if(i == argc) {
	    /* realloc argv */
	    char **tmp = realloc(argv, (argc + increment) * sizeof(*argv));
	    if(tmp == NULL) {
		free(argv);
		errno = ENOMEM;
		return NULL;
	    }
	    argv  = tmp;
	    argc += increment;
	}
	argv[i++] = va_arg(*ap, char*);
    } while(argv[i - 1] != NULL);
    return argv;
}

/*
 * return a malloced vector of pointers to the strings in `ap'
 * terminated by NULL.
 */

ROKEN_LIB_FUNCTION char ** ROKEN_LIB_CALL
vstrcollect(va_list *ap)
{
    return sub (NULL, 0, 0, ap);
}

/*
 *
 */

ROKEN_LIB_FUNCTION char ** ROKEN_LIB_CALL
strcollect(char *first, ...)
{
    va_list ap;
    char **ret = malloc (initial * sizeof(char *));

    if (ret == NULL)
	return ret;

    ret[0] = first;
    va_start(ap, first);
    ret = sub (ret, 1, initial, &ap);
    va_end(ap);
    return ret;
}

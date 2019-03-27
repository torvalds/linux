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

static struct gai_error {
    int code;
    const char *str;
} errors[] = {
{EAI_NOERROR,		"no error"},
#ifdef EAI_ADDRFAMILY
{EAI_ADDRFAMILY,	"address family for nodename not supported"},
#endif
{EAI_AGAIN,		"temporary failure in name resolution"},
{EAI_BADFLAGS,		"invalid value for ai_flags"},
{EAI_FAIL,		"non-recoverable failure in name resolution"},
{EAI_FAMILY,		"ai_family not supported"},
{EAI_MEMORY,		"memory allocation failure"},
#ifdef EAI_NODATA
{EAI_NODATA,		"no address associated with nodename"},
#endif
{EAI_NONAME,		"nodename nor servname provided, or not known"},
{EAI_SERVICE,		"servname not supported for ai_socktype"},
{EAI_SOCKTYPE,		"ai_socktype not supported"},
{EAI_SYSTEM,		"system error returned in errno"},
{0,			NULL},
};

/*
 *
 */

ROKEN_LIB_FUNCTION const char * ROKEN_LIB_CALL
gai_strerror(int ecode)
{
    struct gai_error *g;

    for (g = errors; g->str != NULL; ++g)
	if (g->code == ecode)
	    return g->str;
    return "unknown error code in gai_strerror";
}

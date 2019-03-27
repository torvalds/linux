/*
 * Copyright (c) 1999 - 2000 Kungliga Tekniska Högskolan
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

#ifdef HAVE_WINSOCK

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
inet_pton(int af, const char *csrc, void *dst)
{
    char * src;

    if (csrc == NULL || (src = strdup(csrc)) == NULL) {
	_set_errno( ENOMEM );
	return 0;
    }

    switch (af) {
    case AF_INET:
	{
	    struct sockaddr_in  si4;
	    INT r;
	    INT s = sizeof(si4);

	    si4.sin_family = AF_INET;
	    r = WSAStringToAddress(src, AF_INET, NULL, (LPSOCKADDR) &si4, &s);
	    free(src);
	    src = NULL;

	    if (r == 0) {
		memcpy(dst, &si4.sin_addr, sizeof(si4.sin_addr));
		return 1;
	    }
	}
	break;

    case AF_INET6:
	{
	    struct sockaddr_in6 si6;
	    INT r;
	    INT s = sizeof(si6);

	    si6.sin6_family = AF_INET6;
	    r = WSAStringToAddress(src, AF_INET6, NULL, (LPSOCKADDR) &si6, &s);
	    free(src);
	    src = NULL;

	    if (r == 0) {
		memcpy(dst, &si6.sin6_addr, sizeof(si6.sin6_addr));
		return 1;
	    }
	}
	break;

    default:
	_set_errno( EAFNOSUPPORT );
	return -1;
    }

    /* the call failed */
    {
	int le = WSAGetLastError();

	if (le == WSAEINVAL)
	    return 0;

	_set_errno(le);
	return -1;
    }
}

#else  /* !HAVE_WINSOCK */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET) {
	errno = EAFNOSUPPORT;
	return -1;
    }
    return inet_aton (src, dst);
}

#endif

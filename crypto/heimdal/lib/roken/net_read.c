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

#include "roken.h"

/*
 * Like read but never return partial data.
 */

#ifndef _WIN32

ROKEN_LIB_FUNCTION ssize_t ROKEN_LIB_CALL
net_read (rk_socket_t fd, void *buf, size_t nbytes)
{
    char *cbuf = (char *)buf;
    ssize_t count;
    size_t rem = nbytes;

    while (rem > 0) {
	count = read (fd, cbuf, rem);
	if (count < 0) {
	    if (errno == EINTR)
		continue;
	    else
		return count;
	} else if (count == 0) {
	    return count;
	}
	cbuf += count;
	rem -= count;
    }
    return nbytes;
}

#else

ROKEN_LIB_FUNCTION ssize_t ROKEN_LIB_CALL
net_read(rk_socket_t sock, void *buf, size_t nbytes)
{
    char *cbuf = (char *)buf;
    ssize_t count;
    size_t rem = nbytes;

#ifdef SOCKET_IS_NOT_AN_FD
    int use_read = 0;
#endif

    while (rem > 0) {
#ifdef SOCKET_IS_NOT_AN_FD
	if (use_read)
	    count = _read (sock, cbuf, rem);
	else
	    count = recv (sock, cbuf, rem, 0);

	if (use_read == 0 &&
	    rk_IS_SOCKET_ERROR(count) &&
            (rk_SOCK_ERRNO == WSANOTINITIALISED ||
             rk_SOCK_ERRNO == WSAENOTSOCK)) {
	    use_read = 1;

	    count = _read (sock, cbuf, rem);
	}
#else
	count = recv (sock, cbuf, rem, 0);
#endif
	if (count < 0) {

	    /* With WinSock, the error EINTR (WSAEINTR), is used to
	       indicate that a blocking call was cancelled using
	       WSACancelBlockingCall(). */

#ifndef HAVE_WINSOCK
	    if (rk_SOCK_ERRNO == EINTR)
		continue;
#endif
	    return count;
	} else if (count == 0) {
	    return count;
	}
	cbuf += count;
	rem -= count;
    }
    return nbytes;
}

#endif

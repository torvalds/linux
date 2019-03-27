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
#include <err.h>

/*
 * Set `sa' to the unitialized address of address family `af'
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_any (struct sockaddr *sa, int af)
{
    switch (af) {
    case AF_INET : {
	struct sockaddr_in *sin4 = (struct sockaddr_in *)sa;

	memset (sin4, 0, sizeof(*sin4));
	sin4->sin_family = AF_INET;
	sin4->sin_port   = 0;
	sin4->sin_addr.s_addr = INADDR_ANY;
	break;
    }
#ifdef HAVE_IPV6
    case AF_INET6 : {
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

	memset (sin6, 0, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port   = 0;
	sin6->sin6_addr   = in6addr_any;
	break;
    }
#endif
    default :
	errx (1, "unknown address family %d", sa->sa_family);
	break;
    }
}

/*
 * set `sa' to (`ptr', `port')
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_address_and_port (struct sockaddr *sa, const void *ptr, int port)
{
    switch (sa->sa_family) {
    case AF_INET : {
	struct sockaddr_in *sin4 = (struct sockaddr_in *)sa;

	memset (sin4, 0, sizeof(*sin4));
	sin4->sin_family = AF_INET;
	sin4->sin_port   = port;
	memcpy (&sin4->sin_addr, ptr, sizeof(struct in_addr));
	break;
    }
#ifdef HAVE_IPV6
    case AF_INET6 : {
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

	memset (sin6, 0, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port   = port;
	memcpy (&sin6->sin6_addr, ptr, sizeof(struct in6_addr));
	break;
    }
#endif
    default :
	errx (1, "unknown address family %d", sa->sa_family);
	break;
    }
}

/*
 * Return the size of an address of the type in `sa'
 */

ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
socket_addr_size (const struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET :
	return sizeof(struct in_addr);
#ifdef HAVE_IPV6
    case AF_INET6 :
	return sizeof(struct in6_addr);
#endif
    default :
	return 0;
    }
}

/*
 * Return the size of a `struct sockaddr' in `sa'.
 */

ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
socket_sockaddr_size (const struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET :
	return sizeof(struct sockaddr_in);
#ifdef HAVE_IPV6
    case AF_INET6 :
	return sizeof(struct sockaddr_in6);
#endif
    default:
	return 0;
    }
}

/*
 * Return the binary address of `sa'.
 */

ROKEN_LIB_FUNCTION void * ROKEN_LIB_CALL
socket_get_address (const struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET : {
	const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;
	return rk_UNCONST(&sin4->sin_addr);
    }
#ifdef HAVE_IPV6
    case AF_INET6 : {
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
	return rk_UNCONST(&sin6->sin6_addr);
    }
#endif
    default:
	return NULL;
    }
}

/*
 * Return the port number from `sa'.
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
socket_get_port (const struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET : {
	const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;
	return sin4->sin_port;
    }
#ifdef HAVE_IPV6
    case AF_INET6 : {
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
	return sin6->sin6_port;
    }
#endif
    default :
	return 0;
    }
}

/*
 * Set the port in `sa' to `port'.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_port (struct sockaddr *sa, int port)
{
    switch (sa->sa_family) {
    case AF_INET : {
	struct sockaddr_in *sin4 = (struct sockaddr_in *)sa;
	sin4->sin_port = port;
	break;
    }
#ifdef HAVE_IPV6
    case AF_INET6 : {
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	sin6->sin6_port = port;
	break;
    }
#endif
    default :
	errx (1, "unknown address family %d", sa->sa_family);
	break;
    }
}

/*
 * Set the range of ports to use when binding with port = 0.
 */
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_portrange (rk_socket_t sock, int restr, int af)
{
#if defined(IP_PORTRANGE)
	if (af == AF_INET) {
		int on = restr ? IP_PORTRANGE_HIGH : IP_PORTRANGE_DEFAULT;
		setsockopt (sock, IPPROTO_IP, IP_PORTRANGE, &on, sizeof(on));
	}
#endif
#if defined(IPV6_PORTRANGE)
	if (af == AF_INET6) {
		int on = restr ? IPV6_PORTRANGE_HIGH : IPV6_PORTRANGE_DEFAULT;
		setsockopt (sock, IPPROTO_IPV6, IPV6_PORTRANGE, &on, sizeof(on));
	}
#endif
}

/*
 * Enable debug on `sock'.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_debug (rk_socket_t sock)
{
#if defined(SO_DEBUG) && defined(HAVE_SETSOCKOPT)
    int on = 1;
    setsockopt (sock, SOL_SOCKET, SO_DEBUG, (void *) &on, sizeof (on));
#endif
}

/*
 * Set the type-of-service of `sock' to `tos'.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_tos (rk_socket_t sock, int tos)
{
#if defined(IP_TOS) && defined(HAVE_SETSOCKOPT)
    setsockopt (sock, IPPROTO_IP, IP_TOS, (void *) &tos, sizeof(int));
#endif
}

/*
 * set the reuse of addresses on `sock' to `val'.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_reuseaddr (rk_socket_t sock, int val)
{
#if defined(SO_REUSEADDR) && defined(HAVE_SETSOCKOPT)
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(val));
#endif
}

/*
 * Set the that the `sock' should bind to only IPv6 addresses.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_ipv6only (rk_socket_t sock, int val)
{
#if defined(IPV6_V6ONLY) && defined(HAVE_SETSOCKOPT)
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&val, sizeof(val));
#endif
}

/**
 * Create a file descriptor from a socket
 *
 * While the socket handle in \a sock can be used with WinSock
 * functions after calling socket_to_fd(), it should not be closed
 * with rk_closesocket().  The socket will be closed when the associated
 * file descriptor is closed.
 */
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
socket_to_fd(rk_socket_t sock, int flags)
{
#ifndef _WIN32
    return sock;
#else
    return _open_osfhandle((intptr_t) sock, flags);
#endif
}

#ifdef HAVE_WINSOCK
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_SOCK_IOCTL(SOCKET s, long cmd, int * argp) {
    u_long ul = (argp)? *argp : 0;
    int rv;

    rv = ioctlsocket(s, cmd, &ul);
    if (argp)
	*argp = (int) ul;
    return rv;
}
#endif

#ifndef HEIMDAL_SMALLER
#undef socket

int rk_socket(int, int, int);

int
rk_socket(int domain, int type, int protocol)
{
    int s;
    s = socket (domain, type, protocol);
#ifdef SOCK_CLOEXEC
    if ((SOCK_CLOEXEC & type) && s < 0 && errno == EINVAL) {
	type &= ~SOCK_CLOEXEC;
	s = socket (domain, type, protocol);
    }
#endif
    return s;
}

#endif /* HEIMDAL_SMALLER */

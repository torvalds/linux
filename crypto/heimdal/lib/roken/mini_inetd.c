/*
 * Copyright (c) 1995 - 2001 Kungliga Tekniska Högskolan
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

#include <err.h>
#include "roken.h"

/*
 * accept a connection on `s' and pretend it's served by inetd.
 */

static void
accept_it (rk_socket_t s, rk_socket_t *ret_socket)
{
    rk_socket_t as;

    as = accept(s, NULL, NULL);
    if(rk_IS_BAD_SOCKET(as))
	err (1, "accept");

    if (ret_socket) {

	*ret_socket = as;

    } else {
	int fd = socket_to_fd(as, 0);

	/* We would use _O_RDONLY for the socket_to_fd() call for
	   STDIN, but there are instances where we assume that STDIN
	   is a r/w socket. */

	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);

	rk_closesocket(as);
    }
}

/**
 * Listen on a specified addresses
 *
 * Listens on the specified addresses for incoming connections.  If
 * the \a ret_socket parameter is \a NULL, on return STDIN and STDOUT
 * will be connected to an accepted socket.  If the \a ret_socket
 * parameter is non-NULL, the accepted socket will be returned in
 * *ret_socket.  In the latter case, STDIN and STDOUT will be left
 * unmodified.
 *
 * This function does not return if there is an error or if no
 * connection is established.
 *
 * @param[in] ai Addresses to listen on
 * @param[out] ret_socket If non-NULL receives the accepted socket.
 *
 * @see mini_inetd()
 */
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
mini_inetd_addrinfo (struct addrinfo *ai, rk_socket_t *ret_socket)
{
    int ret;
    struct addrinfo *a;
    int n, nalloc, i;
    rk_socket_t *fds;
    fd_set orig_read_set, read_set;
    rk_socket_t max_fd = (rk_socket_t)-1;

    for (nalloc = 0, a = ai; a != NULL; a = a->ai_next)
	++nalloc;

    fds = malloc (nalloc * sizeof(*fds));
    if (fds == NULL) {
	errx (1, "mini_inetd: out of memory");
	UNREACHABLE(return);
    }

    FD_ZERO(&orig_read_set);

    for (i = 0, a = ai; a != NULL; a = a->ai_next) {
	fds[i] = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (rk_IS_BAD_SOCKET(fds[i]))
	    continue;
	socket_set_reuseaddr (fds[i], 1);
	socket_set_ipv6only(fds[i], 1);
	if (rk_IS_SOCKET_ERROR(bind (fds[i], a->ai_addr, a->ai_addrlen))) {
	    warn ("bind af = %d", a->ai_family);
	    rk_closesocket(fds[i]);
	    fds[i] = rk_INVALID_SOCKET;
	    continue;
	}
	if (rk_IS_SOCKET_ERROR(listen (fds[i], SOMAXCONN))) {
	    warn ("listen af = %d", a->ai_family);
	    rk_closesocket(fds[i]);
	    fds[i] = rk_INVALID_SOCKET;
	    continue;
	}
#ifndef NO_LIMIT_FD_SETSIZE
	if (fds[i] >= FD_SETSIZE)
	    errx (1, "fd too large");
#endif
	FD_SET(fds[i], &orig_read_set);
	max_fd = max(max_fd, fds[i]);
	++i;
    }
    if (i == 0)
	errx (1, "no sockets");
    n = i;

    do {
	read_set = orig_read_set;

	ret = select (max_fd + 1, &read_set, NULL, NULL, NULL);
	if (rk_IS_SOCKET_ERROR(ret) && rk_SOCK_ERRNO != EINTR)
	    err (1, "select");
    } while (ret <= 0);

    for (i = 0; i < n; ++i)
	if (FD_ISSET (fds[i], &read_set)) {
	    accept_it (fds[i], ret_socket);
	    for (i = 0; i < n; ++i)
	      rk_closesocket(fds[i]);
	    free(fds);
	    return;
	}
    abort ();
}

/**
 * Listen on a specified port
 *
 * Listens on the specified port for incoming connections.  If the \a
 * ret_socket parameter is \a NULL, on return STDIN and STDOUT will be
 * connected to an accepted socket.  If the \a ret_socket parameter is
 * non-NULL, the accepted socket will be returned in *ret_socket.  In
 * the latter case, STDIN and STDOUT will be left unmodified.
 *
 * This function does not return if there is an error or if no
 * connection is established.
 *
 * @param[in] port Port to listen on
 * @param[out] ret_socket If non-NULL receives the accepted socket.
 *
 * @see mini_inetd_addrinfo()
 */
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
mini_inetd(int port, rk_socket_t * ret_socket)
{
    int error;
    struct addrinfo *ai, hints;
    char portstr[NI_MAXSERV];

    memset (&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = PF_UNSPEC;

    snprintf (portstr, sizeof(portstr), "%d", ntohs(port));

    error = getaddrinfo (NULL, portstr, &hints, &ai);
    if (error)
	errx (1, "getaddrinfo: %s", gai_strerror (error));

    mini_inetd_addrinfo(ai, ret_socket);

    freeaddrinfo(ai);
}


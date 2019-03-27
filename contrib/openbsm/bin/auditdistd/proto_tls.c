/*-
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#include <sys/param.h>	/* MAXHOSTNAMELEN */
#include <sys/socket.h>

#include <arpa/inet.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <compat/compat.h>
#ifndef HAVE_CLOSEFROM
#include <compat/closefrom.h>
#endif
#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#include "pjdlog.h"
#include "proto_impl.h"
#include "sandbox.h"
#include "subr.h"

#define	TLS_CTX_MAGIC	0x715c7
struct tls_ctx {
	int		tls_magic;
	struct proto_conn *tls_sock;
	struct proto_conn *tls_tcp;
	char		tls_laddr[256];
	char		tls_raddr[256];
	int		tls_side;
#define	TLS_SIDE_CLIENT		0
#define	TLS_SIDE_SERVER_LISTEN	1
#define	TLS_SIDE_SERVER_WORK	2
	bool		tls_wait_called;
};

#define	TLS_DEFAULT_TIMEOUT	30

static int tls_connect_wait(void *ctx, int timeout);
static void tls_close(void *ctx);

static void
block(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		pjdlog_exit(EX_TEMPFAIL, "fcntl(F_GETFL) failed");
	flags &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1)
		pjdlog_exit(EX_TEMPFAIL, "fcntl(F_SETFL) failed");
}

static void
nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		pjdlog_exit(EX_TEMPFAIL, "fcntl(F_GETFL) failed");
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1)
		pjdlog_exit(EX_TEMPFAIL, "fcntl(F_SETFL) failed");
}

static int
wait_for_fd(int fd, int timeout)
{
	struct timeval tv;
	fd_set fdset;
	int error, ret;

	error = 0;

	for (;;) {
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);

		tv.tv_sec = timeout;
		tv.tv_usec = 0;

		ret = select(fd + 1, NULL, &fdset, NULL,
		    timeout == -1 ? NULL : &tv);
		if (ret == 0) {
			error = ETIMEDOUT;
			break;
		} else if (ret == -1) {
			if (errno == EINTR)
				continue;
			error = errno;
			break;
		}
		PJDLOG_ASSERT(ret > 0);
		PJDLOG_ASSERT(FD_ISSET(fd, &fdset));
		break;
	}

	return (error);
}

static void
ssl_log_errors(void)
{
	unsigned long error;

	while ((error = ERR_get_error()) != 0)
		pjdlog_error("SSL error: %s", ERR_error_string(error, NULL));
}

static int
ssl_check_error(SSL *ssl, int ret)
{
	int error;

	error = SSL_get_error(ssl, ret);

	switch (error) {
	case SSL_ERROR_NONE:
		return (0);
	case SSL_ERROR_WANT_READ:
		pjdlog_debug(2, "SSL_ERROR_WANT_READ");
		return (-1);
	case SSL_ERROR_WANT_WRITE:
		pjdlog_debug(2, "SSL_ERROR_WANT_WRITE");
		return (-1);
	case SSL_ERROR_ZERO_RETURN:
		pjdlog_exitx(EX_OK, "Connection closed.");
	case SSL_ERROR_SYSCALL:
		ssl_log_errors();
		pjdlog_exitx(EX_TEMPFAIL, "SSL I/O error.");
	case SSL_ERROR_SSL:
		ssl_log_errors();
		pjdlog_exitx(EX_TEMPFAIL, "SSL protocol error.");
	default:
		ssl_log_errors();
		pjdlog_exitx(EX_TEMPFAIL, "Unknown SSL error (%d).", error);
	}
}

static void
tcp_recv_ssl_send(int recvfd, SSL *sendssl)
{
	static unsigned char buf[65536];
	ssize_t tcpdone;
	int sendfd, ssldone;

	sendfd = SSL_get_fd(sendssl);
	PJDLOG_ASSERT(sendfd >= 0);
	pjdlog_debug(2, "%s: start %d -> %d", __func__, recvfd, sendfd);
	for (;;) {
		tcpdone = recv(recvfd, buf, sizeof(buf), 0);
		pjdlog_debug(2, "%s: recv() returned %zd", __func__, tcpdone);
		if (tcpdone == 0) {
			pjdlog_debug(1, "Connection terminated.");
			exit(0);
		} else if (tcpdone == -1) {
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN)
				break;
			pjdlog_exit(EX_TEMPFAIL, "recv() failed");
		}
		for (;;) {
			ssldone = SSL_write(sendssl, buf, (int)tcpdone);
			pjdlog_debug(2, "%s: send() returned %d", __func__,
			    ssldone);
			if (ssl_check_error(sendssl, ssldone) == -1) {
				(void)wait_for_fd(sendfd, -1);
				continue;
			}
			PJDLOG_ASSERT(ssldone == tcpdone);
			break;
		}
	}
	pjdlog_debug(2, "%s: done %d -> %d", __func__, recvfd, sendfd);
}

static void
ssl_recv_tcp_send(SSL *recvssl, int sendfd)
{
	static unsigned char buf[65536];
	unsigned char *ptr;
	ssize_t tcpdone;
	size_t todo;
	int recvfd, ssldone;

	recvfd = SSL_get_fd(recvssl);
	PJDLOG_ASSERT(recvfd >= 0);
	pjdlog_debug(2, "%s: start %d -> %d", __func__, recvfd, sendfd);
	for (;;) {
		ssldone = SSL_read(recvssl, buf, sizeof(buf));
		pjdlog_debug(2, "%s: SSL_read() returned %d", __func__,
		    ssldone);
		if (ssl_check_error(recvssl, ssldone) == -1)
			break;
		todo = (size_t)ssldone;
		ptr = buf;
		do {
			tcpdone = send(sendfd, ptr, todo, MSG_NOSIGNAL);
			pjdlog_debug(2, "%s: send() returned %zd", __func__,
			    tcpdone);
			if (tcpdone == 0) {
				pjdlog_debug(1, "Connection terminated.");
				exit(0);
			} else if (tcpdone == -1) {
				if (errno == EINTR || errno == ENOBUFS)
					continue;
				if (errno == EAGAIN) {
					(void)wait_for_fd(sendfd, -1);
					continue;
				}
				pjdlog_exit(EX_TEMPFAIL, "send() failed");
			}
			todo -= tcpdone;
			ptr += tcpdone;
		} while (todo > 0);
	}
	pjdlog_debug(2, "%s: done %d -> %d", __func__, recvfd, sendfd);
}

static void
tls_loop(int sockfd, SSL *tcpssl)
{
	fd_set fds;
	int maxfd, tcpfd;

	tcpfd = SSL_get_fd(tcpssl);
	PJDLOG_ASSERT(tcpfd >= 0);

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(sockfd, &fds);
		FD_SET(tcpfd, &fds);
		maxfd = MAX(sockfd, tcpfd);

		PJDLOG_ASSERT(maxfd + 1 <= (int)FD_SETSIZE);
		if (select(maxfd + 1, &fds, NULL, NULL, NULL) == -1) {
			if (errno == EINTR)
				continue;
			pjdlog_exit(EX_TEMPFAIL, "select() failed");
		}
		if (FD_ISSET(sockfd, &fds))
			tcp_recv_ssl_send(sockfd, tcpssl);
		if (FD_ISSET(tcpfd, &fds))
			ssl_recv_tcp_send(tcpssl, sockfd);
	}
}

static void
tls_certificate_verify(SSL *ssl, const char *fingerprint)
{
	unsigned char md[EVP_MAX_MD_SIZE];
	char mdstr[sizeof("SHA256=") - 1 + EVP_MAX_MD_SIZE * 3];
	char *mdstrp;
	unsigned int i, mdsize;
	X509 *cert;

	if (fingerprint[0] == '\0') {
		pjdlog_debug(1, "No fingerprint verification requested.");
		return;
	}

	cert = SSL_get_peer_certificate(ssl);
	if (cert == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "No peer certificate received.");

	if (X509_digest(cert, EVP_sha256(), md, &mdsize) != 1)
		pjdlog_exitx(EX_TEMPFAIL, "X509_digest() failed.");
	PJDLOG_ASSERT(mdsize <= EVP_MAX_MD_SIZE);

	X509_free(cert);

	(void)strlcpy(mdstr, "SHA256=", sizeof(mdstr));
	mdstrp = mdstr + strlen(mdstr);
	for (i = 0; i < mdsize; i++) {
		PJDLOG_VERIFY(mdstrp + 3 <= mdstr + sizeof(mdstr));
		(void)sprintf(mdstrp, "%02hhX:", md[i]);
		mdstrp += 3;
	}
	/* Clear last colon. */
	mdstrp[-1] = '\0';
	if (strcasecmp(mdstr, fingerprint) != 0) {
		pjdlog_exitx(EX_NOPERM,
		    "Finger print doesn't match. Received \"%s\", expected \"%s\"",
		    mdstr, fingerprint);
	}
}

static void
tls_exec_client(const char *user, int startfd, const char *srcaddr,
    const char *dstaddr, const char *fingerprint, const char *defport,
    int timeout, int debuglevel)
{
	struct proto_conn *tcp;
	char *saddr, *daddr;
	SSL_CTX *sslctx;
	SSL *ssl;
	long ret;
	int sockfd, tcpfd;
	uint8_t connected;

	pjdlog_debug_set(debuglevel);
	pjdlog_prefix_set("[TLS sandbox] (client) ");
#ifdef HAVE_SETPROCTITLE
	setproctitle("[TLS sandbox] (client) ");
#endif
	proto_set("tcp:port", defport);

	sockfd = startfd;

	/* Change tls:// to tcp://. */
	if (srcaddr == NULL) {
		saddr = NULL;
	} else {
		saddr = strdup(srcaddr);
		if (saddr == NULL)
			pjdlog_exitx(EX_TEMPFAIL, "Unable to allocate memory.");
		bcopy("tcp://", saddr, 6);
	}
	daddr = strdup(dstaddr);
	if (daddr == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "Unable to allocate memory.");
	bcopy("tcp://", daddr, 6);

	/* Establish TCP connection. */
	if (proto_connect(saddr, daddr, timeout, &tcp) == -1)
		exit(EX_TEMPFAIL);

	SSL_load_error_strings();
	SSL_library_init();

	/*
	 * TODO: On FreeBSD we could move this below sandbox() once libc and
	 *       libcrypto use sysctl kern.arandom to obtain random data
	 *       instead of /dev/urandom and friends.
	 */
	sslctx = SSL_CTX_new(TLS_client_method());
	if (sslctx == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_CTX_new() failed.");

	if (sandbox(user, true, "proto_tls client: %s", dstaddr) != 0)
		pjdlog_exitx(EX_CONFIG, "Unable to sandbox TLS client.");
	pjdlog_debug(1, "Privileges successfully dropped.");

	SSL_CTX_set_options(sslctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

	/* Load CA certs. */
	/* TODO */
	//SSL_CTX_load_verify_locations(sslctx, cacerts_file, NULL);

	ssl = SSL_new(sslctx);
	if (ssl == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_new() failed.");

	tcpfd = proto_descriptor(tcp);

	block(tcpfd);

	if (SSL_set_fd(ssl, tcpfd) != 1)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_set_fd() failed.");

	ret = SSL_connect(ssl);
	ssl_check_error(ssl, (int)ret);

	nonblock(sockfd);
	nonblock(tcpfd);

	tls_certificate_verify(ssl, fingerprint);

	/*
	 * The following byte is send to make proto_connect_wait() to work.
	 */
	connected = 1;
	for (;;) {
		switch (send(sockfd, &connected, sizeof(connected), 0)) {
		case -1:
			if (errno == EINTR || errno == ENOBUFS)
				continue;
			if (errno == EAGAIN) {
				(void)wait_for_fd(sockfd, -1);
				continue;
			}
			pjdlog_exit(EX_TEMPFAIL, "send() failed");
		case 0:
			pjdlog_debug(1, "Connection terminated.");
			exit(0);
		case 1:
			break;
		}
		break;
	}

	tls_loop(sockfd, ssl);
}

static void
tls_call_exec_client(struct proto_conn *sock, const char *srcaddr,
    const char *dstaddr, int timeout)
{
	char *timeoutstr, *startfdstr, *debugstr;
	int startfd;

	/* Declare that we are receiver. */
	proto_recv(sock, NULL, 0);

	if (pjdlog_mode_get() == PJDLOG_MODE_STD)
		startfd = 3;
	else /* if (pjdlog_mode_get() == PJDLOG_MODE_SYSLOG) */
		startfd = 0;

	if (proto_descriptor(sock) != startfd) {
		/* Move socketpair descriptor to descriptor number startfd. */
		if (dup2(proto_descriptor(sock), startfd) == -1)
			pjdlog_exit(EX_OSERR, "dup2() failed");
		proto_close(sock);
	} else {
		/*
		 * The FD_CLOEXEC is cleared by dup2(2), so when we not
		 * call it, we have to clear it by hand in case it is set.
		 */
		if (fcntl(startfd, F_SETFD, 0) == -1)
			pjdlog_exit(EX_OSERR, "fcntl() failed");
	}

	closefrom(startfd + 1);

	if (asprintf(&startfdstr, "%d", startfd) == -1)
		pjdlog_exit(EX_TEMPFAIL, "asprintf() failed");
	if (timeout == -1)
		timeout = TLS_DEFAULT_TIMEOUT;
	if (asprintf(&timeoutstr, "%d", timeout) == -1)
		pjdlog_exit(EX_TEMPFAIL, "asprintf() failed");
	if (asprintf(&debugstr, "%d", pjdlog_debug_get()) == -1)
		pjdlog_exit(EX_TEMPFAIL, "asprintf() failed");

	execl(proto_get("execpath"), proto_get("execpath"), "proto", "tls",
	    proto_get("user"), "client", startfdstr,
	    srcaddr == NULL ? "" : srcaddr, dstaddr,
	    proto_get("tls:fingerprint"), proto_get("tcp:port"), timeoutstr,
	    debugstr, NULL);
	pjdlog_exit(EX_SOFTWARE, "execl() failed");
}

static int
tls_connect(const char *srcaddr, const char *dstaddr, int timeout, void **ctxp)
{
	struct tls_ctx *tlsctx;
	struct proto_conn *sock;
	pid_t pid;
	int error;

	PJDLOG_ASSERT(srcaddr == NULL || srcaddr[0] != '\0');
	PJDLOG_ASSERT(dstaddr != NULL);
	PJDLOG_ASSERT(timeout >= -1);
	PJDLOG_ASSERT(ctxp != NULL);

	if (strncmp(dstaddr, "tls://", 6) != 0)
		return (-1);
	if (srcaddr != NULL && strncmp(srcaddr, "tls://", 6) != 0)
		return (-1);

	if (proto_connect(NULL, "socketpair://", -1, &sock) == -1)
		return (errno);

#if 0
	/*
	 * We use rfork() with the following flags to disable SIGCHLD
	 * delivery upon the sandbox process exit.
	 */
	pid = rfork(RFFDG | RFPROC | RFTSIGZMB | RFTSIGFLAGS(0));
#else
	/*
	 * We don't use rfork() to be able to log information about sandbox
	 * process exiting.
	 */
	pid = fork();
#endif
	switch (pid) {
	case -1:
		/* Failure. */
		error = errno;
		proto_close(sock);
		return (error);
	case 0:
		/* Child. */
		pjdlog_prefix_set("[TLS sandbox] (client) ");
#ifdef HAVE_SETPROCTITLE
		setproctitle("[TLS sandbox] (client) ");
#endif
		tls_call_exec_client(sock, srcaddr, dstaddr, timeout);
		/* NOTREACHED */
	default:
		/* Parent. */
		tlsctx = calloc(1, sizeof(*tlsctx));
		if (tlsctx == NULL) {
			error = errno;
			proto_close(sock);
			(void)kill(pid, SIGKILL);
			return (error);
		}
		proto_send(sock, NULL, 0);
		tlsctx->tls_sock = sock;
		tlsctx->tls_tcp = NULL;
		tlsctx->tls_side = TLS_SIDE_CLIENT;
		tlsctx->tls_wait_called = false;
		tlsctx->tls_magic = TLS_CTX_MAGIC;
		if (timeout >= 0) {
			error = tls_connect_wait(tlsctx, timeout);
			if (error != 0) {
				(void)kill(pid, SIGKILL);
				tls_close(tlsctx);
				return (error);
			}
		}
		*ctxp = tlsctx;
		return (0);
	}
}

static int
tls_connect_wait(void *ctx, int timeout)
{
	struct tls_ctx *tlsctx = ctx;
	int error, sockfd;
	uint8_t connected;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_side == TLS_SIDE_CLIENT);
	PJDLOG_ASSERT(tlsctx->tls_sock != NULL);
	PJDLOG_ASSERT(!tlsctx->tls_wait_called);
	PJDLOG_ASSERT(timeout >= 0);

	sockfd = proto_descriptor(tlsctx->tls_sock);
	error = wait_for_fd(sockfd, timeout);
	if (error != 0)
		return (error);

	for (;;) {
		switch (recv(sockfd, &connected, sizeof(connected),
		    MSG_WAITALL)) {
		case -1:
			if (errno == EINTR || errno == ENOBUFS)
				continue;
			error = errno;
			break;
		case 0:
			pjdlog_debug(1, "Connection terminated.");
			error = ENOTCONN;
			break;
		case 1:
			tlsctx->tls_wait_called = true;
			break;
		}
		break;
	}

	return (error);
}

static int
tls_server(const char *lstaddr, void **ctxp)
{
	struct proto_conn *tcp;
	struct tls_ctx *tlsctx;
	char *laddr;
	int error;

	if (strncmp(lstaddr, "tls://", 6) != 0)
		return (-1);

	tlsctx = malloc(sizeof(*tlsctx));
	if (tlsctx == NULL) {
		pjdlog_warning("Unable to allocate memory.");
		return (ENOMEM);
	}

	laddr = strdup(lstaddr);
	if (laddr == NULL) {
		free(tlsctx);
		pjdlog_warning("Unable to allocate memory.");
		return (ENOMEM);
	}
	bcopy("tcp://", laddr, 6);

	if (proto_server(laddr, &tcp) == -1) {
		error = errno;
		free(tlsctx);
		free(laddr);
		return (error);
	}
	free(laddr);

	tlsctx->tls_sock = NULL;
	tlsctx->tls_tcp = tcp;
	tlsctx->tls_side = TLS_SIDE_SERVER_LISTEN;
	tlsctx->tls_wait_called = true;
	tlsctx->tls_magic = TLS_CTX_MAGIC;
	*ctxp = tlsctx;

	return (0);
}

static void
tls_exec_server(const char *user, int startfd, const char *privkey,
    const char *cert, int debuglevel)
{
	SSL_CTX *sslctx;
	SSL *ssl;
	int sockfd, tcpfd, ret;

	pjdlog_debug_set(debuglevel);
	pjdlog_prefix_set("[TLS sandbox] (server) ");
#ifdef HAVE_SETPROCTITLE
	setproctitle("[TLS sandbox] (server) ");
#endif

	sockfd = startfd;
	tcpfd = startfd + 1;

	SSL_load_error_strings();
	SSL_library_init();

	sslctx = SSL_CTX_new(TLS_server_method());
	if (sslctx == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_CTX_new() failed.");

	SSL_CTX_set_options(sslctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

	ssl = SSL_new(sslctx);
	if (ssl == NULL)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_new() failed.");

	if (SSL_use_RSAPrivateKey_file(ssl, privkey, SSL_FILETYPE_PEM) != 1) {
		ssl_log_errors();
		pjdlog_exitx(EX_CONFIG,
		    "SSL_use_RSAPrivateKey_file(%s) failed.", privkey);
	}

	if (SSL_use_certificate_file(ssl, cert, SSL_FILETYPE_PEM) != 1) {
		ssl_log_errors();
		pjdlog_exitx(EX_CONFIG, "SSL_use_certificate_file(%s) failed.",
		    cert);
	}

	if (sandbox(user, true, "proto_tls server") != 0)
		pjdlog_exitx(EX_CONFIG, "Unable to sandbox TLS server.");
	pjdlog_debug(1, "Privileges successfully dropped.");

	nonblock(sockfd);
	nonblock(tcpfd);

	if (SSL_set_fd(ssl, tcpfd) != 1)
		pjdlog_exitx(EX_TEMPFAIL, "SSL_set_fd() failed.");

	ret = SSL_accept(ssl);
	ssl_check_error(ssl, ret);

	tls_loop(sockfd, ssl);
}

static void
tls_call_exec_server(struct proto_conn *sock, struct proto_conn *tcp)
{
	int startfd, sockfd, tcpfd, safefd;
	char *startfdstr, *debugstr;

	if (pjdlog_mode_get() == PJDLOG_MODE_STD)
		startfd = 3;
	else /* if (pjdlog_mode_get() == PJDLOG_MODE_SYSLOG) */
		startfd = 0;

	/* Declare that we are receiver. */
	proto_send(sock, NULL, 0);

	sockfd = proto_descriptor(sock);
	tcpfd = proto_descriptor(tcp);

	safefd = MAX(sockfd, tcpfd);
	safefd = MAX(safefd, startfd);
	safefd++;

	/* Move sockfd and tcpfd to safe numbers first. */
	if (dup2(sockfd, safefd) == -1)
		pjdlog_exit(EX_OSERR, "dup2() failed");
	proto_close(sock);
	sockfd = safefd;
	if (dup2(tcpfd, safefd + 1) == -1)
		pjdlog_exit(EX_OSERR, "dup2() failed");
	proto_close(tcp);
	tcpfd = safefd + 1;

	/* Move socketpair descriptor to descriptor number startfd. */
	if (dup2(sockfd, startfd) == -1)
		pjdlog_exit(EX_OSERR, "dup2() failed");
	(void)close(sockfd);
	/* Move tcp descriptor to descriptor number startfd + 1. */
	if (dup2(tcpfd, startfd + 1) == -1)
		pjdlog_exit(EX_OSERR, "dup2() failed");
	(void)close(tcpfd);

	closefrom(startfd + 2);

	/*
	 * Even if FD_CLOEXEC was set on descriptors before dup2(), it should
	 * have been cleared on dup2(), but better be safe than sorry.
	 */
	if (fcntl(startfd, F_SETFD, 0) == -1)
		pjdlog_exit(EX_OSERR, "fcntl() failed");
	if (fcntl(startfd + 1, F_SETFD, 0) == -1)
		pjdlog_exit(EX_OSERR, "fcntl() failed");

	if (asprintf(&startfdstr, "%d", startfd) == -1)
		pjdlog_exit(EX_TEMPFAIL, "asprintf() failed");
	if (asprintf(&debugstr, "%d", pjdlog_debug_get()) == -1)
		pjdlog_exit(EX_TEMPFAIL, "asprintf() failed");

	execl(proto_get("execpath"), proto_get("execpath"), "proto", "tls",
	    proto_get("user"), "server", startfdstr, proto_get("tls:keyfile"),
	    proto_get("tls:certfile"), debugstr, NULL);
	pjdlog_exit(EX_SOFTWARE, "execl() failed");
}

static int
tls_accept(void *ctx, void **newctxp)
{
	struct tls_ctx *tlsctx = ctx;
	struct tls_ctx *newtlsctx;
	struct proto_conn *sock, *tcp;
	pid_t pid;
	int error;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_side == TLS_SIDE_SERVER_LISTEN);

	if (proto_connect(NULL, "socketpair://", -1, &sock) == -1)
		return (errno);

	/* Accept TCP connection. */
	if (proto_accept(tlsctx->tls_tcp, &tcp) == -1) {
		error = errno;
		proto_close(sock);
		return (error);
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Failure. */
		error = errno;
		proto_close(sock);
		return (error);
	case 0:
		/* Child. */
		pjdlog_prefix_set("[TLS sandbox] (server) ");
#ifdef HAVE_SETPROCTITLE
		setproctitle("[TLS sandbox] (server) ");
#endif
		/* Close listen socket. */
		proto_close(tlsctx->tls_tcp);
		tls_call_exec_server(sock, tcp);
		/* NOTREACHED */
		PJDLOG_ABORT("Unreachable.");
	default:
		/* Parent. */
		newtlsctx = calloc(1, sizeof(*tlsctx));
		if (newtlsctx == NULL) {
			error = errno;
			proto_close(sock);
			proto_close(tcp);
			(void)kill(pid, SIGKILL);
			return (error);
		}
		proto_local_address(tcp, newtlsctx->tls_laddr,
		    sizeof(newtlsctx->tls_laddr));
		PJDLOG_ASSERT(strncmp(newtlsctx->tls_laddr, "tcp://", 6) == 0);
		bcopy("tls://", newtlsctx->tls_laddr, 6);
		*strrchr(newtlsctx->tls_laddr, ':') = '\0';
		proto_remote_address(tcp, newtlsctx->tls_raddr,
		    sizeof(newtlsctx->tls_raddr));
		PJDLOG_ASSERT(strncmp(newtlsctx->tls_raddr, "tcp://", 6) == 0);
		bcopy("tls://", newtlsctx->tls_raddr, 6);
		*strrchr(newtlsctx->tls_raddr, ':') = '\0';
		proto_close(tcp);
		proto_recv(sock, NULL, 0);
		newtlsctx->tls_sock = sock;
		newtlsctx->tls_tcp = NULL;
		newtlsctx->tls_wait_called = true;
		newtlsctx->tls_side = TLS_SIDE_SERVER_WORK;
		newtlsctx->tls_magic = TLS_CTX_MAGIC;
		*newctxp = newtlsctx;
		return (0);
	}
}

static int
tls_wrap(int fd, bool client, void **ctxp)
{
	struct tls_ctx *tlsctx;
	struct proto_conn *sock;
	int error;

	tlsctx = calloc(1, sizeof(*tlsctx));
	if (tlsctx == NULL)
		return (errno);

	if (proto_wrap("socketpair", client, fd, &sock) == -1) {
		error = errno;
		free(tlsctx);
		return (error);
	}

	tlsctx->tls_sock = sock;
	tlsctx->tls_tcp = NULL;
	tlsctx->tls_wait_called = (client ? false : true);
	tlsctx->tls_side = (client ? TLS_SIDE_CLIENT : TLS_SIDE_SERVER_WORK);
	tlsctx->tls_magic = TLS_CTX_MAGIC;
	*ctxp = tlsctx;

	return (0);
}

static int
tls_send(void *ctx, const unsigned char *data, size_t size, int fd)
{
	struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_side == TLS_SIDE_CLIENT ||
	    tlsctx->tls_side == TLS_SIDE_SERVER_WORK);
	PJDLOG_ASSERT(tlsctx->tls_sock != NULL);
	PJDLOG_ASSERT(tlsctx->tls_wait_called);
	PJDLOG_ASSERT(fd == -1);

	if (proto_send(tlsctx->tls_sock, data, size) == -1)
		return (errno);

	return (0);
}

static int
tls_recv(void *ctx, unsigned char *data, size_t size, int *fdp)
{
	struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_side == TLS_SIDE_CLIENT ||
	    tlsctx->tls_side == TLS_SIDE_SERVER_WORK);
	PJDLOG_ASSERT(tlsctx->tls_sock != NULL);
	PJDLOG_ASSERT(tlsctx->tls_wait_called);
	PJDLOG_ASSERT(fdp == NULL);

	if (proto_recv(tlsctx->tls_sock, data, size) == -1)
		return (errno);

	return (0);
}

static int
tls_descriptor(const void *ctx)
{
	const struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);

	switch (tlsctx->tls_side) {
	case TLS_SIDE_CLIENT:
	case TLS_SIDE_SERVER_WORK:
		PJDLOG_ASSERT(tlsctx->tls_sock != NULL);

		return (proto_descriptor(tlsctx->tls_sock));
	case TLS_SIDE_SERVER_LISTEN:
		PJDLOG_ASSERT(tlsctx->tls_tcp != NULL);

		return (proto_descriptor(tlsctx->tls_tcp));
	default:
		PJDLOG_ABORT("Invalid side (%d).", tlsctx->tls_side);
	}
}

static bool
tcp_address_match(const void *ctx, const char *addr)
{
	const struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);

	return (strcmp(tlsctx->tls_raddr, addr) == 0);
}

static void
tls_local_address(const void *ctx, char *addr, size_t size)
{
	const struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_wait_called);

	switch (tlsctx->tls_side) {
	case TLS_SIDE_CLIENT:
		PJDLOG_ASSERT(tlsctx->tls_sock != NULL);

		PJDLOG_VERIFY(strlcpy(addr, "tls://N/A", size) < size);
		break;
	case TLS_SIDE_SERVER_WORK:
		PJDLOG_ASSERT(tlsctx->tls_sock != NULL);

		PJDLOG_VERIFY(strlcpy(addr, tlsctx->tls_laddr, size) < size);
		break;
	case TLS_SIDE_SERVER_LISTEN:
		PJDLOG_ASSERT(tlsctx->tls_tcp != NULL);

		proto_local_address(tlsctx->tls_tcp, addr, size);
		PJDLOG_ASSERT(strncmp(addr, "tcp://", 6) == 0);
		/* Replace tcp:// prefix with tls:// */
		bcopy("tls://", addr, 6);
		break;
	default:
		PJDLOG_ABORT("Invalid side (%d).", tlsctx->tls_side);
	}
}

static void
tls_remote_address(const void *ctx, char *addr, size_t size)
{
	const struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);
	PJDLOG_ASSERT(tlsctx->tls_wait_called);

	switch (tlsctx->tls_side) {
	case TLS_SIDE_CLIENT:
		PJDLOG_ASSERT(tlsctx->tls_sock != NULL);

		PJDLOG_VERIFY(strlcpy(addr, "tls://N/A", size) < size);
		break;
	case TLS_SIDE_SERVER_WORK:
		PJDLOG_ASSERT(tlsctx->tls_sock != NULL);

		PJDLOG_VERIFY(strlcpy(addr, tlsctx->tls_raddr, size) < size);
		break;
	case TLS_SIDE_SERVER_LISTEN:
		PJDLOG_ASSERT(tlsctx->tls_tcp != NULL);

		proto_remote_address(tlsctx->tls_tcp, addr, size);
		PJDLOG_ASSERT(strncmp(addr, "tcp://", 6) == 0);
		/* Replace tcp:// prefix with tls:// */
		bcopy("tls://", addr, 6);
		break;
	default:
		PJDLOG_ABORT("Invalid side (%d).", tlsctx->tls_side);
	}
}

static void
tls_close(void *ctx)
{
	struct tls_ctx *tlsctx = ctx;

	PJDLOG_ASSERT(tlsctx != NULL);
	PJDLOG_ASSERT(tlsctx->tls_magic == TLS_CTX_MAGIC);

	if (tlsctx->tls_sock != NULL) {
		proto_close(tlsctx->tls_sock);
		tlsctx->tls_sock = NULL;
	}
	if (tlsctx->tls_tcp != NULL) {
		proto_close(tlsctx->tls_tcp);
		tlsctx->tls_tcp = NULL;
	}
	tlsctx->tls_side = 0;
	tlsctx->tls_magic = 0;
	free(tlsctx);
}

static int
tls_exec(int argc, char *argv[])
{

	PJDLOG_ASSERT(argc > 3);
	PJDLOG_ASSERT(strcmp(argv[0], "tls") == 0);

	pjdlog_init(atoi(argv[3]) == 0 ? PJDLOG_MODE_SYSLOG : PJDLOG_MODE_STD);

	if (strcmp(argv[2], "client") == 0) {
		if (argc != 10)
			return (EINVAL);
		tls_exec_client(argv[1], atoi(argv[3]),
		    argv[4][0] == '\0' ? NULL : argv[4], argv[5], argv[6],
		    argv[7], atoi(argv[8]), atoi(argv[9]));
	} else if (strcmp(argv[2], "server") == 0) {
		if (argc != 7)
			return (EINVAL);
		tls_exec_server(argv[1], atoi(argv[3]), argv[4], argv[5],
		    atoi(argv[6]));
	}
	return (EINVAL);
}

static struct proto tls_proto = {
	.prt_name = "tls",
	.prt_connect = tls_connect,
	.prt_connect_wait = tls_connect_wait,
	.prt_server = tls_server,
	.prt_accept = tls_accept,
	.prt_wrap = tls_wrap,
	.prt_send = tls_send,
	.prt_recv = tls_recv,
	.prt_descriptor = tls_descriptor,
	.prt_address_match = tcp_address_match,
	.prt_local_address = tls_local_address,
	.prt_remote_address = tls_remote_address,
	.prt_close = tls_close,
	.prt_exec = tls_exec
};

static __constructor void
tls_ctor(void)
{

	proto_register(&tls_proto, false);
}

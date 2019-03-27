/*	$NetBSD: bl.c,v 1.28 2016/07/29 17:13:09 christos Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/cdefs.h>
__RCSID("$NetBSD: bl.c,v 1.28 2016/07/29 17:13:09 christos Exp $");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>
#include <netinet/in.h>
#ifdef _REENTRANT
#include <pthread.h>
#endif

#include "bl.h"

typedef struct {
	uint32_t bl_len;
	uint32_t bl_version;
	uint32_t bl_type;
	uint32_t bl_salen;
	struct sockaddr_storage bl_ss;
	char bl_data[];
} bl_message_t;

struct blacklist {
#ifdef _REENTRANT
	pthread_mutex_t b_mutex;
# define BL_INIT(b)	pthread_mutex_init(&b->b_mutex, NULL)
# define BL_LOCK(b)	pthread_mutex_lock(&b->b_mutex)
# define BL_UNLOCK(b)	pthread_mutex_unlock(&b->b_mutex)
#else
# define BL_INIT(b)	do {} while(/*CONSTCOND*/0)
# define BL_LOCK(b)	BL_INIT(b)
# define BL_UNLOCK(b)	BL_INIT(b)
#endif
	int b_fd;
	int b_connected;
	struct sockaddr_un b_sun;
	void (*b_fun)(int, const char *, va_list);
	bl_info_t b_info;
};

#define BL_VERSION	1

bool
bl_isconnected(bl_t b)
{
	return b->b_connected == 0;
}

int
bl_getfd(bl_t b)
{
	return b->b_fd;
}

static void
bl_reset(bl_t b, bool locked)
{
	int serrno = errno;
	if (!locked)
		BL_LOCK(b);
	close(b->b_fd);
	errno = serrno;
	b->b_fd = -1;
	b->b_connected = -1;
	if (!locked)
		BL_UNLOCK(b);
}

static void
bl_log(void (*fun)(int, const char *, va_list), int level,
    const char *fmt, ...)
{
	va_list ap;
	int serrno = errno;

	va_start(ap, fmt);
	(*fun)(level, fmt, ap);
	va_end(ap);
	errno = serrno;
}

static int
bl_init(bl_t b, bool srv)
{
	static int one = 1;
	/* AF_UNIX address of local logger */
	mode_t om;
	int rv, serrno;
	struct sockaddr_un *sun = &b->b_sun;

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef SOCK_NOSIGPIPE
#define SOCK_NOSIGPIPE 0
#endif

	BL_LOCK(b);

	if (b->b_fd == -1) {
		b->b_fd = socket(PF_LOCAL,
		    SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK|SOCK_NOSIGPIPE, 0);
		if (b->b_fd == -1) {
			bl_log(b->b_fun, LOG_ERR, "%s: socket failed (%s)",
			    __func__, strerror(errno));
			BL_UNLOCK(b);
			return -1;
		}
#if SOCK_CLOEXEC == 0
		fcntl(b->b_fd, F_SETFD, FD_CLOEXEC);
#endif
#if SOCK_NONBLOCK == 0
		fcntl(b->b_fd, F_SETFL, fcntl(b->b_fd, F_GETFL) | O_NONBLOCK);
#endif
#if SOCK_NOSIGPIPE == 0
#ifdef SO_NOSIGPIPE
		int o = 1;
		setsockopt(b->b_fd, SOL_SOCKET, SO_NOSIGPIPE, &o, sizeof(o));
#else
		signal(SIGPIPE, SIG_IGN);
#endif
#endif
	}

	if (bl_isconnected(b)) {
		BL_UNLOCK(b);
		return 0;
	}

	/*
	 * We try to connect anyway even when we are a server to verify
	 * that no other server is listening to the socket. If we succeed
	 * to connect and we are a server, someone else owns it.
	 */
	rv = connect(b->b_fd, (const void *)sun, (socklen_t)sizeof(*sun));
	if (rv == 0) {
		if (srv) {
			bl_log(b->b_fun, LOG_ERR,
			    "%s: another daemon is handling `%s'",
			    __func__, sun->sun_path);
			goto out;
		}
	} else {
		if (!srv) {
			/*
			 * If the daemon is not running, we just try a
			 * connect, so leave the socket alone until it does
			 * and only log once.
			 */
			if (b->b_connected != 1) {
				bl_log(b->b_fun, LOG_DEBUG,
				    "%s: connect failed for `%s' (%s)",
				    __func__, sun->sun_path, strerror(errno));
				b->b_connected = 1;
			}
			BL_UNLOCK(b);
			return -1;
		}
		bl_log(b->b_fun, LOG_DEBUG, "Connected to blacklist server",
		    __func__);
	}

	if (srv) {
		(void)unlink(sun->sun_path);
		om = umask(0);
		rv = bind(b->b_fd, (const void *)sun, (socklen_t)sizeof(*sun));
		serrno = errno;
		(void)umask(om);
		errno = serrno;
		if (rv == -1) {
			bl_log(b->b_fun, LOG_ERR,
			    "%s: bind failed for `%s' (%s)",
			    __func__, sun->sun_path, strerror(errno));
			goto out;
		}
	}

	b->b_connected = 0;
#define GOT_FD		1
#if defined(LOCAL_CREDS)
#define CRED_LEVEL	0
#define	CRED_NAME	LOCAL_CREDS
#define CRED_SC_UID	sc_euid
#define CRED_SC_GID	sc_egid
#define CRED_MESSAGE	SCM_CREDS
#define CRED_SIZE	SOCKCREDSIZE(NGROUPS_MAX)
#define CRED_TYPE	struct sockcred
#define GOT_CRED	2
#elif defined(SO_PASSCRED)
#define CRED_LEVEL	SOL_SOCKET
#define	CRED_NAME	SO_PASSCRED
#define CRED_SC_UID	uid
#define CRED_SC_GID	gid
#define CRED_MESSAGE	SCM_CREDENTIALS
#define CRED_SIZE	sizeof(struct ucred)
#define CRED_TYPE	struct ucred
#define GOT_CRED	2
#else
#define GOT_CRED	0
/*
 * getpeereid() and LOCAL_PEERCRED don't help here
 * because we are not a stream socket!
 */
#define	CRED_SIZE	0
#define CRED_TYPE	void * __unused
#endif

#ifdef CRED_LEVEL
	if (setsockopt(b->b_fd, CRED_LEVEL, CRED_NAME,
	    &one, (socklen_t)sizeof(one)) == -1) {
		bl_log(b->b_fun, LOG_ERR, "%s: setsockopt %s "
		    "failed (%s)", __func__, __STRING(CRED_NAME),
		    strerror(errno));
		goto out;
	}
#endif

	BL_UNLOCK(b);
	return 0;
out:
	bl_reset(b, true);
	BL_UNLOCK(b);
	return -1;
}

bl_t
bl_create(bool srv, const char *path, void (*fun)(int, const char *, va_list))
{
	bl_t b = calloc(1, sizeof(*b));
	if (b == NULL)
		goto out;
	b->b_fun = fun == NULL ? vsyslog : fun;
	b->b_fd = -1;
	b->b_connected = -1;
	BL_INIT(b);

	memset(&b->b_sun, 0, sizeof(b->b_sun));
	b->b_sun.sun_family = AF_LOCAL;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	b->b_sun.sun_len = sizeof(b->b_sun);
#endif
	strlcpy(b->b_sun.sun_path,
	    path ? path : _PATH_BLSOCK, sizeof(b->b_sun.sun_path));

	bl_init(b, srv);
	return b;
out:
	free(b);
	bl_log(fun, LOG_ERR, "%s: malloc failed (%s)", __func__,
	    strerror(errno));
	return NULL;
}

void
bl_destroy(bl_t b)
{
	bl_reset(b, false);
	free(b);
}

static int
bl_getsock(bl_t b, struct sockaddr_storage *ss, const struct sockaddr *sa,
    socklen_t slen, const char *ctx)
{
	uint8_t family;

	memset(ss, 0, sizeof(*ss));

	switch (slen) {
	case 0:
		return 0;
	case sizeof(struct sockaddr_in):
		family = AF_INET;
		break;
	case sizeof(struct sockaddr_in6):
		family = AF_INET6;
		break;
	default:
		bl_log(b->b_fun, LOG_ERR, "%s: invalid socket len %u (%s)",
		    __func__, (unsigned)slen, ctx);
		errno = EINVAL;
		return -1;
	}

	memcpy(ss, sa, slen);

	if (ss->ss_family != family) {
		bl_log(b->b_fun, LOG_INFO,
		    "%s: correcting socket family %d to %d (%s)",
		    __func__, ss->ss_family, family, ctx);
		ss->ss_family = family;
	}

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	if (ss->ss_len != slen) {
		bl_log(b->b_fun, LOG_INFO,
		    "%s: correcting socket len %u to %u (%s)",
		    __func__, ss->ss_len, (unsigned)slen, ctx);
		ss->ss_len = (uint8_t)slen;
	}
#endif
	return 0;
}

int
bl_send(bl_t b, bl_type_t e, int pfd, const struct sockaddr *sa,
    socklen_t slen, const char *ctx)
{
	struct msghdr   msg;
	struct iovec    iov;
	union {
		char ctrl[CMSG_SPACE(sizeof(int))];
		uint32_t fd;
	} ua;
	struct cmsghdr *cmsg;
	union {
		bl_message_t bl;
		char buf[512];
	} ub;
	size_t ctxlen, tried;
#define NTRIES	5

	ctxlen = strlen(ctx);
	if (ctxlen > 128)
		ctxlen = 128;

	iov.iov_base = ub.buf;
	iov.iov_len = sizeof(bl_message_t) + ctxlen;
	ub.bl.bl_len = (uint32_t)iov.iov_len;
	ub.bl.bl_version = BL_VERSION;
	ub.bl.bl_type = (uint32_t)e;

	if (bl_getsock(b, &ub.bl.bl_ss, sa, slen, ctx) == -1)
		return -1;


	ub.bl.bl_salen = slen;
	memcpy(ub.bl.bl_data, ctx, ctxlen);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	msg.msg_control = ua.ctrl;
	msg.msg_controllen = sizeof(ua.ctrl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	memcpy(CMSG_DATA(cmsg), &pfd, sizeof(pfd));

	tried = 0;
again:
	if (bl_init(b, false) == -1)
		return -1;

	if ((sendmsg(b->b_fd, &msg, 0) == -1) && tried++ < NTRIES) {
		bl_reset(b, false);
		goto again;
	}
	return tried >= NTRIES ? -1 : 0;
}

bl_info_t *
bl_recv(bl_t b)
{
        struct msghdr   msg;
        struct iovec    iov;
	union {
		char ctrl[CMSG_SPACE(sizeof(int)) + CMSG_SPACE(CRED_SIZE)];
		uint32_t fd;
		CRED_TYPE sc;
	} ua;
	struct cmsghdr *cmsg;
	CRED_TYPE *sc;
	union {
		bl_message_t bl;
		char buf[512];
	} ub;
	int got;
	ssize_t rlen;
	bl_info_t *bi = &b->b_info;

	got = 0;
	memset(bi, 0, sizeof(*bi));

	iov.iov_base = ub.buf;
	iov.iov_len = sizeof(ub);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	msg.msg_control = ua.ctrl;
	msg.msg_controllen = sizeof(ua.ctrl) + 100;

        rlen = recvmsg(b->b_fd, &msg, 0);
        if (rlen == -1) {
		bl_log(b->b_fun, LOG_ERR, "%s: recvmsg failed (%s)", __func__,
		    strerror(errno));
		return NULL;
        }

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET) {
			bl_log(b->b_fun, LOG_ERR,
			    "%s: unexpected cmsg_level %d",
			    __func__, cmsg->cmsg_level);
			continue;
		}
		switch (cmsg->cmsg_type) {
		case SCM_RIGHTS:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
				bl_log(b->b_fun, LOG_ERR,
				    "%s: unexpected cmsg_len %d != %zu",
				    __func__, cmsg->cmsg_len,
				    CMSG_LEN(2 * sizeof(int)));
				continue;
			}
			memcpy(&bi->bi_fd, CMSG_DATA(cmsg), sizeof(bi->bi_fd));
			got |= GOT_FD;
			break;
#ifdef CRED_MESSAGE
		case CRED_MESSAGE:
			sc = (void *)CMSG_DATA(cmsg);
			bi->bi_uid = sc->CRED_SC_UID;
			bi->bi_gid = sc->CRED_SC_GID;
			got |= GOT_CRED;
			break;
#endif
		default:
			bl_log(b->b_fun, LOG_ERR,
			    "%s: unexpected cmsg_type %d",
			    __func__, cmsg->cmsg_type);
			continue;
		}

	}

	if (got != (GOT_CRED|GOT_FD)) {
		bl_log(b->b_fun, LOG_ERR, "message missing %s %s", 
#if GOT_CRED != 0
		    (got & GOT_CRED) == 0 ? "cred" :
#endif
		    "", (got & GOT_FD) == 0 ? "fd" : "");
			
		return NULL;
	}

	if ((size_t)rlen <= sizeof(ub.bl)) {
		bl_log(b->b_fun, LOG_ERR, "message too short %zd", rlen);
		return NULL;
	}

	if (ub.bl.bl_version != BL_VERSION) {
		bl_log(b->b_fun, LOG_ERR, "bad version %d", ub.bl.bl_version);
		return NULL;
	}

	bi->bi_type = ub.bl.bl_type;
	bi->bi_slen = ub.bl.bl_salen;
	bi->bi_ss = ub.bl.bl_ss;
#ifndef CRED_MESSAGE
	bi->bi_uid = -1;
	bi->bi_gid = -1;
#endif
	strlcpy(bi->bi_msg, ub.bl.bl_data, MIN(sizeof(bi->bi_msg),
	    ((size_t)rlen - sizeof(ub.bl) + 1)));
	return bi;
}

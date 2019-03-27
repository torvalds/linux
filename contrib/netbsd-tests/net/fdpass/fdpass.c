/*	$NetBSD: fdpass.c,v 1.1 2012/08/13 11:15:05 christos Exp $	*/
/* $OpenBSD: monitor_fdpass.c,v 1.19 2010/01/12 00:58:25 djm Exp $ */
/*
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: fdpass.c,v 1.1 2012/08/13 11:15:05 christos Exp $");
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

static int debug;

static int
send_fd(int sock, int fd)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[1024];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec vec;
	char ch = '\0';
	ssize_t n;
	struct pollfd pfd;

	if (sizeof(cmsgbuf.buf) < CMSG_SPACE(sizeof(int)))
		errx(1, "%s: %zu < %zu, recompile", __func__, 
		    sizeof(cmsgbuf.buf), CMSG_SPACE(sizeof(int)));

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;
	msg.msg_controllen = cmsg->cmsg_len;

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	pfd.fd = sock;
	pfd.events = POLLOUT;
	while ((n = sendmsg(sock, &msg, 0)) == -1 &&
	    (errno == EAGAIN || errno == EINTR)) {
		(void)poll(&pfd, 1, -1);
	}
	switch (n) {
	case -1:
		err(1, "%s: sendmsg(%d)", __func__, fd);
	case 1:
		if (debug)
			fprintf(stderr, "%d: send fd %d\n", getpid(), fd);
		return 0;
	default:
		errx(1, "%s: sendmsg: expected sent 1 got %ld",
		    __func__, (long)n);
	}
}

static int
recv_fd(int sock)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[1024];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec vec;
	ssize_t n;
	char ch;
	int fd;
	struct pollfd pfd;

	if (sizeof(cmsgbuf.buf) < CMSG_SPACE(sizeof(int)))
		errx(1, "%s: %zu < %zu, recompile", __func__, 
		    sizeof(cmsgbuf.buf), CMSG_SPACE(sizeof(int)));

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));

	pfd.fd = sock;
	pfd.events = POLLIN;
	while ((n = recvmsg(sock, &msg, 0)) == -1 &&
	    (errno == EAGAIN || errno == EINTR)) {
		(void)poll(&pfd, 1, -1);
	}
	switch (n) {
	case -1:
		err(1, "%s: recvmsg", __func__);
	case 1:
		break;
	default:
		errx(1, "%s: recvmsg: expected received 1 got %ld",
		    __func__, (long)n);
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL)
		errx(1, "%s: no message header", __func__);

	if (cmsg->cmsg_type != SCM_RIGHTS)
		err(1, "%s: expected type %d got %d", __func__,
		    SCM_RIGHTS, cmsg->cmsg_type);
	fd = (*(int *)CMSG_DATA(cmsg));
	if (debug)
		fprintf(stderr, "%d: recv fd %d\n", getpid(), fd);
	return fd;
}

static void usage(void) __attribute__((__noreturn__));

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-vd] -i <input> -o <output>\n"
	    "\t %s [-v] -p <progname>\n", getprogname(), getprogname());
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int s[2], fd, status, c, verbose;
	char buf[1024], *prog;

	prog = NULL;
	s[0] = s[1] = -1;
	verbose = 0;

	while ((c = getopt(argc, argv, "di:o:p:")) != -1)
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'i':
			s[0] = atoi(optarg);
			break;
		case 'o':
			s[1] = atoi(optarg);
			break;
		case 'p':
			prog = optarg;
			break;
		default:
			usage();
		}

	if ((s[0] == -1 && s[1] != -1) || (s[0] != -1 && s[1] == -1))
		usage();

	if (s[0] == -1) {
		if (socketpair(AF_LOCAL, SOCK_DGRAM, 0, s) == -1)
			err(1, "socketpair");
	} else
		goto recv;

	switch (fork()) {
	case -1:
		err(1, "fork");
	default:
		fd = open("foo", O_RDWR|O_CREAT|O_TRUNC, 0666);
		if (fd == -1)
			err(1, "open");
		send_fd(s[0], fd);
		wait(&status);
		return 0;
	case 0:
		if (prog != NULL) {
			char i[64], o[64];
			snprintf(i, sizeof(i), "%d", s[0]);
			snprintf(o, sizeof(o), "%d", s[1]);
			execlp(prog, prog, "-i", i, "-o", o, NULL);
			err(1, "execlp");
		}
	recv:
		fd = recv_fd(s[1]);
		if (verbose) {
			snprintf(buf, sizeof(buf), "ls -l /proc/%d/fd",
			    getpid());
			system(buf);
		}
		if (write(fd, "foo\n", 4) == -1)
			err(1, "write");
		close(fd);
		return 0;
	}
}

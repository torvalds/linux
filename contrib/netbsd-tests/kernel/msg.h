/*	$NetBSD: msg.h,v 1.1 2016/12/05 20:10:10 christos Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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

struct msg_fds {
	int pfd[2];
	int cfd[2];
};

#define CLOSEFD(fd) do { \
	if (fd != -1) { \
		close(fd); \
		fd = -1; \
	} \
} while (/*CONSTCOND*/ 0)

static int
msg_open(struct msg_fds *fds)
{
	if (pipe(fds->pfd) == -1)
		return -1;
	if (pipe(fds->cfd) == -1) {
		close(fds->pfd[0]);
		close(fds->pfd[1]);
		return -1;
	}
	return 0;
}

static void
msg_close(struct msg_fds *fds)
{
	CLOSEFD(fds->pfd[0]);
	CLOSEFD(fds->pfd[1]);
	CLOSEFD(fds->cfd[0]);
	CLOSEFD(fds->cfd[1]);
}

static int
msg_write_child(const char *info, struct msg_fds *fds, void *msg, size_t len)
{
	ssize_t rv;
	CLOSEFD(fds->cfd[1]);
	CLOSEFD(fds->pfd[0]);

	printf("Send %s\n", info);
	rv = write(fds->pfd[1], msg, len);
	if (rv != (ssize_t)len)
		return 1;
//	printf("Wait %s\n", info);
	rv = read(fds->cfd[0], msg, len);
	if (rv != (ssize_t)len)
		return 1;
	return 0;
}

static int
msg_write_parent(const char *info, struct msg_fds *fds, void *msg, size_t len)
{
	ssize_t rv;
	CLOSEFD(fds->pfd[1]);
	CLOSEFD(fds->cfd[0]);

	printf("Send %s\n", info);
	rv = write(fds->cfd[1], msg, len);
	if (rv != (ssize_t)len)
		return 1;
//	printf("Wait %s\n", info);
	rv = read(fds->pfd[0], msg, len);
	if (rv != (ssize_t)len)
		return 1;
	return 0;
}

static int
msg_read_parent(const char *info, struct msg_fds *fds, void *msg, size_t len)
{
	ssize_t rv;
	CLOSEFD(fds->pfd[1]);
	CLOSEFD(fds->cfd[0]);

	printf("Wait %s\n", info);
	rv = read(fds->pfd[0], msg, len);
	if (rv != (ssize_t)len)
		return 1;
//	printf("Send %s\n", info);
	rv = write(fds->cfd[1], msg, len);
	if (rv != (ssize_t)len)
		return 1;
	return 0;
}

static int
msg_read_child(const char *info, struct msg_fds *fds, void *msg, size_t len)
{
	ssize_t rv;
	CLOSEFD(fds->cfd[1]);
	CLOSEFD(fds->pfd[0]);

	printf("Wait %s\n", info);
	rv = read(fds->cfd[0], msg, len);
	if (rv != (ssize_t)len)
		return 1;
//	printf("Send %s\n", info);
	rv = write(fds->pfd[1], msg, len);
	if (rv != (ssize_t)len)
		return 1;
	return 0;
}

/* $NetBSD: t_file.c,v 1.4 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_file.c,v 1.4 2017/01/13 21:30:41 christos Exp $");

#include <sys/param.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <atf-c.h>

#include "h_macros.h"

#define FILENAME "file"
#define NLINES 5

static void
child(void)
{
	int i, n, fd;

	(void)sleep(1);

	for (i = 0; i < NLINES; ++i) {
		fd = open(FILENAME, O_WRONLY|O_APPEND, 0644);
		if (fd < 0)
			err(EXIT_FAILURE, "open()");

		n = write(fd, "foo\n", 4);
		if (n < 0)
			err(EXIT_FAILURE, "write()");

		(void)close(fd);
		(void)sleep(1);
	}
	_exit(0);
}

ATF_TC(file);
ATF_TC_HEAD(file, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks EVFILT_READ on regular file");
}
ATF_TC_BODY(file, tc)
{
	char buffer[128];
	struct kevent event[1];
	pid_t pid;
	int fd, kq, n, num, status;

	RL(pid = fork());
	if (pid == 0) {
		child();
		/* NOTREACHED */
	}

	RL(fd = open(FILENAME, O_RDONLY|O_CREAT, 0644));

#if 1		/* XXX: why was this disabled? */
	RL(lseek(fd, 0, SEEK_END));
#endif

	RL(kq = kqueue());

	EV_SET(&event[0], fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	for (num = 0; num < NLINES;) {
		RL(n = kevent(kq, NULL, 0, event, 1, NULL));
		num += n;

		(void)printf("kevent num %d flags: %#x, fflags: %#x, data: "
#ifdef __FreeBSD__
		    "%" PRIdPTR "\n", n, event[0].flags, event[0].fflags,
#else
		    "%" PRId64 "\n", n, event[0].flags, event[0].fflags,
#endif
		    event[0].data);

		if (event[0].data < 0)
#if 1	/* XXXLUKEM */
			RL(lseek(fd, 0, SEEK_END));
#else
			RL(lseek(fd, event[0].data, SEEK_END));
#endif

		RL(n = read(fd, buffer, 128));
		buffer[n] = '\0';
		(void)printf("file(%d): %s", num, buffer);
	}

	(void)waitpid(pid, &status, 0);

	(void)printf("read: successful end\n");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, file);

	return atf_no_error();
}

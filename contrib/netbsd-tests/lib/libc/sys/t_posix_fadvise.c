/* $NetBSD: t_posix_fadvise.c,v 1.2 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by YAMAMOTO Takashi.
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

/*-
 * Copyright (c)2005 YAMAMOTO Takashi,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_posix_fadvise.c,v 1.2 2017/01/13 21:30:41 christos Exp $");

#include <sys/fcntl.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

ATF_TC(posix_fadvise);
ATF_TC_HEAD(posix_fadvise, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks posix_fadvise(2)");
}

ATF_TC(posix_fadvise_reg);
ATF_TC_HEAD(posix_fadvise_reg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks posix_fadvise(2) "
	    "for regular files");
}

ATF_TC_BODY(posix_fadvise, tc)
{
	int fd;
	int pipe_fds[2];
	int badfd = 10;
	int ret;

	RL(fd = open("/dev/null", O_RDWR));

	(void)close(badfd);
	RL(pipe(pipe_fds));

	/*
	 * it's hard to check if posix_fadvise is working properly.
	 * only check return values here.
	 */

	/* posix_fadvise shouldn't affect errno. */

#define CE(x, exp) \
	do { \
		int save = errno; \
		errno = 999; \
		ATF_CHECK_EQ_MSG(ret = (x), exp, "got: %d", ret); \
		ATF_CHECK_EQ_MSG(errno, 999, "got: %s", strerror(errno)); \
		errno = save; \
	} while (0);

	CE(posix_fadvise(fd, 0, 0, -1), EINVAL);
	CE(posix_fadvise(pipe_fds[0], 0, 0, POSIX_FADV_NORMAL), ESPIPE);
	CE(posix_fadvise(badfd, 0, 0, POSIX_FADV_NORMAL), EBADF);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_NORMAL), 0);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL), 0);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM), 0);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED), 0);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED), 0);
	CE(posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE), 0);
}

ATF_TC_BODY(posix_fadvise_reg, tc)
{
	int rfd, ret;

	rump_init();
	RL(rfd = rump_sys_open("/a_file", O_CREAT, 0666));

	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_NORMAL), 0);
	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_SEQUENTIAL), 0);
	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_RANDOM), 0);
	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_WILLNEED), 0);
	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_NOREUSE), 0);

	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_NORMAL), 0);
	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_SEQUENTIAL), 0);
	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_RANDOM), 0);
	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_WILLNEED), 0);
	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_NOREUSE), 0);

	//atf_tc_expect_signal(-1, "http://mail-index.netbsd.org/source-changes-d/2010/11/11/msg002508.html");
	CE(rump_sys_posix_fadvise(rfd,
	    INT64_MAX-getpagesize(), getpagesize(), POSIX_FADV_DONTNEED), 0);
	CE(rump_sys_posix_fadvise(rfd, 0, 0, POSIX_FADV_DONTNEED), 0);
#undef CE
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, posix_fadvise);
	ATF_TP_ADD_TC(tp, posix_fadvise_reg);

	return atf_no_error();
}

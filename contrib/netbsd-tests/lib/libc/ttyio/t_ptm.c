/* $NetBSD: t_ptm.c,v 1.1 2011/01/13 03:19:57 pgoyette Exp $ */

/*
 * Copyright (c) 2004, 2008 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_ptm.c,v 1.1 2011/01/13 03:19:57 pgoyette Exp $");

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#define REQUIRE_ERRNO(x, v) \
		ATF_REQUIRE_MSG(x != v, "%s: %s", #x, strerror(errno))

ATF_TC(ptm);

ATF_TC_HEAD(ptm, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks /dev/ptm device");
}

ATF_TC_BODY(ptm, tc)
{
	struct stat stm, sts;
	struct ptmget ptm;
	int fdm;
	struct group *gp;

	if ((fdm = open("/dev/ptm", O_RDWR)) == -1) {
		if (errno == ENOENT || errno == ENODEV)
			atf_tc_skip("/dev/ptm: %s", strerror(errno));
		atf_tc_fail("/dev/ptm: %s", strerror(errno));
	}

	REQUIRE_ERRNO(fstat(fdm, &stm), -1);
	ATF_REQUIRE_EQ(major(stm.st_rdev), 165);
	REQUIRE_ERRNO(ioctl(fdm, TIOCPTMGET, &ptm), -1);

	ATF_REQUIRE_MSG(strncmp(ptm.cn, "/dev/pty", 8) == 0
		|| strncmp(ptm.cn, "/dev/null", 9) == 0,
		"bad master name: %s", ptm.cn);

	ATF_REQUIRE_MSG(strncmp(ptm.sn, "/dev/tty", 8) == 0
		|| strncmp(ptm.sn, "/dev/pts/", 9) == 0,
		"bad slave name: %s", ptm.sn);

	if (strncmp(ptm.cn, "/dev/null", 9) != 0) {
		REQUIRE_ERRNO(fstat(ptm.cfd, &stm), -1);
		REQUIRE_ERRNO(stat(ptm.cn, &sts), -1);
		ATF_REQUIRE_EQ(stm.st_rdev, sts.st_rdev);
	}

	REQUIRE_ERRNO(fstat(ptm.sfd, &stm), -1);
	REQUIRE_ERRNO(stat(ptm.sn, &sts), -1);
	ATF_REQUIRE_EQ(stm.st_rdev, sts.st_rdev);

	ATF_REQUIRE_EQ_MSG(sts.st_uid, getuid(), "bad slave uid");

	ATF_REQUIRE_MSG((gp = getgrnam("tty")) != NULL,
	    "cannot find `tty' group");
	ATF_REQUIRE_EQ_MSG(sts.st_gid, gp->gr_gid, "bad slave grid");

	(void)close(ptm.sfd);
	(void)close(ptm.cfd);
	(void)close(fdm);
}

/*
 * On NetBSD /dev/ptyp0 == /dev/pts/0 so we can check for major
 * and minor device numbers. This check is non-portable. This
 * check is now disabled because we might not have /dev/ptyp0
 * at all.
 */

/*
 * #define PTY_DEVNO_CHECK
 */

ATF_TC(ptmx);

ATF_TC_HEAD(ptmx, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks /dev/ptmx device");
}

ATF_TC_BODY(ptmx, tc)
{
	struct stat stm, sts;
	char *pty;
	int fdm, fds;
	struct group *gp;

	if ((fdm = posix_openpt(O_RDWR|O_NOCTTY)) == -1) {
		if (errno == ENOENT || errno == ENODEV)
			atf_tc_skip("/dev/ptmx: %s", strerror(errno));

		atf_tc_fail("/dev/ptmx: %s", strerror(errno));
	}

	REQUIRE_ERRNO(fstat(fdm, &stm), -1);

#ifdef PTY_DEVNO_CHECK
	REQUIRE_ERRNO(stat("/dev/ptyp0", &sts), -1);

	ATF_REQUIRE_EQ_MSG(major(stm.st_rdev), major(sts.st_rdev),
		"bad master major number");
#endif

	REQUIRE_ERRNO(grantpt(fdm), -1);
	REQUIRE_ERRNO(unlockpt(fdm), -1);
	REQUIRE_ERRNO((pty = ptsname(fdm)), NULL);

	REQUIRE_ERRNO((fds = open(pty, O_RDWR|O_NOCTTY)), -1);
	REQUIRE_ERRNO(fstat(fds, &sts), -1);

#ifdef PTY_DEVNO_CHECK
	ATF_REQUIRE_EQ_MSG(minor(stm.st_rdev), minor(sts.st_rdev),
		"bad slave minor number");
#endif

	ATF_REQUIRE_EQ_MSG(sts.st_uid, getuid(), "bad slave uid");
	ATF_REQUIRE_MSG((gp = getgrnam("tty")) != NULL,
	    "cannot find `tty' group");

	ATF_REQUIRE_EQ_MSG(sts.st_gid, gp->gr_gid, "bad slave gid");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ptm);
	ATF_TP_ADD_TC(tp, ptmx);

	return atf_no_error();
}

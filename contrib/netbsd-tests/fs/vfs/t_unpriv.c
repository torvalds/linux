/*	$NetBSD: t_unpriv.c,v 1.13 2017/01/13 21:30:40 christos Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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

#include <sys/stat.h>
#include <sys/time.h>

#include <atf-c.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"
#include "h_macros.h"

#define USES_OWNER							 \
	if (FSTYPE_MSDOS(tc))						 \
	    atf_tc_skip("owner not supported by file system")

static void
owner(const atf_tc_t *tc, const char *mp)
{

	USES_OWNER;

	FSTEST_ENTER();

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
        if (rump_sys_chown(".", 1, -1) != -1 || errno != EPERM)
		atf_tc_fail_errno("chown");
        if (rump_sys_chmod(".", 0000) != -1 || errno != EPERM) 
                atf_tc_fail_errno("chmod");
	rump_pub_lwproc_releaselwp();

	if (rump_sys_chown(".", 1, -1) == -1)
		atf_tc_fail_errno("chown");

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
        if (rump_sys_chown(".", 1, -1) == -1)
		atf_tc_fail_errno("chown");
        if (rump_sys_chmod(".", 0000) == -1) 
                atf_tc_fail_errno("chmod");
	rump_pub_lwproc_releaselwp();

	FSTEST_EXIT();
}

static void
dirperms(const atf_tc_t *tc, const char *mp)
{
	char name[] = "dir.test/file.test";
	char *dir = dirname(name);
	int fd;

	if (FSTYPE_SYSVBFS(tc))
		atf_tc_skip("directories not supported by file system");

	FSTEST_ENTER();

	if (rump_sys_mkdir(dir, 0777) == -1)
		atf_tc_fail_errno("mkdir");

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
	if (FSTYPE_ZFS(tc))
		atf_tc_expect_fail("PR kern/47656: Test known to be broken");
        if (rump_sys_open(name, O_RDWR|O_CREAT, 0666) != -1 || errno != EACCES)
		atf_tc_fail_errno("open");
	rump_pub_lwproc_releaselwp();

	if ((fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666)) == -1)
		atf_tc_fail_errno("open");
	if (rump_sys_close(fd) == -1)
		atf_tc_fail_errno("close");

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
        if (rump_sys_unlink(name) != -1 || errno != EACCES)
		atf_tc_fail_errno("unlink");
	rump_pub_lwproc_releaselwp();

        if (rump_sys_unlink(name) == -1)
		atf_tc_fail_errno("unlink");

	if (rump_sys_rmdir(dir) == -1)
		atf_tc_fail_errno("rmdir");

	FSTEST_EXIT();
}

static void
times(const atf_tc_t *tc, const char *mp)
{
	const char *name = "file.test";
	int fd;
	unsigned int i, j;
	struct timeval tmv[2];
	static struct timeval tmvs[] = {
		{ QUAD_MIN, 0 },
		{ 0, 0 },
		{ QUAD_MAX, 999999 }
	};

	FSTEST_ENTER();

	if ((fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666)) == -1)
		atf_tc_fail_errno("open");
	if (rump_sys_close(fd) == -1)
		atf_tc_fail_errno("close");

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
	if (FSTYPE_ZFS(tc))
		atf_tc_expect_fail("PR kern/47656: Test known to be broken");
	if (rump_sys_utimes(name, NULL) != -1 || errno != EACCES)
		atf_tc_fail_errno("utimes");
	rump_pub_lwproc_releaselwp();

	if (rump_sys_utimes(name, NULL) == -1)
		atf_tc_fail_errno("utimes");

	for (i = 0; i < sizeof(tmvs) / sizeof(tmvs[0]); i++) {
		for (j = 0; j < sizeof(tmvs) / sizeof(tmvs[0]); j++) {
			tmv[0] = tmvs[i];
			tmv[1] = tmvs[j];
			rump_pub_lwproc_rfork(RUMP_RFCFDG);
			if (rump_sys_setuid(1) == -1)
				atf_tc_fail_errno("setuid");
			if (rump_sys_utimes(name, tmv) != -1 || errno != EPERM)
				atf_tc_fail_errno("utimes");
			rump_pub_lwproc_releaselwp();

			if (rump_sys_utimes(name, tmv) == -1)
				atf_tc_fail_errno("utimes");
		}
	}

	if (rump_sys_unlink(name) == -1)
		atf_tc_fail_errno("unlink");

	FSTEST_EXIT();
}

static void
flags(const atf_tc_t *tc, const char *mp)
{
	const char *name = "file.test";
	int fd, fflags;
	struct stat st;

	FSTEST_ENTER();

	if ((fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666)) == -1)
		atf_tc_fail_errno("open");
	if (rump_sys_close(fd) == -1)
		atf_tc_fail_errno("close");

	if (rump_sys_stat(name, &st) == -1)
		atf_tc_fail_errno("stat");
	if (FSTYPE_ZFS(tc))
		atf_tc_expect_fail("PR kern/47656: Test known to be broken");
	if (rump_sys_chflags(name, st.st_flags) == -1) {
		if (errno == EOPNOTSUPP)
			atf_tc_skip("file flags not supported by file system");
		atf_tc_fail_errno("chflags");
	}

	fflags = st.st_flags | UF_IMMUTABLE;

	rump_pub_lwproc_rfork(RUMP_RFCFDG);
	if (rump_sys_setuid(1) == -1)
		atf_tc_fail_errno("setuid");
	fflags |= UF_IMMUTABLE;
	if (rump_sys_chflags(name, fflags) != -1 || errno != EPERM)
		atf_tc_fail_errno("chflags");
	rump_pub_lwproc_releaselwp();

	if (rump_sys_chflags(name, fflags) == -1)
		atf_tc_fail_errno("chflags");

	fflags &= ~UF_IMMUTABLE;
	if (rump_sys_chflags(name, fflags) == -1)
		atf_tc_fail_errno("chflags");

	if (rump_sys_unlink(name) == -1)
		atf_tc_fail_errno("unlink");

	FSTEST_EXIT();
}

ATF_TC_FSAPPLY(owner, "owner unprivileged checks");
ATF_TC_FSAPPLY(dirperms, "directory permission checks");
ATF_TC_FSAPPLY(times, "time set checks");
ATF_TC_FSAPPLY(flags, "file flags checks");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(owner);
	ATF_TP_FSAPPLY(dirperms);
	ATF_TP_FSAPPLY(times);
	ATF_TP_FSAPPLY(flags);

	return atf_no_error();
}

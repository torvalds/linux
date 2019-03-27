/*	$NetBSD: t_vfsops.c,v 1.12 2017/01/13 21:30:40 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
#include <sys/statvfs.h>

#include <atf-c.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"
#include "h_macros.h"

static void
tmount(const atf_tc_t *tc, const char *path)
{

	return;
}

static void
tstatvfs(const atf_tc_t *tc, const char *path)
{
	const char *fstype = atf_tc_get_md_var(tc, "X-fs.mntname");
	struct statvfs svb;

	if (rump_sys_statvfs1(path, &svb, ST_WAIT) == -1)
		atf_tc_fail_errno("statvfs");

	ATF_REQUIRE(svb.f_namemax > 0 && svb.f_namemax <= MAXNAMLEN);
	if (!(FSTYPE_PUFFS(tc) || FSTYPE_P2K_FFS(tc)))
		ATF_REQUIRE_STREQ(svb.f_fstypename, fstype);
	ATF_REQUIRE_STREQ(svb.f_mntonname, path);
}

static void
tsync(const atf_tc_t *tc, const char *path)
{

	rump_sys_sync();
}

#define MAGICSTR "just a string, I like A"
static void
tfilehandle(const atf_tc_t *tc, const char *path)
{
	char fpath[MAXPATHLEN];
	char buf[sizeof(MAGICSTR)];
	size_t fhsize;
	void *fhp;
	int fd;

	sprintf(fpath, "%s/file", path);
	fd = rump_sys_open(fpath, O_RDWR | O_CREAT, 0777);
	if (fd == -1)
		atf_tc_fail_errno("open");

	if (rump_sys_write(fd, MAGICSTR, sizeof(MAGICSTR)) != sizeof(MAGICSTR))
		atf_tc_fail("write to file");
	rump_sys_close(fd);

	/*
	 * Get file handle size.
	 * This also weeds out unsupported file systems.
	 */
	fhsize = 0;
	if (rump_sys_getfh(fpath, NULL, &fhsize) == -1) {
		if (errno == EOPNOTSUPP) {
			atf_tc_skip("file handles not supported");
		} else if (errno != E2BIG) {
			atf_tc_fail_errno("getfh size");
		}
	}

	fhp = malloc(fhsize);
	if (rump_sys_getfh(fpath, fhp, &fhsize) == -1)
		atf_tc_fail_errno("getfh");

	/* open file based on file handle */
	fd = rump_sys_fhopen(fhp, fhsize, O_RDONLY);
	if (fd == -1) {
		atf_tc_fail_errno("fhopen");
	}

	/* check that we got the same file */
	if (rump_sys_read(fd, buf, sizeof(buf)) != sizeof(MAGICSTR))
		atf_tc_fail("read fhopened file");

	ATF_REQUIRE_STREQ(buf, MAGICSTR);

	rump_sys_close(fd);
}

#define FNAME "a_file"
static void
tfhremove(const atf_tc_t *tc, const char *path)
{
	size_t fhsize;
	void *fhp;
	int fd;

	RL(rump_sys_chdir(path));
	RL(fd = rump_sys_open(FNAME, O_RDWR | O_CREAT, 0777));
	RL(rump_sys_close(fd));

	fhsize = 0;
	if (rump_sys_getfh(FNAME, NULL, &fhsize) == -1) {
		if (errno == EOPNOTSUPP) {
			atf_tc_skip("file handles not supported");
		} else if (errno != E2BIG) {
			atf_tc_fail_errno("getfh size");
		}
	}

	fhp = malloc(fhsize);
	RL(rump_sys_getfh(FNAME, fhp, &fhsize));
	RL(rump_sys_unlink(FNAME));

	if (FSTYPE_LFS(tc))
		atf_tc_expect_fail("fhopen() for removed file succeeds "
		    "(PR kern/43745)");
	ATF_REQUIRE_ERRNO(ESTALE, rump_sys_fhopen(fhp, fhsize, O_RDONLY) == -1);
	atf_tc_expect_pass();

	RL(rump_sys_chdir("/"));
}
#undef FNAME

/*
 * This test only checks the file system doesn't crash.  We *might*
 * try a valid file handle.
 */
static void
tfhinval(const atf_tc_t *tc, const char *path)
{
	size_t fhsize;
	void *fhp;
	unsigned long seed;
	int fd;

	srandom(seed = time(NULL));
	printf("RNG seed %lu\n", seed);

	RL(rump_sys_chdir(path));
	fhsize = 0;
	if (rump_sys_getfh(".", NULL, &fhsize) == -1) {
		if (errno == EOPNOTSUPP) {
			atf_tc_skip("file handles not supported");
		} else if (errno != E2BIG) {
			atf_tc_fail_errno("getfh size");
		}
	}

	fhp = malloc(fhsize);
	tests_makegarbage(fhp, fhsize);
	fd = rump_sys_fhopen(fhp, fhsize, O_RDWR);
	if (fd != -1)
		rump_sys_close(fd);

	RL(rump_sys_chdir("/"));
}

ATF_TC_FSAPPLY(tmount, "mount/unmount");
ATF_TC_FSAPPLY(tstatvfs, "statvfs");
ATF_TC_FSAPPLY(tsync, "sync");
ATF_TC_FSAPPLY(tfilehandle, "file handles");
ATF_TC_FSAPPLY(tfhremove, "fhtovp for removed file");
ATF_TC_FSAPPLY(tfhinval, "fhopen invalid filehandle");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(tmount);
	ATF_TP_FSAPPLY(tstatvfs);
	ATF_TP_FSAPPLY(tsync);
	ATF_TP_FSAPPLY(tfilehandle);
	ATF_TP_FSAPPLY(tfhremove);
	ATF_TP_FSAPPLY(tfhinval);

	return atf_no_error();
}

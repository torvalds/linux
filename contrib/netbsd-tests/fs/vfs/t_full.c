/*	$NetBSD: t_full.c,v 1.9 2017/01/13 21:30:40 christos Exp $	*/

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
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"
#include "h_macros.h"

/*
 * Write this much over the image size.  This is to force an NFS commit,
 * since we might just stuff data into the cache and miss the problem.
 */
#define NFSBONUS (1<<16)

static void
fillfs(const atf_tc_t *tc, const char *mp)
{
	char buf[8192];
	size_t written;
	ssize_t n = 0; /* xxxgcc */
	size_t bonus;
	int fd, i = 0;

	if (FSTYPE_P2K_FFS(tc) || FSTYPE_PUFFS(tc) || FSTYPE_RUMPFS(tc)) {
		atf_tc_skip("fs does not support explicit block allocation "
		    "(GOP_ALLOC)");
	}

	bonus = 0;
	if (FSTYPE_NFS(tc))
		bonus = NFSBONUS;

	if (rump_sys_chdir(mp) == -1)
		atf_tc_fail_errno("chdir mountpoint");
	fd = rump_sys_open("afile", O_CREAT | O_RDWR);
	if (fd == -1)
		atf_tc_fail_errno("create file");

	for (written = 0; written < FSTEST_IMGSIZE + bonus; written +=n) {
		memset(buf, i++, sizeof(buf)); /* known garbage */
		n = rump_sys_write(fd, buf, sizeof(buf));
		if (n == -1)
			break;
	}
	if (FSTYPE_ZFS(tc))
		atf_tc_expect_fail("PR kern/47656: Test known to be broken");
	if (n == -1) {
		if (errno != ENOSPC)
			atf_tc_fail_errno("write");
	} else {
		atf_tc_fail("filled file system over size limit");
	}

	rump_sys_close(fd);
	rump_sys_chdir("/");
}

ATF_TC_FSAPPLY(fillfs, "fills file system, expects ENOSPC");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(fillfs);

	return atf_no_error();
}

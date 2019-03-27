/*	$NetBSD: t_rwtoro.c,v 1.1 2017/01/27 10:45:11 hannken Exp $	*/

/*-
 * Copyright (c) 2017 The NetBSD Foundation, Inc.
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

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <atf-c.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include <miscfs/nullfs/null.h>
#include <fs/tmpfs/tmpfs_args.h>

#include "../common/h_fsmacros.h"
#include "../../h_macros.h"

static const char *unsupported = "fs does not support r/o remount";
static char file_path[MAXPATHLEN];
static int file_fd;

/*
 * Remount the filesystem read-only and test errno.
 * Skip filesystems that don't implement read-write -> read-only.
 */
static void
remount_ro(const atf_tc_t *tc, const char *mp, int expected_errno)
{
	int error;
	union {
		struct tmpfs_args tmpfs;
		char data[4095];
	} mount_args;
	int mount_args_length;
	struct statvfs sbuf;

	if (FSTYPE_ZFS(tc))
		atf_tc_skip("%s", unsupported);

	/* Prepare mount arguments. */
	RL(rump_sys_statvfs1(mp, &sbuf, ST_WAIT));
	mount_args_length = sizeof(mount_args);
	memset(&mount_args, 0, mount_args_length);
	if (FSTYPE_TMPFS(tc))
		mount_args.tmpfs.ta_version = TMPFS_ARGS_VERSION;
	mount_args_length = rump_sys_mount(sbuf.f_fstypename, mp, MNT_GETARGS,
	    &mount_args, mount_args_length);
	ATF_CHECK(mount_args_length >= 0);

	/* Remount and test result. */
	error = rump_sys_mount(sbuf.f_fstypename, mp, MNT_UPDATE | MNT_RDONLY,
	    &mount_args, mount_args_length);
	if (errno == EOPNOTSUPP)
		atf_tc_skip("%s", unsupported);
	if (expected_errno == 0)
		ATF_CHECK(error == 0);
	else
		ATF_CHECK_ERRNO(expected_errno, error == -1);
}

static void
open_file_ro(const char *prefix)
{

	snprintf(file_path, sizeof(file_path), "%s/file", prefix);
	RL(file_fd = rump_sys_open(file_path, O_CREAT | O_RDWR, 0777));
	RL(rump_sys_close(file_fd));
	RL(file_fd = rump_sys_open(file_path, O_RDONLY));
}

static void
open_file_ro_unlink(const char *prefix)
{

	snprintf(file_path, sizeof(file_path), "%s/file", prefix);
	RL(file_fd = rump_sys_open(file_path, O_CREAT | O_RDWR, 0777));
	RL(rump_sys_close(file_fd));
	RL(file_fd = rump_sys_open(file_path, O_RDONLY));
	RL(rump_sys_unlink(file_path));
}

static void
open_file_rw(const char *prefix)
{

	snprintf(file_path, sizeof(file_path), "%s/file", prefix);
	RL(file_fd = rump_sys_open(file_path, O_CREAT | O_RDWR, 0777));
}

static void
close_file(const char *unused)
{

	RL(rump_sys_close(file_fd));
}

static void
basic_test(const atf_tc_t *tc, const char *mp, int expected_errno,
    bool use_layer, void (*pre)(const char *), void (*post)(const char *))
{
	const char *null_mount = "/nullm";
	struct null_args nargs;

	if (use_layer) {
		RL(rump_sys_mkdir(null_mount, 0777));
		memset(&nargs, 0, sizeof(nargs));
		nargs.nulla_target = __UNCONST(mp);;
		RL(rump_sys_mount(MOUNT_NULL, null_mount, 0,
		    &nargs, sizeof(nargs)));
	}
	if (pre)
		(*pre)(use_layer ? null_mount : mp);
	remount_ro(tc, mp, expected_errno);
	if (post)
		(*post)(use_layer ? null_mount : mp);
	if (use_layer)
		RL(rump_sys_unmount(null_mount, 0));
}

static void
noneopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, 0, false, NULL, NULL);
}

static void
readopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, 0, false, open_file_ro, close_file);
}

static void
writeopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, EBUSY, false, open_file_rw, close_file);
}

static void
read_unlinked(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, EBUSY, false, open_file_ro_unlink, close_file);
}

static void
layer_noneopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, 0, true, NULL, NULL);
}

static void
layer_readopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, 0, true, open_file_ro, close_file);
}

static void
layer_writeopen(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, EBUSY, true, open_file_rw, close_file);
}

static void
layer_read_unlinked(const atf_tc_t *tc, const char *mp)
{

	basic_test(tc, mp, EBUSY, true, open_file_ro_unlink, close_file);
}

ATF_TC_FSAPPLY(noneopen, "remount r/o with no file open");
ATF_TC_FSAPPLY(readopen, "remount r/o with file open for reading");
ATF_TC_FSAPPLY(writeopen, "remount r/o with file open for writing");
ATF_TC_FSAPPLY(read_unlinked,
    "remount r/o with unlinked file open for reading");
ATF_TC_FSAPPLY(layer_noneopen, "remount r/o with no file open on layer");
ATF_TC_FSAPPLY(layer_readopen,
    "remount r/o with file open for reading on layer");
ATF_TC_FSAPPLY(layer_writeopen,
    "remount r/o with file open for writing on layer");
ATF_TC_FSAPPLY(layer_read_unlinked,
    "remount r/o with unlinked file open for reading on layer");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(noneopen);
	ATF_TP_FSAPPLY(readopen);
	ATF_TP_FSAPPLY(writeopen);
	ATF_TP_FSAPPLY(read_unlinked);
	ATF_TP_FSAPPLY(layer_noneopen);
	ATF_TP_FSAPPLY(layer_readopen);
	ATF_TP_FSAPPLY(layer_writeopen);
	ATF_TP_FSAPPLY(layer_read_unlinked);

	return atf_no_error();
}

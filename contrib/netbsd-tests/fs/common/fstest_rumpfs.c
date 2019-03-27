/*	$NetBSD: fstest_rumpfs.c,v 1.2 2014/03/16 10:28:03 njoly Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nicolas Joly.
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

#include <sys/mount.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_fsmacros.h"

int
rumpfs_fstest_newfs(const atf_tc_t *tc, void **buf, const char *image,
    off_t size, void *fspriv)
{
	char tmp[64];
	int res;

	snprintf(tmp, sizeof(tmp), "%"PRId64, size);
	res = setenv("RUMP_MEMLIMIT", tmp, 0);
	if (res == -1)
		return res;

	return rump_init();
}

int
rumpfs_fstest_delfs(const atf_tc_t *tc, void *buf)
{

	return 0;
}

int
rumpfs_fstest_mount(const atf_tc_t *tc, void *buf, const char *path, int flags)
{
	int res;

	res = rump_sys_mkdir(path, 0777);
	if (res == -1)
		return res;

	return rump_sys_mount(MOUNT_RUMPFS, path, flags, NULL, 0);
}

int
rumpfs_fstest_unmount(const atf_tc_t *tc, const char *path, int flags)
{
	int res;

	res = rump_sys_unmount(path, flags);
	if (res == -1)
		return res;

	return rump_sys_rmdir(path);
}

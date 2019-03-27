/*	$NetBSD: fstest_zfs.c,v 1.1 2012/08/20 16:37:35 pooka Exp $	*/

/*-
 * Copyright (c) 2010, 2011  The NetBSD Foundation, Inc.
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

#define SRVPATH "zfssurvo"
#define SRVURL "unix://" SRVPATH
#define ZFSDEV "/zfsdev"

int
zfs_fstest_newfs(const atf_tc_t *tc, void **buf, const char *image,
	off_t size, void *fspriv)
{
	int res;
	int fd;

	/* XXX: hardcoded zfs minimum size */
	size = MAX(64*1024*1024, size);

	res = rump_init();
	if (res != 0) {
		errno = res;
		return -1;
	}

	/* create backing image, sparse file is enough */
	if ((fd = open(image, O_RDWR | O_CREAT, 0777)) == -1)
		return -1;
	if (ftruncate(fd, size) == -1) {
		close(fd);
		return -1;
	}
	close(fd);

	res = rump_pub_etfs_register(ZFSDEV, image, RUMP_ETFS_BLK);
	if (res != 0) {
		errno = res;
		return -1;
	}

	res = rump_init_server(SRVURL);
	if (res != 0) {
		errno = res;
		return -1;
	}
	*buf = NULL;

	return 0;
}

int
zfs_fstest_delfs(const atf_tc_t *tc, void *buf)
{

	unlink(SRVPATH);
	return 0;
}

int
zfs_fstest_mount(const atf_tc_t *tc, void *buf, const char *path, int flags)
{
	char tmpbuf[128];
	int error;

	/* set up the hijack env for running zpool */
	setenv("RUMP_SERVER", SRVURL, 1);
	snprintf(tmpbuf, sizeof(tmpbuf)-1, "blanket=/dev/zfs:%s:%s",
	    ZFSDEV, path);
	setenv("RUMPHIJACK", tmpbuf, 1);
	setenv("LD_PRELOAD", "/usr/lib/librumphijack.so", 1);

	while (*path == '/')
		path++;

	/* run zpool create */
	snprintf(tmpbuf, sizeof(tmpbuf)-1, "zpool create %s %s",
	    path, ZFSDEV);
	if ((error = system(tmpbuf)) != 0) {
		errno = error;
		return -1;
	}

	return 0;
}

int
zfs_fstest_unmount(const atf_tc_t *tc, const char *path, int flags)
{

	unmount(path, flags);
	unlink(SRVPATH);

	return 0;
}

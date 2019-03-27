/*	$NetBSD: t_snapshot.c,v 1.4 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <fs/tmpfs/tmpfs_args.h>
#include <msdosfs/msdosfsmount.h>

#include <atf-c.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "h_macros.h"

#define IMGNAME "msdosfs.img"
#define NEWFS "newfs_msdos -C 5M " IMGNAME
#define FSCK "fsck_msdos -fn"
#define BAKNAME "/stor/snap"

static void
mount_diskfs(const char *fspec, const char *path)
{
	struct msdosfs_args margs;

	memset(&margs, 0, sizeof(margs));
	margs.fspec = __UNCONST(fspec);
	margs.version = MSDOSFSMNT_VERSION;

	if (rump_sys_mount(MOUNT_MSDOS, path, 0, &margs, sizeof(margs)) == -1)
		err(1, "mount msdosfs %s", path);
}

static void
begin(void)
{
	struct tmpfs_args targs = { .ta_version = TMPFS_ARGS_VERSION, };

	if (rump_sys_mkdir("/stor", 0777) == -1)
		atf_tc_fail_errno("mkdir /stor");
	if (rump_sys_mount(MOUNT_TMPFS, "/stor", 0, &targs,sizeof(targs)) == -1)
		atf_tc_fail_errno("mount storage");
}

#include "../common/snapshot.c"

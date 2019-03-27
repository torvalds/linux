/*	$NetBSD: t_snapshot_log.c,v 1.3 2017/01/13 21:30:39 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <ufs/ufs/ufsmount.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "h_macros.h"

#define IMGNAME "ffs.img"
#define NEWFS "newfs -F -s 10000 " IMGNAME
#define FSCK "fsck_ffs -fn -F"
#define BAKNAME "/mnt/le_snapp"

static void
mount_diskfs(const char *fspec, const char *path)
{
	struct ufs_args uargs;
	static int flags = MNT_LOG;

	uargs.fspec = __UNCONST(fspec);

	if (rump_sys_mount(MOUNT_FFS,
	    path, flags, &uargs, sizeof(uargs)) == -1)
		atf_tc_fail_errno("mount ffs %s", path);
	flags = 0;
}

static void
begin(void)
{

	/* empty */
}

#include "../common/snapshot.c"

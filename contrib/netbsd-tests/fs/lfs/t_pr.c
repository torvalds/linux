/*	$NetBSD: t_pr.c,v 1.7 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <ufs/ufs/ufsmount.h>

#include "h_macros.h"

ATF_TC(mknod);
ATF_TC_HEAD(mknod, tc)
{

	atf_tc_set_md_var(tc, "descr", "mknod(2) hangs on LFS (PR kern/43503)");
	atf_tc_set_md_var(tc, "timeout", "20");
}

#define IMGNAME "disk.img"
#define FAKEBLK "/dev/blk"
ATF_TC_BODY(mknod, tc)
{
	struct ufs_args args;

	/* hmm, maybe i should fix newfs_lfs instead? */
	if (system("newfs_lfs -D -F -s 10000 ./" IMGNAME) == -1)
		atf_tc_fail_errno("newfs failed");

	memset(&args, 0, sizeof(args));
	args.fspec = __UNCONST(FAKEBLK);

	rump_init();
	if (rump_sys_mkdir("/mp", 0777) == -1)
		atf_tc_fail_errno("cannot create mountpoint");
	rump_pub_etfs_register(FAKEBLK, IMGNAME, RUMP_ETFS_BLK);
	if (rump_sys_mount(MOUNT_LFS, "/mp", 0, &args, sizeof(args)) == -1)
		atf_tc_fail_errno("rump_sys_mount failed");

	//atf_tc_expect_timeout("PR kern/43503");
	if (rump_sys_mknod("/mp/node", S_IFCHR | 0777, 0) == -1)
		atf_tc_fail_errno("mknod failed");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mknod);
	return 0;
}

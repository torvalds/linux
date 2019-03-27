/*	$NetBSD: t_ptyfs.c,v 1.2 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <fs/ptyfs/ptyfs.h>

#include "h_macros.h"

static void
mountptyfs(const char *mp, int flags)
{
	struct ptyfs_args args;

	if (rump_sys_mkdir("/mp", 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("mp1");
	}
	memset(&args, 0, sizeof(args));
	args.version = PTYFS_ARGSVERSION;
	args.mode = 0777;
	if (rump_sys_mount(MOUNT_PTYFS, mp, flags, &args, sizeof(args)) == -1)
		atf_tc_fail_errno("could not mount ptyfs");
}

ATF_TC(basic);
ATF_TC_HEAD(basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "mount ptyfs");
}

ATF_TC_BODY(basic, tc)
{

	rump_init();

	mountptyfs("/mp", 0);
	if (rump_sys_unmount("/mp", 0) == -1)
		atf_tc_fail_errno("unmount failed");

	/* done */
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, basic);

	return atf_no_error();
}

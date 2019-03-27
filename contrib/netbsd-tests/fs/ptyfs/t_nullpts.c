/*	$NetBSD: t_nullpts.c,v 1.6 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/ioctl.h>

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
#include <miscfs/nullfs/null.h>

#include "h_macros.h"

static void
mountptyfs(const char *mp, int flags)
{
	struct ptyfs_args args;

	if (rump_sys_mkdir(mp, 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("null create %s", mp);
	}
	memset(&args, 0, sizeof(args));
	args.version = PTYFS_ARGSVERSION;
	args.mode = 0777;
	if (rump_sys_mount(MOUNT_PTYFS, mp, flags, &args, sizeof(args)) == -1)
		atf_tc_fail_errno("could not mount ptyfs");
}

static void
mountnull(const char *what, const char *mp, int flags)
{
	struct null_args nargs;

	if (rump_sys_mkdir(what, 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("null create %s", what);
	}
	if (rump_sys_mkdir(mp, 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("null create %s", mp);
	}
	memset(&nargs, 0, sizeof(nargs));
	nargs.nulla_target = __UNCONST(what);
	if (rump_sys_mount(MOUNT_NULL, mp, flags, &nargs, sizeof(nargs)) == -1)
		atf_tc_fail_errno("could not mount nullfs");
}

ATF_TC(nullrevoke);
ATF_TC_HEAD(nullrevoke, tc)
{
	atf_tc_set_md_var(tc, "descr", "null mount ptyfs and revoke");
}

ATF_TC_BODY(nullrevoke, tc)
{
	char path[MAXPATHLEN];
	struct ptmget ptg;
	int ptm;

	rump_init();

	/*
	 * mount /dev/pts
	 */
	mountptyfs("/dev/pts", 0);

	/*
	 * null mount /dev/pts to /null/dev/pts
	 */
	if (rump_sys_mkdir("/null", 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("null create /null");
	}
	if (rump_sys_mkdir("/null/dev", 0777) == -1) {
		if (errno != EEXIST)
			atf_tc_fail_errno("null create /null/dev");
	}

	mountnull("/dev/pts", "/null/dev/pts", 0);

	/*
	 * get slave/master pair.
	 */
	ptm = rump_sys_open("/dev/ptm", O_RDWR);
	if (rump_sys_ioctl(ptm, TIOCPTMGET, &ptg) == -1)
		atf_tc_fail_errno("get pty");

	/*
	 * Build nullfs path to slave.
	 */
	strcpy(path, "/null");
	strcat(path, ptg.sn);

	/*
	 * Open slave tty via nullfs.
	 */
	if (rump_sys_open(path, O_RDWR) == -1)
		atf_tc_fail_errno("slave null open");

	/*
	 * Close slave opened with /dev/ptm.  Need purely non-null refs to it.
	 */
	rump_sys_close(ptg.sfd);

	/* revoke slave tty. */
	rump_sys_revoke(path);

	/* done */
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, nullrevoke);

	return atf_no_error();
}

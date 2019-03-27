/*	$NetBSD: t_basic.c,v 1.4 2017/01/13 21:30:40 christos Exp $	*/

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

#include <miscfs/nullfs/null.h>
#include <fs/tmpfs/tmpfs_args.h>

#include "h_macros.h"

ATF_TC(basic);
ATF_TC_HEAD(basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "basic nullfs functionality");
}

#define MSTR "magic bus"

static void
xput_tfile(const char *path, const char *mstr)
{
	int fd;

	fd = rump_sys_open(path, O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create %s", path);
	if (rump_sys_write(fd, MSTR, sizeof(MSTR)) != sizeof(MSTR))
		atf_tc_fail_errno("write to testfile");
	rump_sys_close(fd);
}

static int
xread_tfile(const char *path, const char *mstr)
{
	char buf[128];
	int fd;

	fd = rump_sys_open(path, O_RDONLY);
	if (fd == -1)
		return errno;
	if (rump_sys_read(fd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read tfile");
	rump_sys_close(fd);
	if (strcmp(buf, MSTR) == 0)
		return 0;
	return EPROGMISMATCH;
}

static void
mountnull(const char *what, const char *mp, int flags)
{
	struct null_args nargs;

	memset(&nargs, 0, sizeof(nargs));
	nargs.nulla_target = __UNCONST(what);
	if (rump_sys_mount(MOUNT_NULL, mp, flags, &nargs, sizeof(nargs)) == -1)
		atf_tc_fail_errno("could not mount nullfs");

}

ATF_TC_BODY(basic, tc)
{
	struct tmpfs_args targs;
	struct stat sb;
	int error;

	rump_init();
	if (rump_sys_mkdir("/td1", 0777) == -1)
		atf_tc_fail_errno("mp1");
	if (rump_sys_mkdir("/td2", 0777) == -1)
		atf_tc_fail_errno("mp1");

	/* use tmpfs because rumpfs doesn't support regular files */
	memset(&targs, 0, sizeof(targs));
	targs.ta_version = TMPFS_ARGS_VERSION;
	targs.ta_root_mode = 0777;
	if (rump_sys_mount(MOUNT_TMPFS, "/td1", 0, &targs, sizeof(targs)) == -1)
		atf_tc_fail_errno("could not mount tmpfs td1");

	mountnull("/td1", "/td2", 0);

	/* test unnull -> null */
	xput_tfile("/td1/tensti", "jeppe");
	error = xread_tfile("/td2/tensti", "jeppe");
	if (error != 0)
		atf_tc_fail("null compare failed: %d (%s)",
		    error, strerror(error));

	/* test null -> unnull */
	xput_tfile("/td2/kiekko", "keppi");
	error = xread_tfile("/td1/kiekko", "keppi");
	if (error != 0)
		atf_tc_fail("unnull compare failed: %d (%s)",
		    error, strerror(error));

	/* test unnull -> null overwrite */
	xput_tfile("/td1/tensti", "se oolannin sota");
	error = xread_tfile("/td2/tensti", "se oolannin sota");
	if (error != 0)
		atf_tc_fail("unnull compare failed: %d (%s)",
		    error, strerror(error));

	/* test that /td2 is unaffected in "real life" */
	if (rump_sys_unmount("/td2", 0) == -1)
		atf_tc_fail_errno("cannot unmount nullfs");
	if ((error = rump_sys_stat("/td2/tensti", &sb)) != -1
	    || errno != ENOENT) {
		atf_tc_fail("stat tensti should return ENOENT, got %d", error);
	}
	if ((error = rump_sys_stat("/td2/kiekko", &sb)) != -1
	    || errno != ENOENT) {
		atf_tc_fail("stat kiekko should return ENOENT, got %d", error);
	}

	/* done */
}

ATF_TC(twistymount);
ATF_TC_HEAD(twistymount, tc)
{

	/* this is expected to fail until the PR is fixed */
	atf_tc_set_md_var(tc, "descr", "\"recursive\" mounts deadlock"
	    " (kern/43439)");
}

/*
 * Mapping to identifiers in kern/43439:
 *  /td		= /home/current/pkgsrc
 *  /td/dist	= /home/current/pkgsrc/distiles
 *  /mp		= /usr/pkgsrc
 *  /mp/dist	= /usr/pkgsrc/distfiles -- "created" by first null mount
 */

ATF_TC_BODY(twistymount, tc)
{
	int mkd = 0;

	rump_init();

	if (rump_sys_mkdir("/td", 0777) == -1)
		atf_tc_fail_errno("mkdir %d", mkd++);
	if (rump_sys_mkdir("/td/dist", 0777) == -1)
		atf_tc_fail_errno("mkdir %d", mkd++);
	if (rump_sys_mkdir("/mp", 0777) == -1)
		atf_tc_fail_errno("mkdir %d", mkd++);

	/* MNT_RDONLY doesn't matter, but just for compat with the PR */
	mountnull("/td", "/mp", MNT_RDONLY);
	mountnull("/td/dist", "/mp/dist", 0);

	/* if we didn't get a locking-against-meself panic, we passed */
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, basic);
	ATF_TP_ADD_TC(tp, twistymount);

	return atf_no_error();
}

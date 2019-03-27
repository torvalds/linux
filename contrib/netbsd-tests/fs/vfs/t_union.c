/*	$NetBSD: t_union.c,v 1.9 2017/01/13 21:30:40 christos Exp $	*/

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

#include <miscfs/union/union.h>

#include "h_macros.h"
#include "../common/h_fsmacros.h"

#define MSTR "magic bus"

static void
xput_tfile(const char *mp, const char *path)
{
	char pathb[MAXPATHLEN];
	int fd;

	strcpy(pathb, mp);
	strcat(pathb, "/");
	strcat(pathb, path);

	RL(fd = rump_sys_open(pathb, O_CREAT | O_RDWR, 0777));
	if (rump_sys_write(fd, MSTR, sizeof(MSTR)) != sizeof(MSTR))
		atf_tc_fail_errno("write to testfile");
	RL(rump_sys_close(fd));
}

static int
xread_tfile(const char *mp, const char *path)
{
	char pathb[MAXPATHLEN];
	char buf[128];
	int fd;

	strcpy(pathb, mp);
	strcat(pathb, "/");
	strcat(pathb, path);

	fd = rump_sys_open(pathb, O_RDONLY);
	if (fd == -1)
		return errno;
	if (rump_sys_read(fd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read tfile");
	RL(rump_sys_close(fd));
	if (strcmp(buf, MSTR) == 0)
		return 0;
	return EPROGMISMATCH;
}

/*
 * Mount a unionfs for testing.  Before calling, "mp" contains
 * the upper layer.  Lowerpath is constructed so that the directory
 * contains rumpfs.
 */
static void
mountunion(const char *mp, char *lowerpath)
{
	struct union_args unionargs;

	sprintf(lowerpath, "/lower");
	rump_sys_mkdir(lowerpath, 0777);

	/* mount the union with our testfs as the upper layer */
	memset(&unionargs, 0, sizeof(unionargs));
	unionargs.target = lowerpath;
	unionargs.mntflags = UNMNT_BELOW;

	if (rump_sys_mount(MOUNT_UNION, mp, 0,
	    &unionargs, sizeof(unionargs)) == -1) {
		if (errno == EOPNOTSUPP) {
			atf_tc_skip("fs does not support VOP_WHITEOUT");
		} else {
			atf_tc_fail_errno("union mount");
		}
	}
}

#if 0
static void
toggleroot(void)
{
	static int status;

	status ^= MNT_RDONLY;

	printf("0x%x\n", status);
	RL(rump_sys_mount(MOUNT_RUMPFS, "/", status | MNT_UPDATE, NULL, 0));
}
#endif

#define TFILE "tensti"
#define TDIR "testdir"
#define TDFILE TDIR "/indir"

static void
basic(const atf_tc_t *tc, const char *mp)
{
	char lowerpath[MAXPATHLEN];
	char dbuf[8192];
	struct stat sb;
	struct dirent *dp;
	int error, fd, dsize;

	mountunion(mp, lowerpath);

	/* create a file in the lower layer */
	xput_tfile(lowerpath, TFILE);

	/* first, test we can read the old file from the new namespace */
	error = xread_tfile(mp, TFILE);
	if (error != 0)
		atf_tc_fail("union compare failed: %d (%s)",
		    error, strerror(error));

	/* then, test upper layer writes don't affect the lower layer */
	xput_tfile(mp, "kiekko");
	if ((error = xread_tfile(lowerpath, "kiekko")) != ENOENT)
		atf_tc_fail("invisibility failed: %d (%s)",
		    error, strerror(error));
	
	/* check that we can whiteout stuff in the upper layer */
	FSTEST_ENTER();
	RL(rump_sys_unlink(TFILE));
	ATF_REQUIRE_ERRNO(ENOENT, rump_sys_stat(TFILE, &sb) == -1);
	FSTEST_EXIT();

	/* check that the removed node is not in the directory listing */
	RL(fd = rump_sys_open(mp, O_RDONLY));
	RL(dsize = rump_sys_getdents(fd, dbuf, sizeof(dbuf)));
	for (dp = (struct dirent *)dbuf;
	    (char *)dp < dbuf + dsize;
	    dp = _DIRENT_NEXT(dp)) {
		if (strcmp(dp->d_name, TFILE) == 0 && dp->d_type != DT_WHT)
			atf_tc_fail("removed file non-white-outed");
	}
	RL(rump_sys_close(fd));

	RL(rump_sys_unmount(mp, 0));
}

static void
whiteout(const atf_tc_t *tc, const char *mp)
{
	char lower[MAXPATHLEN];
	struct stat sb;
	void *fsarg;

	/*
	 * XXX: use ffs here to make sure any screwups in rumpfs don't
	 * affect the test
	 */
	RL(ffs_fstest_newfs(tc, &fsarg, "daimage", 1024*1024*5, NULL));
	RL(ffs_fstest_mount(tc, fsarg, "/lower", 0));

	/* create a file in the lower layer */
	RL(rump_sys_chdir("/lower"));
	RL(rump_sys_mkdir(TDIR, 0777));
	RL(rump_sys_mkdir(TDFILE, 0777));
	RL(rump_sys_chdir("/"));

	RL(ffs_fstest_unmount(tc, "/lower", 0));
	RL(ffs_fstest_mount(tc, fsarg, "/lower", MNT_RDONLY));

	mountunion(mp, lower);

	FSTEST_ENTER();
	ATF_REQUIRE_ERRNO(ENOTEMPTY, rump_sys_rmdir(TDIR) == -1);
	RL(rump_sys_rmdir(TDFILE));
	RL(rump_sys_rmdir(TDIR));
	ATF_REQUIRE_ERRNO(ENOENT, rump_sys_stat(TDFILE, &sb) == -1);
	ATF_REQUIRE_ERRNO(ENOENT, rump_sys_stat(TDIR, &sb) == -1);

	RL(rump_sys_mkdir(TDIR, 0777));
	RL(rump_sys_stat(TDIR, &sb));
	ATF_REQUIRE_ERRNO(ENOENT, rump_sys_stat(TDFILE, &sb) == -1);
	FSTEST_EXIT();

	RL(rump_sys_unmount(mp, 0));
}

ATF_TC_FSAPPLY(basic, "check basic union functionality");
ATF_TC_FSAPPLY(whiteout, "create whiteout in upper layer");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(basic);
	ATF_TP_FSAPPLY(whiteout);

	return atf_no_error();
}

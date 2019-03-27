/*	$NetBSD: t_quota2_1.c,v 1.5 2017/01/13 21:30:39 christos Exp $	*/

/*
 * Basic tests for quota2
 */

#include <atf-c.h>

#include "../common/h_fsmacros.h"

#include <sys/types.h>
#include <sys/mount.h>

#include <stdlib.h>

#include <ufs/ufs/ufsmount.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

static void
do_quota(const atf_tc_t *tc, int n, const char *newfs_opts, int log)
{
	int i;
	char buf[1024];
	int res;
	int fd;
	struct ufs_args uargs;
	
	snprintf(buf, sizeof(buf), "newfs -q user -q group -F -s 4000 -n %d "
	    "%s %s", (n + 3),  newfs_opts, FSTEST_IMGNAME);
        if (system(buf) == -1)
                atf_tc_fail_errno("cannot create file system");

	rump_init();
	if (rump_sys_mkdir(FSTEST_MNTNAME, 0777) == -1)
		atf_tc_fail_errno("mount point create");

	rump_pub_etfs_register("/diskdev", FSTEST_IMGNAME, RUMP_ETFS_BLK);

	uargs.fspec = __UNCONST("/diskdev");
	if (rump_sys_mount(MOUNT_FFS, FSTEST_MNTNAME, (log) ? MNT_LOG : 0,
	    &uargs, sizeof(uargs)) == -1)
		atf_tc_fail_errno("mount ffs %s", FSTEST_MNTNAME);

	atf_tc_expect_pass();
	FSTEST_ENTER();
	RL(rump_sys_chown(".", 0, 0));
	for (i = 0 ; i < n; i++) {
		sprintf(buf, "file%d", i);
		RL(fd = rump_sys_open(buf, O_CREAT | O_RDWR, 0755));
		sprintf(buf, "test file no %d", i);
		RL(rump_sys_write(fd, buf, strlen(buf)));
		RL(rump_sys_fchown(fd, i, i+80000));
		rump_sys_close(fd);
	}
	FSTEST_EXIT();
	if (rump_sys_unmount(FSTEST_MNTNAME, 0) != 0) {
		rump_pub_vfs_mount_print(FSTEST_MNTNAME, 1);
		atf_tc_fail_errno("unmount failed");
	}
	snprintf(buf, 1024, "fsck_ffs -fn -F %s",  FSTEST_IMGNAME);
	res = system(buf);
	if (res != 0)
		atf_tc_fail("fsck returned %d", res);
}

#define DECL_TEST(nent, newops, name, descr, log) \
ATF_TC(quota_##name);							\
									\
ATF_TC_HEAD(quota_##name, tc)						\
{									\
	atf_tc_set_md_var(tc, "descr",					\
	    "test quotas with %d users and groups, %s",			\
	    nent, descr);						\
}									\
									\
ATF_TC_BODY(quota_##name, tc)						\
{									\
	do_quota(tc, nent, newops, log);				\
}

DECL_TEST(40, "-O1 -B le", 40_O1_le, "UFS1 little-endian", 0)
DECL_TEST(40, "-O1 -B be", 40_O1_be, "UFS1 big-endian", 0)

DECL_TEST(40, "-O2 -B le", 40_O2_le, "UFS2 little-endian", 0)
DECL_TEST(40, "-O2 -B be", 40_O2_be, "UFS2 big-endian", 0)

DECL_TEST(40, "-O1", 40_O1_log, "UFS1 log", 1)
DECL_TEST(40, "-O2", 40_O2_log, "UFS2 log", 1)

DECL_TEST(1000, "-O1 -B le", 1000_O1_le, "UFS1 little-endian", 0)
DECL_TEST(1000, "-O1 -B be", 1000_O1_be, "UFS1 big-endian", 0)

DECL_TEST(1000, "-O2 -B le", 1000_O2_le, "UFS2 little-endian", 0)
DECL_TEST(1000, "-O2 -B be", 1000_O2_be, "UFS2 big-endian", 0)

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, quota_40_O1_le);
	ATF_TP_ADD_TC(tp, quota_40_O1_be);
	ATF_TP_ADD_TC(tp, quota_40_O2_le);
	ATF_TP_ADD_TC(tp, quota_40_O2_be);
	ATF_TP_ADD_TC(tp, quota_40_O1_log);
	ATF_TP_ADD_TC(tp, quota_40_O2_log);
	ATF_TP_ADD_TC(tp, quota_1000_O1_le);
	ATF_TP_ADD_TC(tp, quota_1000_O1_be);
	ATF_TP_ADD_TC(tp, quota_1000_O2_le);
	ATF_TP_ADD_TC(tp, quota_1000_O2_be);
	return atf_no_error();
}

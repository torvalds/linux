/*	$NetBSD: t_basic.c,v 1.14 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <puffs.h>
#include <puffsdump.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"
#include "../common/h_fsmacros.h"

/*
 * Do a synchronous operation.  When this returns, all FAF operations
 * have at least been delivered to the file system.
 *
 * XXX: is this really good enough considering puffs(9)-issued
 * callback operations?
 */
static void
syncbar(const char *fs)
{
	struct statvfs svb;

	if (rump_sys_statvfs1(fs, &svb, ST_WAIT) == -1)
		atf_tc_fail_errno("statvfs");
}

#ifdef PUFFSDUMP
static void __unused
dumpopcount(struct puffstestargs *args)
{
	size_t i;

	printf("VFS OPS:\n");
	for (i = 0; i < MIN(puffsdump_vfsop_count, PUFFS_VFS_MAX); i++) {
		printf("\t%s: %d\n",
		    puffsdump_vfsop_revmap[i], args->pta_vfs_toserv_ops[i]);
	}

	printf("VN OPS:\n");
	for (i = 0; i < MIN(puffsdump_vnop_count, PUFFS_VN_MAX); i++) {
		printf("\t%s: %d\n",
		    puffsdump_vnop_revmap[i], args->pta_vn_toserv_ops[i]);
	}
}
#endif

ATF_TC(mount);
ATF_TC_HEAD(mount, tc)
{

	atf_tc_set_md_var(tc, "descr", "puffs+dtfs un/mount test");
}

ATF_TC_BODY(mount, tc)
{
	void *args;

	FSTEST_CONSTRUCTOR(tc, puffs, args);
	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(root_reg);
ATF_TC_HEAD(root_reg, tc)
{
	atf_tc_set_md_var(tc, "descr", "root is a regular file");
}

#define MAKEOPTS(...) \
    char *theopts[] = {NULL, "-s", __VA_ARGS__, "dtfs", "n/a", NULL}

ATF_TC_BODY(root_reg, tc)
{
	MAKEOPTS("-r", "reg");
	void *args;
	int fd, rv;

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);

	fd = rump_sys_open(FSTEST_MNTNAME, O_RDWR);
	if (fd == -1)
		atf_tc_fail_errno("open root");
	if (rump_sys_write(fd, &fd, sizeof(fd)) != sizeof(fd))
		atf_tc_fail_errno("write to root");
	rv = rump_sys_mkdir(FSTEST_MNTNAME "/test", 0777);
	ATF_REQUIRE(errno == ENOTDIR);
	ATF_REQUIRE(rv == -1);
	rump_sys_close(fd);

	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(root_lnk);
ATF_TC_HEAD(root_lnk, tc)
{

	atf_tc_set_md_var(tc, "descr", "root is a symbolic link");
}

#define LINKSTR "/path/to/nowhere"
ATF_TC_BODY(root_lnk, tc)
{
	MAKEOPTS("-r", "lnk " LINKSTR);
	void *args;
	char buf[PATH_MAX];
	ssize_t len;

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);

	if ((len = rump_sys_readlink(FSTEST_MNTNAME, buf, sizeof(buf)-1)) == -1)
		atf_tc_fail_errno("readlink");
	buf[len] = '\0';

	ATF_REQUIRE_STREQ(buf, LINKSTR);

#if 0 /* XXX: unmount uses FOLLOW */
	if (rump_sys_unmount("/mp", 0) == -1)
		atf_tc_fail_errno("unmount");
#endif
}

ATF_TC(root_fifo);
ATF_TC_HEAD(root_fifo, tc)
{

	atf_tc_set_md_var(tc, "descr", "root is a symbolic link");
}

#define MAGICSTR "nakit ja muusiperunat maustevoilla"
static void *
dofifow(void *arg)
{
	int fd = (int)(uintptr_t)arg;
	char buf[512];

	printf("writing\n");
	strcpy(buf, MAGICSTR);
	if (rump_sys_write(fd, buf, strlen(buf)+1) != strlen(buf)+1)
		atf_tc_fail_errno("write to fifo");

	return NULL;
}

ATF_TC_BODY(root_fifo, tc)
{
	MAKEOPTS("-r", "fifo");
	void *args;
	pthread_t pt;
	char buf[512];
	int fd;

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);

	fd = rump_sys_open(FSTEST_MNTNAME, O_RDWR);
	if (fd == -1)
		atf_tc_fail_errno("open fifo");

	pthread_create(&pt, NULL, dofifow, (void *)(uintptr_t)fd);

	memset(buf, 0, sizeof(buf));
	if (rump_sys_read(fd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read fifo");

	ATF_REQUIRE_STREQ(buf, MAGICSTR);
	rump_sys_close(fd);

	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(root_chrdev);
ATF_TC_HEAD(root_chrdev, tc)
{

	atf_tc_set_md_var(tc, "descr", "root is /dev/null");
}

ATF_TC_BODY(root_chrdev, tc)
{
	MAKEOPTS("-r", "chr 2 2");
	void *args;
	ssize_t rv;
	char buf[512];
	int fd;

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);

	fd = rump_sys_open(FSTEST_MNTNAME, O_RDWR);
	if (fd == -1)
		atf_tc_fail_errno("open null");

	rv = rump_sys_write(fd, buf, sizeof(buf));
	ATF_REQUIRE(rv == sizeof(buf));

	rv = rump_sys_read(fd, buf, sizeof(buf));
	ATF_REQUIRE(rv == 0);

	rump_sys_close(fd);

	FSTEST_DESTRUCTOR(tc, puffs, args);
}

/*
 * Inactive/reclaim tests
 */

ATF_TC(inactive_basic);
ATF_TC_HEAD(inactive_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "inactive gets called");
}

ATF_TC_BODY(inactive_basic, tc)
{
	struct puffstestargs *pargs;
	void *args;
	int fd;

	FSTEST_CONSTRUCTOR(tc, puffs, args);
	FSTEST_ENTER();
	pargs = args;

	fd = rump_sys_open("file", O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create");

	/* none yet */
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE], 0);

	rump_sys_close(fd);

	/* one for file */
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE], 1);

	FSTEST_EXIT();

	/* another for the mountpoint */
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE], 2);

	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(inactive_reclaim);
ATF_TC_HEAD(inactive_reclaim, tc)
{

	atf_tc_set_md_var(tc, "descr", "inactive/reclaim gets called");
}

ATF_TC_BODY(inactive_reclaim, tc)
{
	struct puffstestargs *pargs;
	void *args;
	int fd;

	FSTEST_CONSTRUCTOR(tc, puffs, args);
	FSTEST_ENTER();
	pargs = args;

	fd = rump_sys_open("file", O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create");

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE], 0);

	if (rump_sys_unlink("file") == -1)
		atf_tc_fail_errno("remove");

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE], 0);
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 0);

	rump_sys_close(fd);
	syncbar(FSTEST_MNTNAME);

	ATF_REQUIRE(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE] > 0);
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 1);

	FSTEST_EXIT();
	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(reclaim_hardlink);
ATF_TC_HEAD(reclaim_hardlink, tc)
{

	atf_tc_set_md_var(tc, "descr", "reclaim gets called only after "
	    "final link is gone");
}

ATF_TC_BODY(reclaim_hardlink, tc)
{
	struct puffstestargs *pargs;
	void *args;
	int fd;
	int ianow;

	FSTEST_CONSTRUCTOR(tc, puffs, args);
	FSTEST_ENTER();
	pargs = args;

	fd = rump_sys_open("file", O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create");

	if (rump_sys_link("file", "anotherfile") == -1)
		atf_tc_fail_errno("create link");
	rump_sys_close(fd);

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 0);

	/* unlink first hardlink */
	if (rump_sys_unlink("file") == -1)
		atf_tc_fail_errno("unlink 1");

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 0);
	ianow = pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE];

	/* unlink second hardlink */
	if (rump_sys_unlink("anotherfile") == -1)
		atf_tc_fail_errno("unlink 2");

	syncbar(FSTEST_MNTNAME);

	ATF_REQUIRE(ianow < pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE]);
	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 1);

	FSTEST_EXIT();
	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(unlink_accessible);
ATF_TC_HEAD(unlink_accessible, tc)
{

	atf_tc_set_md_var(tc, "descr", "open file is accessible after "
	    "having been unlinked");
}

ATF_TC_BODY(unlink_accessible, tc)
{
	MAKEOPTS("-i", "-o", "nopagecache");
	struct puffstestargs *pargs;
	void *args;
	char buf[512];
	int fd, ianow;

	assert(sizeof(buf) > sizeof(MAGICSTR));

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);
	FSTEST_ENTER();
	pargs = args;

	fd = rump_sys_open("file", O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create");

	if (rump_sys_write(fd, MAGICSTR, sizeof(MAGICSTR)) != sizeof(MAGICSTR))
		atf_tc_fail_errno("write");
	if (rump_sys_unlink("file") == -1)
		atf_tc_fail_errno("unlink");

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 0);
	ianow = pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE];

	if (rump_sys_pread(fd, buf, sizeof(buf), 0) == -1)
		atf_tc_fail_errno("read");
	rump_sys_close(fd);

	syncbar(FSTEST_MNTNAME);

	ATF_REQUIRE_EQ(pargs->pta_vn_toserv_ops[PUFFS_VN_RECLAIM], 1);
	ATF_REQUIRE(pargs->pta_vn_toserv_ops[PUFFS_VN_INACTIVE] > ianow);

	ATF_REQUIRE_STREQ(buf, MAGICSTR);

	FSTEST_EXIT();
	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TC(signals);
ATF_TC_HEAD(signals, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that sending a signal can "
	    "cause an interrupt to puffs wait");
}

extern struct proc *rumpns_initproc;
extern void rumpns_psignal(struct proc *, int);
extern void rumpns_sigclearall(struct proc *, void *, void *);
ATF_TC_BODY(signals, tc)
{
	struct stat sb;
	void *args;

	rump_boot_setsigmodel(RUMP_SIGMODEL_RECORD);

	FSTEST_CONSTRUCTOR(tc, puffs, args);
	FSTEST_ENTER();
	RL(rump_sys_stat(".", &sb));

	/* send SIGUSR1, should not affect puffs ops */
	rump_schedule();
	rumpns_psignal(rumpns_initproc, SIGUSR1);
	rump_unschedule();
	RL(rump_sys_stat(".", &sb));

	/* send SIGTERM, should get EINTR */
	rump_schedule();
	rumpns_psignal(rumpns_initproc, SIGTERM);
	rump_unschedule();
	ATF_REQUIRE_ERRNO(EINTR, rump_sys_stat(".", &sb) == -1);

	/* clear sigmask so that we can unmount */
	rump_schedule();
	rumpns_sigclearall(rumpns_initproc, NULL, NULL);
	rump_unschedule();

	FSTEST_EXIT();
	FSTEST_DESTRUCTOR(tc, puffs, args);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mount);

	ATF_TP_ADD_TC(tp, root_fifo);
	ATF_TP_ADD_TC(tp, root_lnk);
	ATF_TP_ADD_TC(tp, root_reg);
	ATF_TP_ADD_TC(tp, root_chrdev);

	ATF_TP_ADD_TC(tp, inactive_basic);
	ATF_TP_ADD_TC(tp, inactive_reclaim);
	ATF_TP_ADD_TC(tp, reclaim_hardlink);
	ATF_TP_ADD_TC(tp, unlink_accessible);

	ATF_TP_ADD_TC(tp, signals);

	return atf_no_error();
}

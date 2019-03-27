/*	$NetBSD: t_fifos.c,v 1.6 2017/01/13 21:30:39 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <ufs/ufs/ufsmount.h>

#include "h_macros.h"

ATF_TC_WITH_CLEANUP(fifos);
ATF_TC_HEAD(fifos, tc)
{
	atf_tc_set_md_var(tc, "descr", "test fifo support in ffs");
	atf_tc_set_md_var(tc, "timeout", "5");
}

#define teststr1 "raving & drooling"
#define teststr2 "haha, charade"

static void *
w1(void *arg)
{
	int fd;

	fd = rump_sys_open("sheep", O_WRONLY);
	if (fd == -1)
		atf_tc_fail_errno("w1 open");
	if (rump_sys_write(fd, teststr1, sizeof(teststr1)) != sizeof(teststr1))
		atf_tc_fail_errno("w1 write");
	rump_sys_close(fd);

	return NULL;
}

static void *
w2(void *arg)
{
	int fd;

	fd = rump_sys_open("pigs", O_WRONLY);
	if (fd == -1)
		atf_tc_fail_errno("w2 open");
	if (rump_sys_write(fd, teststr2, sizeof(teststr2)) != sizeof(teststr2))
		atf_tc_fail_errno("w2 write");
	rump_sys_close(fd);

	return NULL;
}

static void *
r1(void *arg)
{
	char buf[32];
	int fd;

	fd = rump_sys_open("sheep", O_RDONLY);
	if (fd == -1)
		atf_tc_fail_errno("r1 open");
	if (rump_sys_read(fd, buf, sizeof(buf)) != sizeof(teststr1))
		atf_tc_fail_errno("r1 read");
	rump_sys_close(fd);

	if (strcmp(teststr1, buf) != 0)
		atf_tc_fail("got invalid str, %s vs. %s", buf, teststr1);

	return NULL;
}

static void *
r2(void *arg)
{
	char buf[32];
	int fd;

	fd = rump_sys_open("pigs", O_RDONLY);
	if (fd == -1)
		atf_tc_fail_errno("r2 open");
	if (rump_sys_read(fd, buf, sizeof(buf)) != sizeof(teststr2))
		atf_tc_fail_errno("r2 read");
	rump_sys_close(fd);

	if (strcmp(teststr2, buf) != 0)
		atf_tc_fail("got invalid str, %s vs. %s", buf, teststr2);

	return NULL;
}

#define IMGNAME "atf.img"

const char *newfs = "newfs -F -s 10000 " IMGNAME;
#define FAKEBLK "/dev/sp00ka"

ATF_TC_BODY(fifos, tc)
{
	struct ufs_args args;
	pthread_t ptw1, ptw2, ptr1, ptr2;

	if (system(newfs) == -1)
		atf_tc_fail_errno("newfs failed");

	memset(&args, 0, sizeof(args));
	args.fspec = __UNCONST(FAKEBLK);

	rump_init();
	if (rump_sys_mkdir("/animals", 0777) == -1)
		atf_tc_fail_errno("cannot create mountpoint");
	rump_pub_etfs_register(FAKEBLK, IMGNAME, RUMP_ETFS_BLK);
	if (rump_sys_mount(MOUNT_FFS, "/animals", 0, &args, sizeof(args))==-1)
		atf_tc_fail_errno("rump_sys_mount failed");

	/* create fifos */
	if (rump_sys_chdir("/animals") == 1)
		atf_tc_fail_errno("chdir");
	if (rump_sys_mkfifo("pigs", S_IFIFO | 0777) == -1)
		atf_tc_fail_errno("mknod1");
	if (rump_sys_mkfifo("sheep", S_IFIFO | 0777) == -1)
		atf_tc_fail_errno("mknod2");
		
	pthread_create(&ptw1, NULL, w1, NULL);
	pthread_create(&ptw2, NULL, w2, NULL);
	pthread_create(&ptr1, NULL, r1, NULL);
	pthread_create(&ptr2, NULL, r2, NULL);

	pthread_join(ptw1, NULL);
	pthread_join(ptw2, NULL);
	pthread_join(ptr1, NULL);
	pthread_join(ptr2, NULL);

	if (rump_sys_chdir("/") == 1)
		atf_tc_fail_errno("chdir");

	if (rump_sys_unmount("/animals", 0) == -1)
		atf_tc_fail_errno("unmount failed");
}

ATF_TC_CLEANUP(fifos, tc)
{

	unlink(IMGNAME);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fifos);
	return 0;
}

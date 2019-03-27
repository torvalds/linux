/*	$NetBSD: t_renamerace.c,v 1.14 2017/01/13 21:30:40 christos Exp $	*/

/*
 * Modified for rump and atf from a program supplied
 * by Nicolas Joly in kern/40948
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/utsname.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <fs/tmpfs/tmpfs_args.h>

#include "h_macros.h"

ATF_TC(renamerace2);
ATF_TC_HEAD(renamerace2, tc)
{
	atf_tc_set_md_var(tc, "descr", "rename(2) lock order inversion");
	atf_tc_set_md_var(tc, "timeout", "6");
}

static volatile int quittingtime = 0;
static pid_t wrkpid;

static void *
r2w1(void *arg)
{
	int fd;

	rump_pub_lwproc_newlwp(wrkpid);

	fd = rump_sys_open("/file", O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("creat");
	rump_sys_close(fd);

	while (!quittingtime) {
		if (rump_sys_rename("/file", "/dir/file") == -1)
			atf_tc_fail_errno("rename 1");
		if (rump_sys_rename("/dir/file", "/file") == -1)
			atf_tc_fail_errno("rename 2");
	}

	return NULL;
}

static void *
r2w2(void *arg)
{
	int fd;

	rump_pub_lwproc_newlwp(wrkpid);

	while (!quittingtime) {
		fd = rump_sys_open("/dir/file1", O_RDWR);
		if (fd != -1)
			rump_sys_close(fd);
	}

	return NULL;
}

ATF_TC_BODY(renamerace2, tc)
{
	struct tmpfs_args args;
	pthread_t pt[2];

	/*
	 * Force SMP regardless of how many host CPUs there are.
	 * Deadlock is highly unlikely to trigger otherwise.
	 */
	setenv("RUMP_NCPU", "2", 1);

	rump_init();
	memset(&args, 0, sizeof(args));
	args.ta_version = TMPFS_ARGS_VERSION;
	args.ta_root_mode = 0777;
	if (rump_sys_mount(MOUNT_TMPFS, "/", 0, &args, sizeof(args)) == -1)
		atf_tc_fail_errno("could not mount tmpfs");

	if (rump_sys_mkdir("/dir", 0777) == -1)
		atf_tc_fail_errno("cannot create directory");

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	RL(wrkpid = rump_sys_getpid());
	pthread_create(&pt[0], NULL, r2w1, NULL);
	pthread_create(&pt[1], NULL, r2w2, NULL);

	/* usually triggers in <<1s for me */
	sleep(4);
	quittingtime = 1;

	pthread_join(pt[0], NULL);
	pthread_join(pt[1], NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, renamerace2);

	return atf_no_error();
}

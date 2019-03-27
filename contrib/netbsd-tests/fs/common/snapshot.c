/*	$NetBSD: snapshot.c,v 1.7 2013/02/06 09:05:01 hannken Exp $	*/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#include <dev/fssvar.h>

#include <atf-c.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC_WITH_CLEANUP(snapshot);
ATF_TC_HEAD(snapshot, tc)
{

	atf_tc_set_md_var(tc, "descr", "basic snapshot features");
}

static void
makefile(const char *path)
{
	int fd;

	fd = rump_sys_open(path, O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create %s", path);
	rump_sys_close(fd);
}

ATF_TC_BODY(snapshot, tc)
{
	char buf[1024];
	struct fss_set fss;
	int fssfd;
	int fd, fd2, i;

	if (system(NEWFS) == -1)
		atf_tc_fail_errno("cannot create file system");

	rump_init();
	begin();

	if (rump_sys_mkdir("/mnt", 0777) == -1)
		atf_tc_fail_errno("mount point create");
	if (rump_sys_mkdir("/snap", 0777) == -1)
		atf_tc_fail_errno("mount point 2 create");

	rump_pub_etfs_register("/diskdev", IMGNAME, RUMP_ETFS_BLK);

	mount_diskfs("/diskdev", "/mnt");

#define TESTSTR1 "huihai\n"
#define TESTSZ1 (sizeof(TESTSTR1)-1)
#define TESTSTR2 "baana liten\n"
#define TESTSZ2 (sizeof(TESTSTR2)-1)

	fd = rump_sys_open("/mnt/myfile", O_RDWR | O_CREAT, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create file");
	if (rump_sys_write(fd, TESTSTR1, TESTSZ1) != TESTSZ1)
		atf_tc_fail_errno("write fail");

	fssfd = rump_sys_open("/dev/rfss0", O_RDWR);
	if (fssfd == -1)
		atf_tc_fail_errno("cannot open fss");
	makefile(BAKNAME);
	memset(&fss, 0, sizeof(fss));
	fss.fss_mount = __UNCONST("/mnt");
	fss.fss_bstore = __UNCONST(BAKNAME);
	fss.fss_csize = 0;
	if (rump_sys_ioctl(fssfd, FSSIOCSET, &fss) == -1)
		atf_tc_fail_errno("create snapshot");

	for (i = 0; i < 10000; i++) {
		if (rump_sys_write(fd, TESTSTR2, TESTSZ2) != TESTSZ2)
			atf_tc_fail_errno("write fail");
	}
	rump_sys_sync();

	/* technically we should fsck it first? */
	mount_diskfs("/dev/fss0", "/snap");

	/* check for old contents */
	fd2 = rump_sys_open("/snap/myfile", O_RDONLY);
	if (fd2 == -1)
		atf_tc_fail_errno("fail");
	memset(buf, 0, sizeof(buf));
	if (rump_sys_read(fd2, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read snap");
	ATF_CHECK(strcmp(buf, TESTSTR1) == 0);

	/* check that new files are invisible in the snapshot */
	makefile("/mnt/newfile");
	if (rump_sys_open("/snap/newfile", O_RDONLY) != -1)
		atf_tc_fail("newfile exists in snapshot");
	if (errno != ENOENT)
		atf_tc_fail_errno("newfile open should fail with ENOENT");

	/* check that removed files are still visible in the snapshot */
	rump_sys_unlink("/mnt/myfile");
	if (rump_sys_open("/snap/myfile", O_RDONLY) == -1)
		atf_tc_fail_errno("unlinked file no longer in snapshot");

	/* done for now */
}

ATF_TC_CLEANUP(snapshot, tc)
{

	unlink(IMGNAME);
}

ATF_TC_WITH_CLEANUP(snapshotstress);
ATF_TC_HEAD(snapshotstress, tc)
{

	atf_tc_set_md_var(tc, "descr", "snapshot on active file system");
}

#define NACTIVITY 4

static bool activity_stop = false;
static pid_t wrkpid;

static void *
fs_activity(void *arg)
{
	int di, fi;
	char *prefix = arg, path[128];

	rump_pub_lwproc_newlwp(wrkpid);

	RL(rump_sys_mkdir(prefix, 0777));
	while (! activity_stop) {
		for (di = 0; di < 5; di++) {
			snprintf(path, sizeof(path), "%s/d%d", prefix, di);
			RL(rump_sys_mkdir(path, 0777));
			for (fi = 0; fi < 5; fi++) {
				snprintf(path, sizeof(path), "%s/d%d/f%d",
				    prefix, di, fi);
				makefile(path);
			}
		}
		for (di = 0; di < 5; di++) {
			for (fi = 0; fi < 5; fi++) {
				snprintf(path, sizeof(path), "%s/d%d/f%d",
				    prefix, di, fi);
				RL(rump_sys_unlink(path));
			}
			snprintf(path, sizeof(path), "%s/d%d", prefix, di);
			RL(rump_sys_rmdir(path));
		}
	}
	RL(rump_sys_rmdir(prefix));

	rump_pub_lwproc_releaselwp();

	return NULL;
}

ATF_TC_BODY(snapshotstress, tc)
{
	pthread_t at[NACTIVITY];
	struct fss_set fss;
	char prefix[NACTIVITY][128];
	int i, fssfd;

	if (system(NEWFS) == -1)
		atf_tc_fail_errno("cannot create file system");
	/* Force SMP so the stress makes sense. */
	RL(setenv("RUMP_NCPU", "4", 1));
	RZ(rump_init());
	/* Prepare for fsck to use the RUMP /dev/fss0. */
	RL(rump_init_server("unix://commsock"));
	RL(setenv("LD_PRELOAD", "/usr/lib/librumphijack.so", 1));
	RL(setenv("RUMP_SERVER", "unix://commsock", 1));
	RL(setenv("RUMPHIJACK", "blanket=/dev/rfss0", 1));
	begin();

	RL(rump_sys_mkdir("/mnt", 0777));

	rump_pub_etfs_register("/diskdev", IMGNAME, RUMP_ETFS_BLK);

	mount_diskfs("/diskdev", "/mnt");

	/* Start file system activity. */
	RL(wrkpid = rump_sys_getpid());
	for (i = 0; i < NACTIVITY; i++) {
		snprintf(prefix[i], sizeof(prefix[i]),  "/mnt/a%d", i);
		RL(pthread_create(&at[i], NULL, fs_activity, prefix[i]));
		sleep(1);
	}

	fssfd = rump_sys_open("/dev/rfss0", O_RDWR);
	if (fssfd == -1)
		atf_tc_fail_errno("cannot open fss");
	makefile(BAKNAME);
	memset(&fss, 0, sizeof(fss));
	fss.fss_mount = __UNCONST("/mnt");
	fss.fss_bstore = __UNCONST(BAKNAME);
	fss.fss_csize = 0;
	if (rump_sys_ioctl(fssfd, FSSIOCSET, &fss) == -1)
		atf_tc_fail_errno("create snapshot");

	activity_stop = true;
	for (i = 0; i < NACTIVITY; i++)
		RL(pthread_join(at[i], NULL));

	RL(system(FSCK " /dev/rfss0"));
}

ATF_TC_CLEANUP(snapshotstress, tc)
{

	unlink(IMGNAME);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, snapshot);
	ATF_TP_ADD_TC(tp, snapshotstress);
	return 0;
}

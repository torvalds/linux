/*	$NetBSD: h_quota2_tests.c,v 1.5 2017/01/13 21:30:39 christos Exp $	*/

/*
 * rump server for advanced quota tests
 * this one includes functions to run against the filesystem before
 * starting to handle rump requests from clients.
 */

#include "../common/h_fsmacros.h"

#include <err.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/mount.h>

#include <stdlib.h>
#include <unistd.h>

#include <ufs/ufs/ufsmount.h>
#include <dev/fssvar.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

int background = 0;

#define TEST_NONROOT_ID 1

static int
quota_test0(const char *testopts)
{
	static char buf[512];
	int fd;
	int error;
	unsigned int i;
	int chowner = 1;
	for (i =0; testopts && i < strlen(testopts); i++) {
		switch(testopts[i]) {
		case 'C':
			chowner = 0;
			break;
		default:
			errx(1, "test4: unknown option %c", testopts[i]);
		}
	}
	if (chowner)
		rump_sys_chown(".", TEST_NONROOT_ID, TEST_NONROOT_ID);
	rump_sys_chmod(".", 0777);
	if (rump_sys_setegid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_setegid");
		return error;
	}
	if (rump_sys_seteuid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_seteuid");
		return error;
	}
	fd = rump_sys_open("test_fillup", O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		error = errno;
		warn("rump_sys_open");
	} else {
		while (rump_sys_write(fd, buf, sizeof(buf)) == sizeof(buf))
			error = 0;
		error = errno;
	}
	rump_sys_close(fd);
	rump_sys_seteuid(0);
	rump_sys_setegid(0);
	return error;
}

static int
quota_test1(const char *testopts)
{
	static char buf[512];
	int fd;
	int error;
	rump_sys_chown(".", TEST_NONROOT_ID, TEST_NONROOT_ID);
	rump_sys_chmod(".", 0777);
	if (rump_sys_setegid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_setegid");
		return error;
	}
	if (rump_sys_seteuid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_seteuid");
		return error;
	}
	fd = rump_sys_open("test_fillup", O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		error = errno;
		warn("rump_sys_open");
	} else {
		/*
		 * write up to the soft limit, wait a bit, an try to
		 * keep on writing
		 */
		int i;

		/* write 2k: with the directory this makes 2.5K */
		for (i = 0; i < 4; i++) {
			error = rump_sys_write(fd, buf, sizeof(buf));
			if (error != sizeof(buf))
				err(1, "write failed early");
		}
		sleep(2);
		/* now try to write an extra .5k */
		if (rump_sys_write(fd, buf, sizeof(buf)) != sizeof(buf))
			error = errno;
		else
			error = 0;
	}
	rump_sys_close(fd);
	rump_sys_seteuid(0);
	rump_sys_setegid(0);
	return error;
}

static int
quota_test2(const char *testopts)
{
	static char buf[512];
	int fd;
	int error;
	int i;
	rump_sys_chown(".", TEST_NONROOT_ID, TEST_NONROOT_ID);
	rump_sys_chmod(".", 0777);
	if (rump_sys_setegid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_setegid");
		return error;
	}
	if (rump_sys_seteuid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_seteuid");
		return error;
	}

	for (i = 0; ; i++) {
		sprintf(buf, "file%d", i);
		fd = rump_sys_open(buf, O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			break;
		sprintf(buf, "test file no %d", i);
		rump_sys_write(fd, buf, strlen(buf));
		rump_sys_close(fd);
	}
	error = errno;
	
	rump_sys_close(fd);
	rump_sys_seteuid(0);
	rump_sys_setegid(0);
	return error;
}

static int
quota_test3(const char *testopts)
{
	static char buf[512];
	int fd;
	int error;
	int i;
	rump_sys_chown(".", TEST_NONROOT_ID, TEST_NONROOT_ID);
	rump_sys_chmod(".", 0777);
	if (rump_sys_setegid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_setegid");
		return error;
	}
	if (rump_sys_seteuid(TEST_NONROOT_ID) != 0) {
		error = errno;
		warn("rump_sys_seteuid");
		return error;
	}

	/*
	 * create files one past the soft limit: one less as we already own the
	 * root directory
	 */
	for (i = 0; i < 4; i++) {
		sprintf(buf, "file%d", i);
		fd = rump_sys_open(buf, O_EXCL| O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			err(1, "file create failed early");
		sprintf(buf, "test file no %d", i);
		rump_sys_write(fd, buf, strlen(buf));
		rump_sys_close(fd);
	}
	/* now create an extra file after grace time: this should fail */
	sleep(2);
	sprintf(buf, "file%d", i);
	fd = rump_sys_open(buf, O_EXCL| O_CREAT | O_RDWR, 0644);
	if (fd < 0)
		error = errno;
	else
		error = 0;
	
	rump_sys_close(fd);
	rump_sys_seteuid(0);
	rump_sys_setegid(0);
	return error;
}

static int
quota_test4(const char *testopts)
{
	static char buf[512];
	int fd, fssfd;
	struct fss_set fss;
	unsigned int i;
	int unl=0;
	int unconf=0;

	/*
	 * take an internal snapshot of the filesystem, and create a new
	 * file with some data
	 */
	rump_sys_chown(".", 0, 0);
	rump_sys_chmod(".", 0777);

	for (i =0; testopts && i < strlen(testopts); i++) {
		switch(testopts[i]) {
		case 'L':
			unl++;
			break;
		case 'C':
			unconf++;
			break;
		default:
			errx(1, "test4: unknown option %c", testopts[i]);
		}
	}

	/* first create the snapshot */

	 fd = rump_sys_open(FSTEST_MNTNAME "/le_snap", O_CREAT | O_RDWR, 0777);
	 if (fd == -1)
		err(1, "create " FSTEST_MNTNAME "/le_snap");
	 rump_sys_close(fd);
	 fssfd = rump_sys_open("/dev/rfss0", O_RDWR);
	 if (fssfd == -1)
		err(1, "cannot open fss");
	memset(&fss, 0, sizeof(fss));
	fss.fss_mount = __UNCONST("/mnt");
	fss.fss_bstore = __UNCONST(FSTEST_MNTNAME "/le_snap");
	fss.fss_csize = 0;
	if (rump_sys_ioctl(fssfd, FSSIOCSET, &fss) == -1)
		err(1, "create snapshot");
	if (unl) {
		if (rump_sys_unlink(FSTEST_MNTNAME "/le_snap") == -1)
			err(1, "unlink snapshot");
	}

	/* now create some extra files */

	for (i = 0; i < 4; i++) {
		sprintf(buf, "file%d", i);
		fd = rump_sys_open(buf, O_EXCL| O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			err(1, "create %s", buf);
		sprintf(buf, "test file no %d", i);
		rump_sys_write(fd, buf, strlen(buf));
		rump_sys_close(fd);
	}
	if (unconf)
		if (rump_sys_ioctl(fssfd, FSSIOCCLR, NULL) == -1)
			err(1, "unconfigure snapshot");
	return 0;
}

static int
quota_test5(const char *testopts)
{
	static char buf[512];
	int fd;
	int remount = 0;
	int unlnk = 0;
	int log = 0;
	unsigned int i;

	for (i =0; testopts && i < strlen(testopts); i++) {
		switch(testopts[i]) {
		case 'L':
			log++;
			break;
		case 'R':
			remount++;
			break;
		case 'U':
			unlnk++;
			break;
		default:
			errx(1, "test4: unknown option %c", testopts[i]);
		}
	}
	if (remount) {
		struct ufs_args uargs;
		uargs.fspec = __UNCONST("/diskdev");
		/* remount the fs read/write */
		if (rump_sys_mount(MOUNT_FFS, FSTEST_MNTNAME,
		    MNT_UPDATE | (log ? MNT_LOG : 0),
		    &uargs, sizeof(uargs)) == -1)
			err(1, "mount ffs rw %s", FSTEST_MNTNAME);
	}

	if (unlnk) {
		/*
		 * open and unlink a file
		 */

		fd = rump_sys_open("unlinked_file",
		    O_EXCL| O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			err(1, "create %s", "unlinked_file");
		sprintf(buf, "test unlinked_file");
		rump_sys_write(fd, buf, strlen(buf));
		if (rump_sys_unlink("unlinked_file") == -1)
			err(1, "unlink unlinked_file");
		if (rump_sys_fsync(fd) == -1) 
			err(1, "fsync unlinked_file");
		rump_sys_reboot(RUMP_RB_NOSYNC, NULL);
		errx(1, "reboot failed");
		return 1;
	}
	return 0;
}

struct quota_test {
	int (*func)(const char *);
	const char *desc;
};

struct quota_test quota_tests[] = {
	{ quota_test0, "write up to hard limit"},
	{ quota_test1, "write beyond the soft limit after grace time"},
	{ quota_test2, "create file up to hard limit"},
	{ quota_test3, "create file beyond the soft limit after grace time"},
	{ quota_test4, "take a snapshot and add some data"},
	{ quota_test5, "open and unlink a file"},
};

static void
usage(void)
{
	unsigned int test;
	fprintf(stderr, "usage: %s [-b] [-l] test# diskimage bindurl\n",
	    getprogname());
	fprintf(stderr, "available tests:\n");
	for (test = 0; test < sizeof(quota_tests) / sizeof(quota_tests[0]);
	    test++)
		fprintf(stderr, "\t%d: %s\n", test, quota_tests[test].desc);
	exit(1);
}

static void
die(const char *reason, int error)
{

	warnx("%s: %s", reason, strerror(error));
	if (background)
		rump_daemonize_done(error);
	exit(1);
}

static sem_t sigsem;
static void
sigreboot(int sig)
{

	sem_post(&sigsem);
}

int 
main(int argc, char **argv)
{
	int error;
	u_long test;
	char *end;
	struct ufs_args uargs;
	const char *filename;
	const char *serverurl;
	const char *topts = NULL;
	int mntopts = 0;
	int ch;

	while ((ch = getopt(argc, argv, "blo:r")) != -1) {
		switch(ch) {
		case 'b':
			background = 1;
			break;
		case 'l':
			mntopts |= MNT_LOG;
			break;
		case 'r':
			mntopts |= MNT_RDONLY;
			break;
		case 'o':
			topts = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 3)
		usage();

	filename = argv[1];
	serverurl = argv[2];

	test = strtoul(argv[0], &end, 10);
	if (*end != '\0') {
		usage();
	}
	if (test > sizeof(quota_tests) / sizeof(quota_tests[0])) {
		usage();
	}

	if (background) {
		error = rump_daemonize_begin();
		if (error)
			errx(1, "rump daemonize: %s", strerror(error));
	}

	error = rump_init();
	if (error)
		die("rump init failed", error);

	if (rump_sys_mkdir(FSTEST_MNTNAME, 0777) == -1)
		err(1, "mount point create");
	rump_pub_etfs_register("/diskdev", filename, RUMP_ETFS_BLK);
	uargs.fspec = __UNCONST("/diskdev");
	if (rump_sys_mount(MOUNT_FFS, FSTEST_MNTNAME, mntopts,
	    &uargs, sizeof(uargs)) == -1)
		die("mount ffs", errno);

	if (rump_sys_chdir(FSTEST_MNTNAME) == -1)
		err(1, "cd %s", FSTEST_MNTNAME);
	error = quota_tests[test].func(topts);
	if (error) {
		fprintf(stderr, " test %lu: %s returned %d: %s\n",
		    test, quota_tests[test].desc, error, strerror(error));
	}
	if (rump_sys_chdir("/") == -1)
		err(1, "cd /");

	error = rump_init_server(serverurl);
	if (error)
		die("rump server init failed", error);
	if (background)
		rump_daemonize_done(RUMP_DAEMONIZE_SUCCESS);

	sem_init(&sigsem, 0, 0);
	signal(SIGTERM, sigreboot);
	signal(SIGINT, sigreboot);
	sem_wait(&sigsem);

	rump_sys_reboot(0, NULL);
	/*NOTREACHED*/
	return 0;
}

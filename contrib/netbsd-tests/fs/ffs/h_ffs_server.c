/*	$NetBSD: h_ffs_server.c,v 1.2 2012/08/24 20:25:50 jmmv Exp $	*/

/*
 * rump server for advanced quota tests
 */

#include "../common/h_fsmacros.h"

#include <err.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/mount.h>

#include <stdlib.h>
#include <unistd.h>

#include <ufs/ufs/ufsmount.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

int background = 0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-b] [-l] diskimage bindurl\n",
	    getprogname());
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
	struct ufs_args uargs;
	const char *filename;
	const char *serverurl;
	int log = 0;
	int ch;

	while ((ch = getopt(argc, argv, "bl")) != -1) {
		switch(ch) {
		case 'b':
			background = 1;
			break;
		case 'l':
			log = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	filename = argv[0];
	serverurl = argv[1];

	if (background) {
		error = rump_daemonize_begin();
		if (error)
			errx(1, "rump daemonize: %s", strerror(error));
	}

	error = rump_init();
	if (error)
		die("rump init failed", error);

	if (rump_sys_mkdir(FSTEST_MNTNAME, 0777) == -1)
		die("mount point create", errno);
	rump_pub_etfs_register("/diskdev", filename, RUMP_ETFS_BLK);
	uargs.fspec = __UNCONST("/diskdev");
	if (rump_sys_mount(MOUNT_FFS, FSTEST_MNTNAME, (log) ? MNT_LOG : 0,
	    &uargs, sizeof(uargs)) == -1)
		die("mount ffs", errno);

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

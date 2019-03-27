/*	$NetBSD: h_mdserv.c,v 1.4 2011/02/10 13:29:02 pooka Exp $	*/

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <dev/md.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#define MDSIZE (1024*1024)

#define REQUIRE(a, msg) if ((a) != 0) err(1, msg);

static void *
prober(void *arg)
{
	int fd, error;
	char buf[4];
	ssize_t n;

	fd = rump_sys_open(arg, O_RDONLY);
	for (;;) {
		n = rump_sys_read(fd, buf, sizeof(buf));

		switch (n) {
		case 4:
			error = 0;
			goto out;

		case -1:
			if (errno == ENXIO) {
				usleep(1000);
				continue;
			}

			/* FALLTHROUGH */
		default:
			error = EPIPE;
			goto out;
		}
	}
 out:

	error = rump_daemonize_done(error);
	REQUIRE(error, "rump_daemonize_done");

	if (error)
		exit(1);

	return NULL;
}

int
main(int argc, char *argv[])
{
	pthread_t pt;
	struct md_conf md;
	int fd, error;

	if (argc != 2)
		exit(1);

	md.md_addr = calloc(1, MDSIZE);
	md.md_size = MDSIZE;
	md.md_type = MD_UMEM_SERVER;

	error = rump_daemonize_begin();
	REQUIRE(error, "rump_daemonize_begin");

	error = rump_init();
	REQUIRE(error, "rump_init");

	error = rump_init_server("unix://commsock");
	REQUIRE(error, "init server");

	if ((fd = rump_sys_open(argv[1], O_RDWR)) == -1)
		err(1, "open");

	/*
	 * Now, configuring the md driver also causes our process
	 * to start acting as the worker for the md.  Splitting it
	 * into two steps in the driver is not easy, since md is
	 * supposed to be unconfigured when the process dies
	 * (process may exit between calling ioctl1 and ioctl2).
	 * So, start a probe thread which attempts to read the md
	 * and declares the md as configured when the read is
	 * succesful.
	 */
	error = pthread_create(&pt, NULL, prober, argv[1]);
	REQUIRE(error, "pthread_create");
	pthread_detach(pt);

	if (rump_sys_ioctl(fd, MD_SETCONF, &md) == -1) {
		rump_daemonize_done(errno);
		exit(1);
	}

	return 0;
}

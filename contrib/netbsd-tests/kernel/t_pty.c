/* $Id: t_pty.c,v 1.2 2017/01/13 21:30:41 christos Exp $ */

/*
 * Allocates a pty(4) device, and sends the specified number of packets of the
 * specified length though it, while a child reader process reads and reports
 * results.
 *
 * Written by Matthew Mondor
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_pty.c,v 1.2 2017/01/13 21:30:41 christos Exp $");

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#ifdef __linux__
#define _XOPEN_SOURCE
#define __USE_XOPEN
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef STANDALONE
static __dead void	usage(const char *);
static void		parse_args(int, char **);
#else
#include <atf-c.h>
#include "h_macros.h"
#endif

static int		pty_open(void);
static int		tty_open(const char *);
static void		fd_nonblock(int);
static pid_t		child_spawn(const char *);
static void		run(void);

static size_t		buffer_size = 4096;
static size_t		packets = 2;
static uint8_t		*dbuf;
static int		verbose;
static int		qsize;


static
void run(void)
{
	size_t i;
	int pty;
	int status;
	pid_t child;
	if ((dbuf = calloc(1, buffer_size)) == NULL)
		err(EXIT_FAILURE, "malloc(%zu)", buffer_size);

	if (verbose)
		(void)printf(
		    "parent: started; opening PTY and spawning child\n");
	pty = pty_open();
	child = child_spawn(ptsname(pty));
	if (verbose)
		(void)printf("parent: sleeping to make sure child is ready\n");
	(void)sleep(1);

	for (i = 0; i < buffer_size; i++)
		dbuf[i] = i & 0xff;

	if (verbose)
		(void)printf("parent: writing\n");

	for (i = 0; i < packets; i++) {
		ssize_t	size;

		if (verbose)
			(void)printf(
			    "parent: attempting to write %zu bytes to PTY\n",
			    buffer_size);
		if ((size = write(pty, dbuf, buffer_size)) == -1) {
			err(EXIT_FAILURE, "parent: write()");
			break;
		}
		if (verbose)
			(void)printf("parent: wrote %zd bytes to PTY\n", size);
	}

	if (verbose)
		(void)printf("parent: waiting for child to exit\n");
	if (waitpid(child, &status, 0) == -1)
		err(EXIT_FAILURE, "waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		errx(EXIT_FAILURE, "child failed");

	if (verbose)
		(void)printf("parent: closing PTY\n");
	(void)close(pty);
	if (verbose)
		(void)printf("parent: exiting\n");
}

static void
condition(int fd)
{
	struct termios	tios;

	if (qsize) {
		int opt = qsize;
		if (ioctl(fd, TIOCSQSIZE, &opt) == -1)
			err(EXIT_FAILURE, "Couldn't set tty(4) buffer size");
		if (ioctl(fd, TIOCGQSIZE, &opt) == -1)
			err(EXIT_FAILURE, "Couldn't get tty(4) buffer size");
		if (opt != qsize)
			errx(EXIT_FAILURE, "Wrong qsize %d != %d\n",
			    qsize, opt);
	}
	if (tcgetattr(fd, &tios) == -1)
		err(EXIT_FAILURE, "tcgetattr()");
	cfmakeraw(&tios);
	cfsetspeed(&tios, B921600);
	if (tcsetattr(fd, TCSANOW, &tios) == -1)
		err(EXIT_FAILURE, "tcsetattr()");
}

static int
pty_open(void)
{
	int	fd;

	if ((fd = posix_openpt(O_RDWR)) == -1)
		err(EXIT_FAILURE, "Couldn't pty(4) device");
	condition(fd);
	if (grantpt(fd) == -1)
		err(EXIT_FAILURE,
		    "Couldn't grant permissions on tty(4) device");


	condition(fd);

	if (unlockpt(fd) == -1)
		err(EXIT_FAILURE, "unlockpt()");

	return fd;
}

static int
tty_open(const char *ttydev)
{
	int		fd;

	if ((fd = open(ttydev, O_RDWR, 0)) == -1)
		err(EXIT_FAILURE, "Couldn't open tty(4) device");

#ifdef USE_PPP_DISCIPLINE
	{
		int	opt = PPPDISC;
		if (ioctl(fd, TIOCSETD, &opt) == -1)
			err(EXIT_FAILURE,
			    "Couldn't set tty(4) discipline to PPP");
	}
#endif

	condition(fd);

	return fd;
}

static void
fd_nonblock(int fd)
{
	int	opt;

	if ((opt = fcntl(fd, F_GETFL, NULL)) == -1)
		err(EXIT_FAILURE, "fcntl()");
	if (fcntl(fd, F_SETFL, opt | O_NONBLOCK) == -1)
		err(EXIT_FAILURE, "fcntl()");
}

static pid_t
child_spawn(const char *ttydev)
{
	pid_t		pid;
	int		tty;
	struct pollfd	pfd;
	size_t		total = 0;

	if ((pid = fork()) == -1)
		err(EXIT_FAILURE, "fork()");
	(void)setsid();
	if (pid != 0)
		return pid;

	if (verbose)
		(void)printf("child: started; open \"%s\"\n", ttydev);
	tty = tty_open(ttydev);
	fd_nonblock(tty);

	if (verbose)
		(void)printf("child: TTY open, starting read loop\n");
	pfd.fd = tty;
	pfd.events = POLLIN;
	pfd.revents = 0;
	for (;;) {
		int	ret;
		ssize_t	size;

		if (verbose)
			(void)printf("child: polling\n");
		if ((ret = poll(&pfd, 1, 2000)) == -1)
			err(EXIT_FAILURE, "child: poll()");
		if (ret == 0)
			break;
		if ((pfd.revents & POLLERR) != 0)
			break;
		if ((pfd.revents & POLLIN) != 0) {
			for (;;) {
				if (verbose)
					(void)printf(
					    "child: attempting to read %zu"
					    " bytes\n", buffer_size);
				if ((size = read(tty, dbuf, buffer_size))
				    == -1) {
					if (errno == EAGAIN)
						break;
					err(EXIT_FAILURE, "child: read()");
				}
				if (qsize && size < qsize &&
				    (size_t)size < buffer_size)
					errx(EXIT_FAILURE, "read returned %zd "
					    "less than the queue size %d",
					    size, qsize);
				if (verbose)
					(void)printf(
					    "child: read %zd bytes from TTY\n",
					    size);
				if (size == 0)
					goto end;
				total += size;
			}
		}
	}
end:
	if (verbose)
		(void)printf("child: closing TTY %zu\n", total);
	(void)close(tty);
	if (verbose)
		(void)printf("child: exiting\n");
	if (total != buffer_size * packets)
		errx(EXIT_FAILURE,
		    "Lost data %zu != %zu\n", total, buffer_size * packets);

	exit(EXIT_SUCCESS);
}

#ifdef STANDALONE
static void
usage(const char *msg)
{

	if (msg != NULL)
		(void) fprintf(stderr, "\n%s\n\n", msg);

	(void)fprintf(stderr,
	    "Usage: %s [-v] [-q <qsize>] [-s <packetsize>] [-n <packets>]\n",
		getprogname());

	exit(EXIT_FAILURE);
}

static void
parse_args(int argc, char **argv)
{
	int	ch;

	while ((ch = getopt(argc, argv, "n:q:s:v")) != -1) {
		switch (ch) {
		case 'n':
			packets = (size_t)atoi(optarg);
			break;
		case 'q':
			qsize = atoi(optarg);
			break;
		case 's':
			buffer_size = (size_t)atoi(optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage(NULL);
			break;
		}
	}
	if (buffer_size < 0 || buffer_size > 65536)
		usage("-s must be between 0 and 65536");
	if (packets < 1 || packets > 100)
		usage("-p must be between 1 and 100");
}

int
main(int argc, char **argv)
{

	parse_args(argc, argv);
	run();
	exit(EXIT_SUCCESS);
}

#else
ATF_TC(pty_no_queue);

ATF_TC_HEAD(pty_no_queue, tc)
{
        atf_tc_set_md_var(tc, "descr", "Checks that writing to pty "
	    "does not lose data with the default queue size of 1024");
}

ATF_TC_BODY(pty_no_queue, tc)
{
	qsize = 0;
	run();
}

ATF_TC(pty_queue);

ATF_TC_HEAD(pty_queue, tc)
{
        atf_tc_set_md_var(tc, "descr", "Checks that writing to pty "
	    "does not lose data with the a queue size of 4096");
}

ATF_TC_BODY(pty_queue, tc)
{
	qsize = 4096;
	run();
}

ATF_TP_ADD_TCS(tp)
{
        ATF_TP_ADD_TC(tp, pty_no_queue);
        ATF_TP_ADD_TC(tp, pty_queue);

        return atf_no_error();
}
#endif

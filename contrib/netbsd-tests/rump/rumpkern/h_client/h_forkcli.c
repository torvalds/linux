/*	$NetBSD: h_forkcli.c,v 1.1 2011/01/05 17:19:09 pooka Exp $	*/

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rumpclient.h>

static void
simple(void)
{
	struct rumpclient_fork *rf;
	pid_t pid1, pid2;
	int fd, status;

	if ((pid1 = rump_sys_getpid()) < 2)
		errx(1, "unexpected pid %d", pid1);

	fd = rump_sys_open("/dev/null", O_CREAT | O_RDWR);
	if (rump_sys_write(fd, &fd, sizeof(fd)) != sizeof(fd))
		errx(1, "write newlyopened /dev/null");

	if ((rf = rumpclient_prefork()) == NULL)
		err(1, "prefork");

	switch (fork()) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		if (rumpclient_fork_init(rf) == -1)
			err(1, "postfork init failed");

		if ((pid2 = rump_sys_getpid()) < 2)
			errx(1, "unexpected pid %d", pid2);
		if (pid1 == pid2)
			errx(1, "child and parent pids are equal");

		/* check that we can access the fd, the close it and exit */
		if (rump_sys_write(fd, &fd, sizeof(fd)) != sizeof(fd))
			errx(1, "write child /dev/null");
		rump_sys_close(fd);
		break;
	default:
		/*
		 * check that we can access the fd, wait for the child, and
		 * check we can still access the fd
		 */
		if (rump_sys_write(fd, &fd, sizeof(fd)) != sizeof(fd))
			errx(1, "write parent /dev/null");
		if (wait(&status) == -1)
			err(1, "wait failed");
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			errx(1, "child exited with status %d", status);
		if (rump_sys_write(fd, &fd, sizeof(fd)) != sizeof(fd))
			errx(1, "write parent /dev/null");
		break;
	}
}

static void
cancel(void)
{

	/* XXX: not implemented in client / server !!! */
}

#define TESTSTR "i am your fatherrrrrrr"
#define TESTSLEN (sizeof(TESTSTR)-1)
static void
pipecomm(void)
{
	struct rumpclient_fork *rf;
	char buf[TESTSLEN+1];
	int pipetti[2];
	int status;

	if (rump_sys_pipe(pipetti) == -1)
		errx(1, "pipe");

	if ((rf = rumpclient_prefork()) == NULL)
		err(1, "prefork");

	switch (fork()) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		if (rumpclient_fork_init(rf) == -1)
			err(1, "postfork init failed");

		memset(buf, 0, sizeof(buf));
		if (rump_sys_read(pipetti[0], buf, TESTSLEN) != TESTSLEN)
			err(1, "pipe read");
		if (strcmp(TESTSTR, buf) != 0)
			errx(1, "teststring doesn't match, got %s", buf);
		break;
	default:
		if (rump_sys_write(pipetti[1], TESTSTR, TESTSLEN) != TESTSLEN)
			err(1, "pipe write");
		if (wait(&status) == -1)
			err(1, "wait failed");
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			errx(1, "child exited with status %d", status);
		break;
	}
}

static void
fakeauth(void)
{
	struct rumpclient_fork *rf;
	uint32_t *auth;
	int rv;

	if ((rf = rumpclient_prefork()) == NULL)
		err(1, "prefork");

	/* XXX: we know the internal structure of rf */
	auth = (void *)rf;
	*(auth+3) = *(auth+3) ^ 0x1;

	rv = rumpclient_fork_init(rf);
	if (!(rv == -1 && errno == ESRCH))
		exit(1);
}

struct parsa {
	const char *arg;		/* sp arg, el		*/
	void (*spring)(void);		/* spring into action	*/
} paragus[] = {
	{ "simple", simple },
	{ "cancel", cancel },
	{ "pipecomm", pipecomm },
	{ "fakeauth", fakeauth },
};

int
main(int argc, char *argv[])
{
	unsigned i;

	if (argc != 2)
		errx(1, "invalid usage");

	if (rumpclient_init() == -1)
		err(1, "rumpclient init");

	for (i = 0; i < __arraycount(paragus); i++) {
		if (strcmp(argv[1], paragus[i].arg) == 0) {
			paragus[i].spring();
			break;
		}
	}
	if (i == __arraycount(paragus)) {
		printf("invalid test %s\n", argv[1]);
		exit(1);
	}

	exit(0);
}

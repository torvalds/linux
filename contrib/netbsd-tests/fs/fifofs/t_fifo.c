/* Test case written by Bharat Joshi */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_fifo.c,v 1.2 2017/01/10 22:36:29 christos Exp $");

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <signal.h>

#ifndef STANDALONE
#include <atf-c.h>
#endif

#define FIFO_FILE_PATH       "./fifo_file"
#define NUM_MESSAGES         20
#define MSG_SIZE             240
#define MESSAGE              "I am fine"

static int verbose = 0;

/*
 * child_writer
 *
 * Function that runs in child context and opens and write to the FIFO.
 */
static void
child_writer(void)
{
	ssize_t rv;
	int fd;
	size_t count;
	char message[MSG_SIZE] = MESSAGE;
	static const struct timespec ts = { 0, 10000 };

	/* Open the fifo in write-mode */
	for (;;) {
		fd = open(FIFO_FILE_PATH, O_WRONLY, 0);
		if (fd == -1) {
			if (errno == EINTR)
				continue;
			err(1, "Child: can't open fifo in write mode");
		}
		break;
	}

	for (count = 0; count < NUM_MESSAGES; count++) {
		rv = write(fd, message, MSG_SIZE);
		if (rv == -1) {
			warn("Child: Failed to write");
			break;
		}
		if (rv != MSG_SIZE)
			warnx("Child: wrote only %zd", rv);
		nanosleep(&ts, NULL);
	}

	close(fd);
	if (verbose) {
		printf("Child: Closed the fifo file\n");
		fflush(stdout);
	}
}

/*
 * _sigchild_handler
 *
 * Called when a sigchild is delivered
 */
static void
sigchild_handler(int signo)
{
	if (verbose) {
		if (signo == SIGCHLD) {
			printf("Got sigchild\n");
		} else {
			printf("Got %d signal\n", signo);
		}
		fflush(stdout);
	}

}

static int
run(void)
{
	pid_t pid;
	ssize_t rv;
	int fd, status;
	size_t buf_size = MSG_SIZE;
	char buf[MSG_SIZE];
	struct sigaction action;
	static const struct timespec ts = { 0, 500000000 };

	/* Catch sigchild Signal */
	memset(&action, 0, sizeof(action));
	action.sa_handler = sigchild_handler;
	sigemptyset(&action.sa_mask);

	if (sigaction(SIGCHLD, &action, NULL) == -1)
		err(1, "sigaction");

	(void)unlink(FIFO_FILE_PATH);
	/* First create a fifo */
	if (mkfifo(FIFO_FILE_PATH, S_IRUSR | S_IWUSR) == -1)
		err(1, "mkfifo");

	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		/* Open the file in write mode so that subsequent read 
		 * from parent side does not block the parent..
		 */
		if ((fd = open(FIFO_FILE_PATH, O_WRONLY, 0)) == -1)
			err(1, "failed to open fifo");

		/* In child */
		child_writer();
		return 0;

	default:
		break;
	}

	if (verbose) {
		printf("Child pid is %d\n", pid );
		fflush(stdout);
	}

	/* In parent */
	for (;;) {
		if ((fd = open(FIFO_FILE_PATH, O_RDONLY, 0)) == -1) {
			if (errno == EINTR)
				continue;
			else
				err(1, "Failed to open the fifo in read mode");
		}
		/* Read mode is opened */
		break;

	}

	nanosleep(&ts, NULL);
	if (verbose) {
		printf("Was sleeping...\n");
		fflush(stdout);
	}

	for (;;) {
		rv = read(fd, buf, buf_size);

		if (rv == -1) {
			warn("Failed to read");
			if (errno == EINTR) {
				if (verbose) {
					printf("Parent interrupted, "
					    "continuing...\n");
					fflush(stdout);
				}
				continue;
			}

			break;
		}

		if (rv == 0) {
			if (verbose) {
				printf("Writers have closed, looks like we "
				    "are done\n");
				fflush(stdout);
			}
			break;
		}

		if (verbose) {
			printf("Received %zd bytes message '%s'\n", rv, buf);
			fflush(stdout);
		}
	}

	close(fd);

	if (verbose) {
		printf("We are done.. now reap the child");
		fflush(stdout);
	}

	// Read the child...
	while (waitpid(pid, &status, 0) == -1)
		if (errno != EINTR) {
			warn("Failed to reap the child");
			return 1;
		}

	if (verbose) {
		printf("We are done completely\n");
		fflush(stdout);
	}
	return 0;
}

#ifndef STANDALONE
ATF_TC(parent_child);      

ATF_TC_HEAD(parent_child, tc)
{
        atf_tc_set_md_var(tc, "descr", "Checks that when a fifo is shared "
	    "between a reader parent and a writer child, that read will "
	    "return EOF, and not get stuck after the child exits");
}
 
ATF_TC_BODY(parent_child, tc)
{       
        ATF_REQUIRE(run() == 0);
}       

ATF_TP_ADD_TCS(tp)
{       
        ATF_TP_ADD_TC(tp, parent_child);
        
        return atf_no_error();
}       
#else
int
main(void)
{
	verbose = 1;
	return run();
}
#endif

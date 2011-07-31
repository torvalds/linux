#define _GNU_SOURCE	/* needed to get the defines */
#include <fcntl.h>	/* in glibc 2.2 this has the needed
				   values defined */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int event_fd;

static void handler(int sig, siginfo_t *si, void *data)
{
	event_fd = si->si_fd;
}

int main(void)
{
	struct sigaction act;
	int fd;

	act.sa_sigaction = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGRTMIN + 1, &act, NULL);

	fd = open(".", O_RDONLY);
	fcntl(fd, F_SETSIG, SIGRTMIN + 1);
	fcntl(fd, F_NOTIFY, DN_MODIFY|DN_CREATE|DN_MULTISHOT);
	/* we will now be notified if any of the files
	   in "." is modified or new files are created */
	while (1) {
		pause();
		printf("Got event on fd=%d\n", event_fd);
	}
}

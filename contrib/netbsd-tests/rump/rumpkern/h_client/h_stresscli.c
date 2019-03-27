/*	$NetBSD: h_stresscli.c,v 1.9 2011/06/26 13:17:36 christos Exp $	*/

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <rump/rump_syscalls.h>
#include <rump/rumpclient.h>

static unsigned int syscalls, bindcalls;
static pid_t mypid;
static volatile sig_atomic_t doquit;

static void
signaali(int sig)
{

	doquit = 1;
}

static const int hostnamemib[] = { CTL_KERN, KERN_HOSTNAME };
static char hostnamebuf[128];
#define HOSTNAMEBASE "rumpclient"

static int iskiller;

static void *
client(void *arg)
{
	char buf[256];
	struct sockaddr_in sin;
	size_t blen;
	int port = (int)(uintptr_t)arg;
	int s, fd, x;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(port);

	while (!doquit) {
		pid_t pidi;
		blen = sizeof(buf);
		s = rump_sys_socket(PF_INET, SOCK_STREAM, 0);
		if (s == -1)
			err(1, "socket");
		atomic_inc_uint(&syscalls);

		fd = rump_sys_open("/dev/null", O_RDWR);
		atomic_inc_uint(&syscalls);

		if (doquit)
			goto out;

		x = 1;
		if (rump_sys_setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		    &x, sizeof(x)) == -1)
			err(1, "reuseaddr");

		/*
		 * we don't really know when the kernel handles our disconnect,
		 * so be soft about about the failure in case of a killer client
		 */
		if (rump_sys_bind(s, (struct sockaddr*)&sin, sizeof(sin))==-1) {
			if (!iskiller)
				err(1, "bind to port %d failed",
				    ntohs(sin.sin_port));
		} else {
			atomic_inc_uint(&bindcalls);
		}
		atomic_inc_uint(&syscalls);

		if (doquit)
			goto out;

		if (rump_sys___sysctl(hostnamemib, __arraycount(hostnamemib),
		    buf, &blen, NULL, 0) == -1)
			err(1, "sysctl");
		if (strncmp(buf, hostnamebuf, sizeof(HOSTNAMEBASE)-1) != 0)
			errx(1, "hostname (%s/%s) mismatch", buf, hostnamebuf);
		atomic_inc_uint(&syscalls);

		if (doquit)
			goto out;

		pidi = rump_sys_getpid();
		if (pidi == -1)
			err(1, "getpid");
		if (pidi != mypid)
			errx(1, "mypid mismatch");
		atomic_inc_uint(&syscalls);

		if (doquit)
			goto out;

		if (rump_sys_write(fd, buf, 16) != 16)
			err(1, "write /dev/null");
		atomic_inc_uint(&syscalls);

 out:
		rump_sys_close(fd);
		atomic_inc_uint(&syscalls);
		rump_sys_close(s);
		atomic_inc_uint(&syscalls);
	}

	return NULL;
}

/* Stress with max 32 clients, 8 threads each (256 concurrent threads) */
#define NCLI 32
#define NTHR 8

int
main(int argc, char *argv[])
{
	pthread_t pt[NTHR-1];
	pid_t clis[NCLI];
	pid_t apid;
	int ncli = 0;
	int i = 0, j;
	int status, thesig;
	int rounds, myport;

	if (argc != 2 && argc != 3)
		errx(1, "need roundcount");

	if (argc == 3) {
		if (strcmp(argv[2], "kill") != 0)
			errx(1, "optional 3rd param must be kill");
		thesig = SIGKILL;
		iskiller = 1;
	} else {
		thesig = SIGUSR1;
	}

	signal(SIGUSR1, signaali);

	memset(clis, 0, sizeof(clis));
	for (rounds = 1; rounds < atoi(argv[1])*10; rounds++) {
		while (ncli < NCLI) {
			switch ((apid = fork())) {
			case -1:
				err(1, "fork failed");
			case 0:
				if (rumpclient_init() == -1)
					err(1, "rumpclient init");

				mypid = rump_sys_getpid();
				sprintf(hostnamebuf, HOSTNAMEBASE "%d", mypid);
				if (rump_sys___sysctl(hostnamemib,
				    __arraycount(hostnamemib), NULL, NULL,
				    hostnamebuf, strlen(hostnamebuf)+1) == -1)
					err(1, "sethostname");

				for (j = 0; j < NTHR-1; j++) {
					myport = i*NCLI + j+2;
					if (pthread_create(&pt[j], NULL,
					    client,
					    (void*)(uintptr_t)myport) !=0 )
						err(1, "pthread create");
				}
				myport = i*NCLI+1;
				client((void *)(uintptr_t)myport);
				for (j = 0; j < NTHR-1; j++)
					pthread_join(pt[j], NULL);
				membar_consumer();
				fprintf(stderr, "done %d\n", syscalls);
				exit(0);
				/* NOTREACHED */
			default:
				ncli++;
				clis[i] = apid;
				break;
			}
			
			i = (i + 1) % NCLI;
		}

		usleep(100000);
		kill(clis[i], thesig);

		apid = wait(&status);
		if (apid != clis[i])
			errx(1, "wanted pid %d, got %d\n", clis[i], apid);
		clis[i] = 0;
		ncli--;
		if (thesig == SIGUSR1) {
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
				fprintf(stderr, "child died with 0x%x\n",
				    status);
				exit(1);
			}
		} else {
			if (!WIFSIGNALED(status) || WTERMSIG(status) != thesig){
				fprintf(stderr, "child died with 0x%x\n",
				    status);
				exit(1);
			}
		}
	}

	for (i = 0; i < NCLI; i++)
		if (clis[i])
			kill(clis[i], SIGKILL);
}

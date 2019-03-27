/*	$NetBSD: h_reconcli.c,v 1.2 2011/02/19 09:56:45 pooka Exp $	*/

#include <sys/types.h>
#include <sys/sysctl.h>

#include <rump/rumpclient.h>
#include <rump/rump_syscalls.h>

#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int quit, riseandwhine;

static pthread_mutex_t closermtx;
static pthread_cond_t closercv;

static void *
closer(void *arg)
{

	pthread_mutex_lock(&closermtx);
	while (!quit) {
		while (!riseandwhine)
			pthread_cond_wait(&closercv, &closermtx);
		riseandwhine = 0;
		pthread_mutex_unlock(&closermtx);

		/* try to catch a random slot */
		usleep(random() % 100000);

		/*
		 * wide-angle disintegration beam, but takes care
		 * of the client rumpkernel communication socket.
		 */
		closefrom(3);

		pthread_mutex_lock(&closermtx);
	}
	pthread_mutex_unlock(&closermtx);

	return NULL;
}

static const int hostnamemib[] = { CTL_KERN, KERN_HOSTNAME };
static char goodhostname[128];

static void *
worker(void *arg)
{
	char hostnamebuf[128];
	size_t blen;

	pthread_mutex_lock(&closermtx);
	while (!quit) {
		pthread_mutex_unlock(&closermtx);
		if (rump_sys_getpid() == -1)
			err(1, "getpid");

		blen = sizeof(hostnamebuf);
		memset(hostnamebuf, 0, sizeof(hostnamebuf));
		if (rump_sys___sysctl(hostnamemib, __arraycount(hostnamemib),
		    hostnamebuf, &blen, NULL, 0) == -1)
			err(1, "sysctl");
		if (strcmp(hostnamebuf, goodhostname) != 0)
			exit(1);
		pthread_mutex_lock(&closermtx);
		riseandwhine = 1;
		pthread_cond_signal(&closercv);
	}
	riseandwhine = 1;
	pthread_cond_signal(&closercv);
	pthread_mutex_unlock(&closermtx);

	return NULL;
}

int
main(int argc, char *argv[])
{
	pthread_t pt, w1, w2, w3, w4;
	size_t blen;
	int timecount;

	if (argc != 2)
		errx(1, "need timecount");
	timecount = atoi(argv[1]);
	if (timecount <= 0)
		errx(1, "invalid timecount %d\n", timecount);

	srandom(time(NULL));

	rumpclient_setconnretry(RUMPCLIENT_RETRYCONN_INFTIME);
	if (rumpclient_init() == -1)
		err(1, "init");

	blen = sizeof(goodhostname);
	if (rump_sys___sysctl(hostnamemib, __arraycount(hostnamemib),
	    goodhostname, &blen, NULL, 0) == -1)
		err(1, "sysctl");

	pthread_create(&pt, NULL, closer, NULL);
	pthread_create(&w1, NULL, worker, NULL);
	pthread_create(&w2, NULL, worker, NULL);
	pthread_create(&w3, NULL, worker, NULL);
	pthread_create(&w4, NULL, worker, NULL);

	sleep(timecount);
	quit = 1;

	pthread_join(pt, NULL);
	pthread_join(w1, NULL);
	pthread_join(w2, NULL);
	pthread_join(w3, NULL);
	pthread_join(w4, NULL);

	exit(0);
}

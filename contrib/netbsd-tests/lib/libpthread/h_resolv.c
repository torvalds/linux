/* $NetBSD: h_resolv.c,v 1.2 2010/11/03 16:10:22 christos Exp $ */

/*-
 * Copyright (c) 2004, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: h_resolv.c,v 1.2 2010/11/03 16:10:22 christos Exp $");

#include <pthread.h>
#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stringlist.h>

#define NTHREADS    10
#define NHOSTS        100
#define WS        " \t\n\r"

static StringList *hosts = NULL;
static int debug = 0;
static int *ask = NULL;
static int *got = NULL;

static void usage(void)  __attribute__((__noreturn__));
static void load(const char *);
static void resolvone(int);
static void *resolvloop(void *);
static void run(int *);

static pthread_mutex_t stats = PTHREAD_MUTEX_INITIALIZER;

static void
usage(void)
{
	(void)fprintf(stderr,
		"Usage: %s [-d] [-h <nhosts>] [-n <nthreads>] <file> ...\n",
		getprogname());
	exit(1);
}

static void
load(const char *fname)
{
	FILE *fp;
	size_t len;
	char *line;

	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "Cannot open `%s'", fname);
	while ((line = fgetln(fp, &len)) != NULL) {
		char c = line[len];
		char *ptr;
		line[len] = '\0';
		for (ptr = strtok(line, WS); ptr; ptr = strtok(NULL, WS))
			sl_add(hosts, strdup(ptr));
		line[len] = c;
	}

	(void)fclose(fp);
}

static void
resolvone(int n)
{
	char buf[1024];
	pthread_t self = pthread_self();
	size_t i = (random() & 0x0fffffff) % hosts->sl_cur;
	char *host = hosts->sl_str[i];
	struct addrinfo *res;
	int error, len;
	if (debug) {
		len = snprintf(buf, sizeof(buf), "%p: %d resolving %s %d\n",
			self, n, host, (int)i);
		(void)write(STDOUT_FILENO, buf, len);
	}
	error = getaddrinfo(host, NULL, NULL, &res);
	if (debug) {
		len = snprintf(buf, sizeof(buf), "%p: host %s %s\n",
			self, host, error ? "not found" : "ok");
		(void)write(STDOUT_FILENO, buf, len);
	}
	pthread_mutex_lock(&stats);
	ask[i]++;
	got[i] += error == 0;
	pthread_mutex_unlock(&stats);
	if (error == 0)
		freeaddrinfo(res);
}

static void *
resolvloop(void *p)
{
	int *nhosts = (int *)p;
	if (*nhosts == 0)
		return NULL;
	do
		resolvone(*nhosts);
	while (--(*nhosts));
	return NULL;
}

static void
run(int *nhosts)
{
	pthread_t self = pthread_self();
	if (pthread_create(&self, NULL, resolvloop, nhosts) != 0)
		err(1, "pthread_create");
}

int
main(int argc, char *argv[])
{
	int nthreads = NTHREADS;
	int nhosts = NHOSTS;
	int i, c, done, *nleft;
	hosts = sl_init();

	srandom(1234);

	while ((c = getopt(argc, argv, "dh:n:")) != -1)
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'h':
			nhosts = atoi(optarg);
			break;
		case 'n':
			nthreads = atoi(optarg);
			break;
		default:
			usage();
		}

	for (i = optind; i < argc; i++)
		load(argv[i]);

	if (hosts->sl_cur == 0)
		usage();

	if ((nleft = malloc(nthreads * sizeof(int))) == NULL)
		err(1, "malloc");
	if ((ask = calloc(hosts->sl_cur, sizeof(int))) == NULL)
		err(1, "calloc");
	if ((got = calloc(hosts->sl_cur, sizeof(int))) == NULL)
		err(1, "calloc");


	for (i = 0; i < nthreads; i++) {
		nleft[i] = nhosts;
		run(&nleft[i]);
	}

	for (done = 0; !done;) {
		done = 1;
		for (i = 0; i < nthreads; i++) {
			if (nleft[i] != 0) {
				done = 0;
				break;
			}
		}
		sleep(1);
	}
	c = 0;
	for (i = 0; i < (int)hosts->sl_cur; i++) {
		if (ask[i] != got[i] && got[i] != 0) {
			warnx("Error: host %s ask %d got %d\n",
				hosts->sl_str[i], ask[i], got[i]);
			c++;
		}
	}
	free(nleft);
	free(ask);
	free(got);
	sl_free(hosts, 1);
	return c;
}

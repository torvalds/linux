/*	$NetBSD: blacklistd.c,v 1.37 2017/02/18 00:26:16 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/cdefs.h>
__RCSID("$NetBSD: blacklistd.c,v 1.37 2017/02/18 00:26:16 christos Exp $");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <syslog.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#include "bl.h"
#include "internal.h"
#include "conf.h"
#include "run.h"
#include "state.h"
#include "support.h"

static const char *configfile = _PATH_BLCONF;
static DB *state;
static const char *dbfile = _PATH_BLSTATE;
static sig_atomic_t readconf;
static sig_atomic_t done;
static int vflag;

static void
sigusr1(int n __unused)
{
	debug++;
}

static void
sigusr2(int n __unused)
{
	debug--;
}

static void
sighup(int n __unused)
{
	readconf++;
}

static void
sigdone(int n __unused)
{
	done++;
}

static __dead void
usage(int c)
{
	if (c)
		warnx("Unknown option `%c'", (char)c);
	fprintf(stderr, "Usage: %s [-vdfr] [-c <config>] [-R <rulename>] "
	    "[-P <sockpathsfile>] [-C <controlprog>] [-D <dbfile>] "
	    "[-s <sockpath>] [-t <timeout>]\n", getprogname());
	exit(EXIT_FAILURE);
}

static int
getremoteaddress(bl_info_t *bi, struct sockaddr_storage *rss, socklen_t *rsl)
{
	*rsl = sizeof(*rss);
	memset(rss, 0, *rsl);

	if (getpeername(bi->bi_fd, (void *)rss, rsl) != -1)
		return 0;

	if (errno != ENOTCONN) {
		(*lfun)(LOG_ERR, "getpeername failed (%m)"); 
		return -1;
	}

	if (bi->bi_slen == 0) {
		(*lfun)(LOG_ERR, "unconnected socket with no peer in message");
		return -1;
	}

	switch (bi->bi_ss.ss_family) {
	case AF_INET:
		*rsl = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		*rsl = sizeof(struct sockaddr_in6);
		break;
	default:
		(*lfun)(LOG_ERR, "bad client passed socket family %u",
		    (unsigned)bi->bi_ss.ss_family); 
		return -1;
	}

	if (*rsl != bi->bi_slen) {
		(*lfun)(LOG_ERR, "bad client passed socket length %u != %u",
		    (unsigned)*rsl, (unsigned)bi->bi_slen); 
		return -1;
	}

	memcpy(rss, &bi->bi_ss, *rsl);

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	if (*rsl != rss->ss_len) {
		(*lfun)(LOG_ERR,
		    "bad client passed socket internal length %u != %u",
		    (unsigned)*rsl, (unsigned)rss->ss_len); 
		return -1;
	}
#endif
	return 0;
}

static void
process(bl_t bl)
{
	struct sockaddr_storage rss;
	socklen_t rsl;
	char rbuf[BUFSIZ];
	bl_info_t *bi;
	struct conf c;
	struct dbinfo dbi;
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		(*lfun)(LOG_ERR, "clock_gettime failed (%m)"); 
		return;
	}

	if ((bi = bl_recv(bl)) == NULL) {
		(*lfun)(LOG_ERR, "no message (%m)"); 
		return;
	}

	if (getremoteaddress(bi, &rss, &rsl) == -1)
		goto out;

	if (debug) {
		sockaddr_snprintf(rbuf, sizeof(rbuf), "%a:%p", (void *)&rss);
		(*lfun)(LOG_DEBUG, "processing type=%d fd=%d remote=%s msg=%s"
		    " uid=%lu gid=%lu", bi->bi_type, bi->bi_fd, rbuf,
		    bi->bi_msg, (unsigned long)bi->bi_uid,
		    (unsigned long)bi->bi_gid);
	}

	if (conf_find(bi->bi_fd, bi->bi_uid, &rss, &c) == NULL) {
		(*lfun)(LOG_DEBUG, "no rule matched");
		goto out;
	}


	if (state_get(state, &c, &dbi) == -1)
		goto out;

	if (debug) {
		char b1[128], b2[128];
		(*lfun)(LOG_DEBUG, "%s: initial db state for %s: count=%d/%d "
		    "last=%s now=%s", __func__, rbuf, dbi.count, c.c_nfail,
		    fmttime(b1, sizeof(b1), dbi.last),
		    fmttime(b2, sizeof(b2), ts.tv_sec));
	}

	switch (bi->bi_type) {
	case BL_ABUSE:
		/*
		 * If the application has signaled abusive behavior,
		 * set the number of fails to be one less than the
		 * configured limit.  Fallthrough to the normal BL_ADD
		 * processing, which will increment the failure count
		 * to the threshhold, and block the abusive address.
		 */
		if (c.c_nfail != -1)
			dbi.count = c.c_nfail - 1;
		/*FALLTHROUGH*/
	case BL_ADD:
		dbi.count++;
		dbi.last = ts.tv_sec;
		if (dbi.id[0]) {
			/*
			 * We should not be getting this since the rule
			 * should have blocked the address. A possible
			 * explanation is that someone removed that rule,
			 * and another would be that we got another attempt
			 * before we added the rule. In anycase, we remove
			 * and re-add the rule because we don't want to add
			 * it twice, because then we'd lose track of it.
			 */
			(*lfun)(LOG_DEBUG, "rule exists %s", dbi.id);
			(void)run_change("rem", &c, dbi.id, 0);
			dbi.id[0] = '\0';
		}
		if (c.c_nfail != -1 && dbi.count >= c.c_nfail) {
			int res = run_change("add", &c, dbi.id, sizeof(dbi.id));
			if (res == -1)
				goto out;
			sockaddr_snprintf(rbuf, sizeof(rbuf), "%a",
			    (void *)&rss);
			(*lfun)(LOG_INFO,
			    "blocked %s/%d:%d for %d seconds",
			    rbuf, c.c_lmask, c.c_port, c.c_duration);
				
		}
		break;
	case BL_DELETE:
		if (dbi.last == 0)
			goto out;
		dbi.count = 0;
		dbi.last = 0;
		break;
	case BL_BADUSER:
		/* ignore for now */
		break;
	default:
		(*lfun)(LOG_ERR, "unknown message %d", bi->bi_type); 
	}
	state_put(state, &c, &dbi);

out:
	close(bi->bi_fd);

	if (debug) {
		char b1[128], b2[128];
		(*lfun)(LOG_DEBUG, "%s: final db state for %s: count=%d/%d "
		    "last=%s now=%s", __func__, rbuf, dbi.count, c.c_nfail,
		    fmttime(b1, sizeof(b1), dbi.last),
		    fmttime(b2, sizeof(b2), ts.tv_sec));
	}
}

static void
update_interfaces(void)
{
	struct ifaddrs *oifas, *nifas;

	if (getifaddrs(&nifas) == -1)
		return;

	oifas = ifas;
	ifas = nifas;

	if (oifas)
		freeifaddrs(oifas);
}

static void
update(void)
{
	struct timespec ts;
	struct conf c;
	struct dbinfo dbi;
	unsigned int f, n;
	char buf[128];
	void *ss = &c.c_ss;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		(*lfun)(LOG_ERR, "clock_gettime failed (%m)"); 
		return;
	}

again:
	for (n = 0, f = 1; state_iterate(state, &c, &dbi, f) == 1;
	    f = 0, n++)
	{
		time_t when = c.c_duration + dbi.last;
		if (debug > 1) {
			char b1[64], b2[64];
			sockaddr_snprintf(buf, sizeof(buf), "%a:%p", ss);
			(*lfun)(LOG_DEBUG, "%s:[%u] %s count=%d duration=%d "
			    "last=%s " "now=%s", __func__, n, buf, dbi.count,
			    c.c_duration, fmttime(b1, sizeof(b1), dbi.last),
			    fmttime(b2, sizeof(b2), ts.tv_sec));
		}
		if (c.c_duration == -1 || when >= ts.tv_sec)
			continue;
		if (dbi.id[0]) {
			run_change("rem", &c, dbi.id, 0);
			sockaddr_snprintf(buf, sizeof(buf), "%a", ss);
			(*lfun)(LOG_INFO, "released %s/%d:%d after %d seconds",
			    buf, c.c_lmask, c.c_port, c.c_duration);
		}
		state_del(state, &c);
		goto again;
	}
}

static void
addfd(struct pollfd **pfdp, bl_t **blp, size_t *nfd, size_t *maxfd,
    const char *path)
{
	bl_t bl = bl_create(true, path, vflag ? vdlog : vsyslog);
	if (bl == NULL || !bl_isconnected(bl))
		exit(EXIT_FAILURE);
	if (*nfd >= *maxfd) {
		*maxfd += 10;
		*blp = realloc(*blp, sizeof(**blp) * *maxfd);
		if (*blp == NULL)
			err(EXIT_FAILURE, "malloc");
		*pfdp = realloc(*pfdp, sizeof(**pfdp) * *maxfd);
		if (*pfdp == NULL)
			err(EXIT_FAILURE, "malloc");
	}

	(*pfdp)[*nfd].fd = bl_getfd(bl);
	(*pfdp)[*nfd].events = POLLIN;
	(*blp)[*nfd] = bl;
	*nfd += 1;
}

static void
uniqueadd(struct conf ***listp, size_t *nlist, size_t *mlist, struct conf *c)
{
	struct conf **list = *listp;

	if (c->c_name[0] == '\0')
		return;
	for (size_t i = 0; i < *nlist; i++) {
		if (strcmp(list[i]->c_name, c->c_name) == 0)
			return;
	}
	if (*nlist == *mlist) {
		*mlist += 10;
		void *p = realloc(*listp, *mlist * sizeof(*list));
		if (p == NULL)
			err(EXIT_FAILURE, "Can't allocate for rule list");
		list = *listp = p;
	}
	list[(*nlist)++] = c;
}

static void
rules_flush(void)
{
	struct conf **list;
	size_t nlist, mlist;

	list = NULL;
	mlist = nlist = 0;
	for (size_t i = 0; i < rconf.cs_n; i++)
		uniqueadd(&list, &nlist, &mlist, &rconf.cs_c[i]);
	for (size_t i = 0; i < lconf.cs_n; i++)
		uniqueadd(&list, &nlist, &mlist, &lconf.cs_c[i]);

	for (size_t i = 0; i < nlist; i++)
		run_flush(list[i]);
	free(list);
}

static void
rules_restore(void)
{
	struct conf c;
	struct dbinfo dbi;
	unsigned int f;

	for (f = 1; state_iterate(state, &c, &dbi, f) == 1; f = 0) {
		if (dbi.id[0] == '\0')
			continue;
		(void)run_change("rem", &c, dbi.id, 0);
		(void)run_change("add", &c, dbi.id, sizeof(dbi.id));
	}
}

int
main(int argc, char *argv[])
{
	int c, tout, flags, flush, restore, ret;
	const char *spath, **blsock;
	size_t nblsock, maxblsock;

	setprogname(argv[0]);

	spath = NULL;
	blsock = NULL;
	maxblsock = nblsock = 0;
	flush = 0;
	restore = 0;
	tout = 0;
	flags = O_RDWR|O_EXCL|O_CLOEXEC;
	while ((c = getopt(argc, argv, "C:c:D:dfP:rR:s:t:v")) != -1) {
		switch (c) {
		case 'C':
			controlprog = optarg;
			break;
		case 'c':
			configfile = optarg;
			break;
		case 'D':
			dbfile = optarg;
			break;
		case 'd':
			debug++;
			break;
		case 'f':
			flush++;
			break;
		case 'P':
			spath = optarg;
			break;
		case 'R':
			rulename = optarg;
			break;
		case 'r':
			restore++;
			break;
		case 's':
			if (nblsock >= maxblsock) {
				maxblsock += 10;
				void *p = realloc(blsock,
				    sizeof(*blsock) * maxblsock);
				if (p == NULL)
				    err(EXIT_FAILURE,
					"Can't allocate memory for %zu sockets",
					maxblsock);
				blsock = p;
			}
			blsock[nblsock++] = optarg;
			break;
		case 't':
			tout = atoi(optarg) * 1000;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage(c);
		}
	}

	argc -= optind;
	if (argc)
		usage(0);

	signal(SIGHUP, sighup);
	signal(SIGINT, sigdone);
	signal(SIGQUIT, sigdone);
	signal(SIGTERM, sigdone);
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr2);

	openlog(getprogname(), LOG_PID, LOG_DAEMON);

	if (debug) {
		lfun = dlog;
		if (tout == 0)
			tout = 5000;
	} else {
		if (tout == 0)
			tout = 15000;
	}

	update_interfaces();
	conf_parse(configfile);
	if (flush) {
		rules_flush();
		flags |= O_TRUNC;
	}

	struct pollfd *pfd = NULL;
	bl_t *bl = NULL;
	size_t nfd = 0;
	size_t maxfd = 0;

	for (size_t i = 0; i < nblsock; i++)
		addfd(&pfd, &bl, &nfd, &maxfd, blsock[i]);
	free(blsock);

	if (spath) {
		FILE *fp = fopen(spath, "r");
		char *line;
		if (fp == NULL)
			err(EXIT_FAILURE, "Can't open `%s'", spath);
		for (; (line = fparseln(fp, NULL, NULL, NULL, 0)) != NULL;
		    free(line))
			addfd(&pfd, &bl, &nfd, &maxfd, line);
		fclose(fp);
	}
	if (nfd == 0)
		addfd(&pfd, &bl, &nfd, &maxfd, _PATH_BLSOCK);

	state = state_open(dbfile, flags, 0600);
	if (state == NULL)
		state = state_open(dbfile,  flags | O_CREAT, 0600);
	if (state == NULL)
		return EXIT_FAILURE;

	if (restore)
		rules_restore();

	if (!debug) {
		if (daemon(0, 0) == -1)
			err(EXIT_FAILURE, "daemon failed");
		if (pidfile(NULL) == -1)
			err(EXIT_FAILURE, "Can't create pidfile");
	}

	for (size_t t = 0; !done; t++) {
		if (readconf) {
			readconf = 0;
			conf_parse(configfile);
		}
		ret = poll(pfd, (nfds_t)nfd, tout);
		if (debug)
			(*lfun)(LOG_DEBUG, "received %d from poll()", ret);
		switch (ret) {
		case -1:
			if (errno == EINTR)
				continue;
			(*lfun)(LOG_ERR, "poll (%m)");
			return EXIT_FAILURE;
		case 0:
			state_sync(state);
			break;
		default:
			for (size_t i = 0; i < nfd; i++)
				if (pfd[i].revents & POLLIN)
					process(bl[i]);
		}
		if (t % 100 == 0)
			state_sync(state);
		if (t % 10000 == 0)
			update_interfaces();
		update();
	}
	state_close(state);
	return 0;
}

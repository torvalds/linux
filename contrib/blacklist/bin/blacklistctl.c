/*	$NetBSD: blacklistctl.c,v 1.21 2016/11/02 03:15:07 jnemeth Exp $	*/

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
__RCSID("$NetBSD: blacklistctl.c,v 1.21 2016/11/02 03:15:07 jnemeth Exp $");

#include <stdio.h>
#include <time.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "conf.h"
#include "state.h"
#include "internal.h"
#include "support.h"

static __dead void
usage(int c)
{
	if (c == 0)
		warnx("Missing/unknown command");
	else
		warnx("Unknown option `%c'", (char)c);
	fprintf(stderr, "Usage: %s dump [-abdnrw]\n", getprogname());
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	const char *dbname = _PATH_BLSTATE;
	DB *db;
	struct conf c;
	struct dbinfo dbi;
	unsigned int i;
	struct timespec ts;
	int all, blocked, remain, wide, noheader;
	int o;

	noheader = wide = blocked = all = remain = 0;
	lfun = dlog;

	if (argc == 1 || strcmp(argv[1], "dump") != 0)
		usage(0);

	argc--;
	argv++;

	while ((o = getopt(argc, argv, "abD:dnrw")) != -1)
		switch (o) {
		case 'a':
			all = 1;
			blocked = 0;
			break;
		case 'b':
			blocked = 1;
			break;
		case 'D':
			dbname = optarg;
			break;
		case 'd':
			debug++;
			break;
		case 'n':
			noheader = 1;
			break;
		case 'r':
			remain = 1;
			break;
		case 'w':
			wide = 1;
			break;
		default:
			usage(o);
			break;
		}

	db = state_open(dbname, O_RDONLY, 0);
	if (db == NULL)
		err(EXIT_FAILURE, "Can't open `%s'", dbname);

	clock_gettime(CLOCK_REALTIME, &ts);
	wide = wide ? 8 * 4 + 7 : 4 * 3 + 3;
	if (!noheader)
		printf("%*.*s/ma:port\tid\tnfail\t%s\n", wide, wide,
		    "address", remain ? "remaining time" : "last access");
	for (i = 1; state_iterate(db, &c, &dbi, i) != 0; i = 0) {
		char buf[BUFSIZ];
		if (!all) {
			if (blocked) {
				if (dbi.count < c.c_nfail)
					continue;
			} else {
				if (dbi.count >= c.c_nfail)
					continue;
			}
		}
		sockaddr_snprintf(buf, sizeof(buf), "%a", (void *)&c.c_ss);
		printf("%*.*s/%d:%d\t", wide, wide, buf, c.c_lmask, c.c_port);
		if (remain)
			fmtydhms(buf, sizeof(buf),
			    c.c_duration - (ts.tv_sec - dbi.last));
		else
			fmttime(buf, sizeof(buf), dbi.last);
		printf("%s\t%d/%d\t%-s\n", dbi.id, dbi.count, c.c_nfail, buf);
	}
	state_close(db);
	return EXIT_SUCCESS;
}

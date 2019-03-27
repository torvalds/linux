/*-
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Main file for the audit filter daemon, which presents audit records to a
 * set of run-time registered loadable modules.  This is the main event loop
 * of the daemon, which handles starting up, waiting for records, and
 * presenting records to configured modules.  auditfilterd_conf.c handles the
 * reading and management of the configuration, module list and module state,
 * etc.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else
#include <compat/queue.h>
#endif

#ifndef HAVE_CLOCK_GETTIME
#include <compat/clock_gettime.h>
#endif

#include <bsm/libbsm.h>
#include <bsm/audit_filter.h>
#include <bsm/audit_internal.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "auditfilterd.h"

/*
 * Global list of registered filters.
 */
struct auditfilter_module_list	filter_list;

/*
 * Configuration and signal->main flags.
 */
int	debug;		/* Debugging mode requested, don't detach. */
int	reread_config;	/* SIGHUP has been received. */
int	quit;		/* SIGQUIT/TERM/INT has been received. */

static void
usage(void)
{

	fprintf(stderr, "auditfilterd [-d] [-c conffile] [-p pipefile]"
	    " [-t trailfile]\n");
	fprintf(stderr, "  -c    Specify configuration file (default: %s)\n",
	    AUDITFILTERD_CONFFILE);
	fprintf(stderr, "  -d    Debugging mode, don't daemonize\n");
	fprintf(stderr, "  -p    Specify pipe file (default: %s)\n",
	    AUDITFILTERD_PIPEFILE);
	fprintf(stderr, "  -t    Specify audit trail file (default: none)\n");
	exit(-1);
}

static void
auditfilterd_init(void)
{

	TAILQ_INIT(&filter_list);
}

static void
signal_handler(int signum)
{

	switch (signum) {
	case SIGHUP:
		reread_config++;
		break;

	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		quit++;
		break;
	}
}

/*
 * Present raw BSM to a set of registered and interested filters.
 */
static void
present_rawrecord(struct timespec *ts, u_char *data, u_int len)
{
	struct auditfilter_module *am;

	TAILQ_FOREACH(am, &filter_list, am_list) {
		if (am->am_rawrecord != NULL)
			(am->am_rawrecord)(am, ts, data, len);
	}
}

/*
 * Parse the BSM into a set of tokens, which will be passed to registered
 * and interested filters.
 */
#define	MAX_TOKENS	128	/* Maximum tokens we handle per record. */
static void
present_tokens(struct timespec *ts, u_char *data, u_int len)
{
	struct auditfilter_module *am;
	tokenstr_t tokens[MAX_TOKENS];
	u_int bytesread;
	int tokencount;

	tokencount = 0;
	while (bytesread < len) {
		if (au_fetch_tok(&tokens[tokencount], data + bytesread,
		    len - bytesread) == -1)
			break;
		bytesread += tokens[tokencount].len;
		tokencount++;
	}

	TAILQ_FOREACH(am, &filter_list, am_list) {
		if (am->am_record != NULL)
			(am->am_record)(am, ts, tokencount, tokens);
	}
}

/*
 * The main loop spins pulling records out of the record source and passing
 * them to modules for processing.
 */
static void
mainloop_file(const char *conffile, const char *trailfile, FILE *trail_fp)
{
	struct timespec ts;
	FILE *conf_fp;
	u_char *buf;
	int reclen;

	while (1) {
		/*
		 * On SIGHUP, we reread the configuration file and reopen
		 * the trail file.
		 */
		if (reread_config) {
			reread_config = 0;
			warnx("rereading configuration");
			conf_fp = fopen(conffile, "r");
			if (conf_fp == NULL)
				err(-1, "%s", conffile);
			auditfilterd_conf(conffile, conf_fp);
			fclose(conf_fp);

			fclose(trail_fp);
			trail_fp = fopen(trailfile, "r");
			if (trail_fp == NULL)
				err(-1, "%s", trailfile);
		}
		if (quit) {
			warnx("quitting");
			break;
		}

		/*
		 * For now, be relatively unrobust about incomplete records,
		 * but in the future will want to do better.  Need to look
		 * more at the right blocking and signal behavior here.
		 */
		reclen = au_read_rec(trail_fp, &buf);
		if (reclen == -1)
			continue;
		if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
			err(-1, "clock_gettime");
		present_rawrecord(&ts, buf, reclen);
		present_tokens(&ts, buf, reclen);
		free(buf);
	}
}

/*
 * The main loop spins pulling records out of the record source and passing
 * them to modules for processing.  This version of the function accepts
 * discrete record input from a file descriptor, as opposed to buffered input
 * from a file stream.
 */
static void
mainloop_pipe(const char *conffile, const char *pipefile __unused, int pipe_fd)
{
	u_char record[MAX_AUDIT_RECORD_SIZE];
	struct timespec ts;
	FILE *conf_fp;
	int reclen;

	while (1) {
		/*
		 * On SIGHUP, we reread the configuration file.  Unlike with
		 * a trail file, we don't reopen the pipe, as we don't want
		 * to miss records which will be flushed if we do.
		 */
		if (reread_config) {
			reread_config = 0;
			warnx("rereading configuration");
			conf_fp = fopen(conffile, "r");
			if (conf_fp == NULL)
				err(-1, "%s", conffile);
			auditfilterd_conf(conffile, conf_fp);
			fclose(conf_fp);
		}
		if (quit) {
			warnx("quitting");
			break;
		}

		/*
		 * For now, be relatively unrobust about incomplete records,
		 * but in the future will want to do better.  Need to look
		 * more at the right blocking and signal behavior here.
		 */
		reclen = read(pipe_fd, record, MAX_AUDIT_RECORD_SIZE);
		if (reclen < 0)
			continue;
		if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
			err(-1, "clock_gettime");
		present_rawrecord(&ts, record, reclen);
		present_tokens(&ts, record, reclen);
	}
}

int
main(int argc, char *argv[])
{
	const char *pipefile, *trailfile, *conffile;
	FILE *trail_fp, *conf_fp;
	struct stat sb;
	int pipe_fd;
	int ch;

	conffile = AUDITFILTERD_CONFFILE;
	trailfile = NULL;
	pipefile = NULL;
	while ((ch = getopt(argc, argv, "c:dp:t:")) != -1) {
		switch (ch) {
		case 'c':
			conffile = optarg;
			break;

		case 'd':
			debug++;
			break;

		case 't':
			if (trailfile != NULL || pipefile != NULL)
				usage();
			trailfile = optarg;
			break;

		case 'p':
			if (pipefile != NULL || trailfile != NULL)
				usage();
			pipefile = optarg;
			break;

		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	/*
	 * We allow only one of a pipe or a trail to be used.  If none is
	 * specified, we provide a default pipe path.
	 */
	if (pipefile == NULL && trailfile == NULL)
		pipefile = AUDITFILTERD_PIPEFILE;

	if (pipefile != NULL) {
		pipe_fd = open(pipefile, O_RDONLY);
		if (pipe_fd < 0)
			err(-1, "open:%s", pipefile);
		if (fstat(pipe_fd, &sb) < 0)
			err(-1, "stat: %s", pipefile);
		if (!S_ISCHR(sb.st_mode))
			errx(-1, "fstat: %s not device", pipefile);
	} else {
		trail_fp = fopen(trailfile, "r");
		if (trail_fp == NULL)
			err(-1, "%s", trailfile);
	}

	conf_fp = fopen(conffile, "r");
	if (conf_fp == NULL)
		err(-1, "%s", conffile);

	auditfilterd_init();
	if (auditfilterd_conf(conffile, conf_fp) < 0)
		exit(-1);
	fclose(conf_fp);

	if (!debug) {
		if (daemon(0, 0) < 0)
			err(-1, "daemon");
	}

	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);

	if (pipefile != NULL)
		mainloop_pipe(conffile, pipefile, pipe_fd);
	else
		mainloop_file(conffile, trailfile, trail_fp);

	auditfilterd_conf_shutdown();
	return (0);
}

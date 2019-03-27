/*
 * Copyright (c) 2008-2014, Simon Schubert <2@0x2c.org>.
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon Schubert <2@0x2c.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "dfcompat.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"


static void deliver(struct qitem *);

struct aliases aliases = LIST_HEAD_INITIALIZER(aliases);
struct strlist tmpfs = SLIST_HEAD_INITIALIZER(tmpfs);
struct authusers authusers = LIST_HEAD_INITIALIZER(authusers);
char username[USERNAME_SIZE];
uid_t useruid;
const char *logident_base;
char errmsg[ERRMSG_SIZE];

static int daemonize = 1;
static int doqueue = 0;

struct config config = {
	.smarthost	= NULL,
	.port		= 25,
	.aliases	= "/etc/aliases",
	.spooldir	= "/var/spool/dma",
	.authpath	= NULL,
	.certfile	= NULL,
	.features	= 0,
	.mailname	= NULL,
	.masquerade_host = NULL,
	.masquerade_user = NULL,
};


static void
sighup_handler(int signo)
{
	(void)signo;	/* so that gcc doesn't complain */
}

static char *
set_from(struct queue *queue, const char *osender)
{
	const char *addr;
	char *sender;

	if (osender) {
		addr = osender;
	} else if (getenv("EMAIL") != NULL) {
		addr = getenv("EMAIL");
	} else {
		if (config.masquerade_user)
			addr = config.masquerade_user;
		else
			addr = username;
	}

	if (!strchr(addr, '@')) {
		const char *from_host = hostname();

		if (config.masquerade_host)
			from_host = config.masquerade_host;

		if (asprintf(&sender, "%s@%s", addr, from_host) <= 0)
			return (NULL);
	} else {
		sender = strdup(addr);
		if (sender == NULL)
			return (NULL);
	}

	if (strchr(sender, '\n') != NULL) {
		errno = EINVAL;
		return (NULL);
	}

	queue->sender = sender;
	return (sender);
}

static int
read_aliases(void)
{
	yyin = fopen(config.aliases, "r");
	if (yyin == NULL) {
		/*
		 * Non-existing aliases file is not a fatal error
		 */
		if (errno == ENOENT)
			return (0);
		/* Other problems are. */
		return (-1);
	}
	if (yyparse())
		return (-1);	/* fatal error, probably malloc() */
	fclose(yyin);
	return (0);
}

static int
do_alias(struct queue *queue, const char *addr)
{
	struct alias *al;
        struct stritem *sit;
	int aliased = 0;

        LIST_FOREACH(al, &aliases, next) {
                if (strcmp(al->alias, addr) != 0)
                        continue;
		SLIST_FOREACH(sit, &al->dests, next) {
			if (add_recp(queue, sit->str, EXPAND_ADDR) != 0)
				return (-1);
		}
		aliased = 1;
        }

        return (aliased);
}

int
add_recp(struct queue *queue, const char *str, int expand)
{
	struct qitem *it, *tit;
	struct passwd *pw;
	char *host;
	int aliased = 0;

	it = calloc(1, sizeof(*it));
	if (it == NULL)
		return (-1);
	it->addr = strdup(str);
	if (it->addr == NULL)
		return (-1);

	it->sender = queue->sender;
	host = strrchr(it->addr, '@');
	if (host != NULL &&
	    (strcmp(host + 1, hostname()) == 0 ||
	     strcmp(host + 1, "localhost") == 0)) {
		*host = 0;
	}
	LIST_FOREACH(tit, &queue->queue, next) {
		/* weed out duplicate dests */
		if (strcmp(tit->addr, it->addr) == 0) {
			free(it->addr);
			free(it);
			return (0);
		}
	}
	LIST_INSERT_HEAD(&queue->queue, it, next);

	/**
	 * Do local delivery if there is no @.
	 * Do not do local delivery when NULLCLIENT is set.
	 */
	if (strrchr(it->addr, '@') == NULL && (config.features & NULLCLIENT) == 0) {
		it->remote = 0;
		if (expand) {
			aliased = do_alias(queue, it->addr);
			if (!aliased && expand == EXPAND_WILDCARD)
				aliased = do_alias(queue, "*");
			if (aliased < 0)
				return (-1);
			if (aliased) {
				LIST_REMOVE(it, next);
			} else {
				/* Local destination, check */
				pw = getpwnam(it->addr);
				if (pw == NULL)
					goto out;
				/* XXX read .forward */
				endpwent();
			}
		}
	} else {
		it->remote = 1;
	}

	return (0);

out:
	free(it->addr);
	free(it);
	return (-1);
}

static struct qitem *
go_background(struct queue *queue)
{
	struct sigaction sa;
	struct qitem *it;
	pid_t pid;

	if (daemonize && daemon(0, 0) != 0) {
		syslog(LOG_ERR, "can not daemonize: %m");
		exit(EX_OSERR);
	}
	daemonize = 0;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	LIST_FOREACH(it, &queue->queue, next) {
		/* No need to fork for the last dest */
		if (LIST_NEXT(it, next) == NULL)
			goto retit;

		pid = fork();
		switch (pid) {
		case -1:
			syslog(LOG_ERR, "can not fork: %m");
			exit(EX_OSERR);
			break;

		case 0:
			/*
			 * Child:
			 *
			 * return and deliver mail
			 */
retit:
			/*
			 * If necessary, acquire the queue and * mail files.
			 * If this fails, we probably were raced by another
			 * process.  It is okay to be raced if we're supposed
			 * to flush the queue.
			 */
			setlogident("%s", it->queueid);
			switch (acquirespool(it)) {
			case 0:
				break;
			case 1:
				if (doqueue)
					exit(EX_OK);
				syslog(LOG_WARNING, "could not lock queue file");
				exit(EX_SOFTWARE);
			default:
				exit(EX_SOFTWARE);
			}
			dropspool(queue, it);
			return (it);

		default:
			/*
			 * Parent:
			 *
			 * fork next child
			 */
			break;
		}
	}

	syslog(LOG_CRIT, "reached dead code");
	exit(EX_SOFTWARE);
}

static void
deliver(struct qitem *it)
{
	int error;
	unsigned int backoff = MIN_RETRY, slept;
	struct timeval now;
	struct stat st;

	snprintf(errmsg, sizeof(errmsg), "unknown bounce reason");

retry:
	syslog(LOG_INFO, "<%s> trying delivery", it->addr);

	if (it->remote)
		error = deliver_remote(it);
	else
		error = deliver_local(it);

	switch (error) {
	case 0:
		syslog(LOG_INFO, "<%s> delivery successful", it->addr);
		delqueue(it);
		exit(EX_OK);

	case 1:
		if (stat(it->queuefn, &st) != 0) {
			syslog(LOG_ERR, "lost queue file `%s'", it->queuefn);
			exit(EX_SOFTWARE);
		}
		if (gettimeofday(&now, NULL) == 0 &&
		    (now.tv_sec - st.st_mtim.tv_sec > MAX_TIMEOUT)) {
			snprintf(errmsg, sizeof(errmsg),
				 "Could not deliver for the last %d seconds. Giving up.",
				 MAX_TIMEOUT);
			goto bounce;
		}
		for (slept = 0; slept < backoff;) {
			slept += SLEEP_TIMEOUT - sleep(SLEEP_TIMEOUT);
			if (flushqueue_since(slept)) {
				backoff = MIN_RETRY;
				goto retry;
			}
		}
		if (slept >= backoff) {
			/* pick the next backoff between [1.5, 2.5) times backoff */
			backoff = backoff + backoff / 2 + random() % backoff;
			if (backoff > MAX_RETRY)
				backoff = MAX_RETRY;
		}
		goto retry;

	case -1:
	default:
		break;
	}

bounce:
	bounce(it, errmsg);
	/* NOTREACHED */
}

void
run_queue(struct queue *queue)
{
	struct qitem *it;

	if (LIST_EMPTY(&queue->queue))
		return;

	it = go_background(queue);
	deliver(it);
	/* NOTREACHED */
}

static void
show_queue(struct queue *queue)
{
	struct qitem *it;
	int locked = 0;	/* XXX */

	if (LIST_EMPTY(&queue->queue)) {
		printf("Mail queue is empty\n");
		return;
	}

	LIST_FOREACH(it, &queue->queue, next) {
		printf("ID\t: %s%s\n"
		       "From\t: %s\n"
		       "To\t: %s\n",
		       it->queueid,
		       locked ? "*" : "",
		       it->sender, it->addr);

		if (LIST_NEXT(it, next) != NULL)
			printf("--\n");
	}
}

/*
 * TODO:
 *
 * - alias processing
 * - use group permissions
 * - proper sysexit codes
 */

int
main(int argc, char **argv)
{
	struct sigaction act;
	char *sender = NULL;
	struct queue queue;
	int i, ch;
	int nodot = 0, showq = 0, queue_only = 0;
	int recp_from_header = 0;

	set_username();

	/*
	 * We never run as root.  If called by root, drop permissions
	 * to the mail user.
	 */
	if (geteuid() == 0 || getuid() == 0) {
		struct passwd *pw;

		errno = 0;
		pw = getpwnam(DMA_ROOT_USER);
		if (pw == NULL) {
			if (errno == 0)
				errx(EX_CONFIG, "user '%s' not found", DMA_ROOT_USER);
			else
				err(EX_OSERR, "cannot drop root privileges");
		}

		if (setuid(pw->pw_uid) != 0)
			err(EX_OSERR, "cannot drop root privileges");

		if (geteuid() == 0 || getuid() == 0)
			errx(EX_OSERR, "cannot drop root privileges");
	}

	atexit(deltmp);
	init_random();

	bzero(&queue, sizeof(queue));
	LIST_INIT(&queue.queue);

	if (strcmp(basename(argv[0]), "mailq") == 0) {
		argv++; argc--;
		showq = 1;
		if (argc != 0)
			errx(EX_USAGE, "invalid arguments");
		goto skipopts;
	} else if (strcmp(argv[0], "newaliases") == 0) {
		logident_base = "dma";
		setlogident("%s", logident_base);

		if (read_aliases() != 0)
			errx(EX_SOFTWARE, "could not parse aliases file `%s'", config.aliases);
		exit(EX_OK);
	}

	opterr = 0;
	while ((ch = getopt(argc, argv, ":A:b:B:C:d:Df:F:h:iL:N:no:O:q:r:R:tUV:vX:")) != -1) {
		switch (ch) {
		case 'A':
			/* -AX is being ignored, except for -A{c,m} */
			if (optarg[0] == 'c' || optarg[0] == 'm') {
				break;
			}
			/* else FALLTRHOUGH */
		case 'b':
			/* -bX is being ignored, except for -bp */
			if (optarg[0] == 'p') {
				showq = 1;
				break;
			} else if (optarg[0] == 'q') {
				queue_only = 1;
				break;
			}
			/* else FALLTRHOUGH */
		case 'D':
			daemonize = 0;
			break;
		case 'L':
			logident_base = optarg;
			break;
		case 'f':
		case 'r':
			sender = optarg;
			break;

		case 't':
			recp_from_header = 1;
			break;

		case 'o':
			/* -oX is being ignored, except for -oi */
			if (optarg[0] != 'i')
				break;
			/* else FALLTRHOUGH */
		case 'O':
			break;
		case 'i':
			nodot = 1;
			break;

		case 'q':
			/* Don't let getopt slup up other arguments */
			if (optarg && *optarg == '-')
				optind--;
			doqueue = 1;
			break;

		/* Ignored options */
		case 'B':
		case 'C':
		case 'd':
		case 'F':
		case 'h':
		case 'N':
		case 'n':
		case 'R':
		case 'U':
		case 'V':
		case 'v':
		case 'X':
			break;

		case ':':
			if (optopt == 'q') {
				doqueue = 1;
				break;
			}
			/* FALLTHROUGH */

		default:
			fprintf(stderr, "invalid argument: `-%c'\n", optopt);
			exit(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;
	opterr = 1;

	if (argc != 0 && (showq || doqueue))
		errx(EX_USAGE, "sending mail and queue operations are mutually exclusive");

	if (showq + doqueue > 1)
		errx(EX_USAGE, "conflicting queue operations");

skipopts:
	if (logident_base == NULL)
		logident_base = "dma";
	setlogident("%s", logident_base);

	act.sa_handler = sighup_handler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	if (sigaction(SIGHUP, &act, NULL) != 0)
		syslog(LOG_WARNING, "can not set signal handler: %m");

	parse_conf(CONF_PATH "/dma.conf");

	if (config.authpath != NULL)
		parse_authfile(config.authpath);

	if (showq) {
		if (load_queue(&queue) < 0)
			errlog(EX_NOINPUT, "can not load queue");
		show_queue(&queue);
		return (0);
	}

	if (doqueue) {
		flushqueue_signal();
		if (load_queue(&queue) < 0)
			errlog(EX_NOINPUT, "can not load queue");
		run_queue(&queue);
		return (0);
	}

	if (read_aliases() != 0)
		errlog(EX_SOFTWARE, "could not parse aliases file `%s'", config.aliases);

	if ((sender = set_from(&queue, sender)) == NULL)
		errlog(EX_SOFTWARE, "set_from()");

	if (newspoolf(&queue) != 0)
		errlog(EX_CANTCREAT, "can not create temp file in `%s'", config.spooldir);

	setlogident("%s", queue.id);

	for (i = 0; i < argc; i++) {
		if (add_recp(&queue, argv[i], EXPAND_WILDCARD) != 0)
			errlogx(EX_DATAERR, "invalid recipient `%s'", argv[i]);
	}

	if (LIST_EMPTY(&queue.queue) && !recp_from_header)
		errlogx(EX_NOINPUT, "no recipients");

	if (readmail(&queue, nodot, recp_from_header) != 0)
		errlog(EX_NOINPUT, "can not read mail");

	if (LIST_EMPTY(&queue.queue))
		errlogx(EX_NOINPUT, "no recipients");

	if (linkspool(&queue) != 0)
		errlog(EX_CANTCREAT, "can not create spools");

	/* From here on the mail is safe. */

	if (config.features & DEFER || queue_only)
		return (0);

	run_queue(&queue);

	/* NOTREACHED */
	return (0);
}

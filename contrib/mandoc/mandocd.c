/*	$Id: mandocd.c,v 1.6 2017/06/24 14:38:32 schwarze Exp $ */
/*
 * Copyright (c) 2017 Michael Stapelberg <stapelberg@debian.org>
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#if HAVE_CMSG_XPG42
#define _XPG4_2
#endif

#include <sys/types.h>
#include <sys/socket.h>

#if HAVE_ERR
#include <err.h>
#endif
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "man.h"
#include "main.h"
#include "manconf.h"

enum	outt {
	OUTT_ASCII = 0,
	OUTT_UTF8,
	OUTT_HTML
};

static	void	  process(struct mparse *, enum outt, void *);
static	int	  read_fds(int, int *);
static	void	  usage(void) __attribute__((__noreturn__));


#define NUM_FDS 3
static int
read_fds(int clientfd, int *fds)
{
	struct msghdr	 msg;
	struct iovec	 iov[1];
	unsigned char	 dummy[1];
	struct cmsghdr	*cmsg;
	int		*walk;
	int		 cnt;

	/* Union used for alignment. */
	union {
		uint8_t controlbuf[CMSG_SPACE(NUM_FDS * sizeof(int))];
		struct cmsghdr align;
	} u;

	memset(&msg, '\0', sizeof(msg));
	msg.msg_control = u.controlbuf;
	msg.msg_controllen = sizeof(u.controlbuf);

	/*
	 * Read a dummy byte - sendmsg cannot send an empty message,
	 * even if we are only interested in the OOB data.
	 */

	iov[0].iov_base = dummy;
	iov[0].iov_len = sizeof(dummy);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	switch (recvmsg(clientfd, &msg, 0)) {
	case -1:
		warn("recvmsg");
		return -1;
	case 0:
		return 0;
	default:
		break;
	}

	if ((cmsg = CMSG_FIRSTHDR(&msg)) == NULL) {
		warnx("CMSG_FIRSTHDR: missing control message");
		return -1;
	}

	if (cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(NUM_FDS * sizeof(int))) {
		warnx("CMSG_FIRSTHDR: invalid control message");
		return -1;
	}

	walk = (int *)CMSG_DATA(cmsg);
	for (cnt = 0; cnt < NUM_FDS; cnt++)
		fds[cnt] = *walk++;

	return 1;
}

int
main(int argc, char *argv[])
{
	struct manoutput	 options;
	struct mparse		*parser;
	void			*formatter;
	const char		*defos;
	const char		*errstr;
	int			 clientfd;
	int			 old_stdin;
	int			 old_stdout;
	int			 old_stderr;
	int			 fds[3];
	int			 state, opt;
	enum outt		 outtype;

	defos = NULL;
	outtype = OUTT_ASCII;
	while ((opt = getopt(argc, argv, "I:T:")) != -1) {
		switch (opt) {
		case 'I':
			if (strncmp(optarg, "os=", 3) == 0)
				defos = optarg + 3;
			else {
				warnx("-I %s: Bad argument", optarg);
				usage();
			}
			break;
		case 'T':
			if (strcmp(optarg, "ascii") == 0)
				outtype = OUTT_ASCII;
			else if (strcmp(optarg, "utf8") == 0)
				outtype = OUTT_UTF8;
			else if (strcmp(optarg, "html") == 0)
				outtype = OUTT_HTML;
			else {
				warnx("-T %s: Bad argument", optarg);
				usage();
			}
			break;
		default:
			usage();
		}
	}

	if (argc > 0) {
		argc -= optind;
		argv += optind;
	}
	if (argc != 1)
		usage();

	errstr = NULL;
	clientfd = strtonum(argv[0], 3, INT_MAX, &errstr);
	if (errstr)
		errx(1, "file descriptor %s %s", argv[1], errstr);

	mchars_alloc();
	parser = mparse_alloc(MPARSE_SO | MPARSE_UTF8 | MPARSE_LATIN1,
	    MANDOCERR_MAX, NULL, MANDOC_OS_OTHER, defos);

	memset(&options, 0, sizeof(options));
	switch (outtype) {
	case OUTT_ASCII:
		formatter = ascii_alloc(&options);
		break;
	case OUTT_UTF8:
		formatter = utf8_alloc(&options);
		break;
	case OUTT_HTML:
		options.fragment = 1;
		formatter = html_alloc(&options);
		break;
	}

	state = 1;  /* work to do */
	fflush(stdout);
	fflush(stderr);
	if ((old_stdin = dup(STDIN_FILENO)) == -1 ||
	    (old_stdout = dup(STDOUT_FILENO)) == -1 ||
	    (old_stderr = dup(STDERR_FILENO)) == -1) {
		warn("dup");
		state = -1;  /* error */
	}

	while (state == 1 && (state = read_fds(clientfd, fds)) == 1) {
		if (dup2(fds[0], STDIN_FILENO) == -1 ||
		    dup2(fds[1], STDOUT_FILENO) == -1 ||
		    dup2(fds[2], STDERR_FILENO) == -1) {
			warn("dup2");
			state = -1;
			break;
		}

		close(fds[0]);
		close(fds[1]);
		close(fds[2]);

		process(parser, outtype, formatter);
		mparse_reset(parser);

		fflush(stdout);
		fflush(stderr);
		/* Close file descriptors by restoring the old ones. */
		if (dup2(old_stderr, STDERR_FILENO) == -1 ||
		    dup2(old_stdout, STDOUT_FILENO) == -1 ||
		    dup2(old_stdin, STDIN_FILENO) == -1) {
			warn("dup2");
			state = -1;
			break;
		}
	}

	close(clientfd);
	switch (outtype) {
	case OUTT_ASCII:
	case OUTT_UTF8:
		ascii_free(formatter);
		break;
	case OUTT_HTML:
		html_free(formatter);
		break;
	}
	mparse_free(parser);
	mchars_free();
	return state == -1 ? 1 : 0;
}

static void
process(struct mparse *parser, enum outt outtype, void *formatter)
{
	struct roff_man	 *man;

	mparse_readfd(parser, STDIN_FILENO, "<unixfd>");
	mparse_result(parser, &man, NULL);

	if (man == NULL)
		return;

	if (man->macroset == MACROSET_MDOC) {
		mdoc_validate(man);
		switch (outtype) {
		case OUTT_ASCII:
		case OUTT_UTF8:
			terminal_mdoc(formatter, man);
			break;
		case OUTT_HTML:
			html_mdoc(formatter, man);
			break;
		}
	}
	if (man->macroset == MACROSET_MAN) {
		man_validate(man);
		switch (outtype) {
		case OUTT_ASCII:
		case OUTT_UTF8:
			terminal_man(formatter, man);
			break;
		case OUTT_HTML:
			html_man(formatter, man);
			break;
		}
	}
}

void
usage(void)
{
	fprintf(stderr, "usage: mandocd [-I os=name] [-T output] socket_fd\n");
	exit(1);
}

/*	$OpenBSD: stty.c,v 1.22 2021/10/23 16:45:32 mestre Exp $	*/
/*	$NetBSD: stty.c,v 1.11 1995/03/21 09:11:30 cgd Exp $	*/

/*-
 * Copyright (c) 1989, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "stty.h"
#include "extern.h"

int
main(int argc, char *argv[])
{
	struct info i;
	enum FMT fmt;
	int ch;

	fmt = NOTSET;
	i.fd = STDIN_FILENO;

	opterr = 0;
	while (optind < argc &&
	    strspn(argv[optind], "-aefg") == strlen(argv[optind]) &&
	    (ch = getopt(argc, argv, "aef:g")) != -1)
		switch(ch) {
		case 'a':
			fmt = POSIX;
			break;
		case 'e':
			fmt = BSD;
			break;
		case 'f':
			if ((i.fd = open(optarg, O_RDONLY | O_NONBLOCK)) == -1)
				err(1, "%s", optarg);
			break;
		case 'g':
			fmt = GFLAG;
			break;
		default:
			goto args;
		}

args:	argc -= optind;
	argv += optind;

	if (unveil("/", "") == -1)
		err(1, "unveil /");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (ioctl(i.fd, TIOCGETD, &i.ldisc) == -1)
		err(1, "TIOCGETD");

	if (tcgetattr(i.fd, &i.t) == -1)
		errx(1, "not a terminal");
	if (ioctl(i.fd, TIOCGWINSZ, &i.win) == -1)
		warn("TIOCGWINSZ");

	switch(fmt) {
	case NOTSET:
		if (*argv)
			break;
		/* FALLTHROUGH */
	case BSD:
	case POSIX:
		if (*argv)
			errx(1, "either display or modify");
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");
		print(&i.t, &i.win, i.ldisc, fmt);
		break;
	case GFLAG:
		if (*argv)
			errx(1, "either display or modify");
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");
		gprint(&i.t, &i.win, i.ldisc);
		break;
	}

	/*
	 * Cannot pledge, because of "extproc", "ostart" and "ostop"
	 */

	for (i.set = i.wset = 0; *argv; ++argv) {
		if (ksearch(&argv, &i))
			continue;

		if (csearch(&argv, &i))
			continue;

		if (msearch(&argv, &i))
			continue;

		if (isdigit((unsigned char)**argv)) {
			const char *error;
			int speed;

			speed = strtonum(*argv, 0, INT_MAX, &error);
			if (error)
				err(1, "%s", *argv);
			cfsetospeed(&i.t, speed);
			cfsetispeed(&i.t, speed);
			i.set = 1;
			continue;
		}

		if (!strncmp(*argv, "gfmt1", sizeof("gfmt1") - 1)) {
			gread(&i.t, *argv + sizeof("gfmt1") - 1);
			i.set = 1;
			continue;
		}

		warnx("illegal option -- %s", *argv);
		usage();
	}

	if (i.set && tcsetattr(i.fd, 0, &i.t) == -1)
		err(1, "tcsetattr");
	if (i.wset && ioctl(i.fd, TIOCSWINSZ, &i.win) == -1)
		warn("TIOCSWINSZ");
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-a | -e | -g] [-f file] [operands]\n",
	    __progname);
	exit (1);
}

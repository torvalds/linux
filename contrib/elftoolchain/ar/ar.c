/*-
 * Copyright (c) 2007 Kai Wang
 * Copyright (c) 2007 Tim Kientzle
 * Copyright (c) 2007 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Hugh Smith at The University of Guelph.
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

#include <sys/queue.h>
#include <sys/types.h>
#include <archive.h>
#include <errno.h>
#include <getopt.h>
#include <libelftc.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ar.h"

ELFTC_VCSID("$Id: ar.c 3629 2018-09-30 19:26:28Z jkoshy $");

enum options
{
	OPTION_HELP
};

static struct option longopts[] =
{
	{"flavor", required_argument, NULL, 'F'},
	{"help", no_argument, NULL, OPTION_HELP},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

static void	bsdar_usage(void);
static void	ranlib_usage(void);
static void	set_mode(struct bsdar *bsdar, char opt);
static void	only_mode(struct bsdar *bsdar, const char *opt,
		    const char *valid_modes);
static void	bsdar_version(void);

int
main(int argc, char **argv)
{
	struct bsdar	*bsdar, bsdar_storage;
	char		*arcmd, *argv1_saved;
	size_t		 len;
	int		 exitcode, i, opt;

	bsdar = &bsdar_storage;
	memset(bsdar, 0, sizeof(*bsdar));

	exitcode = EXIT_SUCCESS;
	arcmd = argv1_saved = NULL;
	bsdar->output = stdout;

	if ((bsdar->progname = ELFTC_GETPROGNAME()) == NULL)
		bsdar->progname = "ar";

	if (elf_version(EV_CURRENT) == EV_NONE)
		bsdar_errc(bsdar, 0, "ELF library initialization failed: %s",
		    elf_errmsg(-1));

	/*
	 * Act like ranlib if our name ends in "ranlib"; this
	 * accommodates names like "arm-freebsd7.1-ranlib",
	 * "bsdranlib", etc.
	 */
	len = strlen(bsdar->progname);
	if (len >= strlen("ranlib") &&
	    strcmp(bsdar->progname + len - strlen("ranlib"), "ranlib") == 0) {
		while ((opt = getopt_long(argc, argv, "tDUV", longopts,
		    NULL)) != -1) {
			switch(opt) {
			case 't':
				/* Ignored. */
				break;
			case 'D':
				bsdar->options |= AR_D;
				break;
			case 'U':
				bsdar->options &= ~AR_D;
				break;
			case 'V':
				bsdar_version();
				break;
			case OPTION_HELP:
				ranlib_usage();
			default:
				ranlib_usage();
			}
		}
		argv += optind;
		argc -= optind;

		if (*argv == NULL)
			ranlib_usage();

		bsdar->options |= AR_S;
		while ((bsdar->filename = *argv++) != NULL)
			exitcode |= ar_write_archive(bsdar, 's');

		exit(exitcode);
	} else {
		if (argc < 2)
			bsdar_usage();

		/*
		 * Tack on a leading '-', for old-style usage.
		 */
		if (*argv[1] != '-') {
			argv1_saved = argv[1];
			len = strlen(argv[1]) + 2;
			if ((arcmd = malloc(len)) == NULL)
				bsdar_errc(bsdar, errno, "malloc failed");
			(void) snprintf(arcmd, len, "-%s", argv[1]);
			argv[1] = arcmd;
		}
	}

	while ((opt = getopt_long(argc, argv, "abCcdDfF:ijlMmopqrSsTtUuVvxz",
	    longopts, NULL)) != -1) {
		switch(opt) {
		case 'a':
			bsdar->options |= AR_A;
			break;
		case 'b':
		case 'i':
			bsdar->options |= AR_B;
			break;
		case 'C':
			bsdar->options |= AR_CC;
			break;
		case 'c':
			bsdar->options |= AR_C;
			break;
		case 'd':
			set_mode(bsdar, opt);
			break;
		case 'D':
			bsdar->options |= AR_D;
			break;
		case 'F':
			if (!strcasecmp(optarg, "svr4") ||
			    !strcasecmp(optarg, "gnu"))
				bsdar->options &= ~AR_BSD;
			else if (!strcasecmp(optarg, "bsd"))
				bsdar->options |= AR_BSD;
			else
				bsdar_usage();
			break;
		case 'f':
		case 'T':
			bsdar->options |= AR_TR;
			break;
		case 'j':
			/* ignored */
			break;
		case 'l':
			/* ignored, for GNU ar comptibility */
			break;
		case 'M':
			set_mode(bsdar, opt);
			break;
		case 'm':
			set_mode(bsdar, opt);
			break;
		case 'o':
			bsdar->options |= AR_O;
			break;
		case 'p':
			set_mode(bsdar, opt);
			break;
		case 'q':
			set_mode(bsdar, opt);
			break;
		case 'r':
			set_mode(bsdar, opt);
			break;
		case 'S':
			bsdar->options |= AR_SS;
			break;
		case 's':
			bsdar->options |= AR_S;
			break;
		case 't':
			set_mode(bsdar, opt);
			break;
		case 'U':
			bsdar->options &= ~AR_D;
			break;
		case 'u':
			bsdar->options |= AR_U;
			break;
		case 'V':
			bsdar_version();
			break;
		case 'v':
			bsdar->options |= AR_V;
			break;
		case 'x':
			set_mode(bsdar, opt);
			break;
		case 'z':
			/* ignored */
			break;
		case OPTION_HELP:
			bsdar_usage();
		default:
			bsdar_usage();
		}
	}

	/* Restore argv[1] if we had modified it. */
	if (arcmd != NULL) {
		argv[1] = argv1_saved;
		free(arcmd);
		arcmd = argv1_saved = NULL;
	}

	argv += optind;
	argc -= optind;

	if (*argv == NULL && bsdar->mode != 'M')
		bsdar_usage();

	if (bsdar->options & AR_A && bsdar->options & AR_B)
		bsdar_errc(bsdar, 0,
		    "only one of -a and -[bi] options allowed");

	if (bsdar->options & AR_J && bsdar->options & AR_Z)
		bsdar_errc(bsdar, 0,
		    "only one of -j and -z options allowed");

	if (bsdar->options & AR_S && bsdar->options & AR_SS)
		bsdar_errc(bsdar, 0,
		    "only one of -s and -S options allowed");

	if (bsdar->options & (AR_A | AR_B)) {
		if (*argv == NULL)
			bsdar_errc(bsdar, 0,
			    "no position operand specified");
		if ((bsdar->posarg = basename(*argv)) == NULL)
			bsdar_errc(bsdar, errno,
			    "basename failed");
		argc--;
		argv++;
	}

	if (bsdar->options & AR_A)
		only_mode(bsdar, "-a", "mqr");
	if (bsdar->options & AR_B)
		only_mode(bsdar, "-b", "mqr");
	if (bsdar->options & AR_C)
		only_mode(bsdar, "-c", "qr");
	if (bsdar->options & AR_CC)
		only_mode(bsdar, "-C", "x");
	if (bsdar->options & AR_D)
		only_mode(bsdar, "-D", "qr");
	if (bsdar->options & AR_O)
		only_mode(bsdar, "-o", "x");
	if (bsdar->options & AR_SS)
		only_mode(bsdar, "-S", "mqr");
	if (bsdar->options & AR_U)
		only_mode(bsdar, "-u", "qrx");

	if (bsdar->mode == 'M') {
		ar_mode_script(bsdar);
		exit(EXIT_SUCCESS);
	}

	if ((bsdar->filename = *argv) == NULL)
		bsdar_usage();

	bsdar->argc = --argc;
	bsdar->argv = ++argv;

	if ((!bsdar->mode || strchr("ptx", bsdar->mode)) &&
	    bsdar->options & AR_S) {
		exitcode = ar_write_archive(bsdar, 's');
		if (!bsdar->mode)
			exit(exitcode);
	}

	switch(bsdar->mode) {
	case 'd': case 'm': case 'q': case 'r':
		exitcode = ar_write_archive(bsdar, bsdar->mode);
		break;

	case 'p': case 't': case 'x':
		exitcode = ar_read_archive(bsdar, bsdar->mode);
		break;
	default:
		bsdar_usage();
		/* NOTREACHED */
	}

	for (i = 0; i < bsdar->argc; i++) {
		if (bsdar->argv[i] != NULL) {
			bsdar_warnc(bsdar, 0, "%s: not found in archive",
			    bsdar->argv[i]);
			exitcode = EXIT_FAILURE;
		}
	}

	exit(exitcode);
}

static void
set_mode(struct bsdar *bsdar, char opt)
{

	if (bsdar->mode != '\0' && bsdar->mode != opt)
		bsdar_errc(bsdar, 0, "Can't specify both -%c and -%c",
		    opt, bsdar->mode);
	bsdar->mode = opt;
}

static void
only_mode(struct bsdar *bsdar, const char *opt, const char *valid_modes)
{

	if (strchr(valid_modes, bsdar->mode) == NULL)
		bsdar_errc(bsdar, 0, "Option %s is not permitted in mode -%c",
		    opt, bsdar->mode);
}

#define	AR_USAGE_MESSAGE	"\
Usage: %s <command> [options] archive file...\n\
  Manage archives.\n\n\
  Where <command> is one of:\n\
  -d            Delete members from the archive.\n\
  -m            Move archive members within the archive.\n\
  -p            Write the contents of members to standard output.\n\
  -q            Append files to an archive.\n\
  -r            Replace (add) files to an archive.\n\
  -s            Add an archive symbol to an archive.\n\
  -t            List files in an archive.\n\
  -x            Extract members from an archive.\n\
  -M            Execute MRI librarian commands.\n\
  -V            Print a version identifier and exit.\n\n\
  Options:\n\
  -a MEMBER     Add members after the specified member.\n\
  -b MEMBER | -i MEMBER\n\
                Add members before the specified member.\n\
  -c            Do not print a message when creating a new archive.\n\
  -f | -T       Only use the first fifteen characters of the member name.\n\
  -j            (This option is accepted, but is ignored).\n\
  -l            (This option is accepted, but is ignored).\n\
  -o            Preserve modification times when extracting members.\n\
  -u            Conditionally update or extract members.\n\
  -v            Be verbose.\n\
  -z            (This option is accepted, but is ignored).\n\
  -C            Do not overwrite existing files in the file system.\n\
  -D            Use fixed metadata, for consistent archive checksums.\n\
  -F FORMAT | --flavor=FORMAT\n\
                Create archives with the specified format.\n\
  -S            Do not generate an archive symbol table.\n\
  -U            Use original metadata for archive members.\n"

static void
bsdar_usage(void)
{
	(void) fprintf(stderr, AR_USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(EXIT_FAILURE);
}

#define	RANLIB_USAGE_MESSAGE	"\
Usage: %s [options] archive...\n\
  Update or create archive symbol tables.\n\n\
  Options:\n\
  -t              (This option is accepted, but ignored).\n\
  -D              Use fixed metadata, for consistent archive checksums.\n\
  -U              Use original metadata, for unique archive checksums.\n\
  -V              Print a version identifier and exit.\n"

static void
ranlib_usage(void)
{
	(void)fprintf(stderr, RANLIB_USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(EXIT_FAILURE);
}

static void
bsdar_version(void)
{
	(void)printf("%s (%s, %s)\n", ELFTC_GETPROGNAME(), archive_version_string(),
	    elftc_version());
	exit(EXIT_SUCCESS);
}

/*-
 * Copyright (c) 2004-2009 Apple Inc.
 * Copyright (c) 2006 Martin Voros
 * Copyright (c) 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Tool used to parse audit records conforming to the BSM structure.
 */

/*
 * praudit [-lnpx] [-r | -s] [-d del] [file ...]
 */

#include <config/config.h>

#include <bsm/libbsm.h>

#ifdef HAVE_CAP_ENTER
#include <sys/capsicum.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#endif

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char	*optarg;
extern int	 optind, optopt, opterr,optreset;

static char	*del = ",";	/* Default delimiter. */
static int	 oneline = 0;
static int	 partial = 0;
static int	 oflags = AU_OFLAG_NONE;

static void
usage(void)
{

	fprintf(stderr, "usage: praudit [-lnpx] [-r | -s] [-d del] "
	    "[file ...]\n");
	exit(1);
}

/*
 * Token printing for each token type .
 */
static int
print_tokens(FILE *fp)
{
	u_char *buf;
	tokenstr_t tok;
	int reclen;
	int bytesread;

	/* Allow tail -f | praudit to work. */
	if (partial) {
		u_char type = 0;
		/* Record must begin with a header token. */
		do {
			type = fgetc(fp);
		} while(type != AUT_HEADER32);
		ungetc(type, fp);
	}

	while ((reclen = au_read_rec(fp, &buf)) != -1) {
		bytesread = 0;
		while (bytesread < reclen) {
			/* Is this an incomplete record? */
			if (-1 == au_fetch_tok(&tok, buf + bytesread,
			    reclen - bytesread))
				break;
			au_print_flags_tok(stdout, &tok, del, oflags);
			bytesread += tok.len;
			if (oneline) {
				if (!(oflags & AU_OFLAG_XML))
					printf("%s", del);
			} else
				printf("\n");
		}
		free(buf);
		if (oneline)
			printf("\n");
		fflush(stdout);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	int ch;
	int i;
#ifdef HAVE_CAP_ENTER
	int retval;
	pid_t childpid, pid;
#endif
	FILE *fp;

	while ((ch = getopt(argc, argv, "d:lnprsx")) != -1) {
		switch(ch) {
		case 'd':
			del = optarg;
			break;

		case 'l':
			oneline = 1;
			break;

		case 'n':
			oflags |= AU_OFLAG_NORESOLVE;
			break;

		case 'p':
			partial = 1;
			break;

		case 'r':
			if (oflags & AU_OFLAG_SHORT)
				usage();	/* Exclusive from shortfrm. */
			oflags |= AU_OFLAG_RAW;
			break;

		case 's':
			if (oflags & AU_OFLAG_RAW)
				usage();	/* Exclusive from raw. */
			oflags |= AU_OFLAG_SHORT;
			break;

		case 'x':
			oflags |= AU_OFLAG_XML;
			break;

		case '?':
		default:
			usage();
		}
	}

#ifdef HAVE_CAP_ENTER
	/*
	 * Prime group, password, and audit-event files to be opened before we
	 * enter capability mode.
	 */
	(void)getgrgid(0);
	(void)setgroupent(1);
	(void)getpwuid(0);
	(void)setpassent(1);
	(void)getauevent();
#endif

	if (oflags & AU_OFLAG_XML)
		au_print_xml_header(stdout);

	/* For each of the files passed as arguments dump the contents. */
	if (optind == argc) {
#ifdef HAVE_CAP_ENTER
		retval = cap_enter();
		if (retval != 0 && errno != ENOSYS)
			err(EXIT_FAILURE, "cap_enter");
#endif
		print_tokens(stdin);
		return (1);
	}
	for (i = optind; i < argc; i++) {
		fp = fopen(argv[i], "r");
		if (fp == NULL) {
			perror(argv[i]);
			continue;
		}

		/*
		 * If operating with sandboxing, create a sandbox process for
		 * each trail file we operate on.  This avoids the need to do
		 * fancy things with file descriptors, etc, when iterating on
		 * a list of arguments.
		 */
#ifdef HAVE_CAP_ENTER
		childpid = fork();
		if (childpid == 0) {
			/* Child. */
			retval = cap_enter();
			if (retval != 0 && errno != ENOSYS)
				err(EXIT_FAILURE, "cap_enter");
			if (print_tokens(fp) == -1)
				perror(argv[i]);
			exit(0);
		}

		/* Parent.  Await child termination. */
		while ((pid = waitpid(childpid, NULL, 0)) != childpid);
#else
		if (print_tokens(fp) == -1)
			perror(argv[i]);
#endif
		fclose(fp);
	}

	if (oflags & AU_OFLAG_XML)
		au_print_xml_footer(stdout);

	return (0);
}

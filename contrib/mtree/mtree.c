/*	$NetBSD: mtree.c,v 1.49 2014/04/24 17:22:41 christos Exp $	*/

/*-
 * Copyright (c) 1989, 1990, 1993
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

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#if defined(__COPYRIGHT) && !defined(lint)
__COPYRIGHT("@(#) Copyright (c) 1989, 1990, 1993\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#if defined(__RCSID) && !defined(lint)
#if 0
static char sccsid[] = "@(#)mtree.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: mtree.c,v 1.49 2014/04/24 17:22:41 christos Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int	ftsoptions = FTS_PHYSICAL;
int	bflag, dflag, eflag, iflag, jflag, lflag, mflag, nflag, qflag, rflag,
	sflag, tflag, uflag;
char	fullpath[MAXPATHLEN];

static struct {
	enum flavor flavor;
	const char name[9];
} flavors[] = {
	{F_MTREE, "mtree"},
	{F_FREEBSD9, "freebsd9"},
	{F_NETBSD6, "netbsd6"},
};

__dead static	void	usage(void);

int
main(int argc, char **argv)
{
	int	ch, status;
	unsigned int	i;
	int	cflag, Cflag, Dflag, Uflag, wflag;
	char	*dir, *p;
	FILE	*spec1, *spec2;

	setprogname(argv[0]);

	cflag = Cflag = Dflag = Uflag = wflag = 0;
	dir = NULL;
	init_excludes();
	spec1 = stdin;
	spec2 = NULL;

	while ((ch = getopt(argc, argv,
	    "bcCdDeE:f:F:I:ijk:K:lLmMnN:O:p:PqrR:s:StuUwWxX:"))
	    != -1) {
		switch((char)ch) {
		case 'b':
			bflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'C':
			Cflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'D':
			Dflag = 1;
			break;
		case 'E':
			parsetags(&excludetags, optarg);
			break;
		case 'e':
			eflag = 1;
			break;
		case 'f':
			if (spec1 == stdin) {
				spec1 = fopen(optarg, "r");
				if (spec1 == NULL)
					mtree_err("%s: %s", optarg,
					    strerror(errno));
			} else if (spec2 == NULL) {
				spec2 = fopen(optarg, "r");
				if (spec2 == NULL)
					mtree_err("%s: %s", optarg,
					    strerror(errno));
			} else
				usage();
			break;
		case 'F':
			for (i = 0; i < __arraycount(flavors); i++)
				if (strcmp(optarg, flavors[i].name) == 0) {
					flavor = flavors[i].flavor;
					break;
				}
			if (i == __arraycount(flavors))
				usage();
			break;
		case 'i':
			iflag = 1;
			break;
		case 'I':
			parsetags(&includetags, optarg);
			break;
		case 'j':
			jflag = 1;
			break;
		case 'k':
			keys = F_TYPE;
			while ((p = strsep(&optarg, " \t,")) != NULL)
				if (*p != '\0')
					keys |= parsekey(p, NULL);
			break;
		case 'K':
			while ((p = strsep(&optarg, " \t,")) != NULL)
				if (*p != '\0')
					keys |= parsekey(p, NULL);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'L':
			ftsoptions &= ~FTS_PHYSICAL;
			ftsoptions |= FTS_LOGICAL;
			break;
		case 'm':
			mflag = 1;
			break;
		case 'M':
			mtree_Mflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'N':
			if (! setup_getid(optarg))
				mtree_err(
			    "Unable to use user and group databases in `%s'",
				    optarg);
			break;
		case 'O':
			load_only(optarg);
			break;
		case 'p':
			dir = optarg;
			break;
		case 'P':
			ftsoptions &= ~FTS_LOGICAL;
			ftsoptions |= FTS_PHYSICAL;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'R':
			while ((p = strsep(&optarg, " \t,")) != NULL)
				if (*p != '\0')
					keys &= ~parsekey(p, NULL);
			break;
		case 's':
			sflag = 1;
			crc_total = ~strtol(optarg, &p, 0);
			if (*p)
				mtree_err("illegal seed value -- %s", optarg);
			break;
		case 'S':
			mtree_Sflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'u':
			uflag = 1;
			break;
		case 'U':
			Uflag = uflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		case 'W':
			mtree_Wflag = 1;
			break;
		case 'x':
			ftsoptions |= FTS_XDEV;
			break;
		case 'X':
			read_excludes_file(optarg);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	switch (flavor) {
	case F_FREEBSD9:
		if (cflag && iflag) {
			warnx("-c and -i passed, replacing -i with -j for "
			    "FreeBSD compatibility");
			iflag = 0;
			jflag = 1;
		}
		if (dflag && !bflag) {
			warnx("Adding -b to -d for FreeBSD compatibility");
			bflag = 1;
		}
		if (uflag && !iflag) {
			warnx("Adding -i to -%c for FreeBSD compatibility",
			    Uflag ? 'U' : 'u');
			iflag = 1;
		}
		if (uflag && !tflag) {
			warnx("Adding -t to -%c for FreeBSD compatibility",
			    Uflag ? 'U' : 'u');
			tflag = 1;
		}
		if (wflag)
			warnx("The -w flag is a no-op");
		break;
	default:
		if (wflag)
			usage();
	}

	if (spec2 && (cflag || Cflag || Dflag))
		mtree_err("Double -f, -c, -C and -D flags are mutually "
		    "exclusive");

	if (dir && spec2)
		mtree_err("Double -f and -p flags are mutually exclusive");

	if (dir && chdir(dir))
		mtree_err("%s: %s", dir, strerror(errno));

	if ((cflag || sflag) && !getcwd(fullpath, sizeof(fullpath)))
		mtree_err("%s", strerror(errno));

	if ((cflag && Cflag) || (cflag && Dflag) || (Cflag && Dflag))
		mtree_err("-c, -C and -D flags are mutually exclusive");

	if (iflag && mflag)
		mtree_err("-i and -m flags are mutually exclusive");

	if (lflag && uflag)
		mtree_err("-l and -u flags are mutually exclusive");

	if (cflag) {
		cwalk(stdout);
		exit(0);
	}
	if (Cflag || Dflag) {
		dump_nodes(stdout, "", spec(spec1), Dflag);
		exit(0);
	}
	if (spec2 != NULL)
		status = mtree_specspec(spec1, spec2);
	else
		status = verify(spec1);
	if (Uflag && (status == MISMATCHEXIT))
		status = 0;
	exit(status);
}

static void
usage(void)
{
	unsigned int i;

	fprintf(stderr,
	    "usage: %s [-bCcDdejLlMnPqrStUuWx] [-i|-m] [-E tags]\n"
	    "\t\t[-f spec] [-f spec]\n"
	    "\t\t[-I tags] [-K keywords] [-k keywords] [-N dbdir] [-p path]\n"
	    "\t\t[-R keywords] [-s seed] [-X exclude-file]\n"
	    "\t\t[-F flavor]\n",
	    getprogname());
	fprintf(stderr, "\nflavors:");
	for (i = 0; i < __arraycount(flavors); i++)
		fprintf(stderr, " %s", flavors[i].name);
	fprintf(stderr, "\n");
	exit(1);
}

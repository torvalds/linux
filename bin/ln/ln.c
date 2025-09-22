/*	$OpenBSD: ln.c,v 1.25 2019/06/28 13:34:59 deraadt Exp $	*/
/*	$NetBSD: ln.c,v 1.10 1995/03/21 09:06:10 cgd Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994
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
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

int	fflag;				/* Unlink existing files. */
int	hflag;				/* Check new name for symlink first. */
int	Pflag;				/* Hard link to symlink. */
int	sflag;				/* Symbolic, not hard, link. */

int	linkit(char *, char *, int);
void	usage(void) __dead;

int
main(int argc, char *argv[])
{
	struct stat sb;
	int ch, exitval;
	char *sourcedir;

	if (pledge("stdio rpath cpath", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "fhLnPs")) != -1)
		switch (ch) {
		case 'f':
			fflag = 1;
			break;
		case 'h':
		case 'n':
			hflag = 1;
			break;
		case 'L':
			Pflag = 0;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
		}

	argv += optind;
	argc -= optind;

	switch(argc) {
	case 0:
		usage();
	case 1:				/* ln target */
		exit(linkit(argv[0], ".", 1));
	case 2:				/* ln target source */
		exit(linkit(argv[0], argv[1], 0));
	}
					/* ln target1 target2 directory */
	sourcedir = argv[argc - 1];
	if (stat(sourcedir, &sb))
		err(1, "%s", sourcedir);
	if (!S_ISDIR(sb.st_mode))
		usage();
	for (exitval = 0; *argv != sourcedir; ++argv)
		exitval |= linkit(*argv, sourcedir, 1);
	exit(exitval);
}

 /*
  * Nomenclature warning!
  *
  * In this source "target" and "source" are used the opposite way they
  * are used in the ln(1) manual.  Here "target" is the existing file and
  * "source" specifies the to-be-created link to "target".
  */
int
linkit(char *target, char *source, int isdir)
{
	struct stat sb;
	char *p, path[PATH_MAX];
	int (*statf)(const char *, struct stat *);
	int exists, n;

	if (!sflag) {
		/* If target doesn't exist, quit now. */
		if ((Pflag ? lstat : stat)(target, &sb)) {
			warn("%s", target);
			return (1);
		}
		/* Only symbolic links to directories. */
		if (S_ISDIR(sb.st_mode)) {
			warnc(EISDIR, "%s", target);
			return (1);
		}
	}

	statf = hflag ? lstat : stat;

	/* If the source is a directory, append the target's name. */
	if (isdir || (!statf(source, &sb) && S_ISDIR(sb.st_mode))) {
		if ((p = basename(target)) == NULL) {
			warn("%s", target);
			return (1);
		}
		n = snprintf(path, sizeof(path), "%s/%s", source, p);
		if (n < 0 || n >= sizeof(path)) {
			warnc(ENAMETOOLONG, "%s/%s", source, p);
			return (1);
		}
		source = path;
	}

	exists = (lstat(source, &sb) == 0);
	/*
	 * If doing hard links and the source (destination) exists and it
	 * actually is the same file like the target (existing file), we
	 * complain that the files are identical.  If -f is specified, we
	 * accept the job as already done and return with success.
	 */
	if (exists && !sflag) {
		struct stat tsb;

		if ((Pflag ? lstat : stat)(target, &tsb)) {
			warn("%s: disappeared", target);
			return (1);
		}

		if (tsb.st_dev == sb.st_dev && tsb.st_ino == sb.st_ino) {
			if (fflag)
				return (0);
			else {
				warnx("%s and %s are identical (nothing done).",
				    target, source);
				return (1);
			}
		}
	}
	/*
	 * If the file exists, and -f was specified, unlink it.
	 * Attempt the link.
	 */
	if ((fflag && unlink(source) == -1 && errno != ENOENT) ||
	    (sflag ? symlink(target, source) :
	    linkat(AT_FDCWD, target, AT_FDCWD, source,
	    Pflag ? 0 : AT_SYMLINK_FOLLOW))) {
		warn("%s", source);
		return (1);
	}

	return (0);
}

void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr,
	    "usage: %s [-fhLnPs] source [target]\n"
	    "       %s [-fLPs] source ... [directory]\n",
	    __progname, __progname);
	exit(1);
}

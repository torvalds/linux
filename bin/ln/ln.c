/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static char const copyright[] =
"@(#) Copyright (c) 1987, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)ln.c	8.2 (Berkeley) 3/31/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	fflag;			/* Unlink existing files. */
static int	Fflag;			/* Remove empty directories also. */
static int	hflag;			/* Check new name for symlink first. */
static int	iflag;			/* Interactive mode. */
static int	Pflag;			/* Create hard links to symlinks. */
static int	sflag;			/* Symbolic, not hard, link. */
static int	vflag;			/* Verbose output. */
static int	wflag;			/* Warn if symlink target does not
					 * exist, and -f is not enabled. */
static char	linkch;

static int	linkit(const char *, const char *, int);
static void	usage(void);

int
main(int argc, char *argv[])
{
	struct stat sb;
	char *p, *targetdir;
	int ch, exitval;

	/*
	 * Test for the special case where the utility is called as
	 * "link", for which the functionality provided is greatly
	 * simplified.
	 */
	if ((p = strrchr(argv[0], '/')) == NULL)
		p = argv[0];
	else
		++p;
	if (strcmp(p, "link") == 0) {
		while (getopt(argc, argv, "") != -1)
			usage();
		argc -= optind;
		argv += optind;
		if (argc != 2)
			usage();
		exit(linkit(argv[0], argv[1], 0));
	}

	while ((ch = getopt(argc, argv, "FLPfhinsvw")) != -1)
		switch (ch) {
		case 'F':
			Fflag = 1;
			break;
		case 'L':
			Pflag = 0;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'f':
			fflag = 1;
			iflag = 0;
			wflag = 0;
			break;
		case 'h':
		case 'n':
			hflag = 1;
			break;
		case 'i':
			iflag = 1;
			fflag = 0;
			break;
		case 's':
			sflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		case '?':
		default:
			usage();
		}

	argv += optind;
	argc -= optind;

	linkch = sflag ? '-' : '=';
	if (sflag == 0)
		Fflag = 0;
	if (Fflag == 1 && iflag == 0) {
		fflag = 1;
		wflag = 0;		/* Implied when fflag != 0 */
	}

	switch(argc) {
	case 0:
		usage();
		/* NOTREACHED */
	case 1:				/* ln source */
		exit(linkit(argv[0], ".", 1));
	case 2:				/* ln source target */
		exit(linkit(argv[0], argv[1], 0));
	default:
		;
	}
					/* ln source1 source2 directory */
	targetdir = argv[argc - 1];
	if (hflag && lstat(targetdir, &sb) == 0 && S_ISLNK(sb.st_mode)) {
		/*
		 * We were asked not to follow symlinks, but found one at
		 * the target--simulate "not a directory" error
		 */
		errno = ENOTDIR;
		err(1, "%s", targetdir);
	}
	if (stat(targetdir, &sb))
		err(1, "%s", targetdir);
	if (!S_ISDIR(sb.st_mode))
		usage();
	for (exitval = 0; *argv != targetdir; ++argv)
		exitval |= linkit(*argv, targetdir, 1);
	exit(exitval);
}

/*
 * Two pathnames refer to the same directory entry if the directories match
 * and the final components' names match.
 */
static int
samedirent(const char *path1, const char *path2)
{
	const char *file1, *file2;
	char pathbuf[PATH_MAX];
	struct stat sb1, sb2;

	if (strcmp(path1, path2) == 0)
		return 1;
	file1 = strrchr(path1, '/');
	if (file1 != NULL)
		file1++;
	else
		file1 = path1;
	file2 = strrchr(path2, '/');
	if (file2 != NULL)
		file2++;
	else
		file2 = path2;
	if (strcmp(file1, file2) != 0)
		return 0;
	if (file1 - path1 >= PATH_MAX || file2 - path2 >= PATH_MAX)
		return 0;
	if (file1 == path1)
		memcpy(pathbuf, ".", 2);
	else {
		memcpy(pathbuf, path1, file1 - path1);
		pathbuf[file1 - path1] = '\0';
	}
	if (stat(pathbuf, &sb1) != 0)
		return 0;
	if (file2 == path2)
		memcpy(pathbuf, ".", 2);
	else {
		memcpy(pathbuf, path2, file2 - path2);
		pathbuf[file2 - path2] = '\0';
	}
	if (stat(pathbuf, &sb2) != 0)
		return 0;
	return sb1.st_dev == sb2.st_dev && sb1.st_ino == sb2.st_ino;
}

static int
linkit(const char *source, const char *target, int isdir)
{
	struct stat sb;
	const char *p;
	int ch, exists, first;
	char path[PATH_MAX];
	char wbuf[PATH_MAX];
	char bbuf[PATH_MAX];

	if (!sflag) {
		/* If source doesn't exist, quit now. */
		if ((Pflag ? lstat : stat)(source, &sb)) {
			warn("%s", source);
			return (1);
		}
		/* Only symbolic links to directories. */
		if (S_ISDIR(sb.st_mode)) {
			errno = EISDIR;
			warn("%s", source);
			return (1);
		}
	}

	/*
	 * If the target is a directory (and not a symlink if hflag),
	 * append the source's name, unless Fflag is set.
	 */
	if (!Fflag && (isdir ||
	    (lstat(target, &sb) == 0 && S_ISDIR(sb.st_mode)) ||
	    (!hflag && stat(target, &sb) == 0 && S_ISDIR(sb.st_mode)))) {
		if (strlcpy(bbuf, source, sizeof(bbuf)) >= sizeof(bbuf) ||
		    (p = basename(bbuf)) == NULL ||
		    snprintf(path, sizeof(path), "%s/%s", target, p) >=
		    (ssize_t)sizeof(path)) {
			errno = ENAMETOOLONG;
			warn("%s", source);
			return (1);
		}
		target = path;
	}

	/*
	 * If the link source doesn't exist, and a symbolic link was
	 * requested, and -w was specified, give a warning.
	 */
	if (sflag && wflag) {
		if (*source == '/') {
			/* Absolute link source. */
			if (stat(source, &sb) != 0)
				 warn("warning: %s inaccessible", source);
		} else {
			/*
			 * Relative symlink source.  Try to construct the
			 * absolute path of the source, by appending `source'
			 * to the parent directory of the target.
			 */
			strlcpy(bbuf, target, sizeof(bbuf));
			p = dirname(bbuf);
			if (p != NULL) {
				(void)snprintf(wbuf, sizeof(wbuf), "%s/%s",
						p, source);
				if (stat(wbuf, &sb) != 0)
					warn("warning: %s", source);
			}
		}
	}

	/*
	 * If the file exists, first check it is not the same directory entry.
	 */
	exists = !lstat(target, &sb);
	if (exists) {
		if (!sflag && samedirent(source, target)) {
			warnx("%s and %s are the same directory entry",
			    source, target);
			return (1);
		}
	}
	/*
	 * Then unlink it forcibly if -f was specified
	 * and interactively if -i was specified.
	 */
	if (fflag && exists) {
		if (Fflag && S_ISDIR(sb.st_mode)) {
			if (rmdir(target)) {
				warn("%s", target);
				return (1);
			}
		} else if (unlink(target)) {
			warn("%s", target);
			return (1);
		}
	} else if (iflag && exists) {
		fflush(stdout);
		fprintf(stderr, "replace %s? ", target);

		first = ch = getchar();
		while(ch != '\n' && ch != EOF)
			ch = getchar();
		if (first != 'y' && first != 'Y') {
			fprintf(stderr, "not replaced\n");
			return (1);
		}

		if (Fflag && S_ISDIR(sb.st_mode)) {
			if (rmdir(target)) {
				warn("%s", target);
				return (1);
			}
		} else if (unlink(target)) {
			warn("%s", target);
			return (1);
		}
	}

	/* Attempt the link. */
	if (sflag ? symlink(source, target) :
	    linkat(AT_FDCWD, source, AT_FDCWD, target,
	    Pflag ? 0 : AT_SYMLINK_FOLLOW)) {
		warn("%s", target);
		return (1);
	}
	if (vflag)
		(void)printf("%s %c> %s\n", target, linkch, source);
	return (0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n%s\n",
	    "usage: ln [-s [-F] | -L | -P] [-f | -i] [-hnv] source_file [target_file]",
	    "       ln [-s [-F] | -L | -P] [-f | -i] [-hnv] source_file ... target_dir",
	    "       link source_file target_file");
	exit(1);
}

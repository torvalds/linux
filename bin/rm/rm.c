/*	$OpenBSD: rm.c,v 1.45 2025/04/20 13:47:54 kn Exp $	*/
/*	$NetBSD: rm.c,v 1.19 1995/09/07 06:48:50 jtc Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
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
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

extern char *__progname;

int dflag, eval, fflag, iflag, Pflag, vflag, stdin_ok;

int	check(char *, char *, struct stat *);
void	checkdot(char **);
void	rm_file(char **);
int	rm_overwrite(char *, struct stat *);
int	pass(int, off_t, char *, size_t);
void	rm_tree(char **);
void	usage(void);

/*
 * rm --
 *	This rm is different from historic rm's, but is expected to match
 *	POSIX 1003.2 behavior.  The most visible difference is that -f
 *	has two specific effects now, ignore non-existent files and force
 * 	file removal.
 */
int
main(int argc, char *argv[])
{
	int ch, rflag;

	Pflag = rflag = 0;
	while ((ch = getopt(argc, argv, "dfiPRrv")) != -1)
		switch(ch) {
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			iflag = 0;
			break;
		case 'i':
			fflag = 0;
			iflag = 1;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'R':
		case 'r':			/* Compatibility. */
			rflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (Pflag) {
		if (pledge("stdio rpath wpath cpath getpw", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio rpath cpath getpw", NULL) == -1)
			err(1, "pledge");
	}

	if (argc < 1 && fflag == 0)
		usage();

	checkdot(argv);

	if (*argv) {
		stdin_ok = isatty(STDIN_FILENO);

		if (rflag)
			rm_tree(argv);
		else
			rm_file(argv);
	}

	return (eval);
}

void
rm_tree(char **argv)
{
	FTS *fts;
	FTSENT *p;
	int needstat;
	int flags;

	/*
	 * Remove a file hierarchy.  If forcing removal (-f), or interactive
	 * (-i) or can't ask anyway (stdin_ok), don't stat the file.
	 */
	needstat = !fflag && !iflag && stdin_ok;

	/*
	 * If the -i option is specified, the user can skip on the pre-order
	 * visit.  The fts_number field flags skipped directories.
	 */
#define	SKIPPED	1

	flags = FTS_PHYSICAL;
	if (!needstat)
		flags |= FTS_NOSTAT;
	if (!(fts = fts_open(argv, flags, NULL)))
		err(1, NULL);
	while ((p = fts_read(fts)) != NULL) {
		switch (p->fts_info) {
		case FTS_DNR:
			if (!fflag || p->fts_errno != ENOENT) {
				warnc(p->fts_errno, "%s", p->fts_path);
				eval = 1;
			}
			continue;
		case FTS_ERR:
			errc(1, p->fts_errno, "%s", p->fts_path);
		case FTS_NS:
			/*
			 * FTS_NS: assume that if can't stat the file, it
			 * can't be unlinked.
			 */
			if (!needstat)
				break;
			if (!fflag || p->fts_errno != ENOENT) {
				warnc(p->fts_errno, "%s", p->fts_path);
				eval = 1;
			}
			continue;
		case FTS_D:
			/* Pre-order: give user chance to skip. */
			if (!fflag && !check(p->fts_path, p->fts_accpath,
			    p->fts_statp)) {
				(void)fts_set(fts, p, FTS_SKIP);
				p->fts_number = SKIPPED;
			}
			continue;
		case FTS_DP:
			/* Post-order: see if user skipped. */
			if (p->fts_number == SKIPPED)
				continue;
			break;
		default:
			if (!fflag &&
			    !check(p->fts_path, p->fts_accpath, p->fts_statp))
				continue;
		}

		/*
		 * If we can't read or search the directory, may still be
		 * able to remove it.  Don't print out the un{read,search}able
		 * message unless the remove fails.
		 */
		switch (p->fts_info) {
		case FTS_DP:
		case FTS_DNR:
			if (!rmdir(p->fts_accpath)) {
				if (vflag)
					fprintf(stdout, "%s\n", p->fts_path);
				continue;
			}
			if (fflag && errno == ENOENT)
				continue;
			break;

		case FTS_F:
		case FTS_NSOK:
			if (Pflag)
				rm_overwrite(p->fts_accpath, p->fts_info ==
				    FTS_NSOK ? NULL : p->fts_statp);
			/* FALLTHROUGH */
		default:
			if (!unlink(p->fts_accpath)) {
				if (vflag)
					fprintf(stdout, "%s\n", p->fts_path);
				continue;
			}
			if (fflag && errno == ENOENT)
				continue;
		}
		warn("%s", p->fts_path);
		eval = 1;
	}
	if (errno)
		err(1, "fts_read");
	fts_close(fts);
}

void
rm_file(char **argv)
{
	struct stat sb;
	int rval;
	char *f;

	/*
	 * Remove a file.  POSIX 1003.2 states that, by default, attempting
	 * to remove a directory is an error, so must always stat the file.
	 */
	while ((f = *argv++) != NULL) {
		/* Assume if can't stat the file, can't unlink it. */
		if (lstat(f, &sb)) {
			if (!fflag || errno != ENOENT) {
				warn("%s", f);
				eval = 1;
			}
			continue;
		}

		if (S_ISDIR(sb.st_mode) && !dflag) {
			warnx("%s: is a directory", f);
			eval = 1;
			continue;
		}
		if (!fflag && !check(f, f, &sb))
			continue;
		else if (S_ISDIR(sb.st_mode))
			rval = rmdir(f);
		else {
			if (Pflag)
				rm_overwrite(f, &sb);
			rval = unlink(f);
		}
		if (rval && (!fflag || errno != ENOENT)) {
			warn("%s", f);
			eval = 1;
		} else if (rval == 0 && vflag)
			(void)fprintf(stdout, "%s\n", f);
	}
}

/*
 * rm_overwrite --
 *	Overwrite the file with varying bit patterns.
 *
 * XXX
 * This is a cheap way to *really* delete files.  Note that only regular
 * files are deleted, directories (and therefore names) will remain.
 * Also, this assumes a fixed-block file system (like FFS, or a V7 or a
 * System V file system).  In a logging file system, you'll have to have
 * kernel support.
 * Returns 1 for success.
 */
int
rm_overwrite(char *file, struct stat *sbp)
{
	struct stat sb, sb2;
	struct statfs fsb;
	size_t bsize;
	int fd;
	char *buf = NULL;

	fd = -1;
	if (sbp == NULL) {
		if (lstat(file, &sb))
			goto err;
		sbp = &sb;
	}
	if (!S_ISREG(sbp->st_mode))
		return (1);
	if (sbp->st_nlink > 1) {
		warnx("%s (inode %llu): not overwritten due to multiple links",
		    file, (unsigned long long)sbp->st_ino);
		return (0);
	}
	if ((fd = open(file, O_WRONLY|O_NONBLOCK|O_NOFOLLOW)) == -1)
		goto err;
	if (fstat(fd, &sb2))
		goto err;
	if (sb2.st_dev != sbp->st_dev || sb2.st_ino != sbp->st_ino ||
	    !S_ISREG(sb2.st_mode)) {
		errno = EPERM;
		goto err;
	}
	if (fstatfs(fd, &fsb) == -1)
		goto err;
	bsize = MAXIMUM(fsb.f_iosize, 1024U);
	if ((buf = malloc(bsize)) == NULL)
		err(1, "%s: malloc", file);

	if (!pass(fd, sbp->st_size, buf, bsize))
		goto err;
	if (fsync(fd))
		goto err;
	close(fd);
	free(buf);
	return (1);

err:
	warn("%s", file);
	close(fd);
	eval = 1;
	free(buf);
	return (0);
}

int
pass(int fd, off_t len, char *buf, size_t bsize)
{
	size_t wlen;

	for (; len > 0; len -= wlen) {
		wlen = len < bsize ? len : bsize;
		arc4random_buf(buf, wlen);
		if (write(fd, buf, wlen) != wlen)
			return (0);
	}
	return (1);
}

int
check(char *path, char *name, struct stat *sp)
{
	int ch, first;
	char modep[15];

	/* Check -i first. */
	if (iflag)
		(void)fprintf(stderr, "remove %s? ", path);
	else {
		/*
		 * If it's not a symbolic link and it's unwritable and we're
		 * talking to a terminal, ask.  Symbolic links are excluded
		 * because their permissions are meaningless.  Check stdin_ok
		 * first because we may not have stat'ed the file.
		 */
		if (!stdin_ok || S_ISLNK(sp->st_mode) || !access(name, W_OK) ||
		    errno != EACCES)
			return (1);
		strmode(sp->st_mode, modep);
		(void)fprintf(stderr, "override %s%s%s/%s for %s? ",
		    modep + 1, modep[9] == ' ' ? "" : " ",
		    user_from_uid(sp->st_uid, 0),
		    group_from_gid(sp->st_gid, 0), path);
	}
	(void)fflush(stderr);

	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();
	return (first == 'y' || first == 'Y');
}

/*
 * POSIX.2 requires that if "." or ".." are specified as the basename
 * portion of an operand, a diagnostic message be written to standard
 * error and nothing more be done with such operands.
 *
 * Since POSIX.2 defines basename as the final portion of a path after
 * trailing slashes have been removed, we'll remove them here.
 */
#define ISDOT(a) ((a)[0] == '.' && (!(a)[1] || ((a)[1] == '.' && !(a)[2])))
void
checkdot(char **argv)
{
	char *p, **save, **t;
	int complained;
	struct stat sb, root;

	stat("/", &root);
	complained = 0;
	for (t = argv; *t;) {
		if (lstat(*t, &sb) == 0 &&
		    root.st_ino == sb.st_ino && root.st_dev == sb.st_dev) {
			if (!complained++)
				warnx("\"/\" may not be removed");
			goto skip;
		}
		/* strip trailing slashes */
		p = strrchr(*t, '\0');
		while (--p > *t && *p == '/')
			*p = '\0';

		/* extract basename */
		if ((p = strrchr(*t, '/')) != NULL)
			++p;
		else
			p = *t;

		if (ISDOT(p)) {
			if (!complained++)
				warnx("\".\" and \"..\" may not be removed");
skip:
			eval = 1;
			for (save = t; (t[0] = t[1]) != NULL; ++t)
				continue;
			t = save;
		} else
			++t;
	}
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-dfiPRrv] file ...\n", __progname);
	exit(1);
}

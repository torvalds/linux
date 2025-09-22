/*	$OpenBSD: cp.c,v 1.53 2019/06/28 13:34:58 deraadt Exp $	*/
/*	$NetBSD: cp.c,v 1.14 1995/09/07 06:14:51 jtc Exp $	*/

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems Inc.
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

/*
 * Cp copies source files to target files.
 *
 * The global PATH_T structure "to" always contains the path to the
 * current target file.  Since fts(3) does not change directories,
 * this path can be either absolute or dot-relative.
 *
 * The basic algorithm is to initialize "to" and use fts(3) to traverse
 * the file hierarchy rooted in the argument list.  A trivial case is the
 * case of 'cp file1 file2'.  The more interesting case is the case of
 * 'cp file1 file2 ... fileN dir' where the hierarchy is traversed and the
 * path (relative to the root of the traversal) is appended to dir (stored
 * in "to") to form the final target path.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define	fts_dne(_x)	(_x->fts_pointer != NULL)

PATH_T to = { to.p_path, "" };

uid_t myuid;
int Rflag, fflag, iflag, pflag, rflag, vflag;
mode_t myumask;

enum op { FILE_TO_FILE, FILE_TO_DIR, DIR_TO_DNE };

int copy(char *[], enum op, int);
char *find_last_component(char *);

int
main(int argc, char *argv[])
{
	struct stat to_stat, tmp_stat;
	enum op type;
	int Hflag, Lflag, Pflag, ch, fts_options, r;
	char *target;

	Hflag = Lflag = Pflag = Rflag = 0;
	while ((ch = getopt(argc, argv, "HLPRafiprv")) != -1)
		switch (ch) {
		case 'H':
			Hflag = 1;
			Lflag = Pflag = 0;
			break;
		case 'L':
			Lflag = 1;
			Hflag = Pflag = 0;
			break;
		case 'P':
			Pflag = 1;
			Hflag = Lflag = 0;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'a':
			Rflag = 1;
			pflag = 1;
			Pflag = 1;
			Hflag = Lflag = 0;
			break;
		case 'f':
			fflag = 1;
			iflag = 0;
			break;
		case 'i':
			iflag = 1;
			fflag = 0;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
			break;
		}
	argc -= optind;
	argv += optind;

	/*
	 * Unfortunately, -R will use mkfifo & mknod;
	 * -p will use fchown, fchmod, lchown, fchflags..
	 */
	if (Rflag == 0 && pflag == 0)
		if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
			err(1, "pledge");

	if (argc < 2)
		usage();

	fts_options = FTS_NOCHDIR | FTS_PHYSICAL;
	if (rflag) {
		if (Rflag)
			errx(1,
		    "the -R and -r options may not be specified together.");
		if (Hflag || Lflag || Pflag)
			errx(1,
	"the -H, -L, and -P options may not be specified with the -r option.");
		fts_options &= ~FTS_PHYSICAL;
		fts_options |= FTS_LOGICAL;
	}
	if (Rflag) {
		if (Hflag)
			fts_options |= FTS_COMFOLLOW;
		if (Lflag) {
			fts_options &= ~FTS_PHYSICAL;
			fts_options |= FTS_LOGICAL;
		}
	} else {
		fts_options &= ~FTS_PHYSICAL;
		fts_options |= FTS_LOGICAL;
	}

	myuid = getuid();

	/* Copy the umask for explicit mode setting. */
	myumask = umask(0);
	(void)umask(myumask);

	/* Save the target base in "to". */
	target = argv[--argc];
	if (strlcpy(to.p_path, target, sizeof to.p_path) >= sizeof(to.p_path))
		errx(1, "%s: name too long", target);
	to.p_end = to.p_path + strlen(to.p_path);
	if (to.p_path == to.p_end) {
		*to.p_end++ = '.';
		*to.p_end = '\0';
	}
	to.target_end = to.p_end;

	/* Set end of argument list for fts(3). */
	argv[argc] = NULL;

	/*
	 * Cp has two distinct cases:
	 *
	 * cp [-R] source target
	 * cp [-R] source1 ... sourceN directory
	 *
	 * In both cases, source can be either a file or a directory.
	 *
	 * In (1), the target becomes a copy of the source. That is, if the
	 * source is a file, the target will be a file, and likewise for
	 * directories.
	 *
	 * In (2), the real target is not directory, but "directory/source".
	 */
	r = stat(to.p_path, &to_stat);
	if (r == -1 && errno != ENOENT)
		err(1, "%s", to.p_path);
	if (r == -1 || !S_ISDIR(to_stat.st_mode)) {
		/*
		 * Case (1).  Target is not a directory.
		 */
		if (argc > 1)
			usage();
		/*
		 * Need to detect the case:
		 *	cp -R dir foo
		 * Where dir is a directory and foo does not exist, where
		 * we want pathname concatenations turned on but not for
		 * the initial mkdir().
		 */
		if (r == -1) {
			if (rflag || (Rflag && (Lflag || Hflag)))
				stat(*argv, &tmp_stat);
			else
				lstat(*argv, &tmp_stat);

			if (S_ISDIR(tmp_stat.st_mode) && (Rflag || rflag))
				type = DIR_TO_DNE;
			else
				type = FILE_TO_FILE;
		} else
			type = FILE_TO_FILE;
	} else {
		/*
		 * Case (2).  Target is a directory.
		 */
		type = FILE_TO_DIR;
	}

	return (copy(argv, type, fts_options));
}

char *
find_last_component(char *path)
{
	char *p;

	if ((p = strrchr(path, '/')) == NULL)
		p = path;
	else {
		/* Special case foo/ */
		if (!*(p+1)) {
			while ((p >= path) && *p == '/')
				p--;

			while ((p >= path) && *p != '/')
				p--;
		}

		p++;
	}

	return (p);
}

int
copy(char *argv[], enum op type, int fts_options)
{
	struct stat to_stat;
	FTS *ftsp;
	FTSENT *curr;
	int base, cval, nlen, rval;
	char *p, *target_mid;
	base = 0;

	if ((ftsp = fts_open(argv, fts_options, NULL)) == NULL)
		err(1, NULL);
	for (rval = 0; (curr = fts_read(ftsp)) != NULL;) {
		switch (curr->fts_info) {
		case FTS_NS:
		case FTS_DNR:
		case FTS_ERR:
			warnx("%s: %s",
			    curr->fts_path, strerror(curr->fts_errno));
			rval = 1;
			continue;
		case FTS_DC:
			warnx("%s: directory causes a cycle", curr->fts_path);
			rval = 1;
			continue;
		}

		/*
		 * If we are in case (2) or (3) above, we need to append the
		 * source name to the target name.
		 */
		if (type != FILE_TO_FILE) {
			/*
			 * Need to remember the roots of traversals to create
			 * correct pathnames.  If there's a directory being
			 * copied to a non-existent directory, e.g.
			 *	cp -R a/dir noexist
			 * the resulting path name should be noexist/foo, not
			 * noexist/dir/foo (where foo is a file in dir), which
			 * is the case where the target exists.
			 *
			 * Also, check for "..".  This is for correct path
			 * concatenation for paths ending in "..", e.g.
			 *	cp -R .. /tmp
			 * Paths ending in ".." are changed to ".".  This is
			 * tricky, but seems the easiest way to fix the problem.
			 *
			 * XXX
			 * Since the first level MUST be FTS_ROOTLEVEL, base
			 * is always initialized.
			 */
			if (curr->fts_level == FTS_ROOTLEVEL) {
				if (type != DIR_TO_DNE) {
					p = find_last_component(curr->fts_path);
					base = p - curr->fts_path;
					
					if (!strcmp(&curr->fts_path[base],
					    ".."))
						base += 1;
				} else
					base = curr->fts_pathlen;
			}

			p = &curr->fts_path[base];
			nlen = curr->fts_pathlen - base;
			target_mid = to.target_end;
			if (*p != '/' && target_mid[-1] != '/')
				*target_mid++ = '/';
			*target_mid = '\0';
			if (target_mid - to.p_path + nlen >= PATH_MAX) {
				warnx("%s%s: name too long (not copied)",
				    to.p_path, p);
				rval = 1;
				continue;
			}
			(void)strncat(target_mid, p, nlen);
			to.p_end = target_mid + nlen;
			*to.p_end = '\0';
		}

		/* Not an error but need to remember it happened */
		if (stat(to.p_path, &to_stat) == -1) {
			if (curr->fts_info == FTS_DP)
				continue;
			/*
			 * We use fts_pointer as a boolean to indicate that
			 * we created this directory ourselves.  We'll use
			 * this later on via the fts_dne macro to decide
			 * whether or not to set the directory mode during
			 * the post-order pass.
			 */
			curr->fts_pointer = (void *)1;
		} else {
			/*
			 * Set directory mode/user/times on the post-order
			 * pass.  We can't do this earlier because the mode
			 * may not allow us write permission.  Furthermore,
			 * if we set the times during the pre-order pass,
			 * they will get changed later when the directory
			 * is populated.
			 */
			if (curr->fts_info == FTS_DP) {
				if (!S_ISDIR(to_stat.st_mode))
					continue;
				/*
				 * If not -p and directory didn't exist, set
				 * it to be the same as the from directory,
				 * unmodified by the umask; arguably wrong,
				 * but it's been that way forever.
				 */
				if (pflag && setfile(curr->fts_statp, -1))
					rval = 1;
				else if (fts_dne(curr))
					(void)chmod(to.p_path,
					    curr->fts_statp->st_mode);
				continue;
			}
			if (to_stat.st_dev == curr->fts_statp->st_dev &&
			    to_stat.st_ino == curr->fts_statp->st_ino) {
				warnx("%s and %s are identical (not copied).",
				    to.p_path, curr->fts_path);
				rval = 1;
				if (S_ISDIR(curr->fts_statp->st_mode))
					(void)fts_set(ftsp, curr, FTS_SKIP);
				continue;
			}
			if (!S_ISDIR(curr->fts_statp->st_mode) &&
			    S_ISDIR(to_stat.st_mode)) {
		warnx("cannot overwrite directory %s with non-directory %s",
				    to.p_path, curr->fts_path);
				rval = 1;
				continue;
			}
		}

		switch (curr->fts_statp->st_mode & S_IFMT) {
		case S_IFLNK:
			if ((cval = copy_link(curr, !fts_dne(curr))) == 1)
				rval = 1;
			if (!cval && vflag)
				(void)fprintf(stdout, "%s -> %s\n",
				    curr->fts_path, to.p_path);
			break;
		case S_IFDIR:
			if (!Rflag && !rflag) {
				warnx("%s is a directory (not copied).",
				    curr->fts_path);
				(void)fts_set(ftsp, curr, FTS_SKIP);
				rval = 1;
				break;
			}
			/*
			 * If the directory doesn't exist, create the new
			 * one with the from file mode plus owner RWX bits,
			 * modified by the umask.  Trade-off between being
			 * able to write the directory (if from directory is
			 * 555) and not causing a permissions race.  If the
			 * umask blocks owner writes, we fail..
			 */
			if (fts_dne(curr)) {
				if (mkdir(to.p_path,
				    curr->fts_statp->st_mode | S_IRWXU) == -1)
					err(1, "%s", to.p_path);
				else if (vflag)
					(void)fprintf(stdout, "%s -> %s\n",
					    curr->fts_path, to.p_path);
			} else if (!S_ISDIR(to_stat.st_mode))
				errc(1, ENOTDIR, "%s", to.p_path);
			break;
		case S_IFBLK:
		case S_IFCHR:
			if (Rflag) {
				if ((cval = copy_special(curr->fts_statp,
				    !fts_dne(curr))) == 1)
					rval = 1;
			} else
				if ((cval = copy_file(curr, !fts_dne(curr))) == 1)
					rval = 1;
			if (!cval && vflag)
				(void)fprintf(stdout, "%s -> %s\n",
				    curr->fts_path, to.p_path);
			cval = 0;
			break;
		case S_IFIFO:
			if (Rflag) {
				if ((cval = copy_fifo(curr->fts_statp,
				    !fts_dne(curr))) == 1)
					rval = 1;
			} else
				if ((cval = copy_file(curr, !fts_dne(curr))) == 1)
					rval = 1;
			if (!cval && vflag)
				(void)fprintf(stdout, "%s -> %s\n",
				    curr->fts_path, to.p_path);
			cval = 0;
			break;
		case S_IFSOCK:
			warnc(EOPNOTSUPP, "%s", curr->fts_path);
			break;
		default:
			if ((cval = copy_file(curr, !fts_dne(curr))) == 1)
				rval = 1;
			if (!cval && vflag)
				(void)fprintf(stdout, "%s -> %s\n",
				    curr->fts_path, to.p_path);
			cval = 0;
			break;
		}
	}
	if (errno)
		err(1, "fts_read");
	(void)fts_close(ftsp);
	return (rval);
}

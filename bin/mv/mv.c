/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ken Smith of The State University of New York at Buffalo.
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
"@(#) Copyright (c) 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)mv.c	8.2 (Berkeley) 4/2/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/acl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

/* Exit code for a failed exec. */
#define EXEC_FAILED 127

static int	fflg, hflg, iflg, nflg, vflg;

static int	copy(const char *, const char *);
static int	do_move(const char *, const char *);
static int	fastcopy(const char *, const char *, struct stat *);
static void	usage(void);
static void	preserve_fd_acls(int source_fd, int dest_fd, const char *source_path,
		    const char *dest_path);

int
main(int argc, char *argv[])
{
	size_t baselen, len;
	int rval;
	char *p, *endp;
	struct stat sb;
	int ch;
	char path[PATH_MAX];

	while ((ch = getopt(argc, argv, "fhinv")) != -1)
		switch (ch) {
		case 'h':
			hflg = 1;
			break;
		case 'i':
			iflg = 1;
			fflg = nflg = 0;
			break;
		case 'f':
			fflg = 1;
			iflg = nflg = 0;
			break;
		case 'n':
			nflg = 1;
			fflg = iflg = 0;
			break;
		case 'v':
			vflg = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	/*
	 * If the stat on the target fails or the target isn't a directory,
	 * try the move.  More than 2 arguments is an error in this case.
	 */
	if (stat(argv[argc - 1], &sb) || !S_ISDIR(sb.st_mode)) {
		if (argc > 2)
			errx(1, "%s is not a directory", argv[argc - 1]);
		exit(do_move(argv[0], argv[1]));
	}

	/*
	 * If -h was specified, treat the target as a symlink instead of
	 * directory.
	 */
	if (hflg) {
		if (argc > 2)
			usage();
		if (lstat(argv[1], &sb) == 0 && S_ISLNK(sb.st_mode))
			exit(do_move(argv[0], argv[1]));
	}

	/* It's a directory, move each file into it. */
	if (strlen(argv[argc - 1]) > sizeof(path) - 1)
		errx(1, "%s: destination pathname too long", *argv);
	(void)strcpy(path, argv[argc - 1]);
	baselen = strlen(path);
	endp = &path[baselen];
	if (!baselen || *(endp - 1) != '/') {
		*endp++ = '/';
		++baselen;
	}
	for (rval = 0; --argc; ++argv) {
		/*
		 * Find the last component of the source pathname.  It
		 * may have trailing slashes.
		 */
		p = *argv + strlen(*argv);
		while (p != *argv && p[-1] == '/')
			--p;
		while (p != *argv && p[-1] != '/')
			--p;

		if ((baselen + (len = strlen(p))) >= PATH_MAX) {
			warnx("%s: destination pathname too long", *argv);
			rval = 1;
		} else {
			memmove(endp, p, (size_t)len + 1);
			if (do_move(*argv, path))
				rval = 1;
		}
	}
	exit(rval);
}

static int
do_move(const char *from, const char *to)
{
	struct stat sb;
	int ask, ch, first;
	char modep[15];

	/*
	 * Check access.  If interactive and file exists, ask user if it
	 * should be replaced.  Otherwise if file exists but isn't writable
	 * make sure the user wants to clobber it.
	 */
	if (!fflg && !access(to, F_OK)) {

		/* prompt only if source exist */
	        if (lstat(from, &sb) == -1) {
			warn("%s", from);
			return (1);
		}

#define YESNO "(y/n [n]) "
		ask = 0;
		if (nflg) {
			if (vflg)
				printf("%s not overwritten\n", to);
			return (0);
		} else if (iflg) {
			(void)fprintf(stderr, "overwrite %s? %s", to, YESNO);
			ask = 1;
		} else if (access(to, W_OK) && !stat(to, &sb) && isatty(STDIN_FILENO)) {
			strmode(sb.st_mode, modep);
			(void)fprintf(stderr, "override %s%s%s/%s for %s? %s",
			    modep + 1, modep[9] == ' ' ? "" : " ",
			    user_from_uid((unsigned long)sb.st_uid, 0),
			    group_from_gid((unsigned long)sb.st_gid, 0), to, YESNO);
			ask = 1;
		}
		if (ask) {
			first = ch = getchar();
			while (ch != '\n' && ch != EOF)
				ch = getchar();
			if (first != 'y' && first != 'Y') {
				(void)fprintf(stderr, "not overwritten\n");
				return (0);
			}
		}
	}
	/*
	 * Rename on FreeBSD will fail with EISDIR and ENOTDIR, before failing
	 * with EXDEV.  Therefore, copy() doesn't have to perform the checks
	 * specified in the Step 3 of the POSIX mv specification.
	 */
	if (!rename(from, to)) {
		if (vflg)
			printf("%s -> %s\n", from, to);
		return (0);
	}

	if (errno == EXDEV) {
		struct statfs sfs;
		char path[PATH_MAX];

		/*
		 * If the source is a symbolic link and is on another
		 * filesystem, it can be recreated at the destination.
		 */
		if (lstat(from, &sb) == -1) {
			warn("%s", from);
			return (1);
		}
		if (!S_ISLNK(sb.st_mode)) {
			/* Can't mv(1) a mount point. */
			if (realpath(from, path) == NULL) {
				warn("cannot resolve %s: %s", from, path);
				return (1);
			}
			if (!statfs(path, &sfs) &&
			    !strcmp(path, sfs.f_mntonname)) {
				warnx("cannot rename a mount point");
				return (1);
			}
		}
	} else {
		warn("rename %s to %s", from, to);
		return (1);
	}

	/*
	 * If rename fails because we're trying to cross devices, and
	 * it's a regular file, do the copy internally; otherwise, use
	 * cp and rm.
	 */
	if (lstat(from, &sb)) {
		warn("%s", from);
		return (1);
	}
	return (S_ISREG(sb.st_mode) ?
	    fastcopy(from, to, &sb) : copy(from, to));
}

static int
fastcopy(const char *from, const char *to, struct stat *sbp)
{
	struct timespec ts[2];
	static u_int blen = MAXPHYS;
	static char *bp = NULL;
	mode_t oldmode;
	int nread, from_fd, to_fd;
	struct stat tsb;

	if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
		warn("fastcopy: open() failed (from): %s", from);
		return (1);
	}
	if (bp == NULL && (bp = malloc((size_t)blen)) == NULL) {
		warnx("malloc(%u) failed", blen);
		(void)close(from_fd);
		return (1);
	}
	while ((to_fd =
	    open(to, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0)) < 0) {
		if (errno == EEXIST && unlink(to) == 0)
			continue;
		warn("fastcopy: open() failed (to): %s", to);
		(void)close(from_fd);
		return (1);
	}
	while ((nread = read(from_fd, bp, (size_t)blen)) > 0)
		if (write(to_fd, bp, (size_t)nread) != nread) {
			warn("fastcopy: write() failed: %s", to);
			goto err;
		}
	if (nread < 0) {
		warn("fastcopy: read() failed: %s", from);
err:		if (unlink(to))
			warn("%s: remove", to);
		(void)close(from_fd);
		(void)close(to_fd);
		return (1);
	}

	oldmode = sbp->st_mode & ALLPERMS;
	if (fchown(to_fd, sbp->st_uid, sbp->st_gid)) {
		warn("%s: set owner/group (was: %lu/%lu)", to,
		    (u_long)sbp->st_uid, (u_long)sbp->st_gid);
		if (oldmode & (S_ISUID | S_ISGID)) {
			warnx(
"%s: owner/group changed; clearing suid/sgid (mode was 0%03o)",
			    to, oldmode);
			sbp->st_mode &= ~(S_ISUID | S_ISGID);
		}
	}
	if (fchmod(to_fd, sbp->st_mode))
		warn("%s: set mode (was: 0%03o)", to, oldmode);
	/*
	 * POSIX 1003.2c states that if _POSIX_ACL_EXTENDED is in effect
	 * for dest_file, then its ACLs shall reflect the ACLs of the
	 * source_file.
	 */
	preserve_fd_acls(from_fd, to_fd, from, to);
	(void)close(from_fd);
	/*
	 * XXX
	 * NFS doesn't support chflags; ignore errors unless there's reason
	 * to believe we're losing bits.  (Note, this still won't be right
	 * if the server supports flags and we were trying to *remove* flags
	 * on a file that we copied, i.e., that we didn't create.)
	 */
	if (fstat(to_fd, &tsb) == 0) {
		if ((sbp->st_flags  & ~UF_ARCHIVE) !=
		    (tsb.st_flags & ~UF_ARCHIVE)) {
			if (fchflags(to_fd,
			    sbp->st_flags | (tsb.st_flags & UF_ARCHIVE)))
				if (errno != EOPNOTSUPP ||
				    ((sbp->st_flags & ~UF_ARCHIVE) != 0))
					warn("%s: set flags (was: 0%07o)",
					    to, sbp->st_flags);
		}
	} else
		warn("%s: cannot stat", to);

	ts[0] = sbp->st_atim;
	ts[1] = sbp->st_mtim;
	if (futimens(to_fd, ts))
		warn("%s: set times", to);

	if (close(to_fd)) {
		warn("%s", to);
		return (1);
	}

	if (unlink(from)) {
		warn("%s: remove", from);
		return (1);
	}
	if (vflg)
		printf("%s -> %s\n", from, to);
	return (0);
}

static int
copy(const char *from, const char *to)
{
	struct stat sb;
	int pid, status;

	if (lstat(to, &sb) == 0) {
		/* Destination path exists. */
		if (S_ISDIR(sb.st_mode)) {
			if (rmdir(to) != 0) {
				warn("rmdir %s", to);
				return (1);
			}
		} else {
			if (unlink(to) != 0) {
				warn("unlink %s", to);
				return (1);
			}
		}
	} else if (errno != ENOENT) {
		warn("%s", to);
		return (1);
	}

	/* Copy source to destination. */
	if (!(pid = vfork())) {
		execl(_PATH_CP, "mv", vflg ? "-PRpv" : "-PRp", "--", from, to,
		    (char *)NULL);
		_exit(EXEC_FAILED);
	}
	if (waitpid(pid, &status, 0) == -1) {
		warn("%s %s %s: waitpid", _PATH_CP, from, to);
		return (1);
	}
	if (!WIFEXITED(status)) {
		warnx("%s %s %s: did not terminate normally",
		    _PATH_CP, from, to);
		return (1);
	}
	switch (WEXITSTATUS(status)) {
	case 0:
		break;
	case EXEC_FAILED:
		warnx("%s %s %s: exec failed", _PATH_CP, from, to);
		return (1);
	default:
		warnx("%s %s %s: terminated with %d (non-zero) status",
		    _PATH_CP, from, to, WEXITSTATUS(status));
		return (1);
	}

	/* Delete the source. */
	if (!(pid = vfork())) {
		execl(_PATH_RM, "mv", "-rf", "--", from, (char *)NULL);
		_exit(EXEC_FAILED);
	}
	if (waitpid(pid, &status, 0) == -1) {
		warn("%s %s: waitpid", _PATH_RM, from);
		return (1);
	}
	if (!WIFEXITED(status)) {
		warnx("%s %s: did not terminate normally", _PATH_RM, from);
		return (1);
	}
	switch (WEXITSTATUS(status)) {
	case 0:
		break;
	case EXEC_FAILED:
		warnx("%s %s: exec failed", _PATH_RM, from);
		return (1);
	default:
		warnx("%s %s: terminated with %d (non-zero) status",
		    _PATH_RM, from, WEXITSTATUS(status));
		return (1);
	}
	return (0);
}

static void
preserve_fd_acls(int source_fd, int dest_fd, const char *source_path,
    const char *dest_path)
{
	acl_t acl;
	acl_type_t acl_type;
	int acl_supported = 0, ret, trivial;

	ret = fpathconf(source_fd, _PC_ACL_NFS4);
	if (ret > 0 ) {
		acl_supported = 1;
		acl_type = ACL_TYPE_NFS4;
	} else if (ret < 0 && errno != EINVAL) {
		warn("fpathconf(..., _PC_ACL_NFS4) failed for %s",
		    source_path);
		return;
	}
	if (acl_supported == 0) {
		ret = fpathconf(source_fd, _PC_ACL_EXTENDED);
		if (ret > 0 ) {
			acl_supported = 1;
			acl_type = ACL_TYPE_ACCESS;
		} else if (ret < 0 && errno != EINVAL) {
			warn("fpathconf(..., _PC_ACL_EXTENDED) failed for %s",
			    source_path);
			return;
		}
	}
	if (acl_supported == 0)
		return;

	acl = acl_get_fd_np(source_fd, acl_type);
	if (acl == NULL) {
		warn("failed to get acl entries for %s", source_path);
		return;
	}
	if (acl_is_trivial_np(acl, &trivial)) {
		warn("acl_is_trivial() failed for %s", source_path);
		acl_free(acl);
		return;
	}
	if (trivial) {
		acl_free(acl);
		return;
	}
	if (acl_set_fd_np(dest_fd, acl, acl_type) < 0) {
		warn("failed to set acl entries for %s", dest_path);
		acl_free(acl);
		return;
	}
	acl_free(acl);
}

static void
usage(void)
{

	(void)fprintf(stderr, "%s\n%s\n",
		      "usage: mv [-f | -i | -n] [-hv] source target",
		      "       mv [-f | -i | -n] [-v] source ... directory");
	exit(EX_USAGE);
}

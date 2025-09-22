/*	$OpenBSD: utils.c,v 1.50 2021/11/28 19:28:41 deraadt Exp $	*/
/*	$NetBSD: utils.c,v 1.6 1997/02/26 14:40:51 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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
#include <sys/mman.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "extern.h"

#define _MAXBSIZE	(64 * 1024)

int copy_overwrite(void);

int
copy_file(FTSENT *entp, int exists)
{
	static char *buf;
	static char *zeroes;
	struct stat to_stat, *fs;
	int from_fd, rcount, rval, to_fd, wcount;
	const size_t buflen = _MAXBSIZE;
#ifdef VM_AND_BUFFER_CACHE_SYNCHRONIZED
	char *p;
#endif

	if (!buf) {
		buf = malloc(buflen);
		if (!buf)
			err(1, "malloc");
	}
	if (!zeroes) {
		zeroes = calloc(1, buflen);
		if (!zeroes)
			err(1, "calloc");
	}

	if ((from_fd = open(entp->fts_path, O_RDONLY)) == -1) {
		warn("%s", entp->fts_path);
		return (1);
	}

	fs = entp->fts_statp;

	/*
	 * In -f (force) mode, we always unlink the destination first
	 * if it exists.  Note that -i and -f are mutually exclusive.
	 */
	if (exists && fflag)
		(void)unlink(to.p_path);

	/*
	 * If the file DNE, set the mode to be the from file, minus setuid
	 * bits, modified by the umask; arguably wrong, but it makes copying
	 * executables work right and it's been that way forever.  (The
	 * other choice is 666 or'ed with the execute bits on the from file
	 * modified by the umask.)
	 */
	if (exists && !fflag) {
		if (!copy_overwrite()) {
			(void)close(from_fd);
			return 2;
 		}
		to_fd = open(to.p_path, O_WRONLY | O_TRUNC);
	} else
		to_fd = open(to.p_path, O_WRONLY | O_TRUNC | O_CREAT,
		    fs->st_mode & ~(S_ISTXT | S_ISUID | S_ISGID));

	if (to_fd == -1) {
		warn("%s", to.p_path);
		(void)close(from_fd);
		return (1);
	}

	rval = 0;

	/*
	 * Mmap and write if less than 8M (the limit is so we don't totally
	 * trash memory on big files.  This is really a minor hack, but it
	 * wins some CPU back.
	 */
#ifdef VM_AND_BUFFER_CACHE_SYNCHRONIZED
	/* XXX broken for 0-size mmap */
	if (fs->st_size <= 8 * 1048576) {
		if ((p = mmap(NULL, (size_t)fs->st_size, PROT_READ,
		    MAP_FILE|MAP_SHARED, from_fd, (off_t)0)) == MAP_FAILED) {
			warn("mmap: %s", entp->fts_path);
			rval = 1;
		} else {
			madvise(p, fs->st_size, MADV_SEQUENTIAL);
			if (write(to_fd, p, fs->st_size) != fs->st_size) {
				warn("%s", to.p_path);
				rval = 1;
			}
			/* Some systems don't unmap on close(2). */
			if (munmap(p, fs->st_size) == -1) {
				warn("%s", entp->fts_path);
				rval = 1;
			}
		}
	} else
#endif
	{
		int skipholes = 0;
		struct stat tosb;
		if (!fstat(to_fd, &tosb) && S_ISREG(tosb.st_mode))
			skipholes = 1;
		while ((rcount = read(from_fd, buf, buflen)) > 0) {
			if (skipholes && memcmp(buf, zeroes, rcount) == 0)
				wcount = lseek(to_fd, rcount, SEEK_CUR) == -1 ? -1 : rcount;
			else
				wcount = write(to_fd, buf, rcount);
			if (rcount != wcount || wcount == -1) {
				warn("%s", to.p_path);
				rval = 1;
				break;
			}
		}
		if (skipholes && rcount != -1)
			rcount = ftruncate(to_fd, lseek(to_fd, 0, SEEK_CUR));
		if (rcount == -1) {
			warn("%s", entp->fts_path);
			rval = 1;
		}
	}

	if (rval == 1) {
		(void)close(from_fd);
		(void)close(to_fd);
		return (1);
	}

	if (pflag && setfile(fs, to_fd))
		rval = 1;
	/*
	 * If the source was setuid or setgid, lose the bits unless the
	 * copy is owned by the same user and group.
	 */
#define	RETAINBITS \
	(S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)
	if (!pflag && !exists &&
	    fs->st_mode & (S_ISUID | S_ISGID) && fs->st_uid == myuid) {
		if (fstat(to_fd, &to_stat)) {
			warn("%s", to.p_path);
			rval = 1;
		} else if (fs->st_gid == to_stat.st_gid &&
		    fchmod(to_fd, fs->st_mode & RETAINBITS & ~myumask)) {
			warn("%s", to.p_path);
			rval = 1;
		}
	}
	(void)close(from_fd);
	if (close(to_fd)) {
		warn("%s", to.p_path);
		rval = 1;
	}
	return (rval);
}

int
copy_link(FTSENT *p, int exists)
{
	int len;
	char name[PATH_MAX];

	if (exists && !copy_overwrite())
		return (2);
	if ((len = readlink(p->fts_path, name, sizeof(name)-1)) == -1) {
		warn("readlink: %s", p->fts_path);
		return (1);
	}
	name[len] = '\0';
	if (exists && unlink(to.p_path)) {
		warn("unlink: %s", to.p_path);
		return (1);
	}
	if (symlink(name, to.p_path)) {
		warn("symlink: %s", name);
		return (1);
	}
	return (pflag ? setfile(p->fts_statp, -1) : 0);
}

int
copy_fifo(struct stat *from_stat, int exists)
{
	if (exists && !copy_overwrite())
		return (2);
	if (exists && unlink(to.p_path)) {
		warn("unlink: %s", to.p_path);
		return (1);
	}
	if (mkfifo(to.p_path, from_stat->st_mode)) {
		warn("mkfifo: %s", to.p_path);
		return (1);
	}
	return (pflag ? setfile(from_stat, -1) : 0);
}

int
copy_special(struct stat *from_stat, int exists)
{
	if (exists && !copy_overwrite())
		return (2);
	if (exists && unlink(to.p_path)) {
		warn("unlink: %s", to.p_path);
		return (1);
	}
	if (mknod(to.p_path, from_stat->st_mode, from_stat->st_rdev)) {
		warn("mknod: %s", to.p_path);
		return (1);
	}
	return (pflag ? setfile(from_stat, -1) : 0);
}

/*
 * If the file exists and we're interactive, verify with the user.
 */
int
copy_overwrite(void)
{
	int ch, checkch;

	if (iflag) {
		(void)fprintf(stderr, "overwrite %s? ", to.p_path);
		checkch = ch = getchar();
		while (ch != '\n' && ch != EOF)
			ch = getchar();
		if (checkch != 'y' && checkch != 'Y')
			return (0);
	}
	return 1;
}

int
setfile(struct stat *fs, int fd)
{
	struct timespec ts[2];
	int rval;

	rval = 0;
	fs->st_mode &= S_ISTXT | S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO;

	ts[0] = fs->st_atim;
	ts[1] = fs->st_mtim;
	if (fd >= 0 ? futimens(fd, ts) :
	    utimensat(AT_FDCWD, to.p_path, ts, AT_SYMLINK_NOFOLLOW)) {
		warn("update times: %s", to.p_path);
		rval = 1;
	}
	/*
	 * Changing the ownership probably won't succeed, unless we're root
	 * or POSIX_CHOWN_RESTRICTED is not set.  Set uid/gid before setting
	 * the mode; current BSD behavior is to remove all setuid bits on
	 * chown.  If chown fails, lose setuid/setgid bits.
	 */
	if (fd >= 0 ? fchown(fd, fs->st_uid, fs->st_gid) :
	    lchown(to.p_path, fs->st_uid, fs->st_gid)) {
		if (errno != EPERM) {
			warn("chown: %s", to.p_path);
			rval = 1;
		}
		fs->st_mode &= ~(S_ISTXT | S_ISUID | S_ISGID);
	}
	if (fd >= 0 ? fchmod(fd, fs->st_mode) :
	    fchmodat(AT_FDCWD, to.p_path, fs->st_mode, AT_SYMLINK_NOFOLLOW)) {
		warn("chmod: %s", to.p_path);
		rval = 1;
	}

	/*
	 * XXX
	 * NFS doesn't support chflags; ignore errors unless there's reason
	 * to believe we're losing bits.  (Note, this still won't be right
	 * if the server supports flags and we were trying to *remove* flags
	 * on a file that we copied, i.e., that we didn't create.)
	 */
	errno = 0;
	if (fd >= 0 ? fchflags(fd, fs->st_flags) :
	    chflagsat(AT_FDCWD, to.p_path, fs->st_flags, AT_SYMLINK_NOFOLLOW))
		if (errno != EOPNOTSUPP || fs->st_flags != 0) {
			warn("chflags: %s", to.p_path);
			rval = 1;
		}
	return (rval);
}


void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: %s [-afipv] [-R [-H | -L | -P]] source target\n", __progname);
	(void)fprintf(stderr,
	    "       %s [-afipv] [-R [-H | -L | -P]] source ... directory\n",
	    __progname);
	exit(1);
}

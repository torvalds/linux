/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
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
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)cat.c	8.2 (Berkeley) 4/27/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#ifndef NO_UDOM_SUPPORT
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#endif

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static int bflag, eflag, lflag, nflag, sflag, tflag, vflag;
static int rval;
static const char *filename;

static void usage(void) __dead2;
static void scanfiles(char *argv[], int cooked);
static void cook_cat(FILE *);
static void raw_cat(int);

#ifndef NO_UDOM_SUPPORT
static int udom_open(const char *path, int flags);
#endif

/*
 * Memory strategy threshold, in pages: if physmem is larger than this,
 * use a large buffer.
 */
#define	PHYSPAGES_THRESHOLD (32 * 1024)

/* Maximum buffer size in bytes - do not allow it to grow larger than this. */
#define	BUFSIZE_MAX (2 * 1024 * 1024)

/*
 * Small (default) buffer size in bytes. It's inefficient for this to be
 * smaller than MAXPHYS.
 */
#define	BUFSIZE_SMALL (MAXPHYS)

int
main(int argc, char *argv[])
{
	int ch;
	struct flock stdout_lock;

	setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "belnstuv")) != -1)
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'l':
			lflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'u':
			setbuf(stdout, NULL);
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	argv += optind;

	if (lflag) {
		stdout_lock.l_len = 0;
		stdout_lock.l_start = 0;
		stdout_lock.l_type = F_WRLCK;
		stdout_lock.l_whence = SEEK_SET;
		if (fcntl(STDOUT_FILENO, F_SETLKW, &stdout_lock) == -1)
			err(EXIT_FAILURE, "stdout");
	}

	if (bflag || eflag || nflag || sflag || tflag || vflag)
		scanfiles(argv, 1);
	else
		scanfiles(argv, 0);
	if (fclose(stdout))
		err(1, "stdout");
	exit(rval);
	/* NOTREACHED */
}

static void
usage(void)
{

	fprintf(stderr, "usage: cat [-belnstuv] [file ...]\n");
	exit(1);
	/* NOTREACHED */
}

static void
scanfiles(char *argv[], int cooked)
{
	int fd, i;
	char *path;
	FILE *fp;

	i = 0;
	fd = -1;
	while ((path = argv[i]) != NULL || i == 0) {
		if (path == NULL || strcmp(path, "-") == 0) {
			filename = "stdin";
			fd = STDIN_FILENO;
		} else {
			filename = path;
			fd = open(path, O_RDONLY);
#ifndef NO_UDOM_SUPPORT
			if (fd < 0 && errno == EOPNOTSUPP)
				fd = udom_open(path, O_RDONLY);
#endif
		}
		if (fd < 0) {
			warn("%s", path);
			rval = 1;
		} else if (cooked) {
			if (fd == STDIN_FILENO)
				cook_cat(stdin);
			else {
				fp = fdopen(fd, "r");
				cook_cat(fp);
				fclose(fp);
			}
		} else {
			raw_cat(fd);
			if (fd != STDIN_FILENO)
				close(fd);
		}
		if (path == NULL)
			break;
		++i;
	}
}

static void
cook_cat(FILE *fp)
{
	int ch, gobble, line, prev;
	wint_t wch;

	/* Reset EOF condition on stdin. */
	if (fp == stdin && feof(stdin))
		clearerr(stdin);

	line = gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (sflag) {
				if (ch == '\n') {
					if (gobble)
						continue;
					gobble = 1;
				} else
					gobble = 0;
			}
			if (nflag) {
				if (!bflag || ch != '\n') {
					(void)fprintf(stdout, "%6d\t", ++line);
					if (ferror(stdout))
						break;
				} else if (eflag) {
					(void)fprintf(stdout, "%6s\t", "");
					if (ferror(stdout))
						break;
				}
			}
		}
		if (ch == '\n') {
			if (eflag && putchar('$') == EOF)
				break;
		} else if (ch == '\t') {
			if (tflag) {
				if (putchar('^') == EOF || putchar('I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			(void)ungetc(ch, fp);
			/*
			 * Our getwc(3) doesn't change file position
			 * on error.
			 */
			if ((wch = getwc(fp)) == WEOF) {
				if (ferror(fp) && errno == EILSEQ) {
					clearerr(fp);
					/* Resync attempt. */
					memset(&fp->_mbstate, 0, sizeof(mbstate_t));
					if ((ch = getc(fp)) == EOF)
						break;
					wch = ch;
					goto ilseq;
				} else
					break;
			}
			if (!iswascii(wch) && !iswprint(wch)) {
ilseq:
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				wch = toascii(wch);
			}
			if (iswcntrl(wch)) {
				ch = toascii(wch);
				ch = (ch == '\177') ? '?' : (ch | 0100);
				if (putchar('^') == EOF || putchar(ch) == EOF)
					break;
				continue;
			}
			if (putwchar(wch) == WEOF)
				break;
			ch = -1;
			continue;
		}
		if (putchar(ch) == EOF)
			break;
	}
	if (ferror(fp)) {
		warn("%s", filename);
		rval = 1;
		clearerr(fp);
	}
	if (ferror(stdout))
		err(1, "stdout");
}

static void
raw_cat(int rfd)
{
	long pagesize;
	int off, wfd;
	ssize_t nr, nw;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (buf == NULL) {
		if (fstat(wfd, &sbuf))
			err(1, "stdout");
		if (S_ISREG(sbuf.st_mode)) {
			/* If there's plenty of RAM, use a large copy buffer */
			if (sysconf(_SC_PHYS_PAGES) > PHYSPAGES_THRESHOLD)
				bsize = MIN(BUFSIZE_MAX, MAXPHYS * 8);
			else
				bsize = BUFSIZE_SMALL;
		} else {
			bsize = sbuf.st_blksize;
			pagesize = sysconf(_SC_PAGESIZE);
			if (pagesize > 0)
				bsize = MAX(bsize, (size_t)pagesize);
		}
		if ((buf = malloc(bsize)) == NULL)
			err(1, "malloc() failure of IO buffer");
	}
	while ((nr = read(rfd, buf, bsize)) > 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, (size_t)nr)) < 0)
				err(1, "stdout");
	if (nr < 0) {
		warn("%s", filename);
		rval = 1;
	}
}

#ifndef NO_UDOM_SUPPORT

static int
udom_open(const char *path, int flags)
{
	struct addrinfo hints, *res, *res0;
	char rpath[PATH_MAX];
	int fd = -1;
	int error;

	/*
	 * Construct the unix domain socket address and attempt to connect.
	 */
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_LOCAL;
	if (realpath(path, rpath) == NULL)
		return (-1);
	error = getaddrinfo(rpath, NULL, &hints, &res0);
	if (error) {
		warn("%s", gai_strerror(error));
		errno = EINVAL;
		return (-1);
	}
	for (res = res0; res != NULL; res = res->ai_next) {
		fd = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (fd < 0) {
			freeaddrinfo(res0);
			return (-1);
		}
		error = connect(fd, res->ai_addr, res->ai_addrlen);
		if (error == 0)
			break;
		else {
			close(fd);
			fd = -1;
		}
	}
	freeaddrinfo(res0);

	/*
	 * handle the open flags by shutting down appropriate directions
	 */
	if (fd >= 0) {
		switch(flags & O_ACCMODE) {
		case O_RDONLY:
			if (shutdown(fd, SHUT_WR) == -1)
				warn(NULL);
			break;
		case O_WRONLY:
			if (shutdown(fd, SHUT_RD) == -1)
				warn(NULL);
			break;
		default:
			break;
		}
	}
	return (fd);
}

#endif

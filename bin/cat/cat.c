/*	$OpenBSD: cat.c,v 1.34 2022/02/09 01:58:57 cheloha Exp $	*/
/*	$NetBSD: cat.c,v 1.11 1995/09/07 06:12:54 jtc Exp $	*/

/*
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

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

int bflag, eflag, nflag, sflag, tflag, vflag;
int rval;

void cat_file(const char *);
void cook_buf(FILE *, const char *);
void raw_cat(int, const char *);

int
main(int argc, char *argv[])
{
	int ch;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "benstuv")) != -1) {
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
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
			setvbuf(stdout, NULL, _IONBF, 0);
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [-benstuv] [file ...]\n",
			    getprogname());
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");

		cat_file(NULL);
	} else {
		for (; *argv != NULL; argv++)
			cat_file(*argv);
	}
	if (fclose(stdout))
		err(1, "stdout");
	return rval;
}

void
cat_file(const char *path)
{
	FILE *fp;
	int fd;

	if (bflag || eflag || nflag || sflag || tflag || vflag) {
		if (path == NULL || strcmp(path, "-") == 0) {
			cook_buf(stdin, "stdin");
			clearerr(stdin);
		} else {
			if ((fp = fopen(path, "r")) == NULL) {
				warn("%s", path);
				rval = 1;
				return;
			}
			cook_buf(fp, path);
			fclose(fp);
		}
	} else {
		if (path == NULL || strcmp(path, "-") == 0) {
			raw_cat(STDIN_FILENO, "stdin");
		} else {
			if ((fd = open(path, O_RDONLY)) == -1) {
				warn("%s", path);
				rval = 1;
				return;
			}
			raw_cat(fd, path);
			close(fd);
		}
	}
}

void
cook_buf(FILE *fp, const char *filename)
{
	unsigned long long line;
	int ch, gobble, prev;

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
					fprintf(stdout, "%6llu\t", ++line);
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
			if (!isascii(ch)) {
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (putchar('^') == EOF ||
				    putchar(ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
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

void
raw_cat(int rfd, const char *filename)
{
	int wfd;
	ssize_t nr, nw, off;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (buf == NULL) {
		if (fstat(wfd, &sbuf) == -1)
			err(1, "stdout");
		bsize = MAXIMUM(sbuf.st_blksize, BUFSIZ);
		if ((buf = malloc(bsize)) == NULL)
			err(1, NULL);
	}
	while ((nr = read(rfd, buf, bsize)) != -1 && nr != 0) {
		for (off = 0; nr; nr -= nw, off += nw) {
			if ((nw = write(wfd, buf + off, nr)) == -1 || nw == 0)
				err(1, "stdout");
		}
	}
	if (nr == -1) {
		warn("%s", filename);
		rval = 1;
	}
}

/*	$OpenBSD: print.c,v 1.41 2024/03/27 14:44:52 millert Exp $	*/
/*	$NetBSD: print.c,v 1.15 1996/12/11 03:25:39 thorpej Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Fischbein.
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
#include <fts.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

#include "ls.h"
#include "extern.h"

static int	printaname(FTSENT *, int, int);
static void	printlink(FTSENT *);
static void	printsize(int, off_t);
static void	printtime(time_t);
static int	printtype(mode_t);
static int	compute_columns(DISPLAY *, int *);

#define	IS_NOPRINT(p)	((p)->fts_number == NO_PRINT)

#define	DATELEN		64

#define	SECSPERDAY	(24 * 60 * 60)
#define	SIXMONTHS	(SECSPERDAY * 365 / 2)

void
printscol(DISPLAY *dp)
{
	FTSENT *p;

	for (p = dp->list; p; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		(void)printaname(p, dp->s_inode, dp->s_block);
		(void)putchar('\n');
	}
}

void
printlong(DISPLAY *dp)
{
	struct stat *sp;
	FTSENT *p;
	NAMES *np;
	char buf[20];

	if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
	    (f_longform || f_size))
		(void)printf("total %llu\n", howmany(dp->btotal, blocksize));

	for (p = dp->list; p; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		sp = p->fts_statp;
		if (f_inode)
			(void)printf("%*llu ", dp->s_inode,
			    (unsigned long long)sp->st_ino);
		if (f_size)
			(void)printf("%*lld ", dp->s_block,
			    howmany((long long)sp->st_blocks, blocksize));
		(void)strmode(sp->st_mode, buf);
		np = p->fts_pointer;
		(void)printf("%s %*u ", buf, dp->s_nlink, sp->st_nlink);
		if (!f_grouponly)
			(void)printf("%-*s  ", dp->s_user, np->user);
		(void)printf("%-*s  ", dp->s_group, np->group);
		if (f_flags)
			(void)printf("%-*s ", dp->s_flags, np->flags);
		if (S_ISCHR(sp->st_mode) || S_ISBLK(sp->st_mode))
			(void)printf("%*u, %*u ",
			    dp->s_major, major(sp->st_rdev),
			    dp->s_minor, minor(sp->st_rdev));
		else
			printsize(dp->s_size, sp->st_size);
		if (f_accesstime)
			printtime(sp->st_atime);
		else if (f_statustime)
			printtime(sp->st_ctime);
		else
			printtime(sp->st_mtime);
		(void)mbsprint(p->fts_name, 1);
		if (f_type || (f_typedir && S_ISDIR(sp->st_mode)))
			(void)printtype(sp->st_mode);
		if (S_ISLNK(sp->st_mode))
			printlink(p);
		(void)putchar('\n');
	}
}

static int
compute_columns(DISPLAY *dp, int *pnum)
{
	int colwidth;
	extern int termwidth;
	int mywidth;

	colwidth = dp->maxlen;
	if (f_inode)
		colwidth += dp->s_inode + 1;
	if (f_size)
		colwidth += dp->s_block + 1;
	if (f_type || f_typedir)
		colwidth += 1;

	colwidth += 1;
	mywidth = termwidth + 1;	/* no extra space for last column */

	if (mywidth < 2 * colwidth) {
		printscol(dp);
		return (0);
	}

	*pnum = mywidth / colwidth;
	return (mywidth / *pnum);		/* spread out if possible */
}

void
printcol(DISPLAY *dp)
{
	static FTSENT **array;
	static int lastentries = -1;
	FTSENT *p;
	int base, chcnt, col, colwidth, num;
	int numcols, numrows, row;

	if ((colwidth = compute_columns(dp, &numcols)) == 0)
		return;
	/*
	 * Have to do random access in the linked list -- build a table
	 * of pointers.
	 */
	if (dp->entries > lastentries) {
		FTSENT **a;

		if ((a = reallocarray(array, dp->entries, sizeof(FTSENT *))) ==
		    NULL) {
			free(array);
			array = NULL;
			dp->entries = 0;
			lastentries = -1;
			warn(NULL);
			printscol(dp);
			return;
		}
		lastentries = dp->entries;
		array = a;
	}
	for (p = dp->list, num = 0; p; p = p->fts_link)
		if (p->fts_number != NO_PRINT)
			array[num++] = p;

	numrows = num / numcols;
	if (num % numcols)
		++numrows;

	if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
	    (f_longform || f_size))
		(void)printf("total %llu\n", howmany(dp->btotal, blocksize));
	for (row = 0; row < numrows; ++row) {
		for (base = row, col = 0;;) {
			chcnt = printaname(array[base], dp->s_inode, dp->s_block);
			if ((base += numrows) >= num)
				break;
			if (++col == numcols)
				break;
			while (chcnt++ < colwidth)
				putchar(' ');
		}
		(void)putchar('\n');
	}
}

/*
 * print [inode] [size] name
 * return # of characters printed, no trailing characters.
 */
static int
printaname(FTSENT *p, int inodefield, int sizefield)
{
	struct stat *sp;
	int chcnt;

	sp = p->fts_statp;
	chcnt = 0;
	if (f_inode)
		chcnt += printf("%*llu ", inodefield,
		    (unsigned long long)sp->st_ino);
	if (f_size)
		chcnt += printf("%*lld ", sizefield,
		    howmany((long long)sp->st_blocks, blocksize));
	chcnt += mbsprint(p->fts_name, 1);
	if (f_type || (f_typedir && S_ISDIR(sp->st_mode)))
		chcnt += printtype(sp->st_mode);
	return (chcnt);
}

static void
printtime(time_t ftime)
{
	char f_date[DATELEN];
	struct tm *tm;
	static time_t now;
	static int now_set = 0;

	if (! now_set) {
		now = time(NULL);
		now_set = 1;
	}

	/*
	 * convert time to string, and print
	 */
	if ((tm = localtime(&ftime)) == NULL) {
		/* Invalid time stamp, just display the epoch. */
		ftime = 0;
		tm = localtime(&ftime);
	}
	if (strftime(f_date, sizeof(f_date), f_sectime ? "%b %e %H:%M:%S %Y" :
	    (ftime <= now - SIXMONTHS || ftime > now) ? "%b %e  %Y" :
	    "%b %e %H:%M", tm) == 0)
		f_date[0] = '\0';

	printf("%s ", f_date);
}

void
printacol(DISPLAY *dp)
{
	FTSENT *p;
	int chcnt, col, colwidth;
	int numcols;

	if ( (colwidth = compute_columns(dp, &numcols)) == 0)
		return;

	if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
	    (f_longform || f_size))
		(void)printf("total %llu\n", howmany(dp->btotal, blocksize));
	col = 0;
	for (p = dp->list; p; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		if (col >= numcols) {
			col = 0;
			(void)putchar('\n');
		}
		chcnt = printaname(p, dp->s_inode, dp->s_block);
		col++;
		if (col < numcols)
			while (chcnt++ < colwidth)
				(void)putchar(' ');
	}
	(void)putchar('\n');
}

void
printstream(DISPLAY *dp)
{
	extern int termwidth;
	FTSENT *p;
	int col;
	int extwidth;

	extwidth = 0;
	if (f_inode)
		extwidth += dp->s_inode + 1;
	if (f_size)
		extwidth += dp->s_block + 1;
	if (f_type)
		extwidth += 1;

	for (col = 0, p = dp->list; p != NULL; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		if (col > 0) {
			(void)putchar(','), col++;
			if (col + 1 + extwidth + mbsprint(p->fts_name, 0) >=
			    termwidth)
				(void)putchar('\n'), col = 0;
			else
				(void)putchar(' '), col++;
		}
		col += printaname(p, dp->s_inode, dp->s_block);
	}
	(void)putchar('\n');
}

static int
printtype(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFDIR:
		(void)putchar('/');
		return (1);
	case S_IFIFO:
		(void)putchar('|');
		return (1);
	case S_IFLNK:
		(void)putchar('@');
		return (1);
	case S_IFSOCK:
		(void)putchar('=');
		return (1);
	}
	if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		(void)putchar('*');
		return (1);
	}
	return (0);
}

static void
printlink(FTSENT *p)
{
	int lnklen;
	char name[PATH_MAX], path[PATH_MAX];

	if (p->fts_level == FTS_ROOTLEVEL)
		(void)snprintf(name, sizeof(name), "%s", p->fts_name);
	else
		(void)snprintf(name, sizeof(name),
		    "%s/%s", p->fts_parent->fts_accpath, p->fts_name);
	if ((lnklen = readlink(name, path, sizeof(path) - 1)) == -1) {
		(void)fprintf(stderr, "\nls: %s: %s\n", name, strerror(errno));
		return;
	}
	path[lnklen] = '\0';
	(void)printf(" -> ");
	(void)mbsprint(path, 1);
}

static void
printsize(int width, off_t bytes)
{
	char ret[FMT_SCALED_STRSIZE];

	if ((f_humanval) && (fmt_scaled(bytes, ret) != -1)) {
		(void)printf("%*s ", width, ret);
		return;
	}
	(void)printf("%*lld ", width, (long long)bytes);
}

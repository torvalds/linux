/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)print.c	8.4 (Berkeley) 4/17/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/acl.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <langinfo.h>
#include <libutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#ifdef COLORLS
#include <ctype.h>
#include <termcap.h>
#include <signal.h>
#endif

#include "ls.h"
#include "extern.h"

static int	printaname(const FTSENT *, u_long, u_long);
static void	printdev(size_t, dev_t);
static void	printlink(const FTSENT *);
static void	printtime(time_t);
static int	printtype(u_int);
static void	printsize(size_t, off_t);
#ifdef COLORLS
static void	endcolor_termcap(int);
static void	endcolor_ansi(void);
static void	endcolor(int);
static int	colortype(mode_t);
#endif
static void	aclmode(char *, const FTSENT *);

#define	IS_NOPRINT(p)	((p)->fts_number == NO_PRINT)

#ifdef COLORLS
/* Most of these are taken from <sys/stat.h> */
typedef enum Colors {
	C_DIR,			/* directory */
	C_LNK,			/* symbolic link */
	C_SOCK,			/* socket */
	C_FIFO,			/* pipe */
	C_EXEC,			/* executable */
	C_BLK,			/* block special */
	C_CHR,			/* character special */
	C_SUID,			/* setuid executable */
	C_SGID,			/* setgid executable */
	C_WSDIR,		/* directory writeble to others, with sticky
				 * bit */
	C_WDIR,			/* directory writeble to others, without
				 * sticky bit */
	C_NUMCOLORS		/* just a place-holder */
} Colors;

static const char *defcolors = "exfxcxdxbxegedabagacad";

/* colors for file types */
static struct {
	int	num[2];
	int	bold;
} colors[C_NUMCOLORS];
#endif

static size_t padding_for_month[12];
static size_t month_max_size = 0;

void
printscol(const DISPLAY *dp)
{
	FTSENT *p;

	for (p = dp->list; p; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		(void)printaname(p, dp->s_inode, dp->s_block);
		(void)putchar('\n');
	}
}

/*
 * print name in current style
 */
int
printname(const char *name)
{
	if (f_octal || f_octal_escape)
		return prn_octal(name);
	else if (f_nonprint)
		return prn_printable(name);
	else
		return prn_normal(name);
}

static const char *
get_abmon(int mon)
{

	switch (mon) {
	case 0: return (nl_langinfo(ABMON_1));
	case 1: return (nl_langinfo(ABMON_2));
	case 2: return (nl_langinfo(ABMON_3));
	case 3: return (nl_langinfo(ABMON_4));
	case 4: return (nl_langinfo(ABMON_5));
	case 5: return (nl_langinfo(ABMON_6));
	case 6: return (nl_langinfo(ABMON_7));
	case 7: return (nl_langinfo(ABMON_8));
	case 8: return (nl_langinfo(ABMON_9));
	case 9: return (nl_langinfo(ABMON_10));
	case 10: return (nl_langinfo(ABMON_11));
	case 11: return (nl_langinfo(ABMON_12));
	}

	/* should never happen */
	abort();
}

static size_t
mbswidth(const char *month)
{
	wchar_t wc;
	size_t width, donelen, clen, w;

	width = donelen = 0;
	while ((clen = mbrtowc(&wc, month + donelen, MB_LEN_MAX, NULL)) != 0) {
		if (clen == (size_t)-1 || clen == (size_t)-2)
			return (-1);
		donelen += clen;
		if ((w = wcwidth(wc)) == (size_t)-1)
			return (-1);
		width += w;
	}

	return (width);
}

static void
compute_abbreviated_month_size(void)
{
	int i;
	size_t width;
	size_t months_width[12];

	for (i = 0; i < 12; i++) {
		width = mbswidth(get_abmon(i));
		if (width == (size_t)-1) {
			month_max_size = -1;
			return;
		}
		months_width[i] = width;
		if (width > month_max_size)
			month_max_size = width;
	}

	for (i = 0; i < 12; i++)
		padding_for_month[i] = month_max_size - months_width[i];
}

void
printlong(const DISPLAY *dp)
{
	struct stat *sp;
	FTSENT *p;
	NAMES *np;
	char buf[20];
#ifdef COLORLS
	int color_printed = 0;
#endif

	if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
	    (f_longform || f_size)) {
		(void)printf("total %lu\n", howmany(dp->btotal, blocksize));
	}

	for (p = dp->list; p; p = p->fts_link) {
		if (IS_NOPRINT(p))
			continue;
		sp = p->fts_statp;
		if (f_inode)
			(void)printf("%*ju ",
			    dp->s_inode, (uintmax_t)sp->st_ino);
		if (f_size)
			(void)printf("%*jd ",
			    dp->s_block, howmany(sp->st_blocks, blocksize));
		strmode(sp->st_mode, buf);
		aclmode(buf, p);
		np = p->fts_pointer;
		(void)printf("%s %*ju %-*s  %-*s  ", buf, dp->s_nlink,
		    (uintmax_t)sp->st_nlink, dp->s_user, np->user, dp->s_group,
		    np->group);
		if (f_flags)
			(void)printf("%-*s ", dp->s_flags, np->flags);
		if (f_label)
			(void)printf("%-*s ", dp->s_label, np->label);
		if (S_ISCHR(sp->st_mode) || S_ISBLK(sp->st_mode))
			printdev(dp->s_size, sp->st_rdev);
		else
			printsize(dp->s_size, sp->st_size);
		if (f_accesstime)
			printtime(sp->st_atime);
		else if (f_birthtime)
			printtime(sp->st_birthtime);
		else if (f_statustime)
			printtime(sp->st_ctime);
		else
			printtime(sp->st_mtime);
#ifdef COLORLS
		if (f_color)
			color_printed = colortype(sp->st_mode);
#endif
		(void)printname(p->fts_name);
#ifdef COLORLS
		if (f_color && color_printed)
			endcolor(0);
#endif
		if (f_type)
			(void)printtype(sp->st_mode);
		if (S_ISLNK(sp->st_mode))
			printlink(p);
		(void)putchar('\n');
	}
}

void
printstream(const DISPLAY *dp)
{
	FTSENT *p;
	int chcnt;

	for (p = dp->list, chcnt = 0; p; p = p->fts_link) {
		if (p->fts_number == NO_PRINT)
			continue;
		/* XXX strlen does not take octal escapes into account. */
		if (strlen(p->fts_name) + chcnt +
		    (p->fts_link ? 2 : 0) >= (unsigned)termwidth) {
			putchar('\n');
			chcnt = 0;
		}
		chcnt += printaname(p, dp->s_inode, dp->s_block);
		if (p->fts_link) {
			printf(", ");
			chcnt += 2;
		}
	}
	if (chcnt)
		putchar('\n');
}

void
printcol(const DISPLAY *dp)
{
	static FTSENT **array;
	static int lastentries = -1;
	FTSENT *p;
	FTSENT **narray;
	int base;
	int chcnt;
	int cnt;
	int col;
	int colwidth;
	int endcol;
	int num;
	int numcols;
	int numrows;
	int row;
	int tabwidth;

	if (f_notabs)
		tabwidth = 1;
	else
		tabwidth = 8;

	/*
	 * Have to do random access in the linked list -- build a table
	 * of pointers.
	 */
	if (dp->entries > lastentries) {
		if ((narray =
		    realloc(array, dp->entries * sizeof(FTSENT *))) == NULL) {
			warn(NULL);
			printscol(dp);
			return;
		}
		lastentries = dp->entries;
		array = narray;
	}
	for (p = dp->list, num = 0; p; p = p->fts_link)
		if (p->fts_number != NO_PRINT)
			array[num++] = p;

	colwidth = dp->maxlen;
	if (f_inode)
		colwidth += dp->s_inode + 1;
	if (f_size)
		colwidth += dp->s_block + 1;
	if (f_type)
		colwidth += 1;

	colwidth = (colwidth + tabwidth) & ~(tabwidth - 1);
	if (termwidth < 2 * colwidth) {
		printscol(dp);
		return;
	}
	numcols = termwidth / colwidth;
	numrows = num / numcols;
	if (num % numcols)
		++numrows;

	if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
	    (f_longform || f_size)) {
		(void)printf("total %lu\n", howmany(dp->btotal, blocksize));
	}

	base = 0;
	for (row = 0; row < numrows; ++row) {
		endcol = colwidth;
		if (!f_sortacross)
			base = row;
		for (col = 0, chcnt = 0; col < numcols; ++col) {
			chcnt += printaname(array[base], dp->s_inode,
			    dp->s_block);
			if (f_sortacross)
				base++;
			else
				base += numrows;
			if (base >= num)
				break;
			while ((cnt = ((chcnt + tabwidth) & ~(tabwidth - 1)))
			    <= endcol) {
				if (f_sortacross && col + 1 >= numcols)
					break;
				(void)putchar(f_notabs ? ' ' : '\t');
				chcnt = cnt;
			}
			endcol += colwidth;
		}
		(void)putchar('\n');
	}
}

/*
 * print [inode] [size] name
 * return # of characters printed, no trailing characters.
 */
static int
printaname(const FTSENT *p, u_long inodefield, u_long sizefield)
{
	struct stat *sp;
	int chcnt;
#ifdef COLORLS
	int color_printed = 0;
#endif

	sp = p->fts_statp;
	chcnt = 0;
	if (f_inode)
		chcnt += printf("%*ju ",
		    (int)inodefield, (uintmax_t)sp->st_ino);
	if (f_size)
		chcnt += printf("%*jd ",
		    (int)sizefield, howmany(sp->st_blocks, blocksize));
#ifdef COLORLS
	if (f_color)
		color_printed = colortype(sp->st_mode);
#endif
	chcnt += printname(p->fts_name);
#ifdef COLORLS
	if (f_color && color_printed)
		endcolor(0);
#endif
	if (f_type)
		chcnt += printtype(sp->st_mode);
	return (chcnt);
}

/*
 * Print device special file major and minor numbers.
 */
static void
printdev(size_t width, dev_t dev)
{

	(void)printf("%#*jx ", (u_int)width, (uintmax_t)dev);
}

static size_t
ls_strftime(char *str, size_t len, const char *fmt, const struct tm *tm)
{
	char *posb, nfmt[BUFSIZ];
	const char *format = fmt;
	size_t ret;

	if ((posb = strstr(fmt, "%b")) != NULL) {
		if (month_max_size == 0) {
			compute_abbreviated_month_size();
		}
		if (month_max_size > 0) {
			snprintf(nfmt, sizeof(nfmt),  "%.*s%s%*s%s",
			    (int)(posb - fmt), fmt,
			    get_abmon(tm->tm_mon),
			    (int)padding_for_month[tm->tm_mon],
			    "",
			    posb + 2);
			format = nfmt;
		}
	}
	ret = strftime(str, len, format, tm);
	return (ret);
}

static void
printtime(time_t ftime)
{
	char longstring[80];
	static time_t now = 0;
	const char *format;
	static int d_first = -1;

	if (d_first < 0)
		d_first = (*nl_langinfo(D_MD_ORDER) == 'd');
	if (now == 0)
		now = time(NULL);

#define	SIXMONTHS	((365 / 2) * 86400)
	if (f_timeformat)  /* user specified format */
		format = f_timeformat;
	else if (f_sectime)
		/* mmm dd hh:mm:ss yyyy || dd mmm hh:mm:ss yyyy */
		format = d_first ? "%e %b %T %Y" : "%b %e %T %Y";
	else if (ftime + SIXMONTHS > now && ftime < now + SIXMONTHS)
		/* mmm dd hh:mm || dd mmm hh:mm */
		format = d_first ? "%e %b %R" : "%b %e %R";
	else
		/* mmm dd  yyyy || dd mmm  yyyy */
		format = d_first ? "%e %b  %Y" : "%b %e  %Y";
	ls_strftime(longstring, sizeof(longstring), format, localtime(&ftime));
	fputs(longstring, stdout);
	fputc(' ', stdout);
}

static int
printtype(u_int mode)
{

	if (f_slash) {
		if ((mode & S_IFMT) == S_IFDIR) {
			(void)putchar('/');
			return (1);
		}
		return (0);
	}

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
	case S_IFWHT:
		(void)putchar('%');
		return (1);
	default:
		break;
	}
	if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		(void)putchar('*');
		return (1);
	}
	return (0);
}

#ifdef COLORLS
static int
putch(int c)
{
	(void)putchar(c);
	return 0;
}

static int
writech(int c)
{
	char tmp = (char)c;

	(void)write(STDOUT_FILENO, &tmp, 1);
	return 0;
}

static void
printcolor_termcap(Colors c)
{
	char *ansiseq;

	if (colors[c].bold)
		tputs(enter_bold, 1, putch);

	if (colors[c].num[0] != -1) {
		ansiseq = tgoto(ansi_fgcol, 0, colors[c].num[0]);
		if (ansiseq)
			tputs(ansiseq, 1, putch);
	}
	if (colors[c].num[1] != -1) {
		ansiseq = tgoto(ansi_bgcol, 0, colors[c].num[1]);
		if (ansiseq)
			tputs(ansiseq, 1, putch);
	}
}

static void
printcolor_ansi(Colors c)
{

	printf("\033[");

	if (colors[c].bold)
		printf("1");
	if (colors[c].num[0] != -1)
		printf(";3%d", colors[c].num[0]);
	if (colors[c].num[1] != -1)
		printf(";4%d", colors[c].num[1]);
	printf("m");
}

static void
printcolor(Colors c)
{

	if (explicitansi)
		printcolor_ansi(c);
	else
		printcolor_termcap(c);
}

static void
endcolor_termcap(int sig)
{

	tputs(ansi_coloff, 1, sig ? writech : putch);
	tputs(attrs_off, 1, sig ? writech : putch);
}

static void
endcolor_ansi(void)
{

	printf("\33[m");
}

static void
endcolor(int sig)
{

	if (explicitansi)
		endcolor_ansi();
	else
		endcolor_termcap(sig);
}

static int
colortype(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFDIR:
		if (mode & S_IWOTH)
			if (mode & S_ISTXT)
				printcolor(C_WSDIR);
			else
				printcolor(C_WDIR);
		else
			printcolor(C_DIR);
		return (1);
	case S_IFLNK:
		printcolor(C_LNK);
		return (1);
	case S_IFSOCK:
		printcolor(C_SOCK);
		return (1);
	case S_IFIFO:
		printcolor(C_FIFO);
		return (1);
	case S_IFBLK:
		printcolor(C_BLK);
		return (1);
	case S_IFCHR:
		printcolor(C_CHR);
		return (1);
	default:;
	}
	if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
		if (mode & S_ISUID)
			printcolor(C_SUID);
		else if (mode & S_ISGID)
			printcolor(C_SGID);
		else
			printcolor(C_EXEC);
		return (1);
	}
	return (0);
}

void
parsecolors(const char *cs)
{
	int i;
	int j;
	size_t len;
	char c[2];
	short legacy_warn = 0;

	if (cs == NULL)
		cs = "";	/* LSCOLORS not set */
	len = strlen(cs);
	for (i = 0; i < (int)C_NUMCOLORS; i++) {
		colors[i].bold = 0;

		if (len <= 2 * (size_t)i) {
			c[0] = defcolors[2 * i];
			c[1] = defcolors[2 * i + 1];
		} else {
			c[0] = cs[2 * i];
			c[1] = cs[2 * i + 1];
		}
		for (j = 0; j < 2; j++) {
			/* Legacy colours used 0-7 */
			if (c[j] >= '0' && c[j] <= '7') {
				colors[i].num[j] = c[j] - '0';
				if (!legacy_warn) {
					warnx("LSCOLORS should use "
					    "characters a-h instead of 0-9 ("
					    "see the manual page)");
				}
				legacy_warn = 1;
			} else if (c[j] >= 'a' && c[j] <= 'h')
				colors[i].num[j] = c[j] - 'a';
			else if (c[j] >= 'A' && c[j] <= 'H') {
				colors[i].num[j] = c[j] - 'A';
				colors[i].bold = 1;
			} else if (tolower((unsigned char)c[j]) == 'x')
				colors[i].num[j] = -1;
			else {
				warnx("invalid character '%c' in LSCOLORS"
				    " env var", c[j]);
				colors[i].num[j] = -1;
			}
		}
	}
}

void
colorquit(int sig)
{
	endcolor(sig);

	(void)signal(sig, SIG_DFL);
	(void)kill(getpid(), sig);
}

#endif /* COLORLS */

static void
printlink(const FTSENT *p)
{
	int lnklen;
	char name[MAXPATHLEN + 1];
	char path[MAXPATHLEN + 1];

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
	(void)printname(path);
}

static void
printsize(size_t width, off_t bytes)
{

	if (f_humanval) {
		/*
		 * Reserve one space before the size and allocate room for
		 * the trailing '\0'.
		 */
		char buf[HUMANVALSTR_LEN - 1 + 1];

		humanize_number(buf, sizeof(buf), (int64_t)bytes, "",
		    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
		(void)printf("%*s ", (u_int)width, buf);
	} else if (f_thousands) {		/* with commas */
		/* This format assignment needed to work round gcc bug. */
		const char *format = "%*j'd ";
		(void)printf(format, (u_int)width, bytes);
	} else
		(void)printf("%*jd ", (u_int)width, bytes);
}

/*
 * Add a + after the standard rwxrwxrwx mode if the file has an
 * ACL. strmode() reserves space at the end of the string.
 */
static void
aclmode(char *buf, const FTSENT *p)
{
	char name[MAXPATHLEN + 1];
	int ret, trivial;
	static dev_t previous_dev = NODEV;
	static int supports_acls = -1;
	static int type = ACL_TYPE_ACCESS;
	acl_t facl;

	/*
	 * XXX: ACLs are not supported on whiteouts and device files
	 * residing on UFS.
	 */
	if (S_ISCHR(p->fts_statp->st_mode) || S_ISBLK(p->fts_statp->st_mode) ||
	    S_ISWHT(p->fts_statp->st_mode))
		return;

	if (previous_dev == p->fts_statp->st_dev && supports_acls == 0)
		return;

	if (p->fts_level == FTS_ROOTLEVEL)
		snprintf(name, sizeof(name), "%s", p->fts_name);
	else
		snprintf(name, sizeof(name), "%s/%s",
		    p->fts_parent->fts_accpath, p->fts_name);

	if (previous_dev != p->fts_statp->st_dev) {
		previous_dev = p->fts_statp->st_dev;
		supports_acls = 0;

		ret = lpathconf(name, _PC_ACL_NFS4);
		if (ret > 0) {
			type = ACL_TYPE_NFS4;
			supports_acls = 1;
		} else if (ret < 0 && errno != EINVAL) {
			warn("%s", name);
			return;
		}
		if (supports_acls == 0) {
			ret = lpathconf(name, _PC_ACL_EXTENDED);
			if (ret > 0) {
				type = ACL_TYPE_ACCESS;
				supports_acls = 1;
			} else if (ret < 0 && errno != EINVAL) {
				warn("%s", name);
				return;
			}
		}
	}
	if (supports_acls == 0)
		return;
	facl = acl_get_link_np(name, type);
	if (facl == NULL) {
		warn("%s", name);
		return;
	}
	if (acl_is_trivial_np(facl, &trivial)) {
		acl_free(facl);
		warn("%s", name);
		return;
	}
	if (!trivial)
		buf[10] = '+';
	acl_free(facl);
}

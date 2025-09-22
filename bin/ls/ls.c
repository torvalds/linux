/*	$OpenBSD: ls.c,v 1.56 2023/10/07 13:29:08 schwarze Exp $	*/
/*	$NetBSD: ls.c,v 1.18 1996/07/09 09:16:29 mycroft Exp $	*/

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
#include <sys/ioctl.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <util.h>

#include "ls.h"
#include "extern.h"

static void	 display(FTSENT *, FTSENT *);
static int	 mastercmp(const FTSENT **, const FTSENT **);
static void	 traverse(int, char **, int);

static void (*printfcn)(DISPLAY *);
static int (*sortfcn)(const FTSENT *, const FTSENT *);

#define	BY_NAME 0
#define	BY_SIZE 1
#define	BY_TIME	2

long blocksize;			/* block size units */
int termwidth;			/* default terminal width */
int sortkey = BY_NAME;

/* flags */
int f_accesstime;		/* use time of last access */
int f_column;			/* columnated format */
int f_columnacross;		/* columnated format, sorted across */
int f_flags;			/* show flags associated with a file */
int f_grouponly;		/* long listing format without owner */
int f_humanval;			/* show human-readable file sizes */
int f_inode;			/* print inode */
int f_listdir;			/* list actual directory, not contents */
int f_listdot;			/* list files beginning with . */
int f_longform;			/* long listing format */
int f_nonprint;			/* show unprintables as ? */
int f_nosort;			/* don't sort output */
int f_numericonly;		/* don't expand uid to symbolic name */
int f_recursive;		/* ls subdirectories also */
int f_reversesort;		/* reverse whatever sort is used */
int f_sectime;			/* print the real time for all files */
int f_singlecol;		/* use single column output */
int f_size;			/* list size in short listing */
int f_statustime;		/* use time of last mode change */
int f_stream;			/* stream format */
int f_type;			/* add type character for non-regular files */
int f_typedir;			/* add type character for directories */

int rval;

int
ls_main(int argc, char *argv[])
{
	static char dot[] = ".", *dotav[] = { dot, NULL };
	struct winsize win;
	int ch, fts_options, notused;
	int kflag = 0;
	char *p;

#ifndef SMALL
	setlocale(LC_CTYPE, "");
#endif

	/* Terminal defaults to -Cq, non-terminal defaults to -1. */
	if (isatty(STDOUT_FILENO)) {
		f_column = f_nonprint = 1;
	} else {
		f_singlecol = 1;
	}

	termwidth = 0;
	if ((p = getenv("COLUMNS")) != NULL)
		termwidth = strtonum(p, 1, INT_MAX, NULL);
	if (termwidth == 0 && ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == 0 &&
	    win.ws_col > 0)
		termwidth = win.ws_col;
	if (termwidth == 0)
		termwidth = 80;

	if (pledge("stdio rpath getpw", NULL) == -1)
		err(1, "pledge");

	/* Root is -A automatically. */
	if (!getuid())
		f_listdot = 1;

	fts_options = FTS_PHYSICAL;
	while ((ch = getopt(argc, argv, "1ACFHLRSTacdfghiklmnopqrstux")) != -1) {
		switch (ch) {
		/*
		 * The -1, -C and -l, -m, -n and -x options all override each
		 * other so shell aliasing works right.
		 */
		case '1':
			f_singlecol = 1;
			f_column = f_columnacross = f_longform = 0;
			f_numericonly = f_stream = 0;
			break;
		case 'C':
			f_column = 1;
			f_columnacross = f_longform = f_numericonly = 0;
			f_singlecol = f_stream = 0;
			break;
		case 'g':
			f_longform = 1;
			if (f_grouponly != -1)
				f_grouponly = 1;
			f_column = f_columnacross = f_singlecol = f_stream = 0;
			break;
		case 'l':
			f_longform = 1;
			f_grouponly = -1;	/* -l always overrides -g */
			f_column = f_columnacross = f_singlecol = f_stream = 0;
			break;
		case 'm':
			f_stream = 1;
			f_column = f_columnacross = f_longform = 0;
			f_numericonly = f_singlecol = 0;
			break;
		case 'x':
			f_columnacross = 1;
			f_column = f_longform = f_numericonly = 0;
			f_singlecol = f_stream = 0;
			break;
		case 'n':
			f_longform = 1;
			f_numericonly = 1;
			f_column = f_columnacross = f_singlecol = f_stream = 0;
			break;
		/* The -c and -u options override each other. */
		case 'c':
			f_statustime = 1;
			f_accesstime = 0;
			break;
		case 'u':
			f_accesstime = 1;
			f_statustime = 0;
			break;
		case 'F':
			f_type = 1;
			break;
		case 'H':
			fts_options |= FTS_COMFOLLOW;
			break;
		case 'L':
			fts_options &= ~FTS_PHYSICAL;
			fts_options |= FTS_LOGICAL;
			break;
		case 'R':
			f_recursive = 1;
			break;
		case 'f':
			f_nosort = 1;
			/* FALLTHROUGH */
		case 'a':
			fts_options |= FTS_SEEDOT;
			/* FALLTHROUGH */
		case 'A':
			f_listdot = 1;
			break;
		/* The -d option turns off the -R option. */
		case 'd':
			f_listdir = 1;
			f_recursive = 0;
			break;
		case 'h':
			f_humanval = 1;
			break;
		case 'i':
			f_inode = 1;
			break;
		case 'k':
			blocksize = 1024;
			kflag = 1;
			break;
		case 'o':
			f_flags = 1;
			break;
		case 'p':
			f_typedir = 1;
			break;
		case 'q':
			f_nonprint = 1;
			break;
		case 'r':
			f_reversesort = 1;
			break;
		case 'S':
			sortkey = BY_SIZE;
			break;
		case 's':
			f_size = 1;
			break;
		case 'T':
			f_sectime = 1;
			break;
		case 't':
			sortkey = BY_TIME;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * If both -g and -l options, let -l take precedence.
	 * This preserves compatibility with the historic BSD ls -lg.
	 */
	if (f_grouponly == -1)
		f_grouponly = 0;

	/*
	 * If not -F, -i, -l, -p, -S, -s or -t options, don't require stat
	 * information.
	 */
	if (!f_longform && !f_inode && !f_size && !f_type && !f_typedir &&
	    sortkey == BY_NAME)
		fts_options |= FTS_NOSTAT;

	/*
	 * If not -F, -d or -l options, follow any symbolic links listed on
	 * the command line.
	 */
	if (!f_longform && !f_listdir && !f_type)
		fts_options |= FTS_COMFOLLOW;

	/* If -l or -s, figure out block size. */
	if (f_longform || f_size) {
		if (!kflag)
			(void)getbsize(&notused, &blocksize);
		blocksize /= 512;
	}

	/* Select a sort function. */
	if (f_reversesort) {
		switch (sortkey) {
		case BY_NAME:
			sortfcn = revnamecmp;
			break;
		case BY_SIZE:
			sortfcn = revsizecmp;
			break;
		case BY_TIME:
			if (f_accesstime)
				sortfcn = revacccmp;
			else if (f_statustime)
				sortfcn = revstatcmp;
			else /* Use modification time. */
				sortfcn = revmodcmp;
			break;
		}
	} else {
		switch (sortkey) {
		case BY_NAME:
			sortfcn = namecmp;
			break;
		case BY_SIZE:
			sortfcn = sizecmp;
			break;
		case BY_TIME:
			if (f_accesstime)
				sortfcn = acccmp;
			else if (f_statustime)
				sortfcn = statcmp;
			else /* Use modification time. */
				sortfcn = modcmp;
			break;
		}
	}

	/* Select a print function. */
	if (f_singlecol)
		printfcn = printscol;
	else if (f_columnacross)
		printfcn = printacol;
	else if (f_longform)
		printfcn = printlong;
	else if (f_stream)
		printfcn = printstream;
	else
		printfcn = printcol;

	if (argc)
		traverse(argc, argv, fts_options);
	else
		traverse(1, dotav, fts_options);
	return (rval);
}

static int output;			/* If anything output. */

/*
 * Traverse() walks the logical directory structure specified by the argv list
 * in the order specified by the mastercmp() comparison function.  During the
 * traversal it passes linked lists of structures to display() which represent
 * a superset (may be exact set) of the files to be displayed.
 */
static void
traverse(int argc, char *argv[], int options)
{
	FTS *ftsp;
	FTSENT *p, *chp;
	int ch_options, saved_errno;

	if ((ftsp =
	    fts_open(argv, options, f_nosort ? NULL : mastercmp)) == NULL)
		err(1, NULL);

	/*
	 * We ignore errors from fts_children here since they will be
	 * replicated and signalled on the next call to fts_read() below.
	 */
	chp = fts_children(ftsp, 0);
	if (chp != NULL)
		display(NULL, chp);
	if (f_listdir)
		return;

	/*
	 * If not recursing down this tree and don't need stat info, just get
	 * the names.
	 */
	ch_options = !f_recursive && options & FTS_NOSTAT ? FTS_NAMEONLY : 0;

	while ((p = fts_read(ftsp)) != NULL)
		switch (p->fts_info) {
		case FTS_D:
			if (p->fts_name[0] == '.' &&
			    p->fts_level != FTS_ROOTLEVEL && !f_listdot) {
				(void)fts_set(ftsp, p, FTS_SKIP);
				break;
			}

			/*
			 * If already output something, put out a newline as
			 * a separator.  If multiple arguments, precede each
			 * directory with its name.
			 */
			if (output)
				(void)printf("\n%s:\n", p->fts_path);
			else if (f_recursive || argc > 1) {
				(void)printf("%s:\n", p->fts_path);
				output = 1;
			}

			chp = fts_children(ftsp, ch_options);
			saved_errno = errno;
			display(p, chp);

			/*
			 * On fts_children() returning error do recurse to see
			 * the error.
			 */
			if (!f_recursive && !(chp == NULL && saved_errno != 0))
				(void)fts_set(ftsp, p, FTS_SKIP);
			break;
		case FTS_DC:
			warnx("%s: directory causes a cycle", p->fts_name);
			break;
		case FTS_DNR:
		case FTS_ERR:
			warnx("%s: %s", p->fts_name[0] == '\0' ? p->fts_path :
			    p->fts_name, strerror(p->fts_errno));
			rval = 1;
			break;
		}
	if (errno)
		err(1, "fts_read");

	fts_close(ftsp);
}

/*
 * Display() takes a linked list of FTSENT structures and passes the list
 * along with any other necessary information to the print function.  P
 * points to the parent directory of the display list.
 */
static void
display(FTSENT *p, FTSENT *list)
{
	struct stat *sp;
	DISPLAY d;
	FTSENT *cur;
	NAMES *np;
	off_t maxsize;
	nlink_t maxnlink;
	unsigned long long btotal;
	blkcnt_t maxblock;
	ino_t maxinode;
	unsigned int maxmajor, maxminor;
	int bcfile, flen, glen, ulen, maxflags, maxgroup, maxuser, maxlen;
	int entries, needstats;
	int width;
	const char *user, *group;
	char nuser[12], ngroup[12];
	char *flags = NULL;

	needstats = f_inode || f_longform || f_size;
	flen = 0;
	btotal = maxblock = maxinode = maxlen = maxnlink = 0;
	bcfile = 0;
	maxuser = maxgroup = maxflags = 0;
	maxmajor = maxminor = 0;
	maxsize = 0;
	for (cur = list, entries = 0; cur != NULL; cur = cur->fts_link) {
		if (cur->fts_info == FTS_ERR || cur->fts_info == FTS_NS) {
			warnx("%s: %s",
			    cur->fts_name, strerror(cur->fts_errno));
			cur->fts_number = NO_PRINT;
			rval = 1;
			continue;
		}

		/*
		 * P is NULL if list is the argv list, to which different rules
		 * apply.
		 */
		if (p == NULL) {
			/* Directories will be displayed later. */
			if (cur->fts_info == FTS_D && !f_listdir) {
				cur->fts_number = NO_PRINT;
				continue;
			}
		} else {
			/* Only display dot file if -a/-A set. */
			if (cur->fts_name[0] == '.' && !f_listdot) {
				cur->fts_number = NO_PRINT;
				continue;
			}
		}
		if ((width = mbsprint(cur->fts_name, 0)) > maxlen)
			maxlen = width;
		if (needstats) {
			sp = cur->fts_statp;
			if (sp->st_blocks > maxblock)
				maxblock = sp->st_blocks;
			if (sp->st_ino > maxinode)
				maxinode = sp->st_ino;
			if (sp->st_nlink > maxnlink)
				maxnlink = sp->st_nlink;
			if (sp->st_size > maxsize)
				maxsize = sp->st_size;

			btotal += sp->st_blocks;
			if (f_longform) {
				if (f_numericonly) {
					snprintf(nuser, sizeof nuser, "%u", sp->st_uid);
					snprintf(ngroup, sizeof nuser, "%u", sp->st_gid);
					user = nuser;
					group = ngroup;
				} else {
					user = user_from_uid(sp->st_uid, 0);
					group = group_from_gid(sp->st_gid, 0);
				}
				if ((ulen = strlen(user)) > maxuser)
					maxuser = ulen;
				if ((glen = strlen(group)) > maxgroup)
					maxgroup = glen;
				if (f_flags) {
					flags = fflagstostr(sp->st_flags);
					if (*flags == '\0')
						flags = "-";
					if ((flen = strlen(flags)) > maxflags)
						maxflags = flen;
				} else
					flen = 0;

				if ((np = malloc(sizeof(NAMES) +
				    ulen + 1 + glen + 1 + flen + 1)) == NULL)
					err(1, NULL);

				np->user = &np->data[0];
				(void)strlcpy(np->user, user, ulen + 1);
				np->group = &np->data[ulen + 1];
				(void)strlcpy(np->group, group, glen + 1);

				if (S_ISCHR(sp->st_mode) ||
				    S_ISBLK(sp->st_mode)) {
					bcfile = 1;
					if (maxmajor < major(sp->st_rdev))
						maxmajor = major(sp->st_rdev);
					if (maxminor < minor(sp->st_rdev))
						maxminor = minor(sp->st_rdev);
				}
				if (f_flags) {
					np->flags = &np->data[ulen + 1 + glen + 1];
					(void)strlcpy(np->flags, flags, flen + 1);
					if (*flags != '-')
						free(flags);
				}
				cur->fts_pointer = np;
			}
		}
		++entries;
	}

	/*
	 * If there are no entries to display, we normally stop right
	 * here.  However, we must continue if we have to display the
	 * total block count.  In this case, we display the total only
	 * on the second (p != NULL) pass.
	 */
	if (!entries && (!(f_longform || f_size) || p == NULL))
		return;

	d.list = list;
	d.entries = entries;
	d.maxlen = maxlen;
	if (needstats) {
		d.btotal = btotal;
		d.s_block = snprintf(NULL, 0, "%llu",
		    (unsigned long long)maxblock);
		d.s_flags = maxflags;
		d.s_group = maxgroup;
		d.s_inode = snprintf(NULL, 0, "%llu",
		    (unsigned long long)maxinode);
		d.s_nlink = snprintf(NULL, 0, "%lu",
		    (unsigned long)maxnlink);
		if (!f_humanval)
			d.s_size = snprintf(NULL, 0, "%lld",
			    (long long)maxsize);
		else
			d.s_size = FMT_SCALED_STRSIZE-2; /* no - or '\0' */
		d.s_major = d.s_minor = 3;
		if (bcfile) {
			d.s_major = snprintf(NULL, 0, "%u", maxmajor);
			d.s_minor = snprintf(NULL, 0, "%u", maxminor);
			if (d.s_size <= d.s_major + 2 + d.s_minor)
				d.s_size = d.s_major + 2 + d.s_minor;
			else
				d.s_major = d.s_size - 2 - d.s_minor;
		}
		d.s_user = maxuser;
	}

	printfcn(&d);
	output = 1;

	if (f_longform)
		for (cur = list; cur != NULL; cur = cur->fts_link)
			free(cur->fts_pointer);
}

/*
 * Ordering for mastercmp:
 * If ordering the argv (fts_level = FTS_ROOTLEVEL) return non-directories
 * as larger than directories.  Within either group, use the sort function.
 * All other levels use the sort function.  Error entries remain unsorted.
 */
static int
mastercmp(const FTSENT **a, const FTSENT **b)
{
	int a_info, b_info;

	a_info = (*a)->fts_info;
	if (a_info == FTS_ERR)
		return (0);
	b_info = (*b)->fts_info;
	if (b_info == FTS_ERR)
		return (0);

	if (a_info == FTS_NS || b_info == FTS_NS) {
		if (b_info != FTS_NS)
			return (1);
		else if (a_info != FTS_NS)
			return (-1);
		else
			return (namecmp(*a, *b));
	}

	if (a_info != b_info &&
	    (*a)->fts_level == FTS_ROOTLEVEL && !f_listdir) {
		if (a_info == FTS_D)
			return (1);
		if (b_info == FTS_D)
			return (-1);
	}
	return (sortfcn(*a, *b));
}

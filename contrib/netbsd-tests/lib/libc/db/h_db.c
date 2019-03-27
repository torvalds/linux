/*	$NetBSD: h_db.c,v 1.3 2016/09/24 21:18:22 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993, 1994
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

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1992, 1993, 1994\
	The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)dbtest.c	8.17 (Berkeley) 9/1/94";
#else
__RCSID("$NetBSD: h_db.c,v 1.3 2016/09/24 21:18:22 christos Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <db.h>
#include "btree.h"

enum S { COMMAND, COMPARE, GET, PUT, REMOVE, SEQ, SEQFLAG, KEY, DATA };

static void	 compare(DBT *, DBT *);
static DBTYPE	 dbtype(const char *);
static void	 dump(DB *, int, int);
static void	 get(DB *, DBT *);
static void	 getdata(DB *, DBT *, DBT *);
static void	 put(DB *, DBT *, DBT *);
static void	 rem(DB *, DBT *);
static const char *sflags(int);
static void	 synk(DB *);
static void	*rfile(char *, size_t *);
static void	 seq(DB *, DBT *);
static u_int	 setflags(char *);
static void	*setinfo(DBTYPE, char *);
#ifdef	__NetBSD__
static void	 unlinkpg(DB *);
#endif
static void	 usage(void) __attribute__((__noreturn__));
static void	*xcopy(void *, size_t);
static void	 chkcmd(enum S);
static void	 chkdata(enum S);
static void	 chkkey(enum S);

#ifdef STATISTICS
extern void __bt_stat(DB *);
#endif
#ifdef	__NetBSD__
extern int __bt_relink(BTREE *, PAGE *);
#endif

static DBTYPE type;			/* Database type. */
static void *infop;			/* Iflags. */
static size_t lineno;			/* Current line in test script. */
static u_int flags;				/* Current DB flags. */
static int ofd = STDOUT_FILENO;		/* Standard output fd. */

static DB *XXdbp;			/* Global for gdb. */
static size_t XXlineno;			/* Fast breakpoint for gdb. */

int
main(int argc, char *argv[])
{
	extern int optind;
	extern char *optarg;
	enum S command = COMMAND, state;
	DB *dbp;
	DBT data, key, keydata;
	size_t len;
	int ch, oflags, sflag;
	char *fname, *infoarg, *p, *t, buf[8 * 1024];
	bool unlink_dbfile;

	infoarg = NULL;
	fname = NULL;
	unlink_dbfile = false;
	oflags = O_CREAT | O_RDWR;
	sflag = 0;
	while ((ch = getopt(argc, argv, "f:i:lo:s")) != -1)
		switch (ch) {
		case 'f':
			fname = optarg;
			break;
		case 'i':
			infoarg = optarg;
			break;
		case 'l':
			oflags |= DB_LOCK;
			break;
		case 'o':
			if ((ofd = open(optarg,
			    O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0)
				err(1, "Cannot create `%s'", optarg);
			break;
		case 's':
			sflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	/* Set the type. */
	type = dbtype(*argv++);

	/* Open the descriptor file. */
        if (strcmp(*argv, "-") && freopen(*argv, "r", stdin) == NULL)
	    err(1, "Cannot reopen `%s'", *argv);

	/* Set up the db structure as necessary. */
	if (infoarg == NULL)
		infop = NULL;
	else
		for (p = strtok(infoarg, ",\t "); p != NULL;
		    p = strtok(0, ",\t "))
			if (*p != '\0')
				infop = setinfo(type, p);

	/*
	 * Open the DB.  Delete any preexisting copy, you almost never
	 * want it around, and it often screws up tests.
	 */
	if (fname == NULL) {
		const char *q = getenv("TMPDIR");
		if (q == NULL)
			q = "/var/tmp";
		(void)snprintf(buf, sizeof(buf), "%s/__dbtest", q);
		fname = buf;
		(void)unlink(buf);
		unlink_dbfile = true;
	} else  if (!sflag)
		(void)unlink(fname);

	if ((dbp = dbopen(fname,
	    oflags, S_IRUSR | S_IWUSR, type, infop)) == NULL)
		err(1, "Cannot dbopen `%s'", fname);
	XXdbp = dbp;
	if (unlink_dbfile)
		(void)unlink(fname);

	state = COMMAND;
	for (lineno = 1;
	    (p = fgets(buf, sizeof(buf), stdin)) != NULL; ++lineno) {
		/* Delete the newline, displaying the key/data is easier. */
		if (ofd == STDOUT_FILENO && (t = strchr(p, '\n')) != NULL)
			*t = '\0';
		if ((len = strlen(buf)) == 0 || isspace((unsigned char)*p) ||
		    *p == '#')
			continue;

		/* Convenient gdb break point. */
		if (XXlineno == lineno)
			XXlineno = 1;
		switch (*p) {
		case 'c':			/* compare */
			chkcmd(state);
			state = KEY;
			command = COMPARE;
			break;
		case 'e':			/* echo */
			chkcmd(state);
			/* Don't display the newline, if CR at EOL. */
			if (p[len - 2] == '\r')
				--len;
			if (write(ofd, p + 1, len - 1) != (ssize_t)len - 1 ||
			    write(ofd, "\n", 1) != 1)
				err(1, "write failed");
			break;
		case 'g':			/* get */
			chkcmd(state);
			state = KEY;
			command = GET;
			break;
		case 'p':			/* put */
			chkcmd(state);
			state = KEY;
			command = PUT;
			break;
		case 'r':			/* remove */
			chkcmd(state);
                        if (flags == R_CURSOR) {
				rem(dbp, &key);
				state = COMMAND;
                        } else {
				state = KEY;
				command = REMOVE;
			}
			break;
		case 'S':			/* sync */
			chkcmd(state);
			synk(dbp);
			state = COMMAND;
			break;
		case 's':			/* seq */
			chkcmd(state);
			if (flags == R_CURSOR) {
				state = KEY;
				command = SEQ;
			} else
				seq(dbp, &key);
			break;
		case 'f':
			flags = setflags(p + 1);
			break;
		case 'D':			/* data file */
			chkdata(state);
			data.data = rfile(p + 1, &data.size);
			goto ldata;
		case 'd':			/* data */
			chkdata(state);
			data.data = xcopy(p + 1, len - 1);
			data.size = len - 1;
ldata:			switch (command) {
			case COMPARE:
				compare(&keydata, &data);
				break;
			case PUT:
				put(dbp, &key, &data);
				break;
			default:
				errx(1, "line %zu: command doesn't take data",
				    lineno);
			}
			if (type != DB_RECNO)
				free(key.data);
			free(data.data);
			state = COMMAND;
			break;
		case 'K':			/* key file */
			chkkey(state);
			if (type == DB_RECNO)
				errx(1, "line %zu: 'K' not available for recno",
				    lineno);
			key.data = rfile(p + 1, &key.size);
			goto lkey;
		case 'k':			/* key */
			chkkey(state);
			if (type == DB_RECNO) {
				static recno_t recno;
				recno = atoi(p + 1);
				key.data = &recno;
				key.size = sizeof(recno);
			} else {
				key.data = xcopy(p + 1, len - 1);
				key.size = len - 1;
			}
lkey:			switch (command) {
			case COMPARE:
				getdata(dbp, &key, &keydata);
				state = DATA;
				break;
			case GET:
				get(dbp, &key);
				if (type != DB_RECNO)
					free(key.data);
				state = COMMAND;
				break;
			case PUT:
				state = DATA;
				break;
			case REMOVE:
				rem(dbp, &key);
				if ((type != DB_RECNO) && (flags != R_CURSOR))
					free(key.data);
				state = COMMAND;
				break;
			case SEQ:
				seq(dbp, &key);
				if ((type != DB_RECNO) && (flags != R_CURSOR))
					free(key.data);
				state = COMMAND;
				break;
			default:
				errx(1, "line %zu: command doesn't take a key",
				    lineno);
			}
			break;
		case 'o':
			dump(dbp, p[1] == 'r', 0);
			break;
#ifdef	__NetBSD__
		case 'O':
			dump(dbp, p[1] == 'r', 1);
			break;
		case 'u':
			unlinkpg(dbp);
			break;
#endif
		default:
			errx(1, "line %zu: %s: unknown command character",
			    lineno, p);
		}
	}
#ifdef STATISTICS
	/*
	 * -l must be used (DB_LOCK must be set) for this to be
	 * used, otherwise a page will be locked and it will fail.
	 */
	if (type == DB_BTREE && oflags & DB_LOCK)
		__bt_stat(dbp);
#endif
	if ((*dbp->close)(dbp))
		err(1, "db->close failed");
	(void)close(ofd);
	return 0;
}

#define	NOOVERWRITE	"put failed, would overwrite key\n"

static void
compare(DBT *db1, DBT *db2)
{
	size_t len;
	u_char *p1, *p2;

	if (db1->size != db2->size)
		printf("compare failed: key->data len %zu != data len %zu\n",
		    db1->size, db2->size);

	len = MIN(db1->size, db2->size);
	for (p1 = db1->data, p2 = db2->data; len--;)
		if (*p1++ != *p2++) {
			printf("compare failed at offset %lu\n",
			    (unsigned long)(p1 - (u_char *)db1->data));
			break;
		}
}

static void
get(DB *dbp, DBT *kp)
{
	DBT data;

	switch ((*dbp->get)(dbp, kp, &data, flags)) {
	case 0:
		(void)write(ofd, data.data, data.size);
		if (ofd == STDOUT_FILENO)
			(void)write(ofd, "\n", 1);
		break;
	case -1:
		err(1, "line %zu: get failed", lineno);
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"get failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else
			(void)fprintf(stderr, "%zu: %.*s: %s",
			    lineno, (int)MIN(kp->size, 20),
			    (const char *)kp->data,
			    NOSUCHKEY);
#undef	NOSUCHKEY
		break;
	}
}

static void
getdata(DB *dbp, DBT *kp, DBT *dp)
{
	switch ((*dbp->get)(dbp, kp, dp, flags)) {
	case 0:
		return;
	case -1:
		err(1, "line %zu: getdata failed", lineno);
		/* NOTREACHED */
	case 1:
		errx(1, "line %zu: getdata failed, no such key", lineno);
		/* NOTREACHED */
	}
}

static void
put(DB *dbp, DBT *kp, DBT *dp)
{
	switch ((*dbp->put)(dbp, kp, dp, flags)) {
	case 0:
		break;
	case -1:
		err(1, "line %zu: put failed", lineno);
		/* NOTREACHED */
	case 1:
		(void)write(ofd, NOOVERWRITE, sizeof(NOOVERWRITE) - 1);
		break;
	}
}

static void
rem(DB *dbp, DBT *kp)
{
	switch ((*dbp->del)(dbp, kp, flags)) {
	case 0:
		break;
	case -1:
		err(1, "line %zu: rem failed", lineno);
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"rem failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else if (flags != R_CURSOR)
			(void)fprintf(stderr, "%zu: %.*s: %s", 
			    lineno, (int)MIN(kp->size, 20),
			    (const char *)kp->data, NOSUCHKEY);
		else
			(void)fprintf(stderr,
			    "%zu: rem of cursor failed\n", lineno);
#undef	NOSUCHKEY
		break;
	}
}

static void
synk(DB *dbp)
{
	switch ((*dbp->sync)(dbp, flags)) {
	case 0:
		break;
	case -1:
		err(1, "line %zu: synk failed", lineno);
		/* NOTREACHED */
	}
}

static void
seq(DB *dbp, DBT *kp)
{
	DBT data;

	switch (dbp->seq(dbp, kp, &data, flags)) {
	case 0:
		(void)write(ofd, data.data, data.size);
		if (ofd == STDOUT_FILENO)
			(void)write(ofd, "\n", 1);
		break;
	case -1:
		err(1, "line %zu: seq failed", lineno);
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"seq failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else if (flags == R_CURSOR)
			(void)fprintf(stderr, "%zu: %.*s: %s", 
			    lineno, (int)MIN(kp->size, 20),
			    (const char *)kp->data, NOSUCHKEY);
		else
			(void)fprintf(stderr,
			    "%zu: seq (%s) failed\n", lineno, sflags(flags));
#undef	NOSUCHKEY
		break;
	}
}

static void
dump(DB *dbp, int rev, int recurse)
{
	DBT key, data;
	int xflags, nflags;

	if (rev) {
		xflags = R_LAST;
#ifdef __NetBSD__
		nflags = recurse ? R_RPREV : R_PREV;
#else
		nflags = R_PREV;
#endif
	} else {
		xflags = R_FIRST;
#ifdef __NetBSD__
		nflags = recurse ? R_RNEXT : R_NEXT;
#else
		nflags = R_NEXT;
#endif
	}
	for (;; xflags = nflags)
		switch (dbp->seq(dbp, &key, &data, xflags)) {
		case 0:
			(void)write(ofd, data.data, data.size);
			if (ofd == STDOUT_FILENO)
				(void)write(ofd, "\n", 1);
			break;
		case 1:
			goto done;
		case -1:
			err(1, "line %zu: (dump) seq failed", lineno);
			/* NOTREACHED */
		}
done:	return;
}
	
#ifdef __NetBSD__
void
unlinkpg(DB *dbp)
{
	BTREE *t = dbp->internal;
	PAGE *h = NULL;
	pgno_t pg;

	for (pg = P_ROOT; pg < t->bt_mp->npages;
	     mpool_put(t->bt_mp, h, 0), pg++) {
		if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
			break;
		/* Look for a nonempty leaf page that has both left
		 * and right siblings. */
		if (h->prevpg == P_INVALID || h->nextpg == P_INVALID)
			continue;
		if (NEXTINDEX(h) == 0)
			continue;
		if ((h->flags & (P_BLEAF | P_RLEAF)))
			break;
	}
	if (h == NULL || pg == t->bt_mp->npages) {
		errx(1, "%s: no appropriate page found", __func__);
		return;
	}
	if (__bt_relink(t, h) != 0) {
		perror("unlinkpg");
		goto cleanup;
	}
	h->prevpg = P_INVALID;
	h->nextpg = P_INVALID;
cleanup:
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);
}
#endif

static u_int
setflags(char *s)
{
	char *p;

	for (; isspace((unsigned char)*s); ++s);
	if (*s == '\n' || *s == '\0')
		return 0;
	if ((p = strchr(s, '\n')) != NULL)
		*p = '\0';
	if (!strcmp(s, "R_CURSOR"))		return R_CURSOR;
	if (!strcmp(s, "R_FIRST"))		return R_FIRST;
	if (!strcmp(s, "R_IAFTER")) 		return R_IAFTER;
	if (!strcmp(s, "R_IBEFORE")) 		return R_IBEFORE;
	if (!strcmp(s, "R_LAST")) 		return R_LAST;
	if (!strcmp(s, "R_NEXT")) 		return R_NEXT;
	if (!strcmp(s, "R_NOOVERWRITE"))	return R_NOOVERWRITE;
	if (!strcmp(s, "R_PREV"))		return R_PREV;
	if (!strcmp(s, "R_SETCURSOR"))		return R_SETCURSOR;

	errx(1, "line %zu: %s: unknown flag", lineno, s);
	/* NOTREACHED */
}

static const char *
sflags(int xflags)
{
	switch (xflags) {
	case R_CURSOR:		return "R_CURSOR";
	case R_FIRST:		return "R_FIRST";
	case R_IAFTER:		return "R_IAFTER";
	case R_IBEFORE:		return "R_IBEFORE";
	case R_LAST:		return "R_LAST";
	case R_NEXT:		return "R_NEXT";
	case R_NOOVERWRITE:	return "R_NOOVERWRITE";
	case R_PREV:		return "R_PREV";
	case R_SETCURSOR:	return "R_SETCURSOR";
	}

	return "UNKNOWN!";
}
	
static DBTYPE
dbtype(const char *s)
{
	if (!strcmp(s, "btree"))
		return DB_BTREE;
	if (!strcmp(s, "hash"))
		return DB_HASH;
	if (!strcmp(s, "recno"))
		return DB_RECNO;
	errx(1, "%s: unknown type (use btree, hash or recno)", s);
	/* NOTREACHED */
}

static void *
setinfo(DBTYPE dtype, char *s)
{
	static BTREEINFO ib;
	static HASHINFO ih;
	static RECNOINFO rh;
	char *eq;

	if ((eq = strchr(s, '=')) == NULL)
		errx(1, "%s: illegal structure set statement", s);
	*eq++ = '\0';
	if (!isdigit((unsigned char)*eq))
		errx(1, "%s: structure set statement must be a number", s);
		
	switch (dtype) {
	case DB_BTREE:
		if (!strcmp("flags", s)) {
			ib.flags = atoi(eq);
			return &ib;
		}
		if (!strcmp("cachesize", s)) {
			ib.cachesize = atoi(eq);
			return &ib;
		}
		if (!strcmp("maxkeypage", s)) {
			ib.maxkeypage = atoi(eq);
			return &ib;
		}
		if (!strcmp("minkeypage", s)) {
			ib.minkeypage = atoi(eq);
			return &ib;
		}
		if (!strcmp("lorder", s)) {
			ib.lorder = atoi(eq);
			return &ib;
		}
		if (!strcmp("psize", s)) {
			ib.psize = atoi(eq);
			return &ib;
		}
		break;
	case DB_HASH:
		if (!strcmp("bsize", s)) {
			ih.bsize = atoi(eq);
			return &ih;
		}
		if (!strcmp("ffactor", s)) {
			ih.ffactor = atoi(eq);
			return &ih;
		}
		if (!strcmp("nelem", s)) {
			ih.nelem = atoi(eq);
			return &ih;
		}
		if (!strcmp("cachesize", s)) {
			ih.cachesize = atoi(eq);
			return &ih;
		}
		if (!strcmp("lorder", s)) {
			ih.lorder = atoi(eq);
			return &ih;
		}
		break;
	case DB_RECNO:
		if (!strcmp("flags", s)) {
			rh.flags = atoi(eq);
			return &rh;
		}
		if (!strcmp("cachesize", s)) {
			rh.cachesize = atoi(eq);
			return &rh;
		}
		if (!strcmp("lorder", s)) {
			rh.lorder = atoi(eq);
			return &rh;
		}
		if (!strcmp("reclen", s)) {
			rh.reclen = atoi(eq);
			return &rh;
		}
		if (!strcmp("bval", s)) {
			rh.bval = atoi(eq);
			return &rh;
		}
		if (!strcmp("psize", s)) {
			rh.psize = atoi(eq);
			return &rh;
		}
		break;
	}
	errx(1, "%s: unknown structure value", s);
	/* NOTREACHED */
}

static void *
rfile(char *name, size_t *lenp)
{
	struct stat sb;
	void *p;
	int fd;
	char *np;

	for (; isspace((unsigned char)*name); ++name)
		continue;
	if ((np = strchr(name, '\n')) != NULL)
		*np = '\0';
	if ((fd = open(name, O_RDONLY, 0)) == -1 || fstat(fd, &sb) == -1)
		err(1, "Cannot open `%s'", name);
#ifdef NOT_PORTABLE
	if (sb.st_size > (off_t)SIZE_T_MAX) {
		errno = E2BIG;
		err("Cannot process `%s'", name);
	}
#endif
	if ((p = malloc((size_t)sb.st_size)) == NULL)
		err(1, "Cannot allocate %zu bytes", (size_t)sb.st_size);
	if (read(fd, p, (ssize_t)sb.st_size) != (ssize_t)sb.st_size)
		err(1, "read failed");
	*lenp = (size_t)sb.st_size;
	(void)close(fd);
	return p;
}

static void *
xcopy(void *text, size_t len)
{
	void *p;

	if ((p = malloc(len)) == NULL)
		err(1, "Cannot allocate %zu bytes", len);
	(void)memmove(p, text, len);
	return p;
}

static void
chkcmd(enum S state)
{
	if (state != COMMAND)
		errx(1, "line %zu: not expecting command", lineno);
}

static void
chkdata(enum S state)
{
	if (state != DATA)
		errx(1, "line %zu: not expecting data", lineno);
}

static void
chkkey(enum S state)
{
	if (state != KEY)
		errx(1, "line %zu: not expecting a key", lineno);
}

static void
usage(void)
{
	(void)fprintf(stderr,
#ifdef __NetBSD__
	    "Usage: %s [-lu] [-f file] [-i info] [-o file] [-O file] "
#else
	    "Usage: %s [-l] [-f file] [-i info] [-o file] "
#endif
		"type script\n", getprogname());
	exit(1);
}

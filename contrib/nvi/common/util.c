/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: util.c,v 10.30 2013/03/19 10:00:27 yamt Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>

#ifdef __APPLE__
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

/*
 * binc --
 *	Increase the size of a buffer.
 *
 * PUBLIC: void *binc(SCR *, void *, size_t *, size_t);
 */
void *
binc(
	SCR *sp,			/* sp MAY BE NULL!!! */
	void *bp,
	size_t *bsizep,
	size_t min)
{
	size_t csize;

	/* If already larger than the minimum, just return. */
	if (min && *bsizep >= min)
		return (bp);

	csize = p2roundup(MAX(min, 256));
	REALLOC(sp, bp, void *, csize);

	if (bp == NULL) {
		*bsizep = 0;
		return (NULL);
	}
	/*
	 * Memory is guaranteed to be zero-filled, various parts of
	 * nvi depend on this.
	 */
	memset((char *)bp + *bsizep, 0, csize - *bsizep);
	*bsizep = csize;
	return (bp);
}

/*
 * nonblank --
 *	Set the column number of the first non-blank character
 *	including or after the starting column.  On error, set
 *	the column to 0, it's safest.
 *
 * PUBLIC: int nonblank(SCR *, recno_t, size_t *);
 */
int
nonblank(
	SCR *sp,
	recno_t lno,
	size_t *cnop)
{
	CHAR_T *p;
	size_t cnt, len, off;
	int isempty;

	/* Default. */
	off = *cnop;
	*cnop = 0;

	/* Get the line, succeeding in an empty file. */
	if (db_eget(sp, lno, &p, &len, &isempty))
		return (!isempty);

	/* Set the offset. */
	if (len == 0 || off >= len)
		return (0);

	for (cnt = off, p = &p[off],
	    len -= off; len && ISBLANK(*p); ++cnt, ++p, --len);

	/* Set the return. */
	*cnop = len ? cnt : cnt - 1;
	return (0);
}

/*
 * tail --
 *	Return tail of a path.
 *
 * PUBLIC: char *tail(char *);
 */
char *
tail(char *path)
{
	char *p;

	if ((p = strrchr(path, '/')) == NULL)
		return (path);
	return (p + 1);
}

/*
 * join --
 *	Join two paths; need free.
 *
 * PUBLIC: char *join(char *, char *);
 */
char *
join(
	char *path1,
	char *path2)
{
	char *p;

	if (path1[0] == '\0' || path2[0] == '/')
		return strdup(path2);
	(void)asprintf(&p, path1[strlen(path1)-1] == '/' ?
	    "%s%s" : "%s/%s", path1, path2);
	return p;
}

/*
 * expanduser --
 *	Return a "~" or "~user" expanded path; need free.
 *
 * PUBLIC: char *expanduser(char *);
 */
char *
expanduser(char *str)
{
	struct passwd *pwd;
	char *p, *t, *u, *h;

	/*
	 * This function always expands the content between the
	 * leading '~' and the first '/' or '\0' from the input.
	 * Return NULL whenever we fail to do so.
	 */
	if (*str != '~')
		return (NULL);
	p = str + 1;
	for (t = p; *t != '/' && *t != '\0'; ++t)
		continue;
	if (t == p) {
		/* ~ */
		if (issetugid() != 0 ||
		    (h = getenv("HOME")) == NULL) {
			if (((h = getlogin()) != NULL &&
			     (pwd = getpwnam(h)) != NULL) ||
			    (pwd = getpwuid(getuid())) != NULL)
				h = pwd->pw_dir;
			else
				return (NULL);
		}
	} else {
		/* ~user */
		if ((u = strndup(p, t - p)) == NULL)
			return (NULL);
		if ((pwd = getpwnam(u)) == NULL) {
			free(u);
			return (NULL);
		} else
			h = pwd->pw_dir;
		free(u);
	}

	for (; *t == '/' && *t != '\0'; ++t)
		continue;
	return (join(h, t));
}

/*
 * quote --
 *	Return a escaped string for /bin/sh; need free.
 *
 * PUBLIC: char *quote(char *);
 */
char *
quote(char *str)
{
	char *p, *t;
	size_t i = 0, n = 0;
	int unsafe = 0;

	for (p = str; *p != '\0'; p++, i++) {
		if (*p == '\'')
			n++;
		if (unsafe)
			continue;
		if (isascii(*p)) {
			if (isalnum(*p))
				continue;
			switch (*p) {
			case '%': case '+': case ',': case '-': case '.':
			case '/': case ':': case '=': case '@': case '_':
				continue;
			}
		}
		unsafe = 1;
	}
	if (!unsafe)
		t = strdup(str);
#define SQT "'\\''"
	else if ((p = t = malloc(i + n * (sizeof(SQT) - 2) + 3)) != NULL) {
		*p++ = '\'';
		for (; *str != '\0'; str++) {
			if (*str == '\'') {
				(void)memcpy(p, SQT, sizeof(SQT) - 1);
				p += sizeof(SQT) - 1;
			} else
				*p++ = *str;
		}
		*p++ = '\'';
		*p = '\0';
	}
	return t;
}

/*
 * v_strdup --
 *	Strdup for 8-bit character strings with an associated length.
 *
 * PUBLIC: char *v_strdup(SCR *, const char *, size_t);
 */
char *
v_strdup(
	SCR *sp,
	const char *str,
	size_t len)
{
	char *copy;

	MALLOC(sp, copy, char *, len + 1);
	if (copy == NULL)
		return (NULL);
	memcpy(copy, str, len);
	copy[len] = '\0';
	return (copy);
}

/*
 * v_wstrdup --
 *	Strdup for wide character strings with an associated length.
 *
 * PUBLIC: CHAR_T *v_wstrdup(SCR *, const CHAR_T *, size_t);
 */
CHAR_T *
v_wstrdup(SCR *sp,
	const CHAR_T *str,
	size_t len)
{
	CHAR_T *copy;

	MALLOC(sp, copy, CHAR_T *, (len + 1) * sizeof(CHAR_T));
	if (copy == NULL)
		return (NULL);
	MEMCPY(copy, str, len);
	copy[len] = '\0';
	return (copy);
}

/*
 * nget_uslong --
 *      Get an unsigned long, checking for overflow.
 *
 * PUBLIC: enum nresult nget_uslong(u_long *, const CHAR_T *, CHAR_T **, int);
 */
enum nresult
nget_uslong(
	u_long *valp,
	const CHAR_T *p,
	CHAR_T **endp,
	int base)
{
	errno = 0;
	*valp = STRTOUL(p, endp, base);
	if (errno == 0)
		return (NUM_OK);
	if (errno == ERANGE && *valp == ULONG_MAX)
		return (NUM_OVER);
	return (NUM_ERR);
}

/*
 * nget_slong --
 *      Convert a signed long, checking for overflow and underflow.
 *
 * PUBLIC: enum nresult nget_slong(long *, const CHAR_T *, CHAR_T **, int);
 */
enum nresult
nget_slong(
	long *valp,
	const CHAR_T *p,
	CHAR_T **endp,
	int base)
{
	errno = 0;
	*valp = STRTOL(p, endp, base);
	if (errno == 0)
		return (NUM_OK);
	if (errno == ERANGE) {
		if (*valp == LONG_MAX)
			return (NUM_OVER);
		if (*valp == LONG_MIN)
			return (NUM_UNDER);
	}
	return (NUM_ERR);
}

/*
 * timepoint_steady --
 *      Get a timestamp from a monotonic clock.
 *
 * PUBLIC: void timepoint_steady(struct timespec *);
 */
void
timepoint_steady(
	struct timespec *ts)
{
#ifdef __APPLE__
	static mach_timebase_info_data_t base = { 0 };
	uint64_t val;
	uint64_t ns;

	if (base.denom == 0)
		(void)mach_timebase_info(&base);

	val = mach_absolute_time();
	ns = val * base.numer / base.denom;
	ts->tv_sec = ns / 1000000000;
	ts->tv_nsec = ns % 1000000000;
#else
#ifdef CLOCK_MONOTONIC_FAST
	(void)clock_gettime(CLOCK_MONOTONIC_FAST, ts);
#else
	(void)clock_gettime(CLOCK_MONOTONIC, ts);
#endif
#endif
}

/*
 * timepoint_system --
 *      Get the current calendar time.
 *
 * PUBLIC: void timepoint_system(struct timespec *);
 */
void
timepoint_system(
	struct timespec *ts)
{
#ifdef __APPLE__
	clock_serv_t clk;
	mach_timespec_t mts;
	kern_return_t kr;

	kr = host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &clk);
	if (kr != KERN_SUCCESS)
		return;
	(void)clock_get_time(clk, &mts);
	(void)mach_port_deallocate(mach_task_self(), clk);
	ts->tv_sec = mts.tv_sec;
	ts->tv_nsec = mts.tv_nsec;
#else
#ifdef CLOCK_REALTIME_FAST
	(void)clock_gettime(CLOCK_REALTIME_FAST, ts);
#else
	(void)clock_gettime(CLOCK_REALTIME, ts);
#endif
#endif
}

#ifdef DEBUG
#include <stdarg.h>

/*
 * TRACE --
 *	debugging trace routine.
 *
 * PUBLIC: void TRACE(SCR *, const char *, ...);
 */
void
TRACE(
	SCR *sp,
	const char *fmt,
	...)
{
	FILE *tfp;
	va_list ap;

	if ((tfp = sp->gp->tracefp) == NULL)
		return;
	va_start(ap, fmt);
	(void)vfprintf(tfp, fmt, ap);
	va_end(ap);

	(void)fflush(tfp);
}
#endif

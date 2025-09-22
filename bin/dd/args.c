/*	$OpenBSD: args.c,v 1.31 2019/02/16 10:54:00 bluhm Exp $	*/
/*	$NetBSD: args.c,v 1.7 1996/03/01 01:18:58 jtc Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego and Lance
 * Visser of Convex Computer Corporation.
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
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dd.h"
#include "extern.h"

static int	c_arg(const void *, const void *);
static void	f_bs(char *);
static void	f_cbs(char *);
static void	f_conv(char *);
static void	f_count(char *);
static void	f_files(char *);
static void	f_ibs(char *);
static void	f_if(char *);
static void	f_obs(char *);
static void	f_of(char *);
static void	f_seek(char *);
static void	f_skip(char *);
static void	f_status(char *);
static size_t	get_bsz(char *);
static off_t	get_off(char *);

static const struct arg {
	const char *name;
	void (*f)(char *);
	u_int set, noset;
} args[] = {
	{ "bs",		f_bs,		C_BS,	 C_BS|C_IBS|C_OBS|C_OSYNC },
	{ "cbs",	f_cbs,		C_CBS,	 C_CBS },
	{ "conv",	f_conv,		0,	 0 },
	{ "count",	f_count,	C_COUNT, C_COUNT },
	{ "files",	f_files,	C_FILES, C_FILES },
	{ "ibs",	f_ibs,		C_IBS,	 C_BS|C_IBS },
	{ "if",		f_if,		C_IF,	 C_IF },
	{ "obs",	f_obs,		C_OBS,	 C_BS|C_OBS },
	{ "of",		f_of,		C_OF,	 C_OF },
	{ "seek",	f_seek,		C_SEEK,	 C_SEEK },
	{ "skip",	f_skip,		C_SKIP,	 C_SKIP },
	{ "status",	f_status,	C_STATUS,C_STATUS },
};

static char *oper;

/*
 * args -- parse JCL syntax of dd.
 */
void
jcl(char **argv)
{
	struct arg *ap, tmp;
	char *arg;

	in.dbsz = out.dbsz = 512;

	while (*++argv != NULL) {
		if ((oper = strdup(*argv)) == NULL)
			err(1, NULL);
		if ((arg = strchr(oper, '=')) == NULL)
			errx(1, "unknown operand %s", oper);
		*arg++ = '\0';
		if (!*arg)
			errx(1, "no value specified for %s", oper);
		tmp.name = oper;
		if (!(ap = (struct arg *)bsearch(&tmp, args,
		    sizeof(args)/sizeof(struct arg), sizeof(struct arg),
		    c_arg)))
			errx(1, "unknown operand %s", tmp.name);
		if (ddflags & ap->noset)
			errx(1, "%s: illegal argument combination or already set",
			    tmp.name);
		ddflags |= ap->set;
		ap->f(arg);
		free(oper);
	}

	/* Final sanity checks. */

	if (ddflags & C_BS) {
		/*
		 * Bs is turned off by any conversion -- we assume the user
		 * just wanted to set both the input and output block sizes
		 * and didn't want the bs semantics, so we don't warn.
		 */
		if (ddflags & (C_BLOCK|C_LCASE|C_SWAB|C_UCASE|C_UNBLOCK))
			ddflags &= ~C_BS;

		/* Bs supersedes ibs and obs. */
		if (ddflags & C_BS && ddflags & (C_IBS|C_OBS))
			warnx("bs supersedes ibs and obs");
	}

	/*
	 * Ascii/ebcdic and cbs implies block/unblock.
	 * Block/unblock requires cbs and vice-versa.
	 */
	if (ddflags & (C_BLOCK|C_UNBLOCK)) {
		if (!(ddflags & C_CBS))
			errx(1, "record operations require cbs");
		if (cbsz == 0)
			errx(1, "cbs cannot be zero");
		cfunc = ddflags & C_BLOCK ? block : unblock;
	} else if (ddflags & C_CBS) {
		if (ddflags & (C_ASCII|C_EBCDIC)) {
			if (ddflags & C_ASCII) {
				ddflags |= C_UNBLOCK;
				cfunc = unblock;
			} else {
				ddflags |= C_BLOCK;
				cfunc = block;
			}
		} else
			errx(1, "cbs meaningless if not doing record operations");
		if (cbsz == 0)
			errx(1, "cbs cannot be zero");
	} else
		cfunc = def;

	if (in.dbsz == 0 || out.dbsz == 0)
		errx(1, "buffer sizes cannot be zero");

	/*
	 * Read and write take size_t's as arguments.  Lseek, however,
	 * takes an off_t.
	 */
	if (cbsz > SSIZE_MAX || in.dbsz > SSIZE_MAX || out.dbsz > SSIZE_MAX)
		errx(1, "buffer sizes cannot be greater than %zd",
		    (ssize_t)SSIZE_MAX);
	if (in.offset > LLONG_MAX / in.dbsz || out.offset > LLONG_MAX / out.dbsz)
		errx(1, "seek offsets cannot be larger than %lld", LLONG_MAX);
}

static int
c_arg(const void *a, const void *b)
{

	return (strcmp(((struct arg *)a)->name, ((struct arg *)b)->name));
}

static void
f_bs(char *arg)
{

	in.dbsz = out.dbsz = get_bsz(arg);
}

static void
f_cbs(char *arg)
{

	cbsz = get_bsz(arg);
}

static void
f_count(char *arg)
{

	if ((cpy_cnt = get_bsz(arg)) == 0)
		cpy_cnt = (size_t)-1;
}

static void
f_files(char *arg)
{

	files_cnt = get_bsz(arg);
}

static void
f_ibs(char *arg)
{

	if (!(ddflags & C_BS))
		in.dbsz = get_bsz(arg);
}

static void
f_if(char *arg)
{
	if ((in.name = strdup(arg)) == NULL)
		err(1, NULL);
}

static void
f_obs(char *arg)
{

	if (!(ddflags & C_BS))
		out.dbsz = get_bsz(arg);
}

static void
f_of(char *arg)
{
	if ((out.name = strdup(arg)) == NULL)
		err(1, NULL);
}

static void
f_seek(char *arg)
{

	out.offset = get_off(arg);
}

static void
f_skip(char *arg)
{

	in.offset = get_off(arg);
}

static void
f_status(char *arg)
{

	if (strcmp(arg, "none") == 0)
		ddflags |= C_NOINFO;
	else if (strcmp(arg, "noxfer") == 0)
		ddflags |= C_NOXFER;
	else
		errx(1, "unknown status %s", arg);
}


static const struct conv {
	const char *name;
	u_int set, noset;
	const u_char *ctab;
} clist[] = {
#ifndef	NO_CONV
	{ "ascii",	C_ASCII,	C_EBCDIC,	e2a_POSIX },
	{ "block",	C_BLOCK,	C_UNBLOCK,	NULL },
	{ "ebcdic",	C_EBCDIC,	C_ASCII,	a2e_POSIX },
	{ "fsync",	C_FSYNC,	0,		NULL },
	{ "ibm",	C_EBCDIC,	C_ASCII,	a2ibm_POSIX },
	{ "lcase",	C_LCASE,	C_UCASE,	NULL },
	{ "osync",	C_OSYNC,	C_BS,		NULL },
	{ "swab",	C_SWAB,		0,		NULL },
	{ "sync",	C_SYNC,		0,		NULL },
	{ "ucase",	C_UCASE,	C_LCASE,	NULL },
	{ "unblock",	C_UNBLOCK,	C_BLOCK,	NULL },
#endif
	{ "noerror",	C_NOERROR,	0,		NULL },
	{ "notrunc",	C_NOTRUNC,	0,		NULL },
	{ NULL,		0,		0,		NULL }
};

static void
f_conv(char *arg)
{
	const struct conv *cp;
	const char *name;

	while (arg != NULL) {
		name = strsep(&arg, ",");
		for (cp = &clist[0]; cp->name; cp++)
			if (strcmp(name, cp->name) == 0)
				break;
		if (!cp->name)
			errx(1, "unknown conversion %s", name);
		if (ddflags & cp->noset)
			errx(1, "%s: illegal conversion combination", name);
		ddflags |= cp->set;
		if (cp->ctab)
			ctab = cp->ctab;
	}
}

/*
 * Convert an expression of the following forms to a size_t
 *	1) A positive decimal number, optionally followed by
 *		b - multiply by 512.
 *		k, m or g - multiply by 1024 each.
 *		w - multiply by sizeof int
 *	2) Two or more of the above, separated by x
 *	   (or * for backwards compatibility), specifying
 *	   the product of the indicated values.
 */
static size_t
get_bsz(char *val)
{
	size_t num, t;
	char *expr;

	if (strchr(val, '-'))
		errx(1, "%s: illegal numeric value", oper);

	errno = 0;
	num = strtoul(val, &expr, 0);
	if (num == ULONG_MAX && errno == ERANGE)	/* Overflow. */
		err(1, "%s", oper);
	if (expr == val)			/* No digits. */
		errx(1, "%s: illegal numeric value", oper);

	switch(*expr) {
	case 'b':
		t = num;
		num *= 512;
		if (t > num)
			goto erange;
		++expr;
		break;
	case 'g':
	case 'G':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		/* fallthrough */
	case 'm':
	case 'M':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		/* fallthrough */
	case 'k':
	case 'K':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		++expr;
		break;
	case 'w':
		t = num;
		num *= sizeof(int);
		if (t > num)
			goto erange;
		++expr;
		break;
	}

	switch(*expr) {
		case '\0':
			break;
		case '*':			/* Backward compatible. */
		case 'x':
			t = num;
			num *= get_bsz(expr + 1);
			if (t > num)
				goto erange;
			break;
		default:
			errx(1, "%s: illegal numeric value", oper);
	}
	return (num);
erange:
	errc(1, ERANGE, "%s", oper);
}

/*
 * Convert an expression of the following forms to an off_t
 *	1) A positive decimal number, optionally followed by
 *		b - multiply by 512.
 *		k, m or g - multiply by 1024 each.
 *		w - multiply by sizeof int
 *	2) Two or more of the above, separated by x
 *	   (or * for backwards compatibility), specifying
 *	   the product of the indicated values.
 */
static off_t
get_off(char *val)
{
	off_t num, t;
	char *expr;

	errno = 0;
	num = strtoll(val, &expr, 0);
	if (num == LLONG_MAX && errno == ERANGE)	/* Overflow. */
		err(1, "%s", oper);
	if (expr == val)			/* No digits. */
		errx(1, "%s: illegal numeric value", oper);

	switch(*expr) {
	case 'b':
		t = num;
		num *= 512;
		if (t > num)
			goto erange;
		++expr;
		break;
	case 'g':
	case 'G':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		/* fallthrough */
	case 'm':
	case 'M':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		/* fallthrough */
	case 'k':
	case 'K':
		t = num;
		num *= 1024;
		if (t > num)
			goto erange;
		++expr;
		break;
	case 'w':
		t = num;
		num *= sizeof(int);
		if (t > num)
			goto erange;
		++expr;
		break;
	}

	switch(*expr) {
		case '\0':
			break;
		case '*':			/* Backward compatible. */
		case 'x':
			t = num;
			num *= get_off(expr + 1);
			if (t > num)
				goto erange;
			break;
		default:
			errx(1, "%s: illegal numeric value", oper);
	}
	return (num);
erange:
	errc(1, ERANGE, "%s", oper);
}

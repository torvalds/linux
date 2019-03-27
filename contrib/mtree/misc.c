/*	$NetBSD: misc.c,v 1.34 2012/12/20 19:09:25 christos Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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
 *
 *	@(#)misc.c	8.1 (Berkeley) 6/6/93
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#if defined(__RCSID) && !defined(lint)
__RCSID("$NetBSD: misc.c,v 1.34 2012/12/20 19:09:25 christos Exp $");
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

enum flavor	flavor = F_MTREE;

typedef struct _key {
	const char	*name;		/* key name */
	u_int		val;		/* value */

#define	NEEDVALUE	0x01
	u_int		flags;
} KEY;

/* NB: the following tables must be sorted lexically. */
static KEY keylist[] = {
	{"cksum",	F_CKSUM,	NEEDVALUE},
	{"device",	F_DEV,		NEEDVALUE},
	{"flags",	F_FLAGS,	NEEDVALUE},
	{"gid",		F_GID,		NEEDVALUE},
	{"gname",	F_GNAME,	NEEDVALUE},
	{"ignore",	F_IGN,		0},
	{"link",	F_SLINK,	NEEDVALUE},
	{"md5",		F_MD5,		NEEDVALUE},
	{"md5digest",	F_MD5,		NEEDVALUE},
	{"mode",	F_MODE,		NEEDVALUE},
	{"nlink",	F_NLINK,	NEEDVALUE},
	{"nochange",	F_NOCHANGE,	0},
	{"optional",	F_OPT,		0},
	{"ripemd160digest", F_RMD160,	NEEDVALUE},
	{"rmd160",	F_RMD160,	NEEDVALUE},
	{"rmd160digest",F_RMD160,	NEEDVALUE},
	{"sha1",	F_SHA1,		NEEDVALUE},
	{"sha1digest",	F_SHA1,		NEEDVALUE},
	{"sha256",	F_SHA256,	NEEDVALUE},
	{"sha256digest",F_SHA256,	NEEDVALUE},
	{"sha384",	F_SHA384,	NEEDVALUE},
	{"sha384digest",F_SHA384,	NEEDVALUE},
	{"sha512",	F_SHA512,	NEEDVALUE},
	{"sha512digest",F_SHA512,	NEEDVALUE},
	{"size",	F_SIZE,		NEEDVALUE},
	{"tags",	F_TAGS,		NEEDVALUE},
	{"time",	F_TIME,		NEEDVALUE},
	{"type",	F_TYPE,		NEEDVALUE},
	{"uid",		F_UID,		NEEDVALUE},
	{"uname",	F_UNAME,	NEEDVALUE}
};

static KEY typelist[] = {
	{"block",	F_BLOCK,	0},
	{"char",	F_CHAR,		0},
	{"dir",		F_DIR,		0},
#ifdef S_IFDOOR
	{"door",	F_DOOR,		0},
#endif
	{"fifo",	F_FIFO,		0},
	{"file",	F_FILE,		0},
	{"link",	F_LINK,		0},
	{"socket",	F_SOCK,		0},
};

slist_t	excludetags, includetags;
int	keys = KEYDEFAULT;


int keycompare(const void *, const void *);

u_int
parsekey(const char *name, int *needvaluep)
{
	static int allbits;
	KEY *k, tmp;

	if (allbits == 0) {
		size_t i;

		for (i = 0; i < sizeof(keylist) / sizeof(KEY); i++)
			allbits |= keylist[i].val;
	}
	tmp.name = name;
	if (strcmp(name, "all") == 0)
		return (allbits);
	k = (KEY *)bsearch(&tmp, keylist, sizeof(keylist) / sizeof(KEY),
	    sizeof(KEY), keycompare);
	if (k == NULL)
		mtree_err("unknown keyword `%s'", name);

	if (needvaluep)
		*needvaluep = k->flags & NEEDVALUE ? 1 : 0;

	return (k->val);
}

u_int
parsetype(const char *name)
{
	KEY *k, tmp;

	tmp.name = name;
	k = (KEY *)bsearch(&tmp, typelist, sizeof(typelist) / sizeof(KEY),
	    sizeof(KEY), keycompare);
	if (k == NULL)
		mtree_err("unknown file type `%s'", name);

	return (k->val);
}

int
keycompare(const void *a, const void *b)
{

	return (strcmp(((const KEY *)a)->name, ((const KEY *)b)->name));
}

void
mtree_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	if (mtree_lineno)
		warnx("failed at line %lu of the specification",
		    (u_long) mtree_lineno);
	exit(1);
	/* NOTREACHED */
}

void
addtag(slist_t *list, char *elem)
{

#define	TAG_CHUNK 20

	if ((list->count % TAG_CHUNK) == 0) {
		char **new;

		new = (char **)realloc(list->list, (list->count + TAG_CHUNK)
		    * sizeof(char *));
		if (new == NULL)
			mtree_err("memory allocation error");
		list->list = new;
	}
	list->list[list->count] = elem;
	list->count++;
}

void
parsetags(slist_t *list, char *args)
{
	char	*p, *e;
	int	len;

	if (args == NULL) {
		addtag(list, NULL);
		return;
	}
	while ((p = strsep(&args, ",")) != NULL) {
		if (*p == '\0')
			continue;
		len = strlen(p) + 3;	/* "," + p + ",\0" */
		if ((e = malloc(len)) == NULL)
			mtree_err("memory allocation error");
		snprintf(e, len, ",%s,", p);
		addtag(list, e);
	}
}

/*
 * matchtags
 *	returns 0 if there's a match from the exclude list in the node's tags,
 *	or there's an include list and no match.
 *	return 1 otherwise.
 */
int
matchtags(NODE *node)
{
	int	i;

	if (node->tags) {
		for (i = 0; i < excludetags.count; i++)
			if (strstr(node->tags, excludetags.list[i]))
				break;
		if (i < excludetags.count)
			return (0);

		for (i = 0; i < includetags.count; i++)
			if (strstr(node->tags, includetags.list[i]))
				break;
		if (i > 0 && i == includetags.count)
			return (0);
	} else if (includetags.count > 0) {
		return (0);
	}
	return (1);
}

u_int
nodetoino(u_int type)
{

	switch (type) {
	case F_BLOCK:
		return S_IFBLK;
	case F_CHAR:
		return S_IFCHR;
	case F_DIR:
		return S_IFDIR;
	case F_FIFO:
		return S_IFIFO;
	case F_FILE:
		return S_IFREG;
	case F_LINK:
		return S_IFLNK;
#ifdef S_IFSOCK
	case F_SOCK:
		return S_IFSOCK;
#endif
	default:
		printf("unknown type %d", type);
		abort();
	}
	/* NOTREACHED */
}

const char *
nodetype(u_int type)
{

	return (inotype(nodetoino(type)));
}


const char *
inotype(u_int type)
{

	switch (type & S_IFMT) {
	case S_IFBLK:
		return ("block");
	case S_IFCHR:
		return ("char");
	case S_IFDIR:
		return ("dir");
	case S_IFIFO:
		return ("fifo");
	case S_IFREG:
		return ("file");
	case S_IFLNK:
		return ("link");
#ifdef S_IFSOCK
	case S_IFSOCK:
		return ("socket");
#endif
#ifdef S_IFDOOR
	case S_IFDOOR:
		return ("door");
#endif
	default:
		return ("unknown");
	}
	/* NOTREACHED */
}

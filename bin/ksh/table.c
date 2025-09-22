/*	$OpenBSD: table.c,v 1.25 2018/01/16 22:52:32 jca Exp $	*/

/*
 * dynamic hashed associative table for commands and variables
 */

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "sh.h"

#define	INIT_TBLS	8	/* initial table size (power of 2) */

struct table taliases;	/* tracked aliases */
struct table builtins;	/* built-in commands */
struct table aliases;	/* aliases */
struct table keywords;	/* keywords */
struct table homedirs;	/* homedir() cache */

char *search_path;	/* copy of either PATH or def_path */
const char *def_path;	/* path to use if PATH not set */
char *tmpdir;		/* TMPDIR value */
const char *prompt;
int cur_prompt;		/* PS1 or PS2 */
int current_lineno;	/* LINENO value */

static void	texpand(struct table *, int);
static int	tnamecmp(const void *, const void *);


unsigned int
hash(const char *n)
{
	unsigned int h = 0;

	while (*n != '\0')
		h = 33*h + (unsigned char)(*n++);
	return h;
}

void
ktinit(struct table *tp, Area *ap, int tsize)
{
	tp->areap = ap;
	tp->tbls = NULL;
	tp->size = tp->nfree = 0;
	if (tsize)
		texpand(tp, tsize);
}

static void
texpand(struct table *tp, int nsize)
{
	int i;
	struct tbl *tblp, **p;
	struct tbl **ntblp, **otblp = tp->tbls;
	int osize = tp->size;

	ntblp = areallocarray(NULL, nsize, sizeof(struct tbl *), tp->areap);
	for (i = 0; i < nsize; i++)
		ntblp[i] = NULL;
	tp->size = nsize;
	tp->nfree = 7*nsize/10;	/* table can get 70% full */
	tp->tbls = ntblp;
	if (otblp == NULL)
		return;
	for (i = 0; i < osize; i++)
		if ((tblp = otblp[i]) != NULL) {
			if ((tblp->flag&DEFINED)) {
				for (p = &ntblp[hash(tblp->name) &
				    (tp->size-1)]; *p != NULL; p--)
					if (p == ntblp) /* wrap */
						p += tp->size;
				*p = tblp;
				tp->nfree--;
			} else if (!(tblp->flag & FINUSE)) {
				afree(tblp, tp->areap);
			}
		}
	afree(otblp, tp->areap);
}

/* table */
/* name to enter */
/* hash(n) */
struct tbl *
ktsearch(struct table *tp, const char *n, unsigned int h)
{
	struct tbl **pp, *p;

	if (tp->size == 0)
		return NULL;

	/* search for name in hashed table */
	for (pp = &tp->tbls[h & (tp->size-1)]; (p = *pp) != NULL; pp--) {
		if (*p->name == *n && strcmp(p->name, n) == 0 &&
		    (p->flag&DEFINED))
			return p;
		if (pp == tp->tbls) /* wrap */
			pp += tp->size;
	}

	return NULL;
}

/* table */
/* name to enter */
/* hash(n) */
struct tbl *
ktenter(struct table *tp, const char *n, unsigned int h)
{
	struct tbl **pp, *p;
	int len;

	if (tp->size == 0)
		texpand(tp, INIT_TBLS);
  Search:
	/* search for name in hashed table */
	for (pp = &tp->tbls[h & (tp->size-1)]; (p = *pp) != NULL; pp--) {
		if (*p->name == *n && strcmp(p->name, n) == 0)
			return p;	/* found */
		if (pp == tp->tbls) /* wrap */
			pp += tp->size;
	}

	if (tp->nfree <= 0) {	/* too full */
		if (tp->size <= INT_MAX/2)
			texpand(tp, 2*tp->size);
		else
			internal_errorf("too many vars");
		goto Search;
	}

	/* create new tbl entry */
	len = strlen(n) + 1;
	p = alloc(offsetof(struct tbl, name[0]) + len,
				 tp->areap);
	p->flag = 0;
	p->type = 0;
	p->areap = tp->areap;
	p->u2.field = 0;
	p->u.array = NULL;
	memcpy(p->name, n, len);

	/* enter in tp->tbls */
	tp->nfree--;
	*pp = p;
	return p;
}

void
ktdelete(struct tbl *p)
{
	p->flag = 0;
}

void
ktwalk(struct tstate *ts, struct table *tp)
{
	ts->left = tp->size;
	ts->next = tp->tbls;
}

struct tbl *
ktnext(struct tstate *ts)
{
	while (--ts->left >= 0) {
		struct tbl *p = *ts->next++;
		if (p != NULL && (p->flag&DEFINED))
			return p;
	}
	return NULL;
}

static int
tnamecmp(const void *p1, const void *p2)
{
	char *name1 = (*(struct tbl **)p1)->name;
	char *name2 = (*(struct tbl **)p2)->name;
	return strcmp(name1, name2);
}

struct tbl **
ktsort(struct table *tp)
{
	int i;
	struct tbl **p, **sp, **dp;

	p = areallocarray(NULL, tp->size + 1,
	    sizeof(struct tbl *), ATEMP);
	sp = tp->tbls;		/* source */
	dp = p;			/* dest */
	for (i = 0; i < tp->size; i++)
		if ((*dp = *sp++) != NULL && (((*dp)->flag&DEFINED) ||
		    ((*dp)->flag&ARRAY)))
			dp++;
	i = dp - p;
	qsortp((void**)p, (size_t)i, tnamecmp);
	p[i] = NULL;
	return p;
}

#ifdef PERF_DEBUG /* performance debugging */

void tprintinfo(struct table *tp);

void
tprintinfo(struct table *tp)
{
	struct tbl *te;
	char *n;
	unsigned int h;
	int ncmp;
	int totncmp = 0, maxncmp = 0;
	int nentries = 0;
	struct tstate ts;

	shellf("table size %d, nfree %d\n", tp->size, tp->nfree);
	shellf("    Ncmp name\n");
	ktwalk(&ts, tp);
	while ((te = ktnext(&ts))) {
		struct tbl **pp, *p;

		h = hash(n = te->name);
		ncmp = 0;

		/* taken from ktsearch() and added counter */
		for (pp = &tp->tbls[h & (tp->size-1)]; (p = *pp); pp--) {
			ncmp++;
			if (*p->name == *n && strcmp(p->name, n) == 0 &&
			    (p->flag&DEFINED))
				break; /* return p; */
			if (pp == tp->tbls) /* wrap */
				pp += tp->size;
		}
		shellf("    %4d %s\n", ncmp, n);
		totncmp += ncmp;
		nentries++;
		if (ncmp > maxncmp)
			maxncmp = ncmp;
	}
	if (nentries)
		shellf("  %d entries, worst ncmp %d, avg ncmp %d.%02d\n",
		    nentries, maxncmp,
		    totncmp / nentries,
		    (totncmp % nentries) * 100 / nentries);
}
#endif /* PERF_DEBUG */

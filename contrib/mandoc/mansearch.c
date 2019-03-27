/*	$Id: mansearch.c,v 1.77 2017/08/22 17:50:11 schwarze Exp $ */
/*
 * Copyright (c) 2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2013-2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/mman.h>
#include <sys/types.h>

#include <assert.h>
#if HAVE_ERR
#include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc.h"
#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "manconf.h"
#include "mansearch.h"
#include "dbm.h"

struct	expr {
	/* Used for terms: */
	struct dbm_match match;   /* Match type and expression. */
	uint64_t	 bits;    /* Type mask. */
	/* Used for OR and AND groups: */
	struct expr	*next;    /* Next child in the parent group. */
	struct expr	*child;   /* First child in this group. */
	enum { EXPR_TERM, EXPR_OR, EXPR_AND } type;
};

const char *const mansearch_keynames[KEY_MAX] = {
	"arch",	"sec",	"Xr",	"Ar",	"Fa",	"Fl",	"Dv",	"Fn",
	"Ic",	"Pa",	"Cm",	"Li",	"Em",	"Cd",	"Va",	"Ft",
	"Tn",	"Er",	"Ev",	"Sy",	"Sh",	"In",	"Ss",	"Ox",
	"An",	"Mt",	"St",	"Bx",	"At",	"Nx",	"Fx",	"Lk",
	"Ms",	"Bsx",	"Dx",	"Rs",	"Vt",	"Lb",	"Nm",	"Nd"
};


static	struct ohash	*manmerge(struct expr *, struct ohash *);
static	struct ohash	*manmerge_term(struct expr *, struct ohash *);
static	struct ohash	*manmerge_or(struct expr *, struct ohash *);
static	struct ohash	*manmerge_and(struct expr *, struct ohash *);
static	char		*buildnames(const struct dbm_page *);
static	char		*buildoutput(size_t, struct dbm_page *);
static	size_t		 lstlen(const char *, size_t);
static	void		 lstcat(char *, size_t *, const char *, const char *);
static	int		 lstmatch(const char *, const char *);
static	struct expr	*exprcomp(const struct mansearch *,
				int, char *[], int *);
static	struct expr	*expr_and(const struct mansearch *,
				int, char *[], int *);
static	struct expr	*exprterm(const struct mansearch *,
				int, char *[], int *);
static	void		 exprfree(struct expr *);
static	int		 manpage_compare(const void *, const void *);


int
mansearch(const struct mansearch *search,
		const struct manpaths *paths,
		int argc, char *argv[],
		struct manpage **res, size_t *sz)
{
	char		 buf[PATH_MAX];
	struct dbm_res	*rp;
	struct expr	*e;
	struct dbm_page	*page;
	struct manpage	*mpage;
	struct ohash	*htab;
	size_t		 cur, i, maxres, outkey;
	unsigned int	 slot;
	int		 argi, chdir_status, getcwd_status, im;

	argi = 0;
	if ((e = exprcomp(search, argc, argv, &argi)) == NULL) {
		*sz = 0;
		return 0;
	}

	cur = maxres = 0;
	if (res != NULL)
		*res = NULL;

	outkey = KEY_Nd;
	if (search->outkey != NULL)
		for (im = 0; im < KEY_MAX; im++)
			if (0 == strcasecmp(search->outkey,
			    mansearch_keynames[im])) {
				outkey = im;
				break;
			}

	/*
	 * Remember the original working directory, if possible.
	 * This will be needed if the second or a later directory
	 * is given as a relative path.
	 * Do not error out if the current directory is not
	 * searchable: Maybe it won't be needed after all.
	 */

	if (getcwd(buf, PATH_MAX) == NULL) {
		getcwd_status = 0;
		(void)strlcpy(buf, strerror(errno), sizeof(buf));
	} else
		getcwd_status = 1;

	/*
	 * Loop over the directories (containing databases) for us to
	 * search.
	 * Don't let missing/bad databases/directories phase us.
	 * In each, try to open the resident database and, if it opens,
	 * scan it for our match expression.
	 */

	chdir_status = 0;
	for (i = 0; i < paths->sz; i++) {
		if (chdir_status && paths->paths[i][0] != '/') {
			if ( ! getcwd_status) {
				warnx("%s: getcwd: %s", paths->paths[i], buf);
				continue;
			} else if (chdir(buf) == -1) {
				warn("%s", buf);
				continue;
			}
		}
		if (chdir(paths->paths[i]) == -1) {
			warn("%s", paths->paths[i]);
			continue;
		}
		chdir_status = 1;

		if (dbm_open(MANDOC_DB) == -1) {
			if (errno != ENOENT)
				warn("%s/%s", paths->paths[i], MANDOC_DB);
			continue;
		}

		if ((htab = manmerge(e, NULL)) == NULL) {
			dbm_close();
			continue;
		}

		for (rp = ohash_first(htab, &slot); rp != NULL;
		    rp = ohash_next(htab, &slot)) {
			page = dbm_page_get(rp->page);

			if (lstmatch(search->sec, page->sect) == 0 ||
			    lstmatch(search->arch, page->arch) == 0 ||
			    (search->argmode == ARG_NAME &&
			     rp->bits <= (int32_t)(NAME_SYN & NAME_MASK)))
				continue;

			if (res == NULL) {
				cur = 1;
				break;
			}
			if (cur + 1 > maxres) {
				maxres += 1024;
				*res = mandoc_reallocarray(*res,
				    maxres, sizeof(**res));
			}
			mpage = *res + cur;
			mandoc_asprintf(&mpage->file, "%s/%s",
			    paths->paths[i], page->file + 1);
			if (access(chdir_status ? page->file + 1 :
			    mpage->file, R_OK) == -1) {
				warn("%s", mpage->file);
				warnx("outdated mandoc.db contains "
				    "bogus %s entry, run makewhatis %s", 
				    page->file + 1, paths->paths[i]);
				free(mpage->file);
				free(rp);
				continue;
			}
			mpage->names = buildnames(page);
			mpage->output = buildoutput(outkey, page);
			mpage->ipath = i;
			mpage->bits = rp->bits;
			mpage->sec = *page->sect - '0';
			if (mpage->sec < 0 || mpage->sec > 9)
				mpage->sec = 10;
			mpage->form = *page->file;
			free(rp);
			cur++;
		}
		ohash_delete(htab);
		free(htab);
		dbm_close();

		/*
		 * In man(1) mode, prefer matches in earlier trees
		 * over matches in later trees.
		 */

		if (cur && search->firstmatch)
			break;
	}
	if (res != NULL)
		qsort(*res, cur, sizeof(struct manpage), manpage_compare);
	if (chdir_status && getcwd_status && chdir(buf) == -1)
		warn("%s", buf);
	exprfree(e);
	*sz = cur;
	return res != NULL || cur;
}

/*
 * Merge the results for the expression tree rooted at e
 * into the the result list htab.
 */
static struct ohash *
manmerge(struct expr *e, struct ohash *htab)
{
	switch (e->type) {
	case EXPR_TERM:
		return manmerge_term(e, htab);
	case EXPR_OR:
		return manmerge_or(e->child, htab);
	case EXPR_AND:
		return manmerge_and(e->child, htab);
	default:
		abort();
	}
}

static struct ohash *
manmerge_term(struct expr *e, struct ohash *htab)
{
	struct dbm_res	 res, *rp;
	uint64_t	 ib;
	unsigned int	 slot;
	int		 im;

	if (htab == NULL) {
		htab = mandoc_malloc(sizeof(*htab));
		mandoc_ohash_init(htab, 4, offsetof(struct dbm_res, page));
	}

	for (im = 0, ib = 1; im < KEY_MAX; im++, ib <<= 1) {
		if ((e->bits & ib) == 0)
			continue;

		switch (ib) {
		case TYPE_arch:
			dbm_page_byarch(&e->match);
			break;
		case TYPE_sec:
			dbm_page_bysect(&e->match);
			break;
		case TYPE_Nm:
			dbm_page_byname(&e->match);
			break;
		case TYPE_Nd:
			dbm_page_bydesc(&e->match);
			break;
		default:
			dbm_page_bymacro(im - 2, &e->match);
			break;
		}

		/*
		 * When hashing for deduplication, use the unique
		 * page ID itself instead of a hash function;
		 * that is quite efficient.
		 */

		for (;;) {
			res = dbm_page_next();
			if (res.page == -1)
				break;
			slot = ohash_lookup_memory(htab,
			    (char *)&res, sizeof(res.page), res.page);
			if ((rp = ohash_find(htab, slot)) != NULL) {
				rp->bits |= res.bits;
				continue;
			}
			rp = mandoc_malloc(sizeof(*rp));
			*rp = res;
			ohash_insert(htab, slot, rp);
		}
	}
	return htab;
}

static struct ohash *
manmerge_or(struct expr *e, struct ohash *htab)
{
	while (e != NULL) {
		htab = manmerge(e, htab);
		e = e->next;
	}
	return htab;
}

static struct ohash *
manmerge_and(struct expr *e, struct ohash *htab)
{
	struct ohash	*hand, *h1, *h2;
	struct dbm_res	*res;
	unsigned int	 slot1, slot2;

	/* Evaluate the first term of the AND clause. */

	hand = manmerge(e, NULL);

	while ((e = e->next) != NULL) {

		/* Evaluate the next term and prepare for ANDing. */

		h2 = manmerge(e, NULL);
		if (ohash_entries(h2) < ohash_entries(hand)) {
			h1 = h2;
			h2 = hand;
		} else
			h1 = hand;
		hand = mandoc_malloc(sizeof(*hand));
		mandoc_ohash_init(hand, 4, offsetof(struct dbm_res, page));

		/* Keep all pages that are in both result sets. */

		for (res = ohash_first(h1, &slot1); res != NULL;
		    res = ohash_next(h1, &slot1)) {
			if (ohash_find(h2, ohash_lookup_memory(h2,
			    (char *)res, sizeof(res->page),
			    res->page)) == NULL)
				free(res);
			else
				ohash_insert(hand, ohash_lookup_memory(hand,
				    (char *)res, sizeof(res->page),
				    res->page), res);
		}

		/* Discard the merged results. */

		for (res = ohash_first(h2, &slot2); res != NULL;
		    res = ohash_next(h2, &slot2))
			free(res);
		ohash_delete(h2);
		free(h2);
		ohash_delete(h1);
		free(h1);
	}

	/* Merge the result of the AND into htab. */

	if (htab == NULL)
		return hand;

	for (res = ohash_first(hand, &slot1); res != NULL;
	    res = ohash_next(hand, &slot1)) {
		slot2 = ohash_lookup_memory(htab,
		    (char *)res, sizeof(res->page), res->page);
		if (ohash_find(htab, slot2) == NULL)
			ohash_insert(htab, slot2, res);
		else
			free(res);
	}

	/* Discard the merged result. */

	ohash_delete(hand);
	free(hand);
	return htab;
}

void
mansearch_free(struct manpage *res, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++) {
		free(res[i].file);
		free(res[i].names);
		free(res[i].output);
	}
	free(res);
}

static int
manpage_compare(const void *vp1, const void *vp2)
{
	const struct manpage	*mp1, *mp2;
	const char		*cp1, *cp2;
	size_t			 sz1, sz2;
	int			 diff;

	mp1 = vp1;
	mp2 = vp2;
	if ((diff = mp2->bits - mp1->bits) ||
	    (diff = mp1->sec - mp2->sec))
		return diff;

	/* Fall back to alphabetic ordering of names. */
	sz1 = strcspn(mp1->names, "(");
	sz2 = strcspn(mp2->names, "(");
	if (sz1 < sz2)
		sz1 = sz2;
	if ((diff = strncasecmp(mp1->names, mp2->names, sz1)))
		return diff;

	/* For identical names and sections, prefer arch-dependent. */
	cp1 = strchr(mp1->names + sz1, '/');
	cp2 = strchr(mp2->names + sz2, '/');
	return cp1 != NULL && cp2 != NULL ? strcasecmp(cp1, cp2) :
	    cp1 != NULL ? -1 : cp2 != NULL ? 1 : 0;
}

static char *
buildnames(const struct dbm_page *page)
{
	char	*buf;
	size_t	 i, sz;

	sz = lstlen(page->name, 2) + 1 + lstlen(page->sect, 2) +
	    (page->arch == NULL ? 0 : 1 + lstlen(page->arch, 2)) + 2;
	buf = mandoc_malloc(sz);
	i = 0;
	lstcat(buf, &i, page->name, ", ");
	buf[i++] = '(';
	lstcat(buf, &i, page->sect, ", ");
	if (page->arch != NULL) {
		buf[i++] = '/';
		lstcat(buf, &i, page->arch, ", ");
	}
	buf[i++] = ')';
	buf[i++] = '\0';
	assert(i == sz);
	return buf;
}

/*
 * Count the buffer space needed to print the NUL-terminated
 * list of NUL-terminated strings, when printing sep separator
 * characters between strings.
 */
static size_t
lstlen(const char *cp, size_t sep)
{
	size_t	 sz;

	for (sz = 0; *cp != '\0'; cp++) {

		/* Skip names appearing only in the SYNOPSIS. */
		if (*cp <= (char)(NAME_SYN & NAME_MASK)) {
			while (*cp != '\0')
				cp++;
			continue;
		}

		/* Skip name class markers. */
		if (*cp < ' ')
			cp++;

		/* Print a separator before each but the first string. */
		if (sz)
			sz += sep;

		/* Copy one string. */
		while (*cp != '\0') {
			sz++;
			cp++;
		}
	}
	return sz;
}

/*
 * Print the NUL-terminated list of NUL-terminated strings
 * into the buffer, seperating strings with sep.
 */
static void
lstcat(char *buf, size_t *i, const char *cp, const char *sep)
{
	const char	*s;
	size_t		 i_start;

	for (i_start = *i; *cp != '\0'; cp++) {

		/* Skip names appearing only in the SYNOPSIS. */
		if (*cp <= (char)(NAME_SYN & NAME_MASK)) {
			while (*cp != '\0')
				cp++;
			continue;
		}

		/* Skip name class markers. */
		if (*cp < ' ')
			cp++;

		/* Print a separator before each but the first string. */
		if (*i > i_start) {
			s = sep;
			while (*s != '\0')
				buf[(*i)++] = *s++;
		}

		/* Copy one string. */
		while (*cp != '\0')
			buf[(*i)++] = *cp++;
	}

}

/*
 * Return 1 if the string *want occurs in any of the strings
 * in the NUL-terminated string list *have, or 0 otherwise.
 * If either argument is NULL or empty, assume no filtering
 * is desired and return 1.
 */
static int
lstmatch(const char *want, const char *have)
{
        if (want == NULL || have == NULL || *have == '\0')
                return 1;
        while (*have != '\0') {
                if (strcasestr(have, want) != NULL)
                        return 1;
                have = strchr(have, '\0') + 1;
        }
        return 0;
}

/*
 * Build a list of values taken by the macro im in the manual page.
 */
static char *
buildoutput(size_t im, struct dbm_page *page)
{
	const char	*oldoutput, *sep, *input;
	char		*output, *newoutput, *value;
	size_t		 sz, i;

	switch (im) {
	case KEY_Nd:
		return mandoc_strdup(page->desc);
	case KEY_Nm:
		input = page->name;
		break;
	case KEY_sec:
		input = page->sect;
		break;
	case KEY_arch:
		input = page->arch;
		if (input == NULL)
			input = "all\0";
		break;
	default:
		input = NULL;
		break;
	}

	if (input != NULL) {
		sz = lstlen(input, 3) + 1;
		output = mandoc_malloc(sz);
		i = 0;
		lstcat(output, &i, input, " # ");
		output[i++] = '\0';
		assert(i == sz);
		return output;
	}

	output = NULL;
	dbm_macro_bypage(im - 2, page->addr);
	while ((value = dbm_macro_next()) != NULL) {
		if (output == NULL) {
			oldoutput = "";
			sep = "";
		} else {
			oldoutput = output;
			sep = " # ";
		}
		mandoc_asprintf(&newoutput, "%s%s%s", oldoutput, sep, value);
		free(output);
		output = newoutput;
	}
	return output;
}

/*
 * Compile a set of string tokens into an expression.
 * Tokens in "argv" are assumed to be individual expression atoms (e.g.,
 * "(", "foo=bar", etc.).
 */
static struct expr *
exprcomp(const struct mansearch *search, int argc, char *argv[], int *argi)
{
	struct expr	*parent, *child;
	int		 needterm, nested;

	if ((nested = *argi) == argc)
		return NULL;
	needterm = 1;
	parent = child = NULL;
	while (*argi < argc) {
		if (strcmp(")", argv[*argi]) == 0) {
			if (needterm)
				warnx("missing term "
				    "before closing parenthesis");
			needterm = 0;
			if (nested)
				break;
			warnx("ignoring unmatched right parenthesis");
			++*argi;
			continue;
		}
		if (strcmp("-o", argv[*argi]) == 0) {
			if (needterm) {
				if (*argi > 0)
					warnx("ignoring -o after %s",
					    argv[*argi - 1]);
				else
					warnx("ignoring initial -o");
			}
			needterm = 1;
			++*argi;
			continue;
		}
		needterm = 0;
		if (child == NULL) {
			child = expr_and(search, argc, argv, argi);
			continue;
		}
		if (parent == NULL) {
			parent = mandoc_calloc(1, sizeof(*parent));
			parent->type = EXPR_OR;
			parent->next = NULL;
			parent->child = child;
		}
		child->next = expr_and(search, argc, argv, argi);
		child = child->next;
	}
	if (needterm && *argi)
		warnx("ignoring trailing %s", argv[*argi - 1]);
	return parent == NULL ? child : parent;
}

static struct expr *
expr_and(const struct mansearch *search, int argc, char *argv[], int *argi)
{
	struct expr	*parent, *child;
	int		 needterm;

	needterm = 1;
	parent = child = NULL;
	while (*argi < argc) {
		if (strcmp(")", argv[*argi]) == 0) {
			if (needterm)
				warnx("missing term "
				    "before closing parenthesis");
			needterm = 0;
			break;
		}
		if (strcmp("-o", argv[*argi]) == 0)
			break;
		if (strcmp("-a", argv[*argi]) == 0) {
			if (needterm) {
				if (*argi > 0)
					warnx("ignoring -a after %s",
					    argv[*argi - 1]);
				else
					warnx("ignoring initial -a");
			}
			needterm = 1;
			++*argi;
			continue;
		}
		if (needterm == 0)
			break;
		if (child == NULL) {
			child = exprterm(search, argc, argv, argi);
			if (child != NULL)
				needterm = 0;
			continue;
		}
		needterm = 0;
		if (parent == NULL) {
			parent = mandoc_calloc(1, sizeof(*parent));
			parent->type = EXPR_AND;
			parent->next = NULL;
			parent->child = child;
		}
		child->next = exprterm(search, argc, argv, argi);
		if (child->next != NULL) {
			child = child->next;
			needterm = 0;
		}
	}
	if (needterm && *argi)
		warnx("ignoring trailing %s", argv[*argi - 1]);
	return parent == NULL ? child : parent;
}

static struct expr *
exprterm(const struct mansearch *search, int argc, char *argv[], int *argi)
{
	char		 errbuf[BUFSIZ];
	struct expr	*e;
	char		*key, *val;
	uint64_t	 iterbit;
	int		 cs, i, irc;

	if (strcmp("(", argv[*argi]) == 0) {
		++*argi;
		e = exprcomp(search, argc, argv, argi);
		if (*argi < argc) {
			assert(strcmp(")", argv[*argi]) == 0);
			++*argi;
		} else
			warnx("unclosed parenthesis");
		return e;
	}

	if (strcmp("-i", argv[*argi]) == 0 && *argi + 1 < argc) {
		cs = 0;
		++*argi;
	} else
		cs = 1;

	e = mandoc_calloc(1, sizeof(*e));
	e->type = EXPR_TERM;
	e->bits = 0;
	e->next = NULL;
	e->child = NULL;

	if (search->argmode == ARG_NAME) {
		e->bits = TYPE_Nm;
		e->match.type = DBM_EXACT;
		e->match.str = argv[(*argi)++];
		return e;
	}

	/*
	 * Separate macro keys from search string.
	 * If needed, request regular expression handling.
	 */

	if (search->argmode == ARG_WORD) {
		e->bits = TYPE_Nm;
		e->match.type = DBM_REGEX;
#if HAVE_REWB_BSD
		mandoc_asprintf(&val, "[[:<:]]%s[[:>:]]", argv[*argi]);
#elif HAVE_REWB_SYSV
		mandoc_asprintf(&val, "\\<%s\\>", argv[*argi]);
#else
		mandoc_asprintf(&val,
		    "(^|[^a-zA-Z01-9_])%s([^a-zA-Z01-9_]|$)", argv[*argi]);
#endif
		cs = 0;
	} else if ((val = strpbrk(argv[*argi], "=~")) == NULL) {
		e->bits = TYPE_Nm | TYPE_Nd;
		e->match.type = DBM_SUB;
		e->match.str = argv[*argi];
	} else {
		if (val == argv[*argi])
			e->bits = TYPE_Nm | TYPE_Nd;
		if (*val == '=') {
			e->match.type = DBM_SUB;
			e->match.str = val + 1;
		} else
			e->match.type = DBM_REGEX;
		*val++ = '\0';
		if (strstr(argv[*argi], "arch") != NULL)
			cs = 0;
	}

	/* Compile regular expressions. */

	if (e->match.type == DBM_REGEX) {
		e->match.re = mandoc_malloc(sizeof(*e->match.re));
		irc = regcomp(e->match.re, val,
		    REG_EXTENDED | REG_NOSUB | (cs ? 0 : REG_ICASE));
		if (irc) {
			regerror(irc, e->match.re, errbuf, sizeof(errbuf));
			warnx("regcomp /%s/: %s", val, errbuf);
		}
		if (search->argmode == ARG_WORD)
			free(val);
		if (irc) {
			free(e->match.re);
			free(e);
			++*argi;
			return NULL;
		}
	}

	if (e->bits) {
		++*argi;
		return e;
	}

	/*
	 * Parse out all possible fields.
	 * If the field doesn't resolve, bail.
	 */

	while (NULL != (key = strsep(&argv[*argi], ","))) {
		if ('\0' == *key)
			continue;
		for (i = 0, iterbit = 1; i < KEY_MAX; i++, iterbit <<= 1) {
			if (0 == strcasecmp(key, mansearch_keynames[i])) {
				e->bits |= iterbit;
				break;
			}
		}
		if (i == KEY_MAX) {
			if (strcasecmp(key, "any"))
				warnx("treating unknown key "
				    "\"%s\" as \"any\"", key);
			e->bits |= ~0ULL;
		}
	}

	++*argi;
	return e;
}

static void
exprfree(struct expr *e)
{
	if (e->next != NULL)
		exprfree(e->next);
	if (e->child != NULL)
		exprfree(e->child);
	free(e);
}

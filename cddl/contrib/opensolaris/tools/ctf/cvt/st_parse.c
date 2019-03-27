/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file is a sewer.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <strings.h>
#include <setjmp.h>
#include <ctype.h>
#include <uts/common/sys/ctf.h>

#include "ctftools.h"
#include "memory.h"
#include "list.h"

#define	HASH(NUM)	((int)(NUM & (BUCKETS - 1)))
#define	BUCKETS		128

#define	TYPEPAIRMULT	10000
#define	MAKETYPEID(file, num)	((file) * TYPEPAIRMULT + num)
#define	TYPEFILE(tid)		((tid) / TYPEPAIRMULT)
#define	TYPENUM(tid)		((tid) % TYPEPAIRMULT)

#define	expected(a, b, c) _expected(a, b, c, __LINE__)

static int faketypenumber = 100000000;

static tdesc_t *hash_table[BUCKETS];
static tdesc_t *name_table[BUCKETS];

static list_t *typedbitfldmems;

static void reset(void);
static jmp_buf	resetbuf;

static char *soudef(char *cp, stabtype_t type, tdesc_t **rtdp);
static void enumdef(char *cp, tdesc_t **rtdp);
static int compute_sum(const char *w);

static char *number(char *cp, int *n);
static char *name(char *cp, char **w);
static char *id(char *cp, int *h);
static char *whitesp(char *cp);
static void addhash(tdesc_t *tdp, int num);
static int tagadd(char *w, int h, tdesc_t *tdp);
static char *tdefdecl(char *cp, int h, tdesc_t **rtdp);
static char *intrinsic(char *cp, tdesc_t **rtdp);
static char *arraydef(char *cp, tdesc_t **rtdp);

int debug_parse = DEBUG_PARSE;

/*PRINTFLIKE3*/
static void
parse_debug(int level, char *cp, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	char tmp[32];
	int i;

	if (level > debug_level || !debug_parse)
		return;

	if (cp != NULL) {
		for (i = 0; i < 30; i++) {
			if (cp[i] == '\0')
				break;
			if (!iscntrl(cp[i]))
				tmp[i] = cp[i];
		}
		tmp[i] = '\0';
		(void) snprintf(buf, sizeof (buf), "%s [cp='%s']\n", fmt, tmp);
	} else {
		strcpy(buf, fmt);
		strcat(buf, "\n");
	}

	va_start(ap, fmt);
	vadebug(level, buf, ap);
	va_end(ap);
}

/* Report unexpected syntax in stabs. */
static void
_expected(
	const char *who,	/* what function, or part thereof, is reporting */
	const char *what,	/* what was expected */
	const char *where,	/* where we were in the line of input */
	int line)
{
	fprintf(stderr, "%s, expecting \"%s\" at \"%s\"\n", who, what, where);
	fprintf(stderr, "code line: %d, file %s\n", line,
	    (curhdr ? curhdr : "NO FILE"));
	reset();
}

/*ARGSUSED*/
void
parse_init(tdata_t *td __unused)
{
	int i;

	for (i = 0; i < BUCKETS; i++) {
		hash_table[i] = NULL;
		name_table[i] = NULL;
	}

	if (typedbitfldmems != NULL) {
		list_free(typedbitfldmems, NULL, NULL);
		typedbitfldmems = NULL;
	}
}

void
parse_finish(tdata_t *td)
{
	td->td_nextid = ++faketypenumber;
}

static tdesc_t *
unres_new(int tid)
{
	tdesc_t *tdp;

	tdp = xcalloc(sizeof (*tdp));
	tdp->t_type = TYPEDEF_UNRES;
	tdp->t_id = tid;

	return (tdp);
}

static char *
read_tid(char *cp, tdesc_t **tdpp)
{
	tdesc_t *tdp;
	int tid;

	cp = id(cp, &tid);

	assert(tid != 0);

	if (*cp == '=') {
		if (!(cp = tdefdecl(cp + 1, tid, &tdp)))
			return (NULL);
		if (tdp->t_id && tdp->t_id != tid) {
			tdesc_t *ntdp = xcalloc(sizeof (*ntdp));

			ntdp->t_type = TYPEDEF;
			ntdp->t_tdesc = tdp;
			tdp = ntdp;
		}
		addhash(tdp, tid);
	} else if ((tdp = lookup(tid)) == NULL)
		tdp = unres_new(tid);

	*tdpp = tdp;
	return (cp);
}

static iitype_t
parse_fun(char *cp, iidesc_t *ii)
{
	iitype_t iitype = 0;
	tdesc_t *tdp;
	tdesc_t **args = NULL;
	int nargs = 0;
	int va = 0;

	/*
	 * name:P		prototype
	 * name:F		global function
	 * name:f		static function
	 */
	switch (*cp++) {
	case 'P':
		iitype = II_NOT; /* not interesting */
		break;

	case 'F':
		iitype = II_GFUN;
		break;

	case 'f':
		iitype = II_SFUN;
		break;

	default:
		expected("parse_nfun", "[PfF]", cp - 1);
	}

	if (!(cp = read_tid(cp, &tdp)))
		return (-1);

	if (*cp)
		args = xmalloc(sizeof (tdesc_t *) * FUNCARG_DEF);

	while (*cp && *++cp) {
		if (*cp == '0') {
			va = 1;
			continue;
		}

		nargs++;
		if (nargs > FUNCARG_DEF)
			args = xrealloc(args, sizeof (tdesc_t *) * nargs);
		if (!(cp = read_tid(cp, &args[nargs - 1]))) {
			if (tdp->t_type == TYPEDEF_UNRES)
				free(tdp);
			free(args);
			return (-1);
		}
	}

	ii->ii_type = iitype;
	ii->ii_dtype = tdp;
	ii->ii_nargs = nargs;
	ii->ii_args = args;
	ii->ii_vargs = va;

	return (iitype);
}

static iitype_t
parse_sym(char *cp, iidesc_t *ii)
{
	tdesc_t *tdp;
	iitype_t iitype = 0;

	/*
	 * name:G		global variable
	 * name:S		static variable
	 */
	switch (*cp++) {
	case 'G':
		iitype = II_GVAR;
		break;
	case 'S':
		iitype = II_SVAR;
		break;
	case 'p':
		iitype = II_PSYM;
		break;
	case '(':
		cp--;
		/*FALLTHROUGH*/
	case 'r':
	case 'V':
		iitype = II_NOT; /* not interesting */
		break;
	default:
		expected("parse_sym", "[GprSV(]", cp - 1);
	}

	if (!(cp = read_tid(cp, &tdp)))
		return (-1);

	ii->ii_type = iitype;
	ii->ii_dtype = tdp;

	return (iitype);
}

static iitype_t
parse_type(char *cp, iidesc_t *ii)
{
	tdesc_t *tdp, *ntdp;
	int tid;

	if (*cp++ != 't')
		expected("parse_type", "t (type)", cp - 1);

	cp = id(cp, &tid);
	if ((tdp = lookup(tid)) == NULL) {
		if (*cp++ != '=')
			expected("parse_type", "= (definition)", cp - 1);

		(void) tdefdecl(cp, tid, &tdp);

		if (tdp->t_id == tid) {
			assert(tdp->t_type != TYPEDEF);
			assert(!lookup(tdp->t_id));

			if (!streq(tdp->t_name, ii->ii_name)) {
				ntdp = xcalloc(sizeof (*ntdp));
				ntdp->t_name = xstrdup(ii->ii_name);
				ntdp->t_type = TYPEDEF;
				ntdp->t_tdesc = tdp;
				tdp->t_id = faketypenumber++;
				tdp = ntdp;
			}
		} else if (tdp->t_id == 0) {
			assert(tdp->t_type == FORWARD ||
			    tdp->t_type == INTRINSIC);

			if (tdp->t_name && !streq(tdp->t_name, ii->ii_name)) {
				ntdp = xcalloc(sizeof (*ntdp));
				ntdp->t_name = xstrdup(ii->ii_name);
				ntdp->t_type = TYPEDEF;
				ntdp->t_tdesc = tdp;
				tdp->t_id = faketypenumber++;
				tdp = ntdp;
			}
		} else if (tdp->t_id != tid) {
			ntdp = xcalloc(sizeof (*ntdp));
			ntdp->t_name = xstrdup(ii->ii_name);
			ntdp->t_type = TYPEDEF;
			ntdp->t_tdesc = tdp;
			tdp = ntdp;
		}

		if (tagadd(ii->ii_name, tid, tdp) < 0)
			return (-1);
	}

	ii->ii_type = II_TYPE;
	ii->ii_dtype = tdp;
	return (II_TYPE);
}

static iitype_t
parse_sou(char *cp, iidesc_t *idp)
{
	tdesc_t *rtdp;
	int tid;

	if (*cp++ != 'T')
		expected("parse_sou", "T (sou)", cp - 1);

	cp = id(cp, &tid);
	if (*cp++ != '=')
		expected("parse_sou", "= (definition)", cp - 1);

	parse_debug(1, NULL, "parse_sou: declaring '%s'", idp->ii_name ?
	    idp->ii_name : "(anon)");
	if ((rtdp = lookup(tid)) != NULL) {
		if (idp->ii_name != NULL) {
			if (rtdp->t_name != NULL &&
			    strcmp(rtdp->t_name, idp->ii_name) != 0) {
				tdesc_t *tdp;

				tdp = xcalloc(sizeof (*tdp));
				tdp->t_name = xstrdup(idp->ii_name);
				tdp->t_type = TYPEDEF;
				tdp->t_tdesc = rtdp;
				addhash(tdp, tid); /* for *(x,y) types */
				parse_debug(3, NULL, "    %s defined as %s(%d)",
				    idp->ii_name, tdesc_name(rtdp), tid);
			} else if (rtdp->t_name == NULL) {
				rtdp->t_name = xstrdup(idp->ii_name);
				addhash(rtdp, tid);
			}
		}
	} else {
		rtdp = xcalloc(sizeof (*rtdp));
		rtdp->t_name = idp->ii_name ? xstrdup(idp->ii_name) : NULL;
		addhash(rtdp, tid);
	}

	switch (*cp++) {
	case 's':
		(void) soudef(cp, STRUCT, &rtdp);
		break;
	case 'u':
		(void) soudef(cp, UNION, &rtdp);
		break;
	case 'e':
		enumdef(cp, &rtdp);
		break;
	default:
		expected("parse_sou", "<tag type s/u/e>", cp - 1);
		break;
	}

	idp->ii_type = II_SOU;
	idp->ii_dtype = rtdp;
	return (II_SOU);
}

int
parse_stab(stab_t *stab, char *cp, iidesc_t **iidescp)
{
	iidesc_t *ii = NULL;
	iitype_t (*parse)(char *, iidesc_t *);
	int rc;

	/*
	 * set up for reset()
	 */
	if (setjmp(resetbuf))
		return (-1);

	cp = whitesp(cp);
	ii = iidesc_new(NULL);
	cp = name(cp, &ii->ii_name);

	switch (stab->n_type) {
	case N_FUN:
		parse = parse_fun;
		break;

	case N_LSYM:
		if (*cp == 't')
			parse = parse_type;
		else if (*cp == 'T')
			parse = parse_sou;
		else
			parse = parse_sym;
		break;

	case N_GSYM:
	case N_LCSYM:
	case N_PSYM:
	case N_ROSYM:
	case N_RSYM:
	case N_STSYM:
		parse = parse_sym;
		break;
	default:
		parse_debug(1, cp, "Unknown stab type %#x", stab->n_type);
		bzero(&resetbuf, sizeof (resetbuf));
		return (-1);
	}

	rc = parse(cp, ii);
	bzero(&resetbuf, sizeof (resetbuf));

	if (rc < 0 || ii->ii_type == II_NOT) {
		iidesc_free(ii, NULL);
		return (rc);
	}

	*iidescp = ii;

	return (1);
}

/*
 * Check if we have this node in the hash table already
 */
tdesc_t *
lookup(int h)
{
	int bucket = HASH(h);
	tdesc_t *tdp = hash_table[bucket];

	while (tdp != NULL) {
		if (tdp->t_id == h)
			return (tdp);
		tdp = tdp->t_hash;
	}
	return (NULL);
}

static char *
whitesp(char *cp)
{
	char c;

	for (c = *cp++; isspace(c); c = *cp++)
		;
	--cp;
	return (cp);
}

static char *
name(char *cp, char **w)
{
	char *new, *orig, c;
	int len;

	orig = cp;
	c = *cp++;
	if (c == ':')
		*w = NULL;
	else if (isalpha(c) || strchr("_.$#", c)) {
		for (c = *cp++; isalnum(c) || strchr(" _.$#", c); c = *cp++)
			;
		if (c != ':')
			reset();
		len = cp - orig;
		new = xmalloc(len);
		while (orig < cp - 1)
			*new++ = *orig++;
		*new = '\0';
		*w = new - (len - 1);
	} else
		reset();

	return (cp);
}

static char *
number(char *cp, int *n)
{
	char *next;

	*n = (int)strtol(cp, &next, 10);
	if (next == cp)
		expected("number", "<number>", cp);
	return (next);
}

static char *
id(char *cp, int *h)
{
	int n1, n2;

	if (*cp == '(') {	/* SunPro style */
		cp++;
		cp = number(cp, &n1);
		if (*cp++ != ',')
			expected("id", ",", cp - 1);
		cp = number(cp, &n2);
		if (*cp++ != ')')
			expected("id", ")", cp - 1);
		*h = MAKETYPEID(n1, n2);
	} else if (isdigit(*cp)) { /* gcc style */
		cp = number(cp, &n1);
		*h = n1;
	} else {
		expected("id", "(/0-9", cp);
	}
	return (cp);
}

static int
tagadd(char *w, int h, tdesc_t *tdp)
{
	tdesc_t *otdp;

	tdp->t_name = w;
	if (!(otdp = lookup(h)))
		addhash(tdp, h);
	else if (otdp != tdp) {
		warning("duplicate entry\n");
		warning("  old: %s %d (%d,%d)\n", tdesc_name(otdp),
		    otdp->t_type, TYPEFILE(otdp->t_id), TYPENUM(otdp->t_id));
		warning("  new: %s %d (%d,%d)\n", tdesc_name(tdp),
		    tdp->t_type, TYPEFILE(tdp->t_id), TYPENUM(tdp->t_id));
		return (-1);
	}

	return (0);
}

static char *
tdefdecl(char *cp, int h, tdesc_t **rtdp)
{
	tdesc_t *ntdp;
	char *w;
	int c, h2;
	char type;

	parse_debug(3, cp, "tdefdecl h=%d", h);

	/* Type codes */
	switch (type = *cp) {
	case 'b': /* integer */
	case 'R': /* fp */
		cp = intrinsic(cp, rtdp);
		break;
	case '(': /* equiv to another type */
		cp = id(cp, &h2);
		ntdp = lookup(h2);

		if (ntdp != NULL && *cp == '=') {
			if (ntdp->t_type == FORWARD && *(cp + 1) == 'x') {
				/*
				 * The 6.2 compiler, and possibly others, will
				 * sometimes emit the same stab for a forward
				 * declaration twice.  That is, "(1,2)=xsfoo:"
				 * will sometimes show up in two different
				 * places.  This is, of course, quite fun.  We
				 * want CTF to work in spite of the compiler,
				 * so we'll let this one through.
				 */
				char *c2 = cp + 2;
				char *nm;

				if (!strchr("sue", *c2++)) {
					expected("tdefdecl/x-redefine", "[sue]",
					    c2 - 1);
				}

				c2 = name(c2, &nm);
				if (strcmp(nm, ntdp->t_name) != 0) {
					terminate("Stabs error: Attempt to "
					    "redefine type (%d,%d) as "
					    "something else: %s\n",
					    TYPEFILE(h2), TYPENUM(h2),
					    c2 - 1);
				}
				free(nm);

				h2 = faketypenumber++;
				ntdp = NULL;
			} else {
				terminate("Stabs error: Attempting to "
				    "redefine type (%d,%d)\n", TYPEFILE(h2),
				    TYPENUM(h2));
			}
		}

		if (ntdp == NULL) {  /* if that type isn't defined yet */
			if (*cp != '=') {
				/* record it as unresolved */
				parse_debug(3, NULL, "tdefdecl unres type %d",
				    h2);
				*rtdp = calloc(sizeof (**rtdp), 1);
				(*rtdp)->t_type = TYPEDEF_UNRES;
				(*rtdp)->t_id = h2;
				break;
			} else
				cp++;

			/* define a new type */
			cp = tdefdecl(cp, h2, rtdp);
			if ((*rtdp)->t_id && (*rtdp)->t_id != h2) {
				ntdp = calloc(sizeof (*ntdp), 1);
				ntdp->t_type = TYPEDEF;
				ntdp->t_tdesc = *rtdp;
				*rtdp = ntdp;
			}

			addhash(*rtdp, h2);

		} else { /* that type is already defined */
			if (ntdp->t_type != TYPEDEF || ntdp->t_name != NULL) {
				*rtdp = ntdp;
			} else {
				parse_debug(3, NULL,
				    "No duplicate typedef anon for ref");
				*rtdp = ntdp;
			}
		}
		break;
	case '*':
		ntdp = NULL;
		cp = tdefdecl(cp + 1, h, &ntdp);
		if (ntdp == NULL)
			expected("tdefdecl/*", "id", cp);

		if (!ntdp->t_id)
			ntdp->t_id = faketypenumber++;

		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = POINTER;
		(*rtdp)->t_size = 0;
		(*rtdp)->t_id = h;
		(*rtdp)->t_tdesc = ntdp;
		break;
	case 'f':
		cp = tdefdecl(cp + 1, h, &ntdp);
		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = FUNCTION;
		(*rtdp)->t_size = 0;
		(*rtdp)->t_id = h;
		(*rtdp)->t_fndef = xcalloc(sizeof (fndef_t));
		/*
		 * The 6.1 compiler will sometimes generate incorrect stabs for
		 * function pointers (it'll get the return type wrong).  This
		 * causes merges to fail.  We therefore treat function pointers
		 * as if they all point to functions that return int.  When
		 * 4432549 is fixed, the lookupname() call below should be
		 * replaced with `ntdp'.
		 */
		(*rtdp)->t_fndef->fn_ret = lookupname("int");
		break;
	case 'a':
	case 'z':
		cp++;
		if (*cp++ != 'r')
			expected("tdefdecl/[az]", "r", cp - 1);
		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = ARRAY;
		(*rtdp)->t_id = h;
		cp = arraydef(cp, rtdp);
		break;
	case 'x':
		c = *++cp;
		if (c != 's' && c != 'u' && c != 'e')
			expected("tdefdecl/x", "[sue]", cp - 1);
		cp = name(cp + 1, &w);

		ntdp = xcalloc(sizeof (*ntdp));
		ntdp->t_type = FORWARD;
		ntdp->t_name = w;
		/*
		 * We explicitly don't set t_id here - the caller will do it.
		 * The caller may want to use a real type ID, or they may
		 * choose to make one up.
		 */

		*rtdp = ntdp;
		break;

	case 'B': /* volatile */
		cp = tdefdecl(cp + 1, h, &ntdp);

		if (!ntdp->t_id)
			ntdp->t_id = faketypenumber++;

		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = VOLATILE;
		(*rtdp)->t_size = 0;
		(*rtdp)->t_tdesc = ntdp;
		(*rtdp)->t_id = h;
		break;

	case 'k': /* const */
		cp = tdefdecl(cp + 1, h, &ntdp);

		if (!ntdp->t_id)
			ntdp->t_id = faketypenumber++;

		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = CONST;
		(*rtdp)->t_size = 0;
		(*rtdp)->t_tdesc = ntdp;
		(*rtdp)->t_id = h;
		break;

	case 'K': /* restricted */
		cp = tdefdecl(cp + 1, h, &ntdp);

		if (!ntdp->t_id)
			ntdp->t_id = faketypenumber++;

		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_type = RESTRICT;
		(*rtdp)->t_size = 0;
		(*rtdp)->t_tdesc = ntdp;
		(*rtdp)->t_id = h;
		break;

	case 'u':
	case 's':
		cp++;

		*rtdp = xcalloc(sizeof (**rtdp));
		(*rtdp)->t_name = NULL;
		cp = soudef(cp, (type == 'u') ? UNION : STRUCT, rtdp);
		break;
	default:
		expected("tdefdecl", "<type code>", cp);
	}
	return (cp);
}

static char *
intrinsic(char *cp, tdesc_t **rtdp)
{
	intr_t *intr = xcalloc(sizeof (intr_t));
	tdesc_t *tdp;
	int width, fmt, i;

	switch (*cp++) {
	case 'b':
		intr->intr_type = INTR_INT;
		if (*cp == 's')
			intr->intr_signed = 1;
		else if (*cp != 'u')
			expected("intrinsic/b", "[su]", cp);
		cp++;

		if (strchr("cbv", *cp))
			intr->intr_iformat = *cp++;

		cp = number(cp, &width);
		if (*cp++ != ';')
			expected("intrinsic/b", "; (post-width)", cp - 1);

		cp = number(cp, &intr->intr_offset);
		if (*cp++ != ';')
			expected("intrinsic/b", "; (post-offset)", cp - 1);

		cp = number(cp, &intr->intr_nbits);
		break;

	case 'R':
		intr->intr_type = INTR_REAL;
		for (fmt = 0, i = 0; isdigit(*(cp + i)); i++)
			fmt = fmt * 10 + (*(cp + i) - '0');

		if (fmt < 1 || fmt > CTF_FP_MAX)
			expected("intrinsic/R", "number <= CTF_FP_MAX", cp);

		intr->intr_fformat = fmt;
		cp += i;

		if (*cp++ != ';')
			expected("intrinsic/R", ";", cp - 1);
		cp = number(cp, &width);

		intr->intr_nbits = width * 8;
		break;
	}

	tdp = xcalloc(sizeof (*tdp));
	tdp->t_type = INTRINSIC;
	tdp->t_size = width;
	tdp->t_name = NULL;
	tdp->t_intr = intr;
	parse_debug(3, NULL, "intrinsic: size=%d", width);
	*rtdp = tdp;

	return (cp);
}

static tdesc_t *
bitintrinsic(tdesc_t *template, int nbits)
{
	tdesc_t *newtdp = xcalloc(sizeof (tdesc_t));

	newtdp->t_name = xstrdup(template->t_name);
	newtdp->t_id = faketypenumber++;
	newtdp->t_type = INTRINSIC;
	newtdp->t_size = template->t_size;
	newtdp->t_intr = xmalloc(sizeof (intr_t));
	bcopy(template->t_intr, newtdp->t_intr, sizeof (intr_t));
	newtdp->t_intr->intr_nbits = nbits;

	return (newtdp);
}

static char *
offsize(char *cp, mlist_t *mlp)
{
	int offset, size;

	if (*cp == ',')
		cp++;
	cp = number(cp, &offset);
	if (*cp++ != ',')
		expected("offsize/2", ",", cp - 1);
	cp = number(cp, &size);
	if (*cp++ != ';')
		expected("offsize/3", ";", cp - 1);
	mlp->ml_offset = offset;
	mlp->ml_size = size;
	return (cp);
}

static tdesc_t *
find_intrinsic(tdesc_t *tdp)
{
	for (;;) {
		switch (tdp->t_type) {
		case TYPEDEF:
		case VOLATILE:
		case CONST:
		case RESTRICT:
			tdp = tdp->t_tdesc;
			break;

		default:
			return (tdp);
		}
	}
}

static char *
soudef(char *cp, stabtype_t type, tdesc_t **rtdp)
{
	mlist_t *mlp, **prev;
	char *w;
	int h;
	int size;
	tdesc_t *tdp, *itdp;

	cp = number(cp, &size);
	(*rtdp)->t_size = size;
	(*rtdp)->t_type = type; /* s or u */

	/*
	 * An '@' here indicates a bitmask follows.   This is so the
	 * compiler can pass information to debuggers about how structures
	 * are passed in the v9 world.  We don't need this information
	 * so we skip over it.
	 */
	if (cp[0] == '@') {
		cp += 3;
	}

	parse_debug(3, cp, "soudef: %s size=%d", tdesc_name(*rtdp),
	    (*rtdp)->t_size);

	prev = &((*rtdp)->t_members);
	/* now fill up the fields */
	while ((*cp != '\0') && (*cp != ';')) { /* signifies end of fields */
		mlp = xcalloc(sizeof (*mlp));
		*prev = mlp;
		cp = name(cp, &w);
		mlp->ml_name = w;
		cp = id(cp, &h);
		/*
		 * find the tdesc struct in the hash table for this type
		 * and stick a ptr in here
		 */
		tdp = lookup(h);
		if (tdp == NULL) { /* not in hash list */
			parse_debug(3, NULL, "      defines %s (%d)", w, h);
			if (*cp++ != '=') {
				tdp = unres_new(h);
				parse_debug(3, NULL,
				    "      refers to %s (unresolved %d)",
				    (w ? w : "anon"), h);
			} else {
				cp = tdefdecl(cp, h, &tdp);

				if (tdp->t_id && tdp->t_id != h) {
					tdesc_t *ntdp = xcalloc(sizeof (*ntdp));

					ntdp->t_type = TYPEDEF;
					ntdp->t_tdesc = tdp;
					tdp = ntdp;
				}

				addhash(tdp, h);
				parse_debug(4, cp,
				    "     soudef now looking at    ");
				cp++;
			}
		} else {
			parse_debug(3, NULL, "      refers to %s (%d, %s)",
			    w ? w : "anon", h, tdesc_name(tdp));
		}

		cp = offsize(cp, mlp);

		itdp = find_intrinsic(tdp);
		if (itdp->t_type == INTRINSIC) {
			if (mlp->ml_size != itdp->t_intr->intr_nbits) {
				parse_debug(4, cp, "making %d bit intrinsic "
				    "from %s", mlp->ml_size, tdesc_name(itdp));
				mlp->ml_type = bitintrinsic(itdp, mlp->ml_size);
			} else
				mlp->ml_type = tdp;
		} else if (itdp->t_type == TYPEDEF_UNRES) {
			list_add(&typedbitfldmems, mlp);
			mlp->ml_type = tdp;
		} else {
			mlp->ml_type = tdp;
		}

		/* cp is now pointing to next field */
		prev = &mlp->ml_next;
	}
	return (cp);
}

static char *
arraydef(char *cp, tdesc_t **rtdp)
{
	int start, end, h;

	cp = id(cp, &h);
	if (*cp++ != ';')
		expected("arraydef/1", ";", cp - 1);

	(*rtdp)->t_ardef = xcalloc(sizeof (ardef_t));
	(*rtdp)->t_ardef->ad_idxtype = lookup(h);

	cp = number(cp, &start); /* lower */
	if (*cp++ != ';')
		expected("arraydef/2", ";", cp - 1);

	if (*cp == 'S') {
		/*
		 * variable length array - treat as null dimensioned
		 *
		 * For VLA variables on sparc, SS12 generated stab entry
		 * looks as follows:
		 * .stabs "buf:(0,28)=zr(0,4);0;S-12;(0,1)", 0x80, 0, 0, -16
		 * Whereas SS12u1 generated stab entry looks like this:
		 * .stabs "buf:(0,28)=zr(0,4);0;S0;(0,1)", 0x80, 0, 0, 0
		 * On x86, both versions generate the first type of entry.
		 * We should be able to parse both.
		 */
		cp++;
		if (*cp == '-')
			cp++;
		cp = number(cp, &end);
		end = start;
	} else {
		/*
		 * normal fixed-dimension array
		 * Stab entry for this looks as follows :
		 * .stabs "x:(0,28)=ar(0,4);0;9;(0,3)", 0x80, 0, 40, 0
		 */
		cp = number(cp, &end);  /* upper */
	}

	if (*cp++ != ';')
		expected("arraydef/3", ";", cp - 1);
	(*rtdp)->t_ardef->ad_nelems = end - start + 1;
	cp = tdefdecl(cp, h, &((*rtdp)->t_ardef->ad_contents));

	parse_debug(3, cp, "defined array idx type %d %d-%d next ",
	    h, start, end);

	return (cp);
}

static void
enumdef(char *cp, tdesc_t **rtdp)
{
	elist_t *elp, **prev;
	char *w;

	(*rtdp)->t_type = ENUM;
	(*rtdp)->t_emem = NULL;

	prev = &((*rtdp)->t_emem);
	while (*cp != ';') {
		elp = xcalloc(sizeof (*elp));
		elp->el_next = NULL;
		*prev = elp;
		cp = name(cp, &w);
		elp->el_name = w;
		cp = number(cp, &elp->el_number);
		parse_debug(3, NULL, "enum %s: %s=%d", tdesc_name(*rtdp),
		    elp->el_name, elp->el_number);
		prev = &elp->el_next;
		if (*cp++ != ',')
			expected("enumdef", ",", cp - 1);
	}
}

static tdesc_t *
lookup_name(tdesc_t **hash, const char *name1)
{
	int bucket = compute_sum(name1);
	tdesc_t *tdp, *ttdp = NULL;

	for (tdp = hash[bucket]; tdp != NULL; tdp = tdp->t_next) {
		if (tdp->t_name != NULL && strcmp(tdp->t_name, name1) == 0) {
			if (tdp->t_type == STRUCT || tdp->t_type == UNION ||
			    tdp->t_type == ENUM || tdp->t_type == INTRINSIC)
				return (tdp);
			if (tdp->t_type == TYPEDEF)
				ttdp = tdp;
		}
	}
	return (ttdp);
}

tdesc_t *
lookupname(const char *name1)
{
	return (lookup_name(name_table, name1));
}

/*
 * Add a node to the hash queues.
 */
static void
addhash(tdesc_t *tdp, int num)
{
	int hash = HASH(num);
	tdesc_t *ttdp;
	char added_num = 0, added_name = 0;

	/*
	 * If it already exists in the hash table don't add it again
	 * (but still check to see if the name should be hashed).
	 */
	ttdp = lookup(num);

	if (ttdp == NULL) {
		tdp->t_id = num;
		tdp->t_hash = hash_table[hash];
		hash_table[hash] = tdp;
		added_num = 1;
	}

	if (tdp->t_name != NULL) {
		ttdp = lookupname(tdp->t_name);
		if (ttdp == NULL) {
			hash = compute_sum(tdp->t_name);
			tdp->t_next = name_table[hash];
			name_table[hash] = tdp;
			added_name = 1;
		}
	}
	if (!added_num && !added_name) {
		terminate("stabs: broken hash\n");
	}
}

static int
compute_sum(const char *w)
{
	char c;
	int sum;

	for (sum = 0; (c = *w) != '\0'; sum += c, w++)
		;
	return (HASH(sum));
}

static void
reset(void)
{
	longjmp(resetbuf, 1);
}

void
check_hash(void)
{
	tdesc_t *tdp;
	int i;

	printf("checking hash\n");
	for (i = 0; i < BUCKETS; i++) {
		if (hash_table[i]) {
			for (tdp = hash_table[i]->t_hash;
			    tdp && tdp != hash_table[i];
			    tdp = tdp->t_hash)
				continue;
			if (tdp) {
				terminate("cycle in hash bucket %d\n", i);
				return;
			}
		}

		if (name_table[i]) {
			for (tdp = name_table[i]->t_next;
			    tdp && tdp != name_table[i];
			    tdp = tdp->t_next)
				continue;
			if (tdp) {
				terminate("cycle in name bucket %d\n", i);
				return;
			}
		}
	}
	printf("done\n");
}

/*ARGSUSED1*/
static int
resolve_typed_bitfields_cb(void *arg, void *private __unused)
{
	mlist_t *ml = arg;
	tdesc_t *tdp = ml->ml_type;

	debug(3, "Resolving typed bitfields (member %s)\n",
	    (ml->ml_name ? ml->ml_name : "(anon)"));

	while (tdp) {
		switch (tdp->t_type) {
		case INTRINSIC:
			if (ml->ml_size != tdp->t_intr->intr_nbits) {
				debug(3, "making %d bit intrinsic from %s",
				    ml->ml_size, tdesc_name(tdp));
				ml->ml_type = bitintrinsic(tdp, ml->ml_size);
			} else {
				debug(3, "using existing %d bit %s intrinsic",
				    ml->ml_size, tdesc_name(tdp));
				ml->ml_type = tdp;
			}
			return (1);

		case POINTER:
		case TYPEDEF:
		case VOLATILE:
		case CONST:
		case RESTRICT:
			tdp = tdp->t_tdesc;
			break;

		default:
			return (1);
		}
	}

	terminate("type chain for bitfield member %s has a NULL", ml->ml_name);
	/*NOTREACHED*/
	return (0);
}

void
resolve_typed_bitfields(void)
{
	(void) list_iter(typedbitfldmems,
	    resolve_typed_bitfields_cb, NULL);
}

/*	$OpenBSD: var.c,v 1.73 2023/07/23 23:42:03 kn Exp $	*/

#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef SMALL
# include <term.h>
# include <curses.h>
#endif

#include "sh.h"

/*
 * Variables
 *
 * WARNING: unreadable code, needs a rewrite
 *
 * if (flag&INTEGER), val.i contains integer value, and type contains base.
 * otherwise, (val.s + type) contains string value.
 * if (flag&EXPORT), val.s contains "name=value" for E-Z exporting.
 */
static	struct tbl vtemp;
static	struct table specials;
static char	*formatstr(struct tbl *, const char *);
static void	export(struct tbl *, const char *);
static int	special(const char *);
static void	unspecial(const char *);
static void	getspec(struct tbl *);
static void	setspec(struct tbl *);
static void	unsetspec(struct tbl *);
static struct tbl *arraysearch(struct tbl *, int);

/*
 * create a new block for function calls and simple commands
 * assume caller has allocated and set up genv->loc
 */
void
newblock(void)
{
	struct block *l;
	static char *const empty[] = {null};

	l = alloc(sizeof(struct block), ATEMP);
	l->flags = 0;
	ainit(&l->area); /* todo: could use genv->area (l->area => l->areap) */
	if (!genv->loc) {
		l->argc = 0;
		l->argv = (char **) empty;
	} else {
		l->argc = genv->loc->argc;
		l->argv = genv->loc->argv;
	}
	l->exit = l->error = NULL;
	ktinit(&l->vars, &l->area, 0);
	ktinit(&l->funs, &l->area, 0);
	l->next = genv->loc;
	genv->loc = l;
}

/*
 * pop a block handling special variables
 */
void
popblock(void)
{
	struct block *l = genv->loc;
	struct tbl *vp, **vpp = l->vars.tbls, *vq;
	int i;

	genv->loc = l->next;	/* pop block */
	for (i = l->vars.size; --i >= 0; )
		if ((vp = *vpp++) != NULL && (vp->flag&SPECIAL)) {
			if ((vq = global(vp->name))->flag & ISSET)
				setspec(vq);
			else
				unsetspec(vq);
		}
	if (l->flags & BF_DOGETOPTS)
		user_opt = l->getopts_state;
	afreeall(&l->area);
	afree(l, ATEMP);
}

/* called by main() to initialize variable data structures */
void
initvar(void)
{
	static const struct {
		const char *name;
		int v;
	} names[] = {
		{ "COLUMNS",		V_COLUMNS },
		{ "IFS",		V_IFS },
		{ "OPTIND",		V_OPTIND },
		{ "PATH",		V_PATH },
		{ "POSIXLY_CORRECT",	V_POSIXLY_CORRECT },
		{ "TMPDIR",		V_TMPDIR },
		{ "HISTCONTROL",	V_HISTCONTROL },
		{ "HISTFILE",		V_HISTFILE },
		{ "HISTSIZE",		V_HISTSIZE },
		{ "EDITOR",		V_EDITOR },
		{ "VISUAL",		V_VISUAL },
#ifndef SMALL
		{ "MAIL",		V_MAIL },
		{ "MAILCHECK",		V_MAILCHECK },
		{ "MAILPATH",		V_MAILPATH },
#endif /* SMALL */
		{ "RANDOM",		V_RANDOM },
		{ "SECONDS",		V_SECONDS },
		{ "TMOUT",		V_TMOUT },
		{ "LINENO",		V_LINENO },
		{ "TERM",		V_TERM },
		{ NULL,	0 }
	};
	int i;
	struct tbl *tp;

	ktinit(&specials, APERM, 32); /* must be 2^n (currently 19 specials) */
	for (i = 0; names[i].name; i++) {
		tp = ktenter(&specials, names[i].name, hash(names[i].name));
		tp->flag = DEFINED|ISSET;
		tp->type = names[i].v;
	}
}

/* Used to calculate an array index for global()/local().  Sets *arrayp to
 * non-zero if this is an array, sets *valp to the array index, returns
 * the basename of the array.
 */
static const char *
array_index_calc(const char *n, bool *arrayp, int *valp)
{
	const char *p;
	int len;

	*arrayp = false;
	p = skip_varname(n, false);
	if (p != n && *p == '[' && (len = array_ref_len(p))) {
		char *sub, *tmp;
		int64_t rval;

		/* Calculate the value of the subscript */
		*arrayp = true;
		tmp = str_nsave(p+1, len-2, ATEMP);
		sub = substitute(tmp, 0);
		afree(tmp, ATEMP);
		n = str_nsave(n, p - n, ATEMP);
		evaluate(sub, &rval, KSH_UNWIND_ERROR, true);
		if (rval < 0 || rval > INT_MAX)
			errorf("%s: subscript %" PRIi64 " out of range",
			    n, rval);
		*valp = rval;
		afree(sub, ATEMP);
	}
	return n;
}

/*
 * Search for variable, if not found create globally.
 */
struct tbl *
global(const char *n)
{
	struct block *l = genv->loc;
	struct tbl *vp;
	long	 num;
	int c;
	unsigned int h;
	bool	 array;
	int	 val;

	/* Check to see if this is an array */
	n = array_index_calc(n, &array, &val);
	h = hash(n);
	c = (unsigned char)n[0];
	if (!letter(c)) {
		if (array)
			errorf("bad substitution");
		vp = &vtemp;
		vp->flag = DEFINED;
		vp->type = 0;
		vp->areap = ATEMP;
		*vp->name = c;
		if (digit(c)) {
			errno = 0;
			num = strtol(n, NULL, 10);
			if (errno == 0 && num <= l->argc)
				/* setstr can't fail here */
				setstr(vp, l->argv[num], KSH_RETURN_ERROR);
			vp->flag |= RDONLY;
			return vp;
		}
		vp->flag |= RDONLY;
		if (n[1] != '\0')
			return vp;
		vp->flag |= ISSET|INTEGER;
		switch (c) {
		case '$':
			vp->val.i = kshpid;
			break;
		case '!':
			/* If no job, expand to nothing */
			if ((vp->val.i = j_async()) == 0)
				vp->flag &= ~(ISSET|INTEGER);
			break;
		case '?':
			vp->val.i = exstat;
			break;
		case '#':
			vp->val.i = l->argc;
			break;
		case '-':
			vp->flag &= ~INTEGER;
			vp->val.s = getoptions();
			break;
		default:
			vp->flag &= ~(ISSET|INTEGER);
		}
		return vp;
	}
	for (l = genv->loc; ; l = l->next) {
		vp = ktsearch(&l->vars, n, h);
		if (vp != NULL) {
			if (array)
				return arraysearch(vp, val);
			else
				return vp;
		}
		if (l->next == NULL)
			break;
	}
	vp = ktenter(&l->vars, n, h);
	if (array)
		vp = arraysearch(vp, val);
	vp->flag |= DEFINED;
	if (special(n))
		vp->flag |= SPECIAL;
	return vp;
}

/*
 * Search for local variable, if not found create locally.
 */
struct tbl *
local(const char *n, bool copy)
{
	struct block *l = genv->loc;
	struct tbl *vp;
	unsigned int h;
	bool	 array;
	int	 val;

	/* Check to see if this is an array */
	n = array_index_calc(n, &array, &val);
	h = hash(n);
	if (!letter(*n)) {
		vp = &vtemp;
		vp->flag = DEFINED|RDONLY;
		vp->type = 0;
		vp->areap = ATEMP;
		return vp;
	}
	vp = ktenter(&l->vars, n, h);
	if (copy && !(vp->flag & DEFINED)) {
		struct block *ll = l;
		struct tbl *vq = NULL;

		while ((ll = ll->next) && !(vq = ktsearch(&ll->vars, n, h)))
			;
		if (vq) {
			vp->flag |= vq->flag &
			    (EXPORT | INTEGER | RDONLY | LJUST | RJUST |
			    ZEROFIL | LCASEV | UCASEV_AL | INT_U | INT_L);
			if (vq->flag & INTEGER)
				vp->type = vq->type;
			vp->u2.field = vq->u2.field;
		}
	}
	if (array)
		vp = arraysearch(vp, val);
	vp->flag |= DEFINED;
	if (special(n))
		vp->flag |= SPECIAL;
	return vp;
}

/* get variable string value */
char *
str_val(struct tbl *vp)
{
	char *s;

	if ((vp->flag&SPECIAL))
		getspec(vp);
	if (!(vp->flag&ISSET))
		s = null;		/* special to dollar() */
	else if (!(vp->flag&INTEGER))	/* string source */
		s = vp->val.s + vp->type;
	else {				/* integer source */
		/* worst case number length is when base=2, so use
		 * minus base # number BITS(int64_t) NUL */
		char strbuf[1 + 2 + 1 + BITS(int64_t) + 1];
		const char *digits = (vp->flag & UCASEV_AL) ?
		    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" :
		    "0123456789abcdefghijklmnopqrstuvwxyz";
		uint64_t n;
		unsigned int base;

		s = strbuf + sizeof(strbuf);
		if (vp->flag & INT_U)
			n = (uint64_t) vp->val.i;
		else
			n = (vp->val.i < 0) ? -vp->val.i : vp->val.i;
		base = (vp->type == 0) ? 10 : vp->type;
		if (base < 2 || base > strlen(digits))
			base = 10;

		*--s = '\0';
		do {
			*--s = digits[n % base];
			n /= base;
		} while (n != 0);
		if (base != 10) {
			*--s = '#';
			*--s = digits[base % 10];
			if (base >= 10)
				*--s = digits[base / 10];
		}
		if (!(vp->flag & INT_U) && vp->val.i < 0)
			*--s = '-';
		if (vp->flag & (RJUST|LJUST)) /* case already dealt with */
			s = formatstr(vp, s);
		else
			s = str_save(s, ATEMP);
	}
	return s;
}

/* get variable integer value, with error checking */
int64_t
intval(struct tbl *vp)
{
	int64_t num;
	int base;

	base = getint(vp, &num, false);
	if (base == -1)
		/* XXX check calls - is error here ok by POSIX? */
		errorf("%s: bad number", str_val(vp));
	return num;
}

/* set variable to string value */
int
setstr(struct tbl *vq, const char *s, int error_ok)
{
	const char *fs = NULL;
	int no_ro_check = error_ok & KSH_IGNORE_RDONLY;
	error_ok &= ~KSH_IGNORE_RDONLY;
	if ((vq->flag & RDONLY) && !no_ro_check) {
		warningf(true, "%s: is read only", vq->name);
		if (!error_ok)
			errorf(NULL);
		return 0;
	}
	if (!(vq->flag&INTEGER)) { /* string dest */
		if ((vq->flag&ALLOC)) {
			/* debugging */
			if (s >= vq->val.s &&
			    s <= vq->val.s + strlen(vq->val.s))
				internal_errorf("%s: %s=%s: assigning to self",
				    __func__, vq->name, s);
			afree(vq->val.s, vq->areap);
		}
		vq->flag &= ~(ISSET|ALLOC);
		vq->type = 0;
		if (s && (vq->flag & (UCASEV_AL|LCASEV|LJUST|RJUST)))
			fs = s = formatstr(vq, s);
		if ((vq->flag&EXPORT))
			export(vq, s);
		else {
			vq->val.s = str_save(s, vq->areap);
			vq->flag |= ALLOC;
		}
	} else {		/* integer dest */
		if (!v_evaluate(vq, s, error_ok, true))
			return 0;
	}
	vq->flag |= ISSET;
	if ((vq->flag&SPECIAL))
		setspec(vq);
	afree((void *)fs, ATEMP);
	return 1;
}

/* set variable to integer */
void
setint(struct tbl *vq, int64_t n)
{
	if (!(vq->flag&INTEGER)) {
		struct tbl *vp = &vtemp;
		vp->flag = (ISSET|INTEGER);
		vp->type = 0;
		vp->areap = ATEMP;
		vp->val.i = n;
		/* setstr can't fail here */
		setstr(vq, str_val(vp), KSH_RETURN_ERROR);
	} else
		vq->val.i = n;
	vq->flag |= ISSET;
	if ((vq->flag&SPECIAL))
		setspec(vq);
}

int
getint(struct tbl *vp, int64_t *nump, bool arith)
{
	char *s;
	int c;
	int base, neg;
	int have_base = 0;
	int64_t num;

	if (vp->flag&SPECIAL)
		getspec(vp);
	/* XXX is it possible for ISSET to be set and val.s to be 0? */
	if (!(vp->flag&ISSET) || (!(vp->flag&INTEGER) && vp->val.s == NULL))
		return -1;
	if (vp->flag&INTEGER) {
		*nump = vp->val.i;
		return vp->type;
	}
	s = vp->val.s + vp->type;
	if (s == NULL)	/* redundant given initial test */
		s = null;
	base = 10;
	num = 0;
	neg = 0;
	if (arith && *s == '0' && *(s+1)) {
		s++;
		if (*s == 'x' || *s == 'X') {
			s++;
			base = 16;
		} else if (vp->flag & ZEROFIL) {
			while (*s == '0')
				s++;
		} else
			base = 8;
		have_base++;
	}
	for (c = (unsigned char)*s++; c ; c = (unsigned char)*s++) {
		if (c == '-') {
			neg++;
		} else if (c == '#') {
			base = (int) num;
			if (have_base || base < 2 || base > 36)
				return -1;
			num = 0;
			have_base = 1;
		} else if (letnum(c)) {
			if (isdigit(c))
				c -= '0';
			else if (islower(c))
				c -= 'a' - 10; /* todo: assumes ascii */
			else if (isupper(c))
				c -= 'A' - 10; /* todo: assumes ascii */
			else
				c = -1; /* _: force error */
			if (c < 0 || c >= base)
				return -1;
			num = num * base + c;
		} else
			return -1;
	}
	if (neg)
		num = -num;
	*nump = num;
	return base;
}

/* convert variable vq to integer variable, setting its value from vp
 * (vq and vp may be the same)
 */
struct tbl *
setint_v(struct tbl *vq, struct tbl *vp, bool arith)
{
	int base;
	int64_t num;

	if ((base = getint(vp, &num, arith)) == -1)
		return NULL;
	if (!(vq->flag & INTEGER) && (vq->flag & ALLOC)) {
		vq->flag &= ~ALLOC;
		afree(vq->val.s, vq->areap);
	}
	vq->val.i = num;
	if (vq->type == 0) /* default base */
		vq->type = base;
	vq->flag |= ISSET|INTEGER;
	if (vq->flag&SPECIAL)
		setspec(vq);
	return vq;
}

static char *
formatstr(struct tbl *vp, const char *s)
{
	int olen, nlen;
	char *p, *q;

	olen = strlen(s);

	if (vp->flag & (RJUST|LJUST)) {
		if (!vp->u2.field)	/* default field width */
			vp->u2.field = olen;
		nlen = vp->u2.field;
	} else
		nlen = olen;

	p = alloc(nlen + 1, ATEMP);
	if (vp->flag & (RJUST|LJUST)) {
		int slen;

		if (vp->flag & RJUST) {
			const char *r = s + olen;
			/* strip trailing spaces (at&t ksh uses r[-1] == ' ') */
			while (r > s && isspace((unsigned char)r[-1]))
				--r;
			slen = r - s;
			if (slen > vp->u2.field) {
				s += slen - vp->u2.field;
				slen = vp->u2.field;
			}
			shf_snprintf(p, nlen + 1,
				((vp->flag & ZEROFIL) && digit(*s)) ?
					  "%0*s%.*s" : "%*s%.*s",
				vp->u2.field - slen, null, slen, s);
		} else {
			/* strip leading spaces/zeros */
			while (isspace((unsigned char)*s))
				s++;
			if (vp->flag & ZEROFIL)
				while (*s == '0')
					s++;
			shf_snprintf(p, nlen + 1, "%-*.*s",
				vp->u2.field, vp->u2.field, s);
		}
	} else
		memcpy(p, s, olen + 1);

	if (vp->flag & UCASEV_AL) {
		for (q = p; *q; q++)
			if (islower((unsigned char)*q))
				*q = toupper((unsigned char)*q);
	} else if (vp->flag & LCASEV) {
		for (q = p; *q; q++)
			if (isupper((unsigned char)*q))
				*q = tolower((unsigned char)*q);
	}

	return p;
}

/*
 * make vp->val.s be "name=value" for quick exporting.
 */
static void
export(struct tbl *vp, const char *val)
{
	char *xp;
	char *op = (vp->flag&ALLOC) ? vp->val.s : NULL;
	int namelen = strlen(vp->name);
	int vallen = strlen(val) + 1;

	vp->flag |= ALLOC;
	xp = alloc(namelen + 1 + vallen, vp->areap);
	memcpy(vp->val.s = xp, vp->name, namelen);
	xp += namelen;
	*xp++ = '=';
	vp->type = xp - vp->val.s; /* offset to value */
	memcpy(xp, val, vallen);
	afree(op, vp->areap);
}

/*
 * lookup variable (according to (set&LOCAL)),
 * set its attributes (INTEGER, RDONLY, EXPORT, TRACE, LJUST, RJUST, ZEROFIL,
 * LCASEV, UCASEV_AL), and optionally set its value if an assignment.
 */
struct tbl *
typeset(const char *var, int set, int clr, int field, int base)
{
	struct tbl *vp;
	struct tbl *vpbase, *t;
	char *tvar;
	const char *val;

	/* check for valid variable name, search for value */
	val = skip_varname(var, false);
	if (val == var)
		return NULL;
	if (*val == '[') {
		int len;

		len = array_ref_len(val);
		if (len == 0)
			return NULL;
		/* IMPORT is only used when the shell starts up and is
		 * setting up its environment.  Allow only simple array
		 * references at this time since parameter/command substitution
		 * is preformed on the [expression], which would be a major
		 * security hole.
		 */
		if (set & IMPORT) {
			int i;
			for (i = 1; i < len - 1; i++)
				if (!digit(val[i]))
					return NULL;
		}
		val += len;
	}
	if (*val == '=')
		tvar = str_nsave(var, val++ - var, ATEMP);
	else {
		/* Importing from original environment: must have an = */
		if (set & IMPORT)
			return NULL;
		tvar = (char *) var;
		val = NULL;
	}

	/* Prevent typeset from creating a local PATH/ENV/SHELL */
	if (Flag(FRESTRICTED) && (strcmp(tvar, "PATH") == 0 ||
	    strcmp(tvar, "ENV") == 0 || strcmp(tvar, "SHELL") == 0))
		errorf("%s: restricted", tvar);

	vp = (set&LOCAL) ? local(tvar, (set & LOCAL_COPY) ? true : false) :
	    global(tvar);
	set &= ~(LOCAL|LOCAL_COPY);

	vpbase = (vp->flag & ARRAY) ? global(arrayname(tvar)) : vp;

	/* only allow export flag to be set.  at&t ksh allows any attribute to
	 * be changed, which means it can be truncated or modified
	 * (-L/-R/-Z/-i).
	 */
	if ((vpbase->flag&RDONLY) &&
	    (val || clr || (set & ~EXPORT)))
		/* XXX check calls - is error here ok by POSIX? */
		errorf("%s: is read only", tvar);
	if (val)
		afree(tvar, ATEMP);

	/* most calls are with set/clr == 0 */
	if (set | clr) {
		int ok = 1;
		/* XXX if x[0] isn't set, there will be problems: need to have
		 * one copy of attributes for arrays...
		 */
		for (t = vpbase; t; t = t->u.array) {
			int fake_assign;
			int error_ok = KSH_RETURN_ERROR;
			char *s = NULL;
			char *free_me = NULL;

			fake_assign = (t->flag & ISSET) && (!val || t != vp) &&
			    ((set & (UCASEV_AL|LCASEV|LJUST|RJUST|ZEROFIL)) ||
			    ((t->flag & INTEGER) && (clr & INTEGER)) ||
			    (!(t->flag & INTEGER) && (set & INTEGER)));
			if (fake_assign) {
				if (t->flag & INTEGER) {
					s = str_val(t);
					free_me = NULL;
				} else {
					s = t->val.s + t->type;
					free_me = (t->flag & ALLOC) ? t->val.s :
					    NULL;
				}
				t->flag &= ~ALLOC;
			}
			if (!(t->flag & INTEGER) && (set & INTEGER)) {
				t->type = 0;
				t->flag &= ~ALLOC;
			}
			if (!(t->flag & RDONLY) && (set & RDONLY)) {
				/* allow var to be initialized read-only */
				error_ok |= KSH_IGNORE_RDONLY;
			}
			t->flag = (t->flag | set) & ~clr;
			/* Don't change base if assignment is to be done,
			 * in case assignment fails.
			 */
			if ((set & INTEGER) && base > 0 && (!val || t != vp))
				t->type = base;
			if (set & (LJUST|RJUST|ZEROFIL))
				t->u2.field = field;
			if (fake_assign) {
				if (!setstr(t, s, error_ok)) {
					/* Somewhat arbitrary action here:
					 * zap contents of variable, but keep
					 * the flag settings.
					 */
					ok = 0;
					if (t->flag & INTEGER)
						t->flag &= ~ISSET;
					else {
						if (t->flag & ALLOC)
							afree(t->val.s, t->areap);
						t->flag &= ~(ISSET|ALLOC);
						t->type = 0;
					}
				}
				afree(free_me, t->areap);
			}
		}
		if (!ok)
		    errorf(NULL);
	}

	if (val != NULL) {
		if (vp->flag&INTEGER) {
			/* do not zero base before assignment */
			setstr(vp, val, KSH_UNWIND_ERROR | KSH_IGNORE_RDONLY);
			/* Done after assignment to override default */
			if (base > 0)
				vp->type = base;
		} else
			/* setstr can't fail (readonly check already done) */
			setstr(vp, val, KSH_RETURN_ERROR | KSH_IGNORE_RDONLY);
	}

	/* only x[0] is ever exported, so use vpbase */
	if ((vpbase->flag&EXPORT) && !(vpbase->flag&INTEGER) &&
	    vpbase->type == 0)
		export(vpbase, (vpbase->flag&ISSET) ? vpbase->val.s : null);

	return vp;
}

/* Unset a variable.  array_ref is set if there was an array reference in
 * the name lookup (eg, x[2]).
 */
void
unset(struct tbl *vp, int array_ref)
{
	if (vp->flag & ALLOC)
		afree(vp->val.s, vp->areap);
	if ((vp->flag & ARRAY) && !array_ref) {
		struct tbl *a, *tmp;

		/* Free up entire array */
		for (a = vp->u.array; a; ) {
			tmp = a;
			a = a->u.array;
			if (tmp->flag & ALLOC)
				afree(tmp->val.s, tmp->areap);
			afree(tmp, tmp->areap);
		}
		vp->u.array = NULL;
	}
	/* If foo[0] is being unset, the remainder of the array is kept... */
	vp->flag &= SPECIAL | (array_ref ? ARRAY|DEFINED : 0);
	if (vp->flag & SPECIAL)
		unsetspec(vp);	/* responsible for `unspecial'ing var */
}

/* return a pointer to the first char past a legal variable name (returns the
 * argument if there is no legal name, returns * a pointer to the terminating
 * null if whole string is legal).
 */
char *
skip_varname(const char *s, int aok)
{
	int alen;

	if (s && letter(*s)) {
		while (*++s && letnum(*s))
			;
		if (aok && *s == '[' && (alen = array_ref_len(s)))
			s += alen;
	}
	return (char *) s;
}

/* Return a pointer to the first character past any legal variable name.  */
char *
skip_wdvarname(const char *s,
    int aok)				/* skip array de-reference? */
{
	if (s[0] == CHAR && letter(s[1])) {
		do {
			s += 2;
		} while (s[0] == CHAR && letnum(s[1]));
		if (aok && s[0] == CHAR && s[1] == '[') {
			/* skip possible array de-reference */
			const char *p = s;
			char c;
			int depth = 0;

			while (1) {
				if (p[0] != CHAR)
					break;
				c = p[1];
				p += 2;
				if (c == '[')
					depth++;
				else if (c == ']' && --depth == 0) {
					s = p;
					break;
				}
			}
		}
	}
	return (char *) s;
}

/* Check if coded string s is a variable name */
int
is_wdvarname(const char *s, int aok)
{
	char *p = skip_wdvarname(s, aok);

	return p != s && p[0] == EOS;
}

/* Check if coded string s is a variable assignment */
int
is_wdvarassign(const char *s)
{
	char *p = skip_wdvarname(s, true);

	return p != s && p[0] == CHAR && p[1] == '=';
}

/*
 * Make the exported environment from the exported names in the dictionary.
 */
char **
makenv(void)
{
	struct block *l;
	XPtrV env;
	struct tbl *vp, **vpp;
	int i;

	XPinit(env, 64);
	for (l = genv->loc; l != NULL; l = l->next)
		for (vpp = l->vars.tbls, i = l->vars.size; --i >= 0; )
			if ((vp = *vpp++) != NULL &&
			    (vp->flag&(ISSET|EXPORT)) == (ISSET|EXPORT)) {
				struct block *l2;
				struct tbl *vp2;
				unsigned int h = hash(vp->name);

				/* unexport any redefined instances */
				for (l2 = l->next; l2 != NULL; l2 = l2->next) {
					vp2 = ktsearch(&l2->vars, vp->name, h);
					if (vp2 != NULL)
						vp2->flag &= ~EXPORT;
				}
				if ((vp->flag&INTEGER)) {
					/* integer to string */
					char *val;
					val = str_val(vp);
					vp->flag &= ~(INTEGER|RDONLY);
					/* setstr can't fail here */
					setstr(vp, val, KSH_RETURN_ERROR);
				}
				XPput(env, vp->val.s);
			}
	XPput(env, NULL);
	return (char **) XPclose(env);
}

/*
 * Called after a fork in parent to bump the random number generator.
 * Done to ensure children will not get the same random number sequence
 * if the parent doesn't use $RANDOM.
 */
void
change_random(void)
{
	rand();
}

/*
 * handle special variables with side effects - PATH, SECONDS.
 */

/* Test if name is a special parameter */
static int
special(const char *name)
{
	struct tbl *tp;

	tp = ktsearch(&specials, name, hash(name));
	return tp && (tp->flag & ISSET) ? tp->type : V_NONE;
}

/* Make a variable non-special */
static void
unspecial(const char *name)
{
	struct tbl *tp;

	tp = ktsearch(&specials, name, hash(name));
	if (tp)
		ktdelete(tp);
}

static	struct	timespec seconds;	/* time SECONDS last set */
static	int	user_lineno;		/* what user set $LINENO to */

static void
getspec(struct tbl *vp)
{
	switch (special(vp->name)) {
	case V_SECONDS:
		vp->flag &= ~SPECIAL;
		/* On start up the value of SECONDS is used before seconds
		 * has been set - don't do anything in this case
		 * (see initcoms[] in main.c).
		 */
		if (vp->flag & ISSET) {
			struct timespec difference, now;

			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &seconds, &difference);
			setint(vp, (int64_t)difference.tv_sec);
		}
		vp->flag |= SPECIAL;
		break;
	case V_RANDOM:
		vp->flag &= ~SPECIAL;
		setint(vp, (int64_t) (rand() & 0x7fff));
		vp->flag |= SPECIAL;
		break;
	case V_HISTSIZE:
		vp->flag &= ~SPECIAL;
		setint(vp, (int64_t) histsize);
		vp->flag |= SPECIAL;
		break;
	case V_OPTIND:
		vp->flag &= ~SPECIAL;
		setint(vp, (int64_t) user_opt.uoptind);
		vp->flag |= SPECIAL;
		break;
	case V_LINENO:
		vp->flag &= ~SPECIAL;
		setint(vp, (int64_t) current_lineno + user_lineno);
		vp->flag |= SPECIAL;
		break;
	}
}

static void
setspec(struct tbl *vp)
{
	char *s;

	switch (special(vp->name)) {
	case V_PATH:
		afree(search_path, APERM);
		search_path = str_save(str_val(vp), APERM);
		flushcom(1);	/* clear tracked aliases */
		break;
	case V_IFS:
		setctypes(s = str_val(vp), C_IFS);
		ifs0 = *s;
		break;
	case V_OPTIND:
		vp->flag &= ~SPECIAL;
		getopts_reset((int) intval(vp));
		vp->flag |= SPECIAL;
		break;
	case V_POSIXLY_CORRECT:
		change_flag(FPOSIX, OF_SPECIAL, 1);
		break;
	case V_TMPDIR:
		afree(tmpdir, APERM);
		tmpdir = NULL;
		/* Use tmpdir iff it is an absolute path, is writable and
		 * searchable and is a directory...
		 */
		{
			struct stat statb;

			s = str_val(vp);
			if (s[0] == '/' && access(s, W_OK|X_OK) == 0 &&
			    stat(s, &statb) == 0 && S_ISDIR(statb.st_mode))
				tmpdir = str_save(s, APERM);
		}
		break;
	case V_HISTCONTROL:
		sethistcontrol(str_val(vp));
		break;
	case V_HISTSIZE:
		vp->flag &= ~SPECIAL;
		sethistsize((int) intval(vp));
		vp->flag |= SPECIAL;
		break;
	case V_HISTFILE:
		sethistfile(str_val(vp));
		break;
	case V_VISUAL:
		set_editmode(str_val(vp));
		break;
	case V_EDITOR:
		if (!(global("VISUAL")->flag & ISSET))
			set_editmode(str_val(vp));
		break;
	case V_COLUMNS:
		{
			int64_t l;

			if (getint(vp, &l, false) == -1) {
				x_cols = MIN_COLS;
				break;
			}
			if (l <= MIN_COLS || l > INT_MAX)
				x_cols = MIN_COLS;
			else
				x_cols = l;
		}
		break;
#ifndef SMALL
	case V_MAIL:
		mbset(str_val(vp));
		break;
	case V_MAILPATH:
		mpset(str_val(vp));
		break;
	case V_MAILCHECK:
		vp->flag &= ~SPECIAL;
		mcset(intval(vp));
		vp->flag |= SPECIAL;
		break;
#endif /* SMALL */
	case V_RANDOM:
		vp->flag &= ~SPECIAL;
		srand_deterministic((unsigned int)intval(vp));
		vp->flag |= SPECIAL;
		break;
	case V_SECONDS:
		vp->flag &= ~SPECIAL;
		clock_gettime(CLOCK_MONOTONIC, &seconds);
		seconds.tv_sec -= intval(vp);
		vp->flag |= SPECIAL;
		break;
	case V_TMOUT:
		/* Enforce integer to avoid command execution from initcoms[] */
		vp->flag &= ~SPECIAL;
		intval(vp);
		vp->flag |= SPECIAL;
		/* at&t ksh seems to do this (only listen if integer) */
		if (vp->flag & INTEGER)
			ksh_tmout = vp->val.i >= 0 ? vp->val.i : 0;
		break;
	case V_LINENO:
		vp->flag &= ~SPECIAL;
		/* The -1 is because line numbering starts at 1. */
		user_lineno = (unsigned int) intval(vp) - current_lineno - 1;
		vp->flag |= SPECIAL;
		break;
	case V_TERM:
#ifndef SMALL
		{
			int ret;

			vp->flag &= ~SPECIAL;
			if (setupterm(str_val(vp), shl_out->fd, &ret) == ERR)
				del_curterm(cur_term);
			vp->flag |= SPECIAL;
		}
#endif
		break;
	}
}

static void
unsetspec(struct tbl *vp)
{
	switch (special(vp->name)) {
	case V_PATH:
		afree(search_path, APERM);
		search_path = str_save(def_path, APERM);
		flushcom(1);	/* clear tracked aliases */
		break;
	case V_IFS:
		setctypes(" \t\n", C_IFS);
		ifs0 = ' ';
		break;
	case V_TMPDIR:
		/* should not become unspecial */
		afree(tmpdir, APERM);
		tmpdir = NULL;
		break;
#ifndef SMALL
	case V_MAIL:
		mbset(NULL);
		break;
	case V_MAILPATH:
		mpset(NULL);
		break;
#endif /* SMALL */
	case V_HISTCONTROL:
		sethistcontrol(NULL);
		break;
	case V_LINENO:
#ifndef SMALL
	case V_MAILCHECK:	/* at&t ksh leaves previous value in place */
#endif /* SMALL */
	case V_RANDOM:
	case V_SECONDS:
	case V_TMOUT:		/* at&t ksh leaves previous value in place */
		unspecial(vp->name);
		break;

	  /* at&t ksh man page says OPTIND, OPTARG and _ lose special meaning,
	   * but OPTARG does not (still set by getopts) and _ is also still
	   * set in various places.
	   * Don't know what at&t does for:
	   *		MAIL, MAILPATH, HISTSIZE, HISTFILE,
	   * Unsetting these in at&t ksh does not loose the `specialness':
	   *    no effect: IFS, COLUMNS, PATH, TMPDIR,
	   *		VISUAL, EDITOR,
	   * pdkshisms: no effect:
	   *		POSIXLY_CORRECT (use set +o posix instead)
	   */
	}
}

/*
 * Search for (and possibly create) a table entry starting with
 * vp, indexed by val.
 */
static struct tbl *
arraysearch(struct tbl *vp, int val)
{
	struct tbl *prev, *curr, *new;
	size_t namelen = strlen(vp->name) + 1;

	vp->flag |= ARRAY|DEFINED;
	vp->index = 0;
	/* The table entry is always [0] */
	if (val == 0)
		return vp;
	prev = vp;
	curr = vp->u.array;
	while (curr && curr->index < val) {
		prev = curr;
		curr = curr->u.array;
	}
	if (curr && curr->index == val) {
		if (curr->flag&ISSET)
			return curr;
		else
			new = curr;
	} else
		new = alloc(sizeof(struct tbl) + namelen,
		    vp->areap);
	strlcpy(new->name, vp->name, namelen);
	new->flag = vp->flag & ~(ALLOC|DEFINED|ISSET|SPECIAL);
	new->type = vp->type;
	new->areap = vp->areap;
	new->u2.field = vp->u2.field;
	new->index = val;
	if (curr != new) {		/* not reusing old array entry */
		prev->u.array = new;
		new->u.array = curr;
	}
	return new;
}

/* Return the length of an array reference (eg, [1+2]) - cp is assumed
 * to point to the open bracket.  Returns 0 if there is no matching closing
 * bracket.
 */
int
array_ref_len(const char *cp)
{
	const char *s = cp;
	int c;
	int depth = 0;

	while ((c = *s++) && (c != ']' || --depth))
		if (c == '[')
			depth++;
	if (!c)
		return 0;
	return s - cp;
}

/*
 * Make a copy of the base of an array name
 */
char *
arrayname(const char *str)
{
	const char *p;

	if ((p = strchr(str, '[')) == 0)
		/* Shouldn't happen, but why worry? */
		return (char *) str;

	return str_nsave(str, p - str, ATEMP);
}

/* Set (or overwrite, if !reset) the array variable var to the values in vals.
 */
void
set_array(const char *var, int reset, char **vals)
{
	struct tbl *vp, *vq;
	int i;

	/* to get local array, use "typeset foo; set -A foo" */
	vp = global(var);

	/* Note: at&t ksh allows set -A but not set +A of a read-only var */
	if ((vp->flag&RDONLY))
		errorf("%s: is read only", var);
	/* This code is quite non-optimal */
	if (reset > 0)
		/* trash existing values and attributes */
		unset(vp, 0);
	/* todo: would be nice for assignment to completely succeed or
	 * completely fail.  Only really effects integer arrays:
	 * evaluation of some of vals[] may fail...
	 */
	for (i = 0; vals[i]; i++) {
		vq = arraysearch(vp, i);
		/* would be nice to deal with errors here... (see above) */
		setstr(vq, vals[i], KSH_RETURN_ERROR);
	}
}

/*	$OpenBSD: tree.c,v 1.34 2018/04/09 17:53:36 tobias Exp $	*/

/*
 * command tree climbing
 */

#include <string.h>

#include "sh.h"

#define INDENT	4

#define tputc(c, shf)	shf_putchar(c, shf);
static void	ptree(struct op *, int, struct shf *);
static void	pioact(struct shf *, int, struct ioword *);
static void	tputC(int, struct shf *);
static void	tputS(char *, struct shf *);
static void	vfptreef(struct shf *, int, const char *, va_list);
static struct ioword **iocopy(struct ioword **, Area *);
static void     iofree(struct ioword **, Area *);

/*
 * print a command tree
 */

static void
ptree(struct op *t, int indent, struct shf *shf)
{
	char **w;
	struct ioword **ioact;
	struct op *t1;

 Chain:
	if (t == NULL)
		return;
	switch (t->type) {
	case TCOM:
		if (t->vars)
			for (w = t->vars; *w != NULL; )
				fptreef(shf, indent, "%S ", *w++);
		else
			fptreef(shf, indent, "#no-vars# ");
		if (t->args)
			for (w = t->args; *w != NULL; )
				fptreef(shf, indent, "%S ", *w++);
		else
			fptreef(shf, indent, "#no-args# ");
		break;
	case TEXEC:
		t = t->left;
		goto Chain;
	case TPAREN:
		fptreef(shf, indent + 2, "( %T) ", t->left);
		break;
	case TPIPE:
		fptreef(shf, indent, "%T| ", t->left);
		t = t->right;
		goto Chain;
	case TLIST:
		fptreef(shf, indent, "%T%;", t->left);
		t = t->right;
		goto Chain;
	case TOR:
	case TAND:
		fptreef(shf, indent, "%T%s %T",
		    t->left, (t->type==TOR) ? "||" : "&&", t->right);
		break;
	case TBANG:
		fptreef(shf, indent, "! ");
		t = t->right;
		goto Chain;
	case TDBRACKET:
	  {
		int i;

		fptreef(shf, indent, "[[");
		for (i = 0; t->args[i]; i++)
			fptreef(shf, indent, " %S", t->args[i]);
		fptreef(shf, indent, " ]] ");
		break;
	  }
	case TSELECT:
		fptreef(shf, indent, "select %s ", t->str);
		/* FALLTHROUGH */
	case TFOR:
		if (t->type == TFOR)
			fptreef(shf, indent, "for %s ", t->str);
		if (t->vars != NULL) {
			fptreef(shf, indent, "in ");
			for (w = t->vars; *w; )
				fptreef(shf, indent, "%S ", *w++);
			fptreef(shf, indent, "%;");
		}
		fptreef(shf, indent + INDENT, "do%N%T", t->left);
		fptreef(shf, indent, "%;done ");
		break;
	case TCASE:
		fptreef(shf, indent, "case %S in", t->str);
		for (t1 = t->left; t1 != NULL; t1 = t1->right) {
			fptreef(shf, indent, "%N(");
			for (w = t1->vars; *w != NULL; w++)
				fptreef(shf, indent, "%S%c", *w,
				    (w[1] != NULL) ? '|' : ')');
			fptreef(shf, indent + INDENT, "%;%T%N;;", t1->left);
		}
		fptreef(shf, indent, "%Nesac ");
		break;
	case TIF:
	case TELIF:
		/* 3 == strlen("if ") */
		fptreef(shf, indent + 3, "if %T", t->left);
		for (;;) {
			t = t->right;
			if (t->left != NULL) {
				fptreef(shf, indent, "%;");
				fptreef(shf, indent + INDENT, "then%N%T",
				    t->left);
			}
			if (t->right == NULL || t->right->type != TELIF)
				break;
			t = t->right;
			fptreef(shf, indent, "%;");
			/* 5 == strlen("elif ") */
			fptreef(shf, indent + 5, "elif %T", t->left);
		}
		if (t->right != NULL) {
			fptreef(shf, indent, "%;");
			fptreef(shf, indent + INDENT, "else%;%T", t->right);
		}
		fptreef(shf, indent, "%;fi ");
		break;
	case TWHILE:
	case TUNTIL:
		/* 6 == strlen("while"/"until") */
		fptreef(shf, indent + 6, "%s %T",
		    (t->type==TWHILE) ? "while" : "until",
		    t->left);
		fptreef(shf, indent, "%;do");
		fptreef(shf, indent + INDENT, "%;%T", t->right);
		fptreef(shf, indent, "%;done ");
		break;
	case TBRACE:
		fptreef(shf, indent + INDENT, "{%;%T", t->left);
		fptreef(shf, indent, "%;} ");
		break;
	case TCOPROC:
		fptreef(shf, indent, "%T|& ", t->left);
		break;
	case TASYNC:
		fptreef(shf, indent, "%T& ", t->left);
		break;
	case TFUNCT:
		fptreef(shf, indent,
		    t->u.ksh_func ? "function %s %T" : "%s() %T",
		    t->str, t->left);
		break;
	case TTIME:
		fptreef(shf, indent, "time %T", t->left);
		break;
	default:
		fptreef(shf, indent, "<botch>");
		break;
	}
	if ((ioact = t->ioact) != NULL) {
		int	need_nl = 0;

		while (*ioact != NULL)
			pioact(shf, indent, *ioact++);
		/* Print here documents after everything else... */
		for (ioact = t->ioact; *ioact != NULL; ) {
			struct ioword *iop = *ioact++;

			/* heredoc is 0 when tracing (set -x) */
			if ((iop->flag & IOTYPE) == IOHERE && iop->heredoc) {
				tputc('\n', shf);
				shf_puts(iop->heredoc, shf);
				fptreef(shf, indent, "%s",
				    evalstr(iop->delim, 0));
				need_nl = 1;
			}
		}
		/* Last delimiter must be followed by a newline (this often
		 * leads to an extra blank line, but its not worth worrying
		 * about)
		 */
		if (need_nl)
			tputc('\n', shf);
	}
}

static void
pioact(struct shf *shf, int indent, struct ioword *iop)
{
	int flag = iop->flag;
	int type = flag & IOTYPE;
	int expected;

	expected = (type == IOREAD || type == IORDWR || type == IOHERE) ? 0 :
	    (type == IOCAT || type == IOWRITE) ? 1 :
	    (type == IODUP && (iop->unit == !(flag & IORDUP))) ? iop->unit :
	    iop->unit + 1;
	if (iop->unit != expected)
		tputc('0' + iop->unit, shf);

	switch (type) {
	case IOREAD:
		fptreef(shf, indent, "< ");
		break;
	case IOHERE:
		if (flag&IOSKIP)
			fptreef(shf, indent, "<<- ");
		else
			fptreef(shf, indent, "<< ");
		break;
	case IOCAT:
		fptreef(shf, indent, ">> ");
		break;
	case IOWRITE:
		if (flag&IOCLOB)
			fptreef(shf, indent, ">| ");
		else
			fptreef(shf, indent, "> ");
		break;
	case IORDWR:
		fptreef(shf, indent, "<> ");
		break;
	case IODUP:
		if (flag & IORDUP)
			fptreef(shf, indent, "<&");
		else
			fptreef(shf, indent, ">&");
		break;
	}
	/* name/delim are 0 when printing syntax errors */
	if (type == IOHERE) {
		if (iop->delim)
			fptreef(shf, indent, "%S ", iop->delim);
	} else if (iop->name)
		fptreef(shf, indent, (iop->flag & IONAMEXP) ? "%s " : "%S ",
		    iop->name);
}


/*
 * variants of fputc, fputs for ptreef and snptreef
 */

static void
tputC(int c, struct shf *shf)
{
	if ((c&0x60) == 0) {		/* C0|C1 */
		tputc((c&0x80) ? '$' : '^', shf);
		tputc(((c&0x7F)|0x40), shf);
	} else if ((c&0x7F) == 0x7F) {	/* DEL */
		tputc((c&0x80) ? '$' : '^', shf);
		tputc('?', shf);
	} else
		tputc(c, shf);
}

static void
tputS(char *wp, struct shf *shf)
{
	int c, quoted=0;

	/* problems:
	 *	`...` -> $(...)
	 *	'foo' -> "foo"
	 * could change encoding to:
	 *	OQUOTE ["'] ... CQUOTE ["']
	 *	COMSUB [(`] ...\0	(handle $ ` \ and maybe " in `...` case)
	 */
	while (1)
		switch ((c = *wp++)) {
		case EOS:
			return;
		case CHAR:
			tputC(*wp++, shf);
			break;
		case QCHAR:
			c = *wp++;
			if (!quoted || (c == '"' || c == '`' || c == '$'))
				tputc('\\', shf);
			tputC(c, shf);
			break;
		case COMSUB:
			tputc('$', shf);
			tputc('(', shf);
			while (*wp != 0)
				tputC(*wp++, shf);
			tputc(')', shf);
			wp++;
			break;
		case EXPRSUB:
			tputc('$', shf);
			tputc('(', shf);
			tputc('(', shf);
			while (*wp != 0)
				tputC(*wp++, shf);
			tputc(')', shf);
			tputc(')', shf);
			wp++;
			break;
		case OQUOTE:
			quoted = 1;
			tputc('"', shf);
			break;
		case CQUOTE:
			quoted = 0;
			tputc('"', shf);
			break;
		case OSUBST:
			tputc('$', shf);
			if (*wp++ == '{')
				tputc('{', shf);
			while ((c = *wp++) != 0)
				tputC(c, shf);
			break;
		case CSUBST:
			if (*wp++ == '}')
				tputc('}', shf);
			break;
		case OPAT:
			tputc(*wp++, shf);
			tputc('(', shf);
			break;
		case SPAT:
			tputc('|', shf);
			break;
		case CPAT:
			tputc(')', shf);
			break;
		}
}

void
fptreef(struct shf *shf, int indent, const char *fmt, ...)
{
  va_list	va;

  va_start(va, fmt);
  vfptreef(shf, indent, fmt, va);
  va_end(va);
}

char *
snptreef(char *s, int n, const char *fmt, ...)
{
  va_list va;
  struct shf shf;

  shf_sopen(s, n, SHF_WR | (s ? 0 : SHF_DYNAMIC), &shf);

  va_start(va, fmt);
  vfptreef(&shf, 0, fmt, va);
  va_end(va);

  return shf_sclose(&shf); /* null terminates */
}

static void
vfptreef(struct shf *shf, int indent, const char *fmt, va_list va)
{
	int c;

	while ((c = *fmt++)) {
		if (c == '%') {
			int64_t n;
			char *p;
			int neg;

			switch ((c = *fmt++)) {
			case 'c':
				tputc(va_arg(va, int), shf);
				break;
			case 'd': /* decimal */
				n = va_arg(va, int);
				neg = n < 0;
				p = u64ton(neg ? -n : n, 10);
				if (neg)
					*--p = '-';
				while (*p)
					tputc(*p++, shf);
				break;
			case 's':
				p = va_arg(va, char *);
				while (*p)
					tputc(*p++, shf);
				break;
			case 'S':	/* word */
				p = va_arg(va, char *);
				tputS(p, shf);
				break;
			case 'u': /* unsigned decimal */
				p = u64ton(va_arg(va, unsigned int), 10);
				while (*p)
					tputc(*p++, shf);
				break;
			case 'T':	/* format tree */
				ptree(va_arg(va, struct op *), indent, shf);
				break;
			case ';':	/* newline or ; */
			case 'N':	/* newline or space */
				if (shf->flags & SHF_STRING) {
					if (c == ';')
						tputc(';', shf);
					tputc(' ', shf);
				} else {
					int i;

					tputc('\n', shf);
					for (i = indent; i >= 8; i -= 8)
						tputc('\t', shf);
					for (; i > 0; --i)
						tputc(' ', shf);
				}
				break;
			case 'R':
				pioact(shf, indent, va_arg(va, struct ioword *));
				break;
			default:
				tputc(c, shf);
				break;
			}
		} else
			tputc(c, shf);
	}
}

/*
 * copy tree (for function definition)
 */

struct op *
tcopy(struct op *t, Area *ap)
{
	struct op *r;
	char **tw, **rw;

	if (t == NULL)
		return NULL;

	r = alloc(sizeof(struct op), ap);

	r->type = t->type;
	r->u.evalflags = t->u.evalflags;

	r->str = t->type == TCASE ? wdcopy(t->str, ap) : str_save(t->str, ap);

	if (t->vars == NULL)
		r->vars = NULL;
	else {
		for (tw = t->vars; *tw++ != NULL; )
			;
		rw = r->vars = areallocarray(NULL, tw - t->vars + 1,
		    sizeof(*tw), ap);
		for (tw = t->vars; *tw != NULL; )
			*rw++ = wdcopy(*tw++, ap);
		*rw = NULL;
	}

	if (t->args == NULL)
		r->args = NULL;
	else {
		for (tw = t->args; *tw++ != NULL; )
			;
		rw = r->args = areallocarray(NULL, tw - t->args + 1,
		    sizeof(*tw), ap);
		for (tw = t->args; *tw != NULL; )
			*rw++ = wdcopy(*tw++, ap);
		*rw = NULL;
	}

	r->ioact = (t->ioact == NULL) ? NULL : iocopy(t->ioact, ap);

	r->left = tcopy(t->left, ap);
	r->right = tcopy(t->right, ap);
	r->lineno = t->lineno;

	return r;
}

char *
wdcopy(const char *wp, Area *ap)
{
	size_t len = wdscan(wp, EOS) - wp;
	return memcpy(alloc(len, ap), wp, len);
}

/* return the position of prefix c in wp plus 1 */
char *
wdscan(const char *wp, int c)
{
	int nest = 0;

	while (1)
		switch (*wp++) {
		case EOS:
			return (char *) wp;
		case CHAR:
		case QCHAR:
			wp++;
			break;
		case COMSUB:
		case EXPRSUB:
			while (*wp++ != 0)
				;
			break;
		case OQUOTE:
		case CQUOTE:
			break;
		case OSUBST:
			nest++;
			while (*wp++ != '\0')
				;
			break;
		case CSUBST:
			wp++;
			if (c == CSUBST && nest == 0)
				return (char *) wp;
			nest--;
			break;
		case OPAT:
			nest++;
			wp++;
			break;
		case SPAT:
		case CPAT:
			if (c == wp[-1] && nest == 0)
				return (char *) wp;
			if (wp[-1] == CPAT)
				nest--;
			break;
		default:
			internal_warningf(
			    "%s: unknown char 0x%x (carrying on)",
			    __func__, wp[-1]);
		}
}

/* return a copy of wp without any of the mark up characters and
 * with quote characters (" ' \) stripped.
 * (string is allocated from ATEMP)
 */
char *
wdstrip(const char *wp)
{
	struct shf shf;
	int c;

	shf_sopen(NULL, 32, SHF_WR | SHF_DYNAMIC, &shf);

	/* problems:
	 *	`...` -> $(...)
	 *	x${foo:-"hi"} -> x${foo:-hi}
	 *	x${foo:-'hi'} -> x${foo:-hi}
	 */
	while (1)
		switch ((c = *wp++)) {
		case EOS:
			return shf_sclose(&shf); /* null terminates */
		case CHAR:
		case QCHAR:
			shf_putchar(*wp++, &shf);
			break;
		case COMSUB:
			shf_putchar('$', &shf);
			shf_putchar('(', &shf);
			while (*wp != 0)
				shf_putchar(*wp++, &shf);
			shf_putchar(')', &shf);
			break;
		case EXPRSUB:
			shf_putchar('$', &shf);
			shf_putchar('(', &shf);
			shf_putchar('(', &shf);
			while (*wp != 0)
				shf_putchar(*wp++, &shf);
			shf_putchar(')', &shf);
			shf_putchar(')', &shf);
			break;
		case OQUOTE:
			break;
		case CQUOTE:
			break;
		case OSUBST:
			shf_putchar('$', &shf);
			if (*wp++ == '{')
			    shf_putchar('{', &shf);
			while ((c = *wp++) != 0)
				shf_putchar(c, &shf);
			break;
		case CSUBST:
			if (*wp++ == '}')
				shf_putchar('}', &shf);
			break;
		case OPAT:
			shf_putchar(*wp++, &shf);
			shf_putchar('(', &shf);
			break;
		case SPAT:
			shf_putchar('|', &shf);
			break;
		case CPAT:
			shf_putchar(')', &shf);
			break;
		}
}

static	struct ioword **
iocopy(struct ioword **iow, Area *ap)
{
	struct ioword **ior;
	int i;

	for (ior = iow; *ior++ != NULL; )
		;
	ior = areallocarray(NULL, ior - iow + 1, sizeof(*ior), ap);

	for (i = 0; iow[i] != NULL; i++) {
		struct ioword *p, *q;

		p = iow[i];
		q = alloc(sizeof(*p), ap);
		ior[i] = q;
		*q = *p;
		if (p->name != NULL)
			q->name = wdcopy(p->name, ap);
		if (p->delim != NULL)
			q->delim = wdcopy(p->delim, ap);
		if (p->heredoc != NULL)
			q->heredoc = str_save(p->heredoc, ap);
	}
	ior[i] = NULL;

	return ior;
}

/*
 * free tree (for function definition)
 */

void
tfree(struct op *t, Area *ap)
{
	char **w;

	if (t == NULL)
		return;

	afree(t->str, ap);

	if (t->vars != NULL) {
		for (w = t->vars; *w != NULL; w++)
			afree(*w, ap);
		afree(t->vars, ap);
	}

	if (t->args != NULL) {
		for (w = t->args; *w != NULL; w++)
			afree(*w, ap);
		afree(t->args, ap);
	}

	if (t->ioact != NULL)
		iofree(t->ioact, ap);

	tfree(t->left, ap);
	tfree(t->right, ap);

	afree(t, ap);
}

static	void
iofree(struct ioword **iow, Area *ap)
{
	struct ioword **iop;
	struct ioword *p;

	for (iop = iow; (p = *iop++) != NULL; ) {
		afree(p->name, ap);
		afree(p->delim, ap);
		afree(p->heredoc, ap);
		afree(p, ap);
	}
	afree(iow, ap);
}

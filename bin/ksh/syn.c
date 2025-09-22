/*	$OpenBSD: syn.c,v 1.40 2021/07/05 13:41:46 millert Exp $	*/

/*
 * shell parser (C version)
 */

#include <string.h>

#include "sh.h"
#include "c_test.h"

struct nesting_state {
	int	start_token;	/* token than began nesting (eg, FOR) */
	int	start_line;	/* line nesting began on */
};

static void	yyparse(void);
static struct op *pipeline(int);
static struct op *andor(void);
static struct op *c_list(int);
static struct ioword *synio(int);
static void	musthave(int, int);
static struct op *nested(int, int, int);
static struct op *get_command(int);
static struct op *dogroup(void);
static struct op *thenpart(void);
static struct op *elsepart(void);
static struct op *caselist(void);
static struct op *casepart(int);
static struct op *function_body(char *, int);
static char **	wordlist(void);
static struct op *block(int, struct op *, struct op *, char **);
static struct op *newtp(int);
static void	syntaxerr(const char *) __attribute__((__noreturn__));
static void	nesting_push(struct nesting_state *, int);
static void	nesting_pop(struct nesting_state *);
static int	assign_command(char *);
static int	inalias(struct source *);
static int	dbtestp_isa(Test_env *, Test_meta);
static const char *dbtestp_getopnd(Test_env *, Test_op, int);
static int	dbtestp_eval(Test_env *, Test_op, const char *, const char *,
		    int);
static void	dbtestp_error(Test_env *, int, const char *);

static	struct	op	*outtree; /* yyparse output */

static struct nesting_state nesting;	/* \n changed to ; */

static	int	reject;		/* token(cf) gets symbol again */
static	int	symbol;		/* yylex value */

#define	token(cf) \
	((reject) ? (reject = false, symbol) : (symbol = yylex(cf)))
#define	tpeek(cf) \
	((reject) ? (symbol) : (reject = true, symbol = yylex(cf)))

static void
yyparse(void)
{
	int c;

	reject = false;

	outtree = c_list(source->type == SSTRING);
	c = tpeek(0);
	if (c == 0 && !outtree)
		outtree = newtp(TEOF);
	else if (c != '\n' && c != 0)
		syntaxerr(NULL);
}

static struct op *
pipeline(int cf)
{
	struct op *t, *p, *tl = NULL;

	t = get_command(cf);
	if (t != NULL) {
		while (token(0) == '|') {
			if ((p = get_command(CONTIN)) == NULL)
				syntaxerr(NULL);
			if (tl == NULL)
				t = tl = block(TPIPE, t, p, NULL);
			else
				tl = tl->right = block(TPIPE, tl->right, p, NULL);
		}
		reject = true;
	}
	return (t);
}

static struct op *
andor(void)
{
	struct op *t, *p;
	int c;

	t = pipeline(0);
	if (t != NULL) {
		while ((c = token(0)) == LOGAND || c == LOGOR) {
			if ((p = pipeline(CONTIN)) == NULL)
				syntaxerr(NULL);
			t = block(c == LOGAND? TAND: TOR, t, p, NULL);
		}
		reject = true;
	}
	return (t);
}

static struct op *
c_list(int multi)
{
	struct op *t = NULL, *p, *tl = NULL;
	int c;
	int have_sep;

	while (1) {
		p = andor();
		/* Token has always been read/rejected at this point, so
		 * we don't worry about what flags to pass token()
		 */
		c = token(0);
		have_sep = 1;
		if (c == '\n' && (multi || inalias(source))) {
			if (!p) /* ignore blank lines */
				continue;
		} else if (!p)
			break;
		else if (c == '&' || c == COPROC)
			p = block(c == '&' ? TASYNC : TCOPROC,
				  p, NULL, NULL);
		else if (c != ';')
			have_sep = 0;
		if (!t)
			t = p;
		else if (!tl)
			t = tl = block(TLIST, t, p, NULL);
		else
			tl = tl->right = block(TLIST, tl->right, p, NULL);
		if (!have_sep)
			break;
	}
	reject = true;
	return t;
}

static struct ioword *
synio(int cf)
{
	struct ioword *iop;
	int ishere;

	if (tpeek(cf) != REDIR)
		return NULL;
	reject = false;
	iop = yylval.iop;
	ishere = (iop->flag&IOTYPE) == IOHERE;
	musthave(LWORD, ishere ? HEREDELIM : 0);
	if (ishere) {
		iop->delim = yylval.cp;
		if (*ident != 0) /* unquoted */
			iop->flag |= IOEVAL;
		if (herep >= &heres[HERES])
			yyerror("too many <<'s\n");
		*herep++ = iop;
	} else
		iop->name = yylval.cp;
	return iop;
}

static void
musthave(int c, int cf)
{
	if ((token(cf)) != c)
		syntaxerr(NULL);
}

static struct op *
nested(int type, int smark, int emark)
{
	struct op *t;
	struct nesting_state old_nesting;

	nesting_push(&old_nesting, smark);
	t = c_list(true);
	musthave(emark, KEYWORD|ALIAS);
	nesting_pop(&old_nesting);
	return (block(type, t, NULL, NULL));
}

static struct op *
get_command(int cf)
{
	struct op *t;
	int c, iopn = 0, syniocf;
	struct ioword *iop, **iops;
	XPtrV args, vars;
	struct nesting_state old_nesting;

	iops = areallocarray(NULL, NUFILE + 1,
	    sizeof(struct ioword *), ATEMP);
	XPinit(args, 16);
	XPinit(vars, 16);

	syniocf = KEYWORD|ALIAS;
	switch (c = token(cf|KEYWORD|ALIAS|VARASN)) {
	default:
		reject = true;
		afree(iops, ATEMP);
		XPfree(args);
		XPfree(vars);
		return NULL; /* empty line */

	case LWORD:
	case REDIR:
		reject = true;
		syniocf &= ~(KEYWORD|ALIAS);
		t = newtp(TCOM);
		t->lineno = source->line;
		while (1) {
			cf = (t->u.evalflags ? ARRAYVAR : 0) |
			    (XPsize(args) == 0 ? ALIAS|VARASN : CMDWORD);
			switch (tpeek(cf)) {
			case REDIR:
				if (iopn >= NUFILE)
					yyerror("too many redirections\n");
				iops[iopn++] = synio(cf);
				break;

			case LWORD:
				reject = false;
				/* the iopn == 0 and XPsize(vars) == 0 are
				 * dubious but at&t ksh acts this way
				 */
				if (iopn == 0 && XPsize(vars) == 0 &&
				    XPsize(args) == 0 &&
				    assign_command(ident))
					t->u.evalflags = DOVACHECK;
				if ((XPsize(args) == 0 || Flag(FKEYWORD)) &&
				    is_wdvarassign(yylval.cp))
					XPput(vars, yylval.cp);
				else
					XPput(args, yylval.cp);
				break;

			case '(':
				/* Check for "> foo (echo hi)", which at&t ksh
				 * allows (not POSIX, but not disallowed)
				 */
				afree(t, ATEMP);
				if (XPsize(args) == 0 && XPsize(vars) == 0) {
					reject = false;
					goto Subshell;
				}
				/* Must be a function */
				if (iopn != 0 || XPsize(args) != 1 ||
				    XPsize(vars) != 0)
					syntaxerr(NULL);
				reject = false;
				/*(*/
				musthave(')', 0);
				t = function_body(XPptrv(args)[0], false);
				goto Leave;

			default:
				goto Leave;
			}
		}
	  Leave:
		break;

	  Subshell:
	case '(':
		t = nested(TPAREN, '(', ')');
		break;

	case '{': /*}*/
		t = nested(TBRACE, '{', '}');
		break;

	case MDPAREN:
	  {
		static const char let_cmd[] = {
			CHAR, 'l', CHAR, 'e',
			CHAR, 't', EOS
		};
		/* Leave KEYWORD in syniocf (allow if (( 1 )) then ...) */
		t = newtp(TCOM);
		t->lineno = source->line;
		reject = false;
		XPput(args, wdcopy(let_cmd, ATEMP));
		musthave(LWORD,LETEXPR);
		XPput(args, yylval.cp);
		break;
	  }

	case DBRACKET: /* [[ .. ]] */
		/* Leave KEYWORD in syniocf (allow if [[ -n 1 ]] then ...) */
		t = newtp(TDBRACKET);
		reject = false;
		{
			Test_env te;

			te.flags = TEF_DBRACKET;
			te.pos.av = &args;
			te.isa = dbtestp_isa;
			te.getopnd = dbtestp_getopnd;
			te.eval = dbtestp_eval;
			te.error = dbtestp_error;

			test_parse(&te);
		}
		break;

	case FOR:
	case SELECT:
		t = newtp((c == FOR) ? TFOR : TSELECT);
		musthave(LWORD, ARRAYVAR);
		if (!is_wdvarname(yylval.cp, true))
			yyerror("%s: bad identifier\n",
			    c == FOR ? "for" : "select");
		t->str = str_save(ident, ATEMP);
		nesting_push(&old_nesting, c);
		t->vars = wordlist();
		t->left = dogroup();
		nesting_pop(&old_nesting);
		break;

	case WHILE:
	case UNTIL:
		nesting_push(&old_nesting, c);
		t = newtp((c == WHILE) ? TWHILE : TUNTIL);
		t->left = c_list(true);
		if (t->left == NULL)
			syntaxerr(NULL);
		t->right = dogroup();
		nesting_pop(&old_nesting);
		break;

	case CASE:
		t = newtp(TCASE);
		musthave(LWORD, 0);
		t->str = yylval.cp;
		nesting_push(&old_nesting, c);
		t->left = caselist();
		nesting_pop(&old_nesting);
		break;

	case IF:
		nesting_push(&old_nesting, c);
		t = newtp(TIF);
		t->left = c_list(true);
		t->right = thenpart();
		musthave(FI, KEYWORD|ALIAS);
		nesting_pop(&old_nesting);
		break;

	case BANG:
		syniocf &= ~(KEYWORD|ALIAS);
		t = pipeline(0);
		if (t == NULL)
			syntaxerr(NULL);
		t = block(TBANG, NULL, t, NULL);
		break;

	case TIME:
		syniocf &= ~(KEYWORD|ALIAS);
		t = pipeline(0);
		if (t) {
			if (t->str) {
				t->str = str_save(t->str, ATEMP);
			} else {
				t->str = alloc(2, ATEMP);
				t->str[0] = '\0'; /* TF_* flags */
				t->str[1] = '\0';
			}
		}
		t = block(TTIME, t, NULL, NULL);
		break;

	case FUNCTION:
		musthave(LWORD, 0);
		t = function_body(yylval.cp, true);
		break;
	}

	while ((iop = synio(syniocf)) != NULL) {
		if (iopn >= NUFILE)
			yyerror("too many redirections\n");
		iops[iopn++] = iop;
	}

	if (iopn == 0) {
		afree(iops, ATEMP);
		t->ioact = NULL;
	} else {
		iops[iopn++] = NULL;
		iops = areallocarray(iops, iopn,
		    sizeof(struct ioword *), ATEMP);
		t->ioact = iops;
	}

	if (t->type == TCOM || t->type == TDBRACKET) {
		XPput(args, NULL);
		t->args = (char **) XPclose(args);
		XPput(vars, NULL);
		t->vars = (char **) XPclose(vars);
	} else {
		XPfree(args);
		XPfree(vars);
	}

	return t;
}

static struct op *
dogroup(void)
{
	int c;
	struct op *list;

	c = token(CONTIN|KEYWORD|ALIAS);
	/* A {...} can be used instead of do...done for for/select loops
	 * but not for while/until loops - we don't need to check if it
	 * is a while loop because it would have been parsed as part of
	 * the conditional command list...
	 */
	if (c == DO)
		c = DONE;
	else if (c == '{')
		c = '}';
	else
		syntaxerr(NULL);
	list = c_list(true);
	musthave(c, KEYWORD|ALIAS);
	return list;
}

static struct op *
thenpart(void)
{
	struct op *t;

	musthave(THEN, KEYWORD|ALIAS);
	t = newtp(0);
	t->left = c_list(true);
	if (t->left == NULL)
		syntaxerr(NULL);
	t->right = elsepart();
	return (t);
}

static struct op *
elsepart(void)
{
	struct op *t;

	switch (token(KEYWORD|ALIAS|VARASN)) {
	case ELSE:
		if ((t = c_list(true)) == NULL)
			syntaxerr(NULL);
		return (t);

	case ELIF:
		t = newtp(TELIF);
		t->left = c_list(true);
		t->right = thenpart();
		return (t);

	default:
		reject = true;
	}
	return NULL;
}

static struct op *
caselist(void)
{
	struct op *t, *tl;
	int c;

	c = token(CONTIN|KEYWORD|ALIAS);
	/* A {...} can be used instead of in...esac for case statements */
	if (c == IN)
		c = ESAC;
	else if (c == '{')
		c = '}';
	else
		syntaxerr(NULL);
	t = tl = NULL;
	while ((tpeek(CONTIN|KEYWORD|ESACONLY)) != c) { /* no ALIAS here */
		struct op *tc = casepart(c);
		if (tl == NULL)
			t = tl = tc, tl->right = NULL;
		else
			tl->right = tc, tl = tc;
	}
	musthave(c, KEYWORD|ALIAS);
	return (t);
}

static struct op *
casepart(int endtok)
{
	struct op *t;
	int c;
	XPtrV ptns;

	XPinit(ptns, 16);
	t = newtp(TPAT);
	c = token(CONTIN|KEYWORD); /* no ALIAS here */
	if (c != '(')
		reject = true;
	do {
		musthave(LWORD, 0);
		XPput(ptns, yylval.cp);
	} while ((c = token(0)) == '|');
	reject = true;
	XPput(ptns, NULL);
	t->vars = (char **) XPclose(ptns);
	musthave(')', 0);

	t->left = c_list(true);
	/* Note: Posix requires the ;; */
	if ((tpeek(CONTIN|KEYWORD|ALIAS)) != endtok)
		musthave(BREAK, CONTIN|KEYWORD|ALIAS);
	return (t);
}

static struct op *
function_body(char *name,
    int ksh_func)		/* function foo { ... } vs foo() { .. } */
{
	char *sname, *p;
	struct op *t;
	int old_func_parse;

	sname = wdstrip(name);
	/* Check for valid characters in name.  posix and ksh93 say only
	 * allow [a-zA-Z_0-9] but this allows more as old pdksh's have
	 * allowed more (the following were never allowed:
	 *	nul space nl tab $ ' " \ ` ( ) & | ; = < >
	 *  C_QUOTE covers all but = and adds # [ ? *)
	 */
	for (p = sname; *p; p++)
		if (ctype(*p, C_QUOTE) || *p == '=')
			yyerror("%s: invalid function name\n", sname);

	t = newtp(TFUNCT);
	t->str = sname;
	t->u.ksh_func = ksh_func;
	t->lineno = source->line;

	/* Note that POSIX allows only compound statements after foo(), sh and
	 * at&t ksh allow any command, go with the later since it shouldn't
	 * break anything.  However, for function foo, at&t ksh only accepts
	 * an open-brace.
	 */
	if (ksh_func) {
		musthave('{', CONTIN|KEYWORD|ALIAS); /* } */
		reject = true;
	}

	old_func_parse = genv->flags & EF_FUNC_PARSE;
	genv->flags |= EF_FUNC_PARSE;
	if ((t->left = get_command(CONTIN)) == NULL) {
		/*
		 * Probably something like foo() followed by eof or ;.
		 * This is accepted by sh and ksh88.
		 * To make "typeset -f foo" work reliably (so its output can
		 * be used as input), we pretend there is a colon here.
		 */
		t->left = newtp(TCOM);
		t->left->args = areallocarray(NULL, 2, sizeof(char *), ATEMP);
		t->left->args[0] = alloc(3, ATEMP);
		t->left->args[0][0] = CHAR;
		t->left->args[0][1] = ':';
		t->left->args[0][2] = EOS;
		t->left->args[1] = NULL;
		t->left->vars = alloc(sizeof(char *), ATEMP);
		t->left->vars[0] = NULL;
		t->left->lineno = 1;
	}
	if (!old_func_parse)
		genv->flags &= ~EF_FUNC_PARSE;

	return t;
}

static char **
wordlist(void)
{
	int c;
	XPtrV args;

	XPinit(args, 16);
	/* Posix does not do alias expansion here... */
	if ((c = token(CONTIN|KEYWORD|ALIAS)) != IN) {
		if (c != ';') /* non-POSIX, but at&t ksh accepts a ; here */
			reject = true;
		return NULL;
	}
	while ((c = token(0)) == LWORD)
		XPput(args, yylval.cp);
	if (c != '\n' && c != ';')
		syntaxerr(NULL);
	XPput(args, NULL);
	return (char **) XPclose(args);
}

/*
 * supporting functions
 */

static struct op *
block(int type, struct op *t1, struct op *t2, char **wp)
{
	struct op *t;

	t = newtp(type);
	t->left = t1;
	t->right = t2;
	t->vars = wp;
	return (t);
}

const	struct tokeninfo {
	const char *name;
	short	val;
	short	reserved;
} tokentab[] = {
	/* Reserved words */
	{ "if",		IF,	true },
	{ "then",	THEN,	true },
	{ "else",	ELSE,	true },
	{ "elif",	ELIF,	true },
	{ "fi",		FI,	true },
	{ "case",	CASE,	true },
	{ "esac",	ESAC,	true },
	{ "for",	FOR,	true },
	{ "select",	SELECT,	true },
	{ "while",	WHILE,	true },
	{ "until",	UNTIL,	true },
	{ "do",		DO,	true },
	{ "done",	DONE,	true },
	{ "in",		IN,	true },
	{ "function",	FUNCTION, true },
	{ "time",	TIME,	true },
	{ "{",		'{',	true },
	{ "}",		'}',	true },
	{ "!",		BANG,	true },
	{ "[[",		DBRACKET, true },
	/* Lexical tokens (0[EOF], LWORD and REDIR handled specially) */
	{ "&&",		LOGAND,	false },
	{ "||",		LOGOR,	false },
	{ ";;",		BREAK,	false },
	{ "((",		MDPAREN, false },
	{ "|&",		COPROC,	false },
	/* and some special cases... */
	{ "newline",	'\n',	false },
	{ 0 }
};

void
initkeywords(void)
{
	struct tokeninfo const *tt;
	struct tbl *p;

	ktinit(&keywords, APERM, 32); /* must be 2^n (currently 20 keywords) */
	for (tt = tokentab; tt->name; tt++) {
		if (tt->reserved) {
			p = ktenter(&keywords, tt->name, hash(tt->name));
			p->flag |= DEFINED|ISSET;
			p->type = CKEYWD;
			p->val.i = tt->val;
		}
	}
}

static void
syntaxerr(const char *what)
{
	char redir[6];	/* 2<<- is the longest redirection, I think */
	const char *s;
	struct tokeninfo const *tt;
	int c;

	if (!what)
		what = "unexpected";
	reject = true;
	c = token(0);
    Again:
	switch (c) {
	case 0:
		if (nesting.start_token) {
			c = nesting.start_token;
			source->errline = nesting.start_line;
			what = "unmatched";
			goto Again;
		}
		/* don't quote the EOF */
		yyerror("syntax error: unexpected EOF\n");
		/* NOTREACHED */

	case LWORD:
		s = snptreef(NULL, 32, "%S", yylval.cp);
		break;

	case REDIR:
		s = snptreef(redir, sizeof(redir), "%R", yylval.iop);
		break;

	default:
		for (tt = tokentab; tt->name; tt++)
			if (tt->val == c)
			    break;
		if (tt->name)
			s = tt->name;
		else {
			if (c > 0 && c < 256) {
				redir[0] = c;
				redir[1] = '\0';
			} else
				shf_snprintf(redir, sizeof(redir),
					"?%d", c);
			s = redir;
		}
	}
	yyerror("syntax error: `%s' %s\n", s, what);
}

static void
nesting_push(struct nesting_state *save, int tok)
{
	*save = nesting;
	nesting.start_token = tok;
	nesting.start_line = source->line;
}

static void
nesting_pop(struct nesting_state *saved)
{
	nesting = *saved;
}

static struct op *
newtp(int type)
{
	struct op *t;

	t = alloc(sizeof(*t), ATEMP);
	t->type = type;
	t->u.evalflags = 0;
	t->args = t->vars = NULL;
	t->ioact = NULL;
	t->left = t->right = NULL;
	t->str = NULL;
	return (t);
}

struct op *
compile(Source *s)
{
	nesting.start_token = 0;
	nesting.start_line = 0;
	herep = heres;
	source = s;
	yyparse();
	return outtree;
}

/* This kludge exists to take care of sh/at&t ksh oddity in which
 * the arguments of alias/export/readonly/typeset have no field
 * splitting, file globbing, or (normal) tilde expansion done.
 * at&t ksh seems to do something similar to this since
 *	$ touch a=a; typeset a=[ab]; echo "$a"
 *	a=[ab]
 *	$ x=typeset; $x a=[ab]; echo "$a"
 *	a=a
 *	$
 */
static int
assign_command(char *s)
{
	if (Flag(FPOSIX) || !*s)
		return 0;
	return (strcmp(s, "alias") == 0) ||
	    (strcmp(s, "export") == 0) ||
	    (strcmp(s, "readonly") == 0) ||
	    (strcmp(s, "typeset") == 0);
}

/* Check if we are in the middle of reading an alias */
static int
inalias(struct source *s)
{
	for (; s && s->type == SALIAS; s = s->next)
		if (!(s->flags & SF_ALIASEND))
			return 1;
	return 0;
}


/* Order important - indexed by Test_meta values
 * Note that ||, &&, ( and ) can't appear in as unquoted strings
 * in normal shell input, so these can be interpreted unambiguously
 * in the evaluation pass.
 */
static const char dbtest_or[] = { CHAR, '|', CHAR, '|', EOS };
static const char dbtest_and[] = { CHAR, '&', CHAR, '&', EOS };
static const char dbtest_not[] = { CHAR, '!', EOS };
static const char dbtest_oparen[] = { CHAR, '(', EOS };
static const char dbtest_cparen[] = { CHAR, ')', EOS };
const char *const dbtest_tokens[] = {
	dbtest_or, dbtest_and, dbtest_not,
	dbtest_oparen, dbtest_cparen
};
const char db_close[] = { CHAR, ']', CHAR, ']', EOS };
const char db_lthan[] = { CHAR, '<', EOS };
const char db_gthan[] = { CHAR, '>', EOS };

/* Test if the current token is a whatever.  Accepts the current token if
 * it is.  Returns 0 if it is not, non-zero if it is (in the case of
 * TM_UNOP and TM_BINOP, the returned value is a Test_op).
 */
static int
dbtestp_isa(Test_env *te, Test_meta meta)
{
	int c = tpeek(ARRAYVAR | (meta == TM_BINOP ? 0 : CONTIN));
	int uqword = 0;
	char *save = NULL;
	int ret = 0;

	/* unquoted word? */
	uqword = c == LWORD && *ident;

	if (meta == TM_OR)
		ret = c == LOGOR;
	else if (meta == TM_AND)
		ret = c == LOGAND;
	else if (meta == TM_NOT)
		ret = uqword && strcmp(yylval.cp, dbtest_tokens[(int) TM_NOT]) == 0;
	else if (meta == TM_OPAREN)
		ret = c == '(' /*)*/;
	else if (meta == TM_CPAREN)
		ret = c == /*(*/ ')';
	else if (meta == TM_UNOP || meta == TM_BINOP) {
		if (meta == TM_BINOP && c == REDIR &&
		    (yylval.iop->flag == IOREAD || yylval.iop->flag == IOWRITE)) {
			ret = 1;
			save = wdcopy(yylval.iop->flag == IOREAD ?
			    db_lthan : db_gthan, ATEMP);
		} else if (uqword && (ret = (int) test_isop(te, meta, ident)))
			save = yylval.cp;
	} else /* meta == TM_END */
		ret = uqword && strcmp(yylval.cp, db_close) == 0;
	if (ret) {
		reject = false;
		if (meta != TM_END) {
			if (!save)
				save = wdcopy(dbtest_tokens[(int) meta], ATEMP);
			XPput(*te->pos.av, save);
		}
	}
	return ret;
}

static const char *
dbtestp_getopnd(Test_env *te, Test_op op, int do_eval)
{
	int c = tpeek(ARRAYVAR);

	if (c != LWORD)
		return NULL;

	reject = false;
	XPput(*te->pos.av, yylval.cp);

	return null;
}

static int
dbtestp_eval(Test_env *te, Test_op op, const char *opnd1, const char *opnd2,
    int do_eval)
{
	return 1;
}

static void
dbtestp_error(Test_env *te, int offset, const char *msg)
{
	te->flags |= TEF_ERROR;

	if (offset < 0) {
		reject = true;
		/* Kludgy to say the least... */
		symbol = LWORD;
		yylval.cp = *(XPptrv(*te->pos.av) + XPsize(*te->pos.av) +
		    offset);
	}
	syntaxerr(msg);
}

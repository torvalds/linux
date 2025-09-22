/*	$OpenBSD: expr.c,v 1.34 2019/02/20 23:59:17 schwarze Exp $	*/

/*
 * Korn expression evaluation
 */
/*
 * todo: better error handling: if in builtin, should be builtin error, etc.
 */

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "sh.h"

/* The order of these enums is constrained by the order of opinfo[] */
enum token {
	/* some (long) unary operators */
	O_PLUSPLUS = 0, O_MINUSMINUS,
	/* binary operators */
	O_EQ, O_NE,
	/* assignments are assumed to be in range O_ASN .. O_BORASN */
	O_ASN, O_TIMESASN, O_DIVASN, O_MODASN, O_PLUSASN, O_MINUSASN,
	O_LSHIFTASN, O_RSHIFTASN, O_BANDASN, O_BXORASN, O_BORASN,
	O_LSHIFT, O_RSHIFT,
	O_LE, O_GE, O_LT, O_GT,
	O_LAND,
	O_LOR,
	O_TIMES, O_DIV, O_MOD,
	O_PLUS, O_MINUS,
	O_BAND,
	O_BXOR,
	O_BOR,
	O_TERN,
	O_COMMA,
	/* things after this aren't used as binary operators */
	/* unary that are not also binaries */
	O_BNOT, O_LNOT,
	/* misc */
	OPEN_PAREN, CLOSE_PAREN, CTERN,
	/* things that don't appear in the opinfo[] table */
	VAR, LIT, END, BAD
};
#define IS_BINOP(op) (((int)op) >= (int)O_EQ && ((int)op) <= (int)O_COMMA)
#define IS_ASSIGNOP(op)	((int)(op) >= (int)O_ASN && (int)(op) <= (int)O_BORASN)

enum prec {
	P_PRIMARY = 0,		/* VAR, LIT, (), ~ ! - + */
	P_MULT,			/* * / % */
	P_ADD,			/* + - */
	P_SHIFT,		/* << >> */
	P_RELATION,		/* < <= > >= */
	P_EQUALITY,		/* == != */
	P_BAND,			/* & */
	P_BXOR,			/* ^ */
	P_BOR,			/* | */
	P_LAND,			/* && */
	P_LOR,			/* || */
	P_TERN,			/* ?: */
	P_ASSIGN,		/* = *= /= %= += -= <<= >>= &= ^= |= */
	P_COMMA			/* , */
};
#define MAX_PREC	P_COMMA

struct opinfo {
	char		name[4];
	int		len;	/* name length */
	enum prec	prec;	/* precedence: lower is higher */
};

/* Tokens in this table must be ordered so the longest are first
 * (eg, += before +).  If you change something, change the order
 * of enum token too.
 */
static const struct opinfo opinfo[] = {
	{ "++",	 2, P_PRIMARY },	/* before + */
	{ "--",	 2, P_PRIMARY },	/* before - */
	{ "==",	 2, P_EQUALITY },	/* before = */
	{ "!=",	 2, P_EQUALITY },	/* before ! */
	{ "=",	 1, P_ASSIGN },		/* keep assigns in a block */
	{ "*=",	 2, P_ASSIGN },
	{ "/=",	 2, P_ASSIGN },
	{ "%=",	 2, P_ASSIGN },
	{ "+=",	 2, P_ASSIGN },
	{ "-=",	 2, P_ASSIGN },
	{ "<<=", 3, P_ASSIGN },
	{ ">>=", 3, P_ASSIGN },
	{ "&=",	 2, P_ASSIGN },
	{ "^=",	 2, P_ASSIGN },
	{ "|=",	 2, P_ASSIGN },
	{ "<<",	 2, P_SHIFT },
	{ ">>",	 2, P_SHIFT },
	{ "<=",	 2, P_RELATION },
	{ ">=",	 2, P_RELATION },
	{ "<",	 1, P_RELATION },
	{ ">",	 1, P_RELATION },
	{ "&&",	 2, P_LAND },
	{ "||",	 2, P_LOR },
	{ "*",	 1, P_MULT },
	{ "/",	 1, P_MULT },
	{ "%",	 1, P_MULT },
	{ "+",	 1, P_ADD },
	{ "-",	 1, P_ADD },
	{ "&",	 1, P_BAND },
	{ "^",	 1, P_BXOR },
	{ "|",	 1, P_BOR },
	{ "?",	 1, P_TERN },
	{ ",",	 1, P_COMMA },
	{ "~",	 1, P_PRIMARY },
	{ "!",	 1, P_PRIMARY },
	{ "(",	 1, P_PRIMARY },
	{ ")",	 1, P_PRIMARY },
	{ ":",	 1, P_PRIMARY },
	{ "",	 0, P_PRIMARY } /* end of table */
};


typedef struct expr_state Expr_state;
struct expr_state {
	const char *expression;		/* expression being evaluated */
	const char *tokp;		/* lexical position */
	enum token  tok;		/* token from token() */
	int	    noassign;		/* don't do assigns (for ?:,&&,||) */
	bool	    arith;		/* true if evaluating an $(())
					 * expression
					 */
	struct tbl *val;		/* value from token() */
	struct tbl *evaling;		/* variable that is being recursively
					 * expanded (EXPRINEVAL flag set)
					 */
};

enum error_type {
	ET_UNEXPECTED, ET_BADLIT, ET_RECURSIVE,
	ET_LVALUE, ET_RDONLY, ET_STR
};

static void	   evalerr(Expr_state *, enum error_type, const char *)
		    __attribute__((__noreturn__));
static struct tbl *evalexpr(Expr_state *, enum prec);
static void	   token(Expr_state *);
static struct tbl *do_ppmm(Expr_state *, enum token, struct tbl *, bool);
static void	   assign_check(Expr_state *, enum token, struct tbl *);
static struct tbl *tempvar(void);
static struct tbl *intvar(Expr_state *, struct tbl *);

/*
 * parse and evaluate expression
 */
int
evaluate(const char *expr, int64_t *rval, int error_ok, bool arith)
{
	struct tbl v;
	int ret;

	v.flag = DEFINED|INTEGER;
	v.type = 0;
	ret = v_evaluate(&v, expr, error_ok, arith);
	*rval = v.val.i;
	return ret;
}

/*
 * parse and evaluate expression, storing result in vp.
 */
int
v_evaluate(struct tbl *vp, const char *expr, volatile int error_ok,
    bool arith)
{
	struct tbl *v;
	Expr_state curstate;
	Expr_state * const es = &curstate;
	int save_disable_subst;
	int i;

	/* save state to allow recursive calls */
	curstate.expression = curstate.tokp = expr;
	curstate.noassign = 0;
	curstate.arith = arith;
	curstate.evaling = NULL;
	curstate.val = NULL;

	newenv(E_ERRH);
	save_disable_subst = disable_subst;
	i = sigsetjmp(genv->jbuf, 0);
	if (i) {
		disable_subst = save_disable_subst;
		/* Clear EXPRINEVAL in of any variables we were playing with */
		if (curstate.evaling)
			curstate.evaling->flag &= ~EXPRINEVAL;
		quitenv(NULL);
		if (i == LAEXPR) {
			if (error_ok == KSH_RETURN_ERROR)
				return 0;
			errorf(NULL);
		}
		unwind(i);
		/* NOTREACHED */
	}

	token(es);
#if 1 /* ifdef-out to disallow empty expressions to be treated as 0 */
	if (es->tok == END) {
		es->tok = LIT;
		es->val = tempvar();
	}
#endif /* 0 */
	v = intvar(es, evalexpr(es, MAX_PREC));

	if (es->tok != END)
		evalerr(es, ET_UNEXPECTED, NULL);

	if (vp->flag & INTEGER)
		setint_v(vp, v, es->arith);
	else
		/* can fail if readonly */
		setstr(vp, str_val(v), error_ok);

	quitenv(NULL);

	return 1;
}

static void
evalerr(Expr_state *es, enum error_type type, const char *str)
{
	char tbuf[2];
	const char *s;

	es->arith = false;
	switch (type) {
	case ET_UNEXPECTED:
		switch (es->tok) {
		case VAR:
			s = es->val->name;
			break;
		case LIT:
			s = str_val(es->val);
			break;
		case END:
			s = "end of expression";
			break;
		case BAD:
			tbuf[0] = *es->tokp;
			tbuf[1] = '\0';
			s = tbuf;
			break;
		default:
			s = opinfo[(int)es->tok].name;
		}
		warningf(true, "%s: unexpected `%s'", es->expression, s);
		break;

	case ET_BADLIT:
		warningf(true, "%s: bad number `%s'", es->expression, str);
		break;

	case ET_RECURSIVE:
		warningf(true, "%s: expression recurses on parameter `%s'",
		    es->expression, str);
		break;

	case ET_LVALUE:
		warningf(true, "%s: %s requires lvalue",
		    es->expression, str);
		break;

	case ET_RDONLY:
		warningf(true, "%s: %s applied to read only variable",
		    es->expression, str);
		break;

	default: /* keep gcc happy */
	case ET_STR:
		warningf(true, "%s: %s", es->expression, str);
		break;
	}
	unwind(LAEXPR);
}

static struct tbl *
evalexpr(Expr_state *es, enum prec prec)
{
	struct tbl *vl, *vr = NULL, *vasn;
	enum token op;
	int64_t res = 0;

	if (prec == P_PRIMARY) {
		op = es->tok;
		if (op == O_BNOT || op == O_LNOT || op == O_MINUS ||
		    op == O_PLUS) {
			token(es);
			vl = intvar(es, evalexpr(es, P_PRIMARY));
			if (op == O_BNOT)
				vl->val.i = ~vl->val.i;
			else if (op == O_LNOT)
				vl->val.i = !vl->val.i;
			else if (op == O_MINUS)
				vl->val.i = -vl->val.i;
			/* op == O_PLUS is a no-op */
		} else if (op == OPEN_PAREN) {
			token(es);
			vl = evalexpr(es, MAX_PREC);
			if (es->tok != CLOSE_PAREN)
				evalerr(es, ET_STR, "missing )");
			token(es);
		} else if (op == O_PLUSPLUS || op == O_MINUSMINUS) {
			token(es);
			vl = do_ppmm(es, op, es->val, true);
			token(es);
		} else if (op == VAR || op == LIT) {
			vl = es->val;
			token(es);
		} else {
			evalerr(es, ET_UNEXPECTED, NULL);
			/* NOTREACHED */
		}
		if (es->tok == O_PLUSPLUS || es->tok == O_MINUSMINUS) {
			vl = do_ppmm(es, es->tok, vl, false);
			token(es);
		}
		return vl;
	}
	vl = evalexpr(es, ((int) prec) - 1);
	for (op = es->tok; IS_BINOP(op) && opinfo[(int) op].prec == prec;
	    op = es->tok) {
		token(es);
		vasn = vl;
		if (op != O_ASN) /* vl may not have a value yet */
			vl = intvar(es, vl);
		if (IS_ASSIGNOP(op)) {
			assign_check(es, op, vasn);
			vr = intvar(es, evalexpr(es, P_ASSIGN));
		} else if (op != O_TERN && op != O_LAND && op != O_LOR)
			vr = intvar(es, evalexpr(es, ((int) prec) - 1));
		if ((op == O_DIV || op == O_MOD || op == O_DIVASN ||
		    op == O_MODASN) && vr->val.i == 0) {
			if (es->noassign)
				vr->val.i = 1;
			else
				evalerr(es, ET_STR, "zero divisor");
		}
		switch ((int) op) {
		case O_TIMES:
		case O_TIMESASN:
			res = vl->val.i * vr->val.i;
			break;
		case O_DIV:
		case O_DIVASN:
			if (vl->val.i == LONG_MIN && vr->val.i == -1)
				res = LONG_MIN;
			else
				res = vl->val.i / vr->val.i;
			break;
		case O_MOD:
		case O_MODASN:
			if (vl->val.i == LONG_MIN && vr->val.i == -1)
				res = 0;
			else
				res = vl->val.i % vr->val.i;
			break;
		case O_PLUS:
		case O_PLUSASN:
			res = vl->val.i + vr->val.i;
			break;
		case O_MINUS:
		case O_MINUSASN:
			res = vl->val.i - vr->val.i;
			break;
		case O_LSHIFT:
		case O_LSHIFTASN:
			res = vl->val.i << vr->val.i;
			break;
		case O_RSHIFT:
		case O_RSHIFTASN:
			res = vl->val.i >> vr->val.i;
			break;
		case O_LT:
			res = vl->val.i < vr->val.i;
			break;
		case O_LE:
			res = vl->val.i <= vr->val.i;
			break;
		case O_GT:
			res = vl->val.i > vr->val.i;
			break;
		case O_GE:
			res = vl->val.i >= vr->val.i;
			break;
		case O_EQ:
			res = vl->val.i == vr->val.i;
			break;
		case O_NE:
			res = vl->val.i != vr->val.i;
			break;
		case O_BAND:
		case O_BANDASN:
			res = vl->val.i & vr->val.i;
			break;
		case O_BXOR:
		case O_BXORASN:
			res = vl->val.i ^ vr->val.i;
			break;
		case O_BOR:
		case O_BORASN:
			res = vl->val.i | vr->val.i;
			break;
		case O_LAND:
			if (!vl->val.i)
				es->noassign++;
			vr = intvar(es, evalexpr(es, ((int) prec) - 1));
			res = vl->val.i && vr->val.i;
			if (!vl->val.i)
				es->noassign--;
			break;
		case O_LOR:
			if (vl->val.i)
				es->noassign++;
			vr = intvar(es, evalexpr(es, ((int) prec) - 1));
			res = vl->val.i || vr->val.i;
			if (vl->val.i)
				es->noassign--;
			break;
		case O_TERN:
			{
				int e = vl->val.i != 0;

				if (!e)
					es->noassign++;
				vl = evalexpr(es, MAX_PREC);
				if (!e)
					es->noassign--;
				if (es->tok != CTERN)
					evalerr(es, ET_STR, "missing :");
				token(es);
				if (e)
					es->noassign++;
				vr = evalexpr(es, P_TERN);
				if (e)
					es->noassign--;
				vl = e ? vl : vr;
			}
			break;
		case O_ASN:
			res = vr->val.i;
			break;
		case O_COMMA:
			res = vr->val.i;
			break;
		}
		if (IS_ASSIGNOP(op)) {
			vr->val.i = res;
			if (vasn->flag & INTEGER)
				setint_v(vasn, vr, es->arith);
			else
				setint(vasn, res);
			vl = vr;
		} else if (op != O_TERN)
			vl->val.i = res;
	}
	return vl;
}

static void
token(Expr_state *es)
{
	const char *cp;
	int c;
	char *tvar;

	/* skip white space */
	for (cp = es->tokp; (c = *cp), isspace((unsigned char)c); cp++)
		;
	es->tokp = cp;

	if (c == '\0')
		es->tok = END;
	else if (letter(c)) {
		for (; letnum(c); c = *cp)
			cp++;
		if (c == '[') {
			int len;

			len = array_ref_len(cp);
			if (len == 0)
				evalerr(es, ET_STR, "missing ]");
			cp += len;
		} else if (c == '(' /*)*/ ) {
			/* todo: add math functions (all take single argument):
			 * abs acos asin atan cos cosh exp int log sin sinh sqrt
			 * tan tanh
			 */
			;
		}
		if (es->noassign) {
			es->val = tempvar();
			es->val->flag |= EXPRLVALUE;
		} else {
			tvar = str_nsave(es->tokp, cp - es->tokp, ATEMP);
			es->val = global(tvar);
			afree(tvar, ATEMP);
		}
		es->tok = VAR;
	} else if (digit(c)) {
		for (; c != '_' && (letnum(c) || c == '#'); c = *cp++)
			;
		tvar = str_nsave(es->tokp, --cp - es->tokp, ATEMP);
		es->val = tempvar();
		es->val->flag &= ~INTEGER;
		es->val->type = 0;
		es->val->val.s = tvar;
		if (setint_v(es->val, es->val, es->arith) == NULL)
			evalerr(es, ET_BADLIT, tvar);
		afree(tvar, ATEMP);
		es->tok = LIT;
	} else {
		int i, n0;

		for (i = 0; (n0 = opinfo[i].name[0]); i++)
			if (c == n0 &&
			    strncmp(cp, opinfo[i].name, opinfo[i].len) == 0) {
				es->tok = (enum token) i;
				cp += opinfo[i].len;
				break;
			}
		if (!n0)
			es->tok = BAD;
	}
	es->tokp = cp;
}

/* Do a ++ or -- operation */
static struct tbl *
do_ppmm(Expr_state *es, enum token op, struct tbl *vasn, bool is_prefix)
{
	struct tbl *vl;
	int oval;

	assign_check(es, op, vasn);

	vl = intvar(es, vasn);
	oval = op == O_PLUSPLUS ? vl->val.i++ : vl->val.i--;
	if (vasn->flag & INTEGER)
		setint_v(vasn, vl, es->arith);
	else
		setint(vasn, vl->val.i);
	if (!is_prefix)		/* undo the inc/dec */
		vl->val.i = oval;

	return vl;
}

static void
assign_check(Expr_state *es, enum token op, struct tbl *vasn)
{
	if (es->tok == END || vasn == NULL ||
	    (vasn->name[0] == '\0' && !(vasn->flag & EXPRLVALUE)))
		evalerr(es, ET_LVALUE, opinfo[(int) op].name);
	else if (vasn->flag & RDONLY)
		evalerr(es, ET_RDONLY, opinfo[(int) op].name);
}

static struct tbl *
tempvar(void)
{
	struct tbl *vp;

	vp = alloc(sizeof(struct tbl), ATEMP);
	vp->flag = ISSET|INTEGER;
	vp->type = 0;
	vp->areap = ATEMP;
	vp->val.i = 0;
	vp->name[0] = '\0';
	return vp;
}

/* cast (string) variable to temporary integer variable */
static struct tbl *
intvar(Expr_state *es, struct tbl *vp)
{
	struct tbl *vq;

	/* try to avoid replacing a temp var with another temp var */
	if (vp->name[0] == '\0' &&
	    (vp->flag & (ISSET|INTEGER|EXPRLVALUE)) == (ISSET|INTEGER))
		return vp;

	vq = tempvar();
	if (setint_v(vq, vp, es->arith) == NULL) {
		if (vp->flag & EXPRINEVAL)
			evalerr(es, ET_RECURSIVE, vp->name);
		es->evaling = vp;
		vp->flag |= EXPRINEVAL;
		disable_subst++;
		v_evaluate(vq, str_val(vp), KSH_UNWIND_ERROR, es->arith);
		disable_subst--;
		vp->flag &= ~EXPRINEVAL;
		es->evaling = NULL;
	}
	return vq;
}

/*
 * parser.c - ktap parser
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"

/* maximum number of local variables per function (must be smaller
   than 250, due to the bytecode format) */
#define MAXVARS		200

#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)


/*
 * nodes for block list (list of active blocks)
 */
typedef struct ktap_blockcnt {
	struct ktap_blockcnt *previous;  /* chain */
	short firstlabel;  /* index of first label in this block */
	short firstgoto;  /* index of first pending goto in this block */
	u8 nactvar;  /* # active locals outside the block */
	u8 upval;  /* true if some variable in the block is an upvalue */
	 u8 isloop;  /* true if `block' is a loop */
} ktap_blockcnt;

/*
 * prototypes for recursive non-terminal functions
 */
static void statement (ktap_lexstate *ls);
static void expr (ktap_lexstate *ls, ktap_expdesc *v);

static void anchor_token(ktap_lexstate *ls)
{
	/* last token from outer function must be EOS */
	ktap_assert((int)(ls->fs != NULL) || ls->t.token == TK_EOS);
	if (ls->t.token == TK_NAME || ls->t.token == TK_STRING) {
		ktap_string *ts = ls->t.seminfo.ts;
		lex_newstring(ls, getstr(ts), ts->tsv.len);
	}
}

/* semantic error */
static void semerror(ktap_lexstate *ls, const char *msg)
{
	ls->t.token = 0;  /* remove 'near to' from final message */
	lex_syntaxerror(ls, msg);
}

static void error_expected(ktap_lexstate *ls, int token)
{
	lex_syntaxerror(ls,
		ktapc_sprintf("%s expected", lex_token2str(ls, token)));
}

static void errorlimit(ktap_funcstate *fs, int limit, const char *what)
{
	const char *msg;
	int line = fs->f->linedefined;
	const char *where = (line == 0) ? "main function"
				: ktapc_sprintf("function at line %d", line);

	msg = ktapc_sprintf("too many %s (limit is %d) in %s",
				what, limit, where);
	lex_syntaxerror(fs->ls, msg);
}

static void checklimit(ktap_funcstate *fs, int v, int l, const char *what)
{
	if (v > l)
		errorlimit(fs, l, what);
}

static int testnext(ktap_lexstate *ls, int c)
{
	if (ls->t.token == c) {
		lex_next(ls);
		return 1;
	}
	else
		return 0;
}

static void check(ktap_lexstate *ls, int c)
{
	if (ls->t.token != c)
		error_expected(ls, c);
}

static void checknext(ktap_lexstate *ls, int c)
{
	check(ls, c);
	lex_next(ls);
}

#define check_condition(ls,c,msg)	{ if (!(c)) lex_syntaxerror(ls, msg); }

static void check_match(ktap_lexstate *ls, int what, int who, int where)
{
	if (!testnext(ls, what)) {
		if (where == ls->linenumber)
			error_expected(ls, what);
		else {
			lex_syntaxerror(ls, ktapc_sprintf(
					"%s expected (to close %s at line %d)",
					lex_token2str(ls, what),
					lex_token2str(ls, who), where));
		}
	}
}

static ktap_string *str_checkname(ktap_lexstate *ls)
{
	ktap_string *ts;

	check(ls, TK_NAME);
	ts = ls->t.seminfo.ts;
	lex_next(ls);
	return ts;
}

static void init_exp(ktap_expdesc *e, expkind k, int i)
{
	e->f = e->t = NO_JUMP;
	e->k = k;
	e->u.info = i;
}

static void codestring(ktap_lexstate *ls, ktap_expdesc *e, ktap_string *s)
{
	init_exp(e, VK, codegen_stringK(ls->fs, s));
}

static void checkname(ktap_lexstate *ls, ktap_expdesc *e)
{
	codestring(ls, e, str_checkname(ls));
}

static int registerlocalvar(ktap_lexstate *ls, ktap_string *varname)
{
	ktap_funcstate *fs = ls->fs;
	ktap_proto *f = fs->f;
	int oldsize = f->sizelocvars;

	ktapc_growvector(f->locvars, fs->nlocvars, f->sizelocvars,
			 ktap_locvar, SHRT_MAX, "local variables");

	while (oldsize < f->sizelocvars)
		f->locvars[oldsize++].varname = NULL;

	f->locvars[fs->nlocvars].varname = varname;
	return fs->nlocvars++;
}

static void new_localvar(ktap_lexstate *ls, ktap_string *name)
{
	ktap_funcstate *fs = ls->fs;
	ktap_dyndata *dyd = ls->dyd;
	int reg = registerlocalvar(ls, name);

	checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
		   MAXVARS, "local variables");
	ktapc_growvector(dyd->actvar.arr, dyd->actvar.n + 1,
			 dyd->actvar.size, ktap_vardesc, MAX_INT, "local variables");
	dyd->actvar.arr[dyd->actvar.n++].idx = (short)reg;
}

static void new_localvarliteral_(ktap_lexstate *ls, const char *name, size_t sz)
{
	new_localvar(ls, lex_newstring(ls, name, sz));
}

#define new_localvarliteral(ls,v) \
	new_localvarliteral_(ls, "" v, (sizeof(v)/sizeof(char))-1)

static ktap_locvar *getlocvar(ktap_funcstate *fs, int i)
{
	int idx = fs->ls->dyd->actvar.arr[fs->firstlocal + i].idx;

	ktap_assert(idx < fs->nlocvars);
	return &fs->f->locvars[idx];
}

static void adjustlocalvars(ktap_lexstate *ls, int nvars)
{
	ktap_funcstate *fs = ls->fs;

	fs->nactvar = (u8)(fs->nactvar + nvars);
	for (; nvars; nvars--) {
		getlocvar(fs, fs->nactvar - nvars)->startpc = fs->pc;
	}
}

static void removevars(ktap_funcstate *fs, int tolevel)
{
	fs->ls->dyd->actvar.n -= (fs->nactvar - tolevel);

	while (fs->nactvar > tolevel)
		getlocvar(fs, --fs->nactvar)->endpc = fs->pc;
}

static int searchupvalue(ktap_funcstate *fs, ktap_string *name)
{
	int i;
	ktap_upvaldesc *up = fs->f->upvalues;

	for (i = 0; i < fs->nups; i++) {
		if (ktapc_ts_eqstr(up[i].name, name))
			return i;
	}
	return -1;  /* not found */
}

static int newupvalue(ktap_funcstate *fs, ktap_string *name, ktap_expdesc *v)
{
	ktap_proto *f = fs->f;
	int oldsize = f->sizeupvalues;

	checklimit(fs, fs->nups + 1, MAXUPVAL, "upvalues");
	ktapc_growvector(f->upvalues, fs->nups, f->sizeupvalues,
			 ktap_upvaldesc, MAXUPVAL, "upvalues");

	while (oldsize < f->sizeupvalues)
		f->upvalues[oldsize++].name = NULL;
	f->upvalues[(int)fs->nups].instack = (v->k == VLOCAL);
	f->upvalues[(int)fs->nups].idx = (u8)(v->u.info);
	f->upvalues[(int)fs->nups].name = name;
	return fs->nups++;
}

static int searchvar(ktap_funcstate *fs, ktap_string *n)
{
	int i;

	for (i = fs->nactvar-1; i >= 0; i--) {
		if (ktapc_ts_eqstr(n, getlocvar(fs, i)->varname))
			return i;
	}
	return -1;  /* not found */
}

/*
 * Mark block where variable at given level was defined
 * (to emit close instructions later).
 */
static void markupval(ktap_funcstate *fs, int level)
{
	ktap_blockcnt *bl = fs->bl;

	while (bl->nactvar > level)
		bl = bl->previous;
	bl->upval = 1;
}

/*
 * Find variable with given name 'n'. If it is an upvalue, add this
 * upvalue into all intermediate functions.
 */
static int singlevaraux(ktap_funcstate *fs, ktap_string *n, ktap_expdesc *var, int base)
{
	if (fs == NULL)  /* no more levels? */
		return VVOID;  /* default is global */
	else {
		int v = searchvar(fs, n);  /* look up locals at current level */
		if (v >= 0) {  /* found? */
			init_exp(var, VLOCAL, v);  /* variable is local */
			if (!base)
				markupval(fs, v);  /* local will be used as an upval */
			return VLOCAL;
		} else {  /* not found as local at current level; try upvalues */
			int idx = searchupvalue(fs, n);  /* try existing upvalues */
			if (idx < 0) {  /* not found? */
				if (singlevaraux(fs->prev, n, var, 0) == VVOID) /* try upper levels */
					return VVOID;  /* not found; is a global */
				/* else was LOCAL or UPVAL */
				idx  = newupvalue(fs, n, var);  /* will be a new upvalue */
			}
			init_exp(var, VUPVAL, idx);
			return VUPVAL;
		}
	}
}

static void singlevar(ktap_lexstate *ls, ktap_expdesc *var)
{
	ktap_string *varname = str_checkname(ls);
	ktap_funcstate *fs = ls->fs;

	if (singlevaraux(fs, varname, var, 1) == VVOID) {  /* global name? */
		ktap_expdesc key;
		singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
		ktap_assert(var->k == VLOCAL || var->k == VUPVAL);
		codestring(ls, &key, varname);  /* key is variable name */
		codegen_indexed(fs, var, &key);  /* env[varname] */
	}
}

static void adjust_assign(ktap_lexstate *ls, int nvars, int nexps, ktap_expdesc *e)
{
	ktap_funcstate *fs = ls->fs;
	int extra = nvars - nexps;

	if (hasmultret(e->k)) {
		extra++;  /* includes call itself */
		if (extra < 0)
			extra = 0;
		codegen_setreturns(fs, e, extra);  /* last exp. provides the difference */
		if (extra > 1)
			codegen_reserveregs(fs, extra-1);
	} else {
		if (e->k != VVOID)
			codegen_exp2nextreg(fs, e);  /* close last expression */
		if (extra > 0) {
			int reg = fs->freereg;

			codegen_reserveregs(fs, extra);
			codegen_nil(fs, reg, extra);
		}
	}
}

static void enterlevel(ktap_lexstate *ls)
{
	++ls->nCcalls;
	checklimit(ls->fs, ls->nCcalls, KTAP_MAXCCALLS, "C levels");
}

static void closegoto(ktap_lexstate *ls, int g, ktap_labeldesc *label)
{
	int i;
	ktap_funcstate *fs = ls->fs;
	ktap_labellist *gl = &ls->dyd->gt;
	ktap_labeldesc *gt = &gl->arr[g];

	ktap_assert(ktapc_ts_eqstr(gt->name, label->name));
	if (gt->nactvar < label->nactvar) {
		ktap_string *vname = getlocvar(fs, gt->nactvar)->varname;
		const char *msg = ktapc_sprintf(
			"<goto %s> at line %d jumps into the scope of local " KTAP_QS,
			getstr(gt->name), gt->line, getstr(vname));
		semerror(ls, msg);
	}

	codegen_patchlist(fs, gt->pc, label->pc);
	/* remove goto from pending list */
	for (i = g; i < gl->n - 1; i++)
		gl->arr[i] = gl->arr[i + 1];
	gl->n--;
}

/*
 * try to close a goto with existing labels; this solves backward jumps
 */
static int findlabel(ktap_lexstate *ls, int g)
{
	int i;
	ktap_blockcnt *bl = ls->fs->bl;
	ktap_dyndata *dyd = ls->dyd;
	ktap_labeldesc *gt = &dyd->gt.arr[g];

	/* check labels in current block for a match */
	for (i = bl->firstlabel; i < dyd->label.n; i++) {
		ktap_labeldesc *lb = &dyd->label.arr[i];
		if (ktapc_ts_eqstr(lb->name, gt->name)) {  /* correct label? */
			if (gt->nactvar > lb->nactvar &&
				(bl->upval || dyd->label.n > bl->firstlabel))
				codegen_patchclose(ls->fs, gt->pc, lb->nactvar);
			closegoto(ls, g, lb);  /* close it */
			return 1;
		}
	}
	return 0;  /* label not found; cannot close goto */
}

static int newlabelentry(ktap_lexstate *ls, ktap_labellist *l, ktap_string *name,
			 int line, int pc)
{
	int n = l->n;

	ktapc_growvector(l->arr, n, l->size,
			 ktap_labeldesc, SHRT_MAX, "labels/gotos");
	l->arr[n].name = name;
	l->arr[n].line = line;
	l->arr[n].nactvar = ls->fs->nactvar;
	l->arr[n].pc = pc;
	l->n++;
	return n;
}


/*
 * check whether new label 'lb' matches any pending gotos in current
 * block; solves forward jumps
 */
static void findgotos(ktap_lexstate *ls, ktap_labeldesc *lb)
{
	ktap_labellist *gl = &ls->dyd->gt;
	int i = ls->fs->bl->firstgoto;

	while (i < gl->n) {
		if (ktapc_ts_eqstr(gl->arr[i].name, lb->name))
			closegoto(ls, i, lb);
		else
			i++;
	}
}

/*
 * "export" pending gotos to outer level, to check them against
 * outer labels; if the block being exited has upvalues, and
 * the goto exits the scope of any variable (which can be the
 * upvalue), close those variables being exited.
 */
static void movegotosout(ktap_funcstate *fs, ktap_blockcnt *bl)
{
	int i = bl->firstgoto;
	ktap_labellist *gl = &fs->ls->dyd->gt;

	/* correct pending gotos to current block and try to close it
		with visible labels */
	while (i < gl->n) {
		ktap_labeldesc *gt = &gl->arr[i];

		if (gt->nactvar > bl->nactvar) {
			if (bl->upval)
				codegen_patchclose(fs, gt->pc, bl->nactvar);
			gt->nactvar = bl->nactvar;
		}
		if (!findlabel(fs->ls, i))
			i++;  /* move to next one */
	}
}

static void enterblock(ktap_funcstate *fs, ktap_blockcnt *bl, u8 isloop)
{
	bl->isloop = isloop;
	bl->nactvar = fs->nactvar;
	bl->firstlabel = fs->ls->dyd->label.n;
	bl->firstgoto = fs->ls->dyd->gt.n;
	bl->upval = 0;
	bl->previous = fs->bl;
	fs->bl = bl;
	ktap_assert(fs->freereg == fs->nactvar);
}


/*
 * create a label named "break" to resolve break statements
 */
static void breaklabel(ktap_lexstate *ls)
{
	ktap_string *n = ktapc_ts_new("break");
	int l = newlabelentry(ls, &ls->dyd->label, n, 0, ls->fs->pc);

	findgotos(ls, &ls->dyd->label.arr[l]);
}

/*
 * generates an error for an undefined 'goto'; choose appropriate
 * message when label name is a reserved word (which can only be 'break')
 */
static void undefgoto(ktap_lexstate *ls, ktap_labeldesc *gt)
{
	const char *msg = isreserved(gt->name)
			? "<%s> at line %d not inside a loop"
			: "no visible label " KTAP_QS " for <goto> at line %d";

	msg = ktapc_sprintf(msg, getstr(gt->name), gt->line);
	semerror(ls, msg);
}

static void leaveblock(ktap_funcstate *fs)
{
	ktap_blockcnt *bl = fs->bl;
	ktap_lexstate *ls = fs->ls;
	if (bl->previous && bl->upval) {
		/* create a 'jump to here' to close upvalues */
		int j = codegen_jump(fs);

		codegen_patchclose(fs, j, bl->nactvar);
		codegen_patchtohere(fs, j);
	}

	if (bl->isloop)
		breaklabel(ls);  /* close pending breaks */

	fs->bl = bl->previous;
	removevars(fs, bl->nactvar);
	ktap_assert(bl->nactvar == fs->nactvar);
	fs->freereg = fs->nactvar;  /* free registers */
	ls->dyd->label.n = bl->firstlabel;  /* remove local labels */
	if (bl->previous)  /* inner block? */
		movegotosout(fs, bl);  /* update pending gotos to outer block */
	else if (bl->firstgoto < ls->dyd->gt.n)  /* pending gotos in outer block? */
		undefgoto(ls, &ls->dyd->gt.arr[bl->firstgoto]);  /* error */
}

/*
 * adds a new prototype into list of prototypes
 */
static ktap_proto *addprototype(ktap_lexstate *ls)
{
	ktap_proto *clp;
	ktap_funcstate *fs = ls->fs;
	ktap_proto *f = fs->f;  /* prototype of current function */

	if (fs->np >= f->sizep) {
		int oldsize = f->sizep;
		ktapc_growvector(f->p, fs->np, f->sizep, ktap_proto *, MAXARG_Bx, "functions");
		while (oldsize < f->sizep)
			f->p[oldsize++] = NULL;
	}
	f->p[fs->np++] = clp = ktapc_newproto();
	return clp;
}

/*
 * codes instruction to create new closure in parent function
 */
static void codeclosure(ktap_lexstate *ls, ktap_expdesc *v)
{
	ktap_funcstate *fs = ls->fs->prev;
	init_exp(v, VRELOCABLE, codegen_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
	codegen_exp2nextreg(fs, v);  /* fix it at stack top (for GC) */
}

static void open_func(ktap_lexstate *ls, ktap_funcstate *fs, ktap_blockcnt *bl)
{
	ktap_proto *f;

	fs->prev = ls->fs;  /* linked list of funcstates */
	fs->ls = ls;
	ls->fs = fs;
	fs->pc = 0;
	fs->lasttarget = 0;
	fs->jpc = NO_JUMP;
	fs->freereg = 0;
	fs->nk = 0;
	fs->np = 0;
	fs->nups = 0;
	fs->nlocvars = 0;
	fs->nactvar = 0;
	fs->firstlocal = ls->dyd->actvar.n;
	fs->bl = NULL;
	f = fs->f;
	f->source = ls->source;
	f->maxstacksize = 2;  /* registers 0/1 are always valid */
	fs->h = ktapc_table_new();
	//table_resize(NULL, fs->h, 32, 32);
	enterblock(fs, bl, 0);
}

static void close_func(ktap_lexstate *ls)
{
	ktap_funcstate *fs = ls->fs;
	ktap_proto *f = fs->f;

	codegen_ret(fs, 0, 0);  /* final return */
	leaveblock(fs);
	ktapc_reallocvector(f->code, f->sizecode, fs->pc, ktap_instruction);
	f->sizecode = fs->pc;
	ktapc_reallocvector(f->lineinfo, f->sizelineinfo, fs->pc, int);
	f->sizelineinfo = fs->pc;
	ktapc_reallocvector(f->k, f->sizek, fs->nk, ktap_value);
	f->sizek = fs->nk;
	ktapc_reallocvector(f->p, f->sizep, fs->np, ktap_proto *);
	f->sizep = fs->np;
	ktapc_reallocvector(f->locvars, f->sizelocvars, fs->nlocvars, ktap_locvar);
	f->sizelocvars = fs->nlocvars;
	ktapc_reallocvector(f->upvalues, f->sizeupvalues, fs->nups, ktap_upvaldesc);
	f->sizeupvalues = fs->nups;
	ktap_assert((int)(fs->bl == NULL));
	ls->fs = fs->prev;
	/* last token read was anchored in defunct function; must re-anchor it */
	anchor_token(ls);
}

/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/

/*
 * check whether current token is in the follow set of a block.
 * 'until' closes syntactical blocks, but do not close scope,
 * so it handled in separate.
 */
static int block_follow(ktap_lexstate *ls, int withuntil)
{
	switch (ls->t.token) {
	case TK_ELSE: case TK_ELSEIF:
	case TK_END: case TK_EOS:
		return 1;
	case TK_UNTIL:
		return withuntil;
	case '}':
		return 1;
	default:
		return 0;
	}
}

static void statlist(ktap_lexstate *ls)
{
	/* statlist -> { stat [`;'] } */
	while (!block_follow(ls, 1)) {
		if (ls->t.token == TK_RETURN) {
			statement(ls);
			return;  /* 'return' must be last statement */
		}
		statement(ls);
	}
}

static void fieldsel(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* fieldsel -> ['.' | ':'] NAME */
	ktap_funcstate *fs = ls->fs;
	ktap_expdesc key;

	codegen_exp2anyregup(fs, v);
	lex_next(ls);  /* skip the dot or colon */
	checkname(ls, &key);
	codegen_indexed(fs, v, &key);
}

static void yindex(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* index -> '[' expr ']' */
	lex_next(ls);  /* skip the '[' */
	expr(ls, v);
	codegen_exp2val(ls->fs, v);
	checknext(ls, ']');
}

/*
 * {======================================================================
 * Rules for Constructors
 * =======================================================================
 */
struct ConsControl {
	ktap_expdesc v;  /* last list item read */
	ktap_expdesc *t;  /* table descriptor */
	int nh;  /* total number of `record' elements */
	int na;  /* total number of array elements */
	int tostore;  /* number of array elements pending to be stored */
};

static void recfield(ktap_lexstate *ls, struct ConsControl *cc)
{
	/* recfield -> (NAME | `['exp1`]') = exp1 */
	ktap_funcstate *fs = ls->fs;
	int reg = ls->fs->freereg;
	ktap_expdesc key, val;
	int rkkey;

	if (ls->t.token == TK_NAME) {
		checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
		checkname(ls, &key);
	} else  /* ls->t.token == '[' */
		yindex(ls, &key);

	cc->nh++;
  	checknext(ls, '=');
	rkkey = codegen_exp2RK(fs, &key);
	expr(ls, &val);
	codegen_codeABC(fs, OP_SETTABLE, cc->t->u.info, rkkey, codegen_exp2RK(fs, &val));
	fs->freereg = reg;  /* free registers */
}

static void closelistfield(ktap_funcstate *fs, struct ConsControl *cc)
{
	if (cc->v.k == VVOID)
		return;  /* there is no list item */
  	codegen_exp2nextreg(fs, &cc->v);
	cc->v.k = VVOID;
	if (cc->tostore == LFIELDS_PER_FLUSH) {
		codegen_setlist(fs, cc->t->u.info, cc->na, cc->tostore);  /* flush */
		cc->tostore = 0;  /* no more items pending */
	}
}

static void lastlistfield(ktap_funcstate *fs, struct ConsControl *cc)
{
	if (cc->tostore == 0)
		return;

	if (hasmultret(cc->v.k)) {
		codegen_setmultret(fs, &cc->v);
		codegen_setlist(fs, cc->t->u.info, cc->na, KTAP_MULTRET);
		cc->na--;  /* do not count last expression (unknown number of elements) */
	} else {
		if (cc->v.k != VVOID)
			codegen_exp2nextreg(fs, &cc->v);
		codegen_setlist(fs, cc->t->u.info, cc->na, cc->tostore);
	}
}

static void listfield(ktap_lexstate *ls, struct ConsControl *cc)
{
	/* listfield -> exp */
	expr(ls, &cc->v);
	checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
	cc->na++;
	cc->tostore++;
}

static void field(ktap_lexstate *ls, struct ConsControl *cc)
{
	/* field -> listfield | recfield */
	switch(ls->t.token) {
	case TK_NAME: {  /* may be 'listfield' or 'recfield' */
		if (lex_lookahead(ls) != '=')  /* expression? */
			listfield(ls, cc);
		else
			recfield(ls, cc);
		break;
	}
	case '[': {
		recfield(ls, cc);
		break;
	}
	default:
		listfield(ls, cc);
		break;
	}
}

static void constructor(ktap_lexstate *ls, ktap_expdesc *t)
{
	/* constructor -> '{' [ field { sep field } [sep] ] '}'
		sep -> ',' | ';' */
	ktap_funcstate *fs = ls->fs;
	int line = ls->linenumber;
	int pc = codegen_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
	struct ConsControl cc;

	cc.na = cc.nh = cc.tostore = 0;
	cc.t = t;
	init_exp(t, VRELOCABLE, pc);
	init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
	codegen_exp2nextreg(ls->fs, t);  /* fix it at stack top */
	checknext(ls, '{');
	do {
		ktap_assert(cc.v.k == VVOID || cc.tostore > 0);
		if (ls->t.token == '}')
			break;
		closelistfield(fs, &cc);
		field(ls, &cc);
	} while (testnext(ls, ',') || testnext(ls, ';'));
	check_match(ls, '}', '{', line);
	lastlistfield(fs, &cc);
	SETARG_B(fs->f->code[pc], ktapc_int2fb(cc.na)); /* set initial array size */
	SETARG_C(fs->f->code[pc], ktapc_int2fb(cc.nh));  /* set initial table size */
}

/* }====================================================================== */

static void parlist(ktap_lexstate *ls)
{
	/* parlist -> [ param { `,' param } ] */
	ktap_funcstate *fs = ls->fs;
	ktap_proto *f = fs->f;
	int nparams = 0;
	f->is_vararg = 0;

	if (ls->t.token != ')') {  /* is `parlist' not empty? */
		do {
			switch (ls->t.token) {
			case TK_NAME: {  /* param -> NAME */
				new_localvar(ls, str_checkname(ls));
				nparams++;
				break;
			}
			case TK_DOTS: {  /* param -> `...' */
				lex_next(ls);
				f->is_vararg = 1;
				break;
			}
			default:
				lex_syntaxerror(ls, "<name> or " KTAP_QL("...") " expected");
			}
		} while (!f->is_vararg && testnext(ls, ','));
	}
	adjustlocalvars(ls, nparams);
	f->numparams = (u8)(fs->nactvar);
	codegen_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
}

static void body(ktap_lexstate *ls, ktap_expdesc *e, int ismethod, int line)
{
	/* body ->  `(' parlist `)' block END */
	ktap_funcstate new_fs;
	ktap_blockcnt bl;

	new_fs.f = addprototype(ls);
	new_fs.f->linedefined = line;
	open_func(ls, &new_fs, &bl);
	checknext(ls, '(');
	if (ismethod) {
		new_localvarliteral(ls, "self");  /* create 'self' parameter */
		adjustlocalvars(ls, 1);
	}
	parlist(ls);
	checknext(ls, ')');
	checknext(ls, '{');
	statlist(ls);
	new_fs.f->lastlinedefined = ls->linenumber;
	checknext(ls, '}');
	//check_match(ls, TK_END, TK_FUNCTION, line);
	codeclosure(ls, e);
	close_func(ls);
}

static void func_body_no_args(ktap_lexstate *ls, ktap_expdesc *e, int line)
{
	/* body ->  `(' parlist `)' block END */
	ktap_funcstate new_fs;
	ktap_blockcnt bl;

	new_fs.f = addprototype(ls);
	new_fs.f->linedefined = line;
	open_func(ls, &new_fs, &bl);
	checknext(ls, '{');
	statlist(ls);
	new_fs.f->lastlinedefined = ls->linenumber;
	checknext(ls, '}');
	//check_match(ls, TK_END, TK_FUNCTION, line);
	codeclosure(ls, e);
	close_func(ls);
}

static int explist(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* explist -> expr { `,' expr } */
	int n = 1;  /* at least one expression */

	expr(ls, v);
	while (testnext(ls, ',')) {
		codegen_exp2nextreg(ls->fs, v);
		expr(ls, v);
		n++;
	}
	return n;
}

static void funcargs(ktap_lexstate *ls, ktap_expdesc *f, int line)
{
	ktap_funcstate *fs = ls->fs;
	ktap_expdesc args;
	int base, nparams;

	switch (ls->t.token) {
	case '(': {  /* funcargs -> `(' [ explist ] `)' */
		lex_next(ls);
		if (ls->t.token == ')')  /* arg list is empty? */
			args.k = VVOID;
		else {
			explist(ls, &args);
			codegen_setmultret(fs, &args);
		}
		check_match(ls, ')', '(', line);
		break;
	}
	case '{': {  /* funcargs -> constructor */
		constructor(ls, &args);
		break;
	}
	case TK_STRING: {  /* funcargs -> STRING */
		codestring(ls, &args, ls->t.seminfo.ts);
		lex_next(ls);  /* must use `seminfo' before `next' */
		break;
	}
	default: {
		lex_syntaxerror(ls, "function arguments expected");
	}
	}
	ktap_assert(f->k == VNONRELOC);
	base = f->u.info;  /* base register for call */
	if (hasmultret(args.k))
		nparams = KTAP_MULTRET;  /* open call */
	else {
		if (args.k != VVOID)
			codegen_exp2nextreg(fs, &args);  /* close last argument */
		nparams = fs->freereg - (base+1);
	}
	init_exp(f, VCALL, codegen_codeABC(fs, OP_CALL, base, nparams+1, 2));
	codegen_fixline(fs, line);
	fs->freereg = base+1;  /* call remove function and arguments and leaves
				(unless changed) one result */
}

/*
 * {======================================================================
 * Expression parsing
 * =======================================================================
 */
static void primaryexp(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* primaryexp -> NAME | '(' expr ')' */
	switch (ls->t.token) {
	case '(': {
		int line = ls->linenumber;

		lex_next(ls);
		expr(ls, v);
		check_match(ls, ')', '(', line);
		codegen_dischargevars(ls->fs, v);
		return;
	}
	case TK_NAME:
		singlevar(ls, v);
		return;
	default:
		lex_syntaxerror(ls, "unexpected symbol");
	}
}

static void suffixedexp(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* suffixedexp ->
		primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
	ktap_funcstate *fs = ls->fs;
	int line = ls->linenumber;

	primaryexp(ls, v);
	for (;;) {
		switch (ls->t.token) {
		case '.': {  /* fieldsel */
			fieldsel(ls, v);
			break;
		}
		case '[': {  /* `[' exp1 `]' */
			ktap_expdesc key;
			codegen_exp2anyregup(fs, v);
			yindex(ls, &key);
			codegen_indexed(fs, v, &key);
			break;
		}
		case ':': {  /* `:' NAME funcargs */
			ktap_expdesc key;
			lex_next(ls);
			checkname(ls, &key);
			codegen_self(fs, v, &key);
			funcargs(ls, v, line);
			break;
		}
		case '(': case TK_STRING: case '{': {  /* funcargs */
			codegen_exp2nextreg(fs, v);
			funcargs(ls, v, line);
			break;
		}
		default:
			return;
		}
	}
}

static void simpleexp(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* simpleexp -> NUMBER | STRING | NIL | TRUE | FALSE | ... |
		constructor | FUNCTION body | suffixedexp */
	switch (ls->t.token) {
	case TK_NUMBER: {
		init_exp(v, VKNUM, 0);
		v->u.nval = ls->t.seminfo.r;
		break;
	}
	case TK_STRING: {
		codestring(ls, v, ls->t.seminfo.ts);
		break;
	}
	case TK_NIL: {
		init_exp(v, VNIL, 0);
		break;
	}
	case TK_TRUE: {
		init_exp(v, VTRUE, 0);
		break;
	}
	case TK_FALSE: {
		init_exp(v, VFALSE, 0);
		break;
	}
	case TK_DOTS: {  /* vararg */
		ktap_funcstate *fs = ls->fs;
		check_condition(ls, fs->f->is_vararg,
                      "cannot use " KTAP_QL("...") " outside a vararg function");
		init_exp(v, VVARARG, codegen_codeABC(fs, OP_VARARG, 0, 1, 0));
		break;
	}
	case '{': {  /* constructor */
		constructor(ls, v);
		return;
	}
	case TK_FUNCTION: {
		lex_next(ls);
		body(ls, v, 0, ls->linenumber);
		return;
	}
	case TK_ARGEVENT:
		init_exp(v, VEVENT, 0);
		break;

	case TK_ARGNAME:
		init_exp(v, VEVENTNAME, 0);
		break;
	case TK_ARG1:
	case TK_ARG2:
	case TK_ARG3:
	case TK_ARG4:
	case TK_ARG5:
	case TK_ARG6:
	case TK_ARG7:
	case TK_ARG8:
	case TK_ARG9:
		init_exp(v, VEVENTARG, ls->t.token - TK_ARG1 + 1);
		break;
	default: {
		suffixedexp(ls, v);
		return;
	}
	}
	lex_next(ls);
}

static UnOpr getunopr(int op)
{
	switch (op) {
	case TK_NOT: return OPR_NOT;
	case '-': return OPR_MINUS;
	case '#': return OPR_LEN;
	default: return OPR_NOUNOPR;
	}
}

static BinOpr getbinopr(int op)
{
	switch (op) {
	case '+': return OPR_ADD;
	case '-': return OPR_SUB;
	case '*': return OPR_MUL;
	case '/': return OPR_DIV;
	case '%': return OPR_MOD;
	case '^': return OPR_POW;
	case TK_CONCAT: return OPR_CONCAT;
	case TK_NE: return OPR_NE;
	case TK_EQ: return OPR_EQ;
	case '<': return OPR_LT;
	case TK_LE: return OPR_LE;
	case '>': return OPR_GT;
	case TK_GE: return OPR_GE;
	case TK_AND: return OPR_AND;
	case TK_OR: return OPR_OR;
	default: return OPR_NOBINOPR;
	}
}

static const struct {
	u8 left;  /* left priority for each binary operator */
	u8 right; /* right priority */
} priority[] = {  /* ORDER OPR */
	{6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `*' `/' `%' */
	{10, 9}, {5, 4},                 /* ^, .. (right associative) */
	{3, 3}, {3, 3}, {3, 3},          /* ==, <, <= */
	{3, 3}, {3, 3}, {3, 3},          /* !=, >, >= */
	{2, 2}, {1, 1}                   /* and, or */
};

#define UNARY_PRIORITY	8  /* priority for unary operators */

#define leavelevel(ls)	(ls->nCcalls--)

/*
 * subexpr -> (simpleexp | unop subexpr) { binop subexpr }
 * where `binop' is any binary operator with a priority higher than `limit'
 */
static BinOpr subexpr(ktap_lexstate *ls, ktap_expdesc *v, int limit)
{
	BinOpr op;
	UnOpr uop;

	enterlevel(ls);
	uop = getunopr(ls->t.token);
	if (uop != OPR_NOUNOPR) {
		int line = ls->linenumber;

		lex_next(ls);
		subexpr(ls, v, UNARY_PRIORITY);
		codegen_prefix(ls->fs, uop, v, line);
	} else
		simpleexp(ls, v);

	/* expand while operators have priorities higher than `limit' */
	op = getbinopr(ls->t.token);
	while (op != OPR_NOBINOPR && priority[op].left > limit) {
		ktap_expdesc v2;
		BinOpr nextop;
		int line = ls->linenumber;

		lex_next(ls);
		codegen_infix(ls->fs, op, v);
		/* read sub-expression with higher priority */
		nextop = subexpr(ls, &v2, priority[op].right);
		codegen_posfix(ls->fs, op, v, &v2, line);
		op = nextop;
	}
	leavelevel(ls);
	return op;  /* return first untreated operator */
}

static void expr(ktap_lexstate *ls, ktap_expdesc *v)
{
	subexpr(ls, v, 0);
}

/* }==================================================================== */

/*
 * {======================================================================
 * Rules for Statements
 * =======================================================================
 */
static void block(ktap_lexstate *ls)
{
	/* block -> statlist */
	ktap_funcstate *fs = ls->fs;
	ktap_blockcnt bl;

	enterblock(fs, &bl, 0);
	statlist(ls);
	leaveblock(fs);
}

/*
 * structure to chain all variables in the left-hand side of an
 * assignment
 */
struct LHS_assign {
	struct LHS_assign *prev;
	ktap_expdesc v;  /* variable (global, local, upvalue, or indexed) */
};

/*
 * check whether, in an assignment to an upvalue/local variable, the
 * upvalue/local variable is begin used in a previous assignment to a
 * table. If so, save original upvalue/local value in a safe place and
 * use this safe copy in the previous assignment.
 */
static void check_conflict(ktap_lexstate *ls, struct LHS_assign *lh, ktap_expdesc *v)
{
	ktap_funcstate *fs = ls->fs;
	int extra = fs->freereg;  /* eventual position to save local variable */
	int conflict = 0;

	for (; lh; lh = lh->prev) {  /* check all previous assignments */
		if (lh->v.k == VINDEXED) {  /* assigning to a table? */
			/* table is the upvalue/local being assigned now? */
			if (lh->v.u.ind.vt == v->k && lh->v.u.ind.t == v->u.info) {
				conflict = 1;
				lh->v.u.ind.vt = VLOCAL;
				lh->v.u.ind.t = extra;  /* previous assignment will use safe copy */
			}
			/* index is the local being assigned? (index cannot be upvalue) */
			if (v->k == VLOCAL && lh->v.u.ind.idx == v->u.info) {
				conflict = 1;
				lh->v.u.ind.idx = extra;  /* previous assignment will use safe copy */
			}
		}
	}
	if (conflict) {
		/* copy upvalue/local value to a temporary (in position 'extra') */
		OpCode op = (v->k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
		codegen_codeABC(fs, op, extra, v->u.info, 0);
		codegen_reserveregs(fs, 1);
	}
}

static void assignment(ktap_lexstate *ls, struct LHS_assign *lh, int nvars)
{
	ktap_expdesc e;

	check_condition(ls, vkisvar(lh->v.k), "syntax error");
	if (testnext(ls, ',')) {  /* assignment -> ',' suffixedexp assignment */
		struct LHS_assign nv;

		nv.prev = lh;
		suffixedexp(ls, &nv.v);
		if (nv.v.k != VINDEXED)
			check_conflict(ls, lh, &nv.v);
		checklimit(ls->fs, nvars + ls->nCcalls, KTAP_MAXCCALLS,
				"C levels");
		assignment(ls, &nv, nvars+1);
	} else if (testnext(ls, '=')) {  /* assignment -> '=' explist */
		int nexps;

		nexps = explist(ls, &e);
		if (nexps != nvars) {
			adjust_assign(ls, nvars, nexps, &e);
			/* remove extra values */
			if (nexps > nvars)
				ls->fs->freereg -= nexps - nvars;
		} else {
			/* close last expression */
			codegen_setoneret(ls->fs, &e);
			codegen_storevar(ls->fs, &lh->v, &e);
			return;  /* avoid default */
		}
	} else if (testnext(ls, TK_INCR)) { /* assignment -> '+=' explist */
		int nexps;

		nexps = explist(ls, &e);
		if (nexps != nvars) {
			lex_syntaxerror(ls, "don't allow multi-assign for +=");
		} else {
			/* close last expression */
			codegen_setoneret(ls->fs, &e);
			codegen_storeincr(ls->fs, &lh->v, &e);
			return;  /* avoid default */
		}
	}

	init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
	codegen_storevar(ls->fs, &lh->v, &e);
}

static int cond(ktap_lexstate *ls)
{
	/* cond -> exp */
	ktap_expdesc v;
	expr(ls, &v);  /* read condition */
	if (v.k == VNIL)
		v.k = VFALSE;  /* `falses' are all equal here */
	codegen_goiftrue(ls->fs, &v);
	return v.f;
}

static void gotostat(ktap_lexstate *ls, int pc)
{
	int line = ls->linenumber;
	ktap_string *label;
	int g;

	if (testnext(ls, TK_GOTO))
		label = str_checkname(ls);
	else {
		lex_next(ls);  /* skip break */
		label = ktapc_ts_new("break");
	}
	g = newlabelentry(ls, &ls->dyd->gt, label, line, pc);
	findlabel(ls, g);  /* close it if label already defined */
}

/* check for repeated labels on the same block */
static void checkrepeated(ktap_funcstate *fs, ktap_labellist *ll, ktap_string *label)
{
	int i;
	for (i = fs->bl->firstlabel; i < ll->n; i++) {
		if (ktapc_ts_eqstr(label, ll->arr[i].name)) {
			const char *msg = ktapc_sprintf(
				"label " KTAP_QS " already defined on line %d",
				getstr(label), ll->arr[i].line);
			semerror(fs->ls, msg);
		}
	}
}

/* skip no-op statements */
static void skipnoopstat(ktap_lexstate *ls)
{
	while (ls->t.token == ';' || ls->t.token == TK_DBCOLON)
		statement(ls);
}

static void labelstat (ktap_lexstate *ls, ktap_string *label, int line)
{
	/* label -> '::' NAME '::' */
	ktap_funcstate *fs = ls->fs;
	ktap_labellist *ll = &ls->dyd->label;
	int l;  /* index of new label being created */

	checkrepeated(fs, ll, label);  /* check for repeated labels */
	checknext(ls, TK_DBCOLON);  /* skip double colon */
	/* create new entry for this label */
	l = newlabelentry(ls, ll, label, line, fs->pc);
	skipnoopstat(ls);  /* skip other no-op statements */
	if (block_follow(ls, 0)) {  /* label is last no-op statement in the block? */
		/* assume that locals are already out of scope */
		ll->arr[l].nactvar = fs->bl->nactvar;
	}
	findgotos(ls, &ll->arr[l]);
}

static void whilestat(ktap_lexstate *ls, int line)
{
	/* whilestat -> WHILE cond DO block END */
	ktap_funcstate *fs = ls->fs;
	int whileinit;
	int condexit;
	ktap_blockcnt bl;

	lex_next(ls);  /* skip WHILE */
	whileinit = codegen_getlabel(fs);
	checknext(ls, '(');
	condexit = cond(ls);
	checknext(ls, ')');

	enterblock(fs, &bl, 1);
	//checknext(ls, TK_DO);
	checknext(ls, '{');
	block(ls);
	codegen_jumpto(fs, whileinit);
	checknext(ls, '}');
	//check_match(ls, TK_END, TK_WHILE, line);
	leaveblock(fs);
	codegen_patchtohere(fs, condexit);  /* false conditions finish the loop */
}

static void repeatstat(ktap_lexstate *ls, int line)
{
	/* repeatstat -> REPEAT block UNTIL cond */
	int condexit;
	ktap_funcstate *fs = ls->fs;
	int repeat_init = codegen_getlabel(fs);
	ktap_blockcnt bl1, bl2;

	enterblock(fs, &bl1, 1);  /* loop block */
	enterblock(fs, &bl2, 0);  /* scope block */
	lex_next(ls);  /* skip REPEAT */
	statlist(ls);
	check_match(ls, TK_UNTIL, TK_REPEAT, line);
	condexit = cond(ls);  /* read condition (inside scope block) */
	if (bl2.upval)  /* upvalues? */
		codegen_patchclose(fs, condexit, bl2.nactvar);
	leaveblock(fs);  /* finish scope */
	codegen_patchlist(fs, condexit, repeat_init);  /* close the loop */
	leaveblock(fs);  /* finish loop */
}

static int exp1(ktap_lexstate *ls)
{
	ktap_expdesc e;
	int reg;

	expr(ls, &e);
	codegen_exp2nextreg(ls->fs, &e);
	ktap_assert(e.k == VNONRELOC);
	reg = e.u.info;
	return reg;
}

static void forbody(ktap_lexstate *ls, int base, int line, int nvars, int isnum)
{
	/* forbody -> DO block */
	ktap_blockcnt bl;
	ktap_funcstate *fs = ls->fs;
	int prep, endfor;

	checknext(ls, ')');

	adjustlocalvars(ls, 3);  /* control variables */
	//checknext(ls, TK_DO);
	checknext(ls, '{');
	prep = isnum ? codegen_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : codegen_jump(fs);
	enterblock(fs, &bl, 0);  /* scope for declared variables */
	adjustlocalvars(ls, nvars);
	codegen_reserveregs(fs, nvars);
	block(ls);
	leaveblock(fs);  /* end of scope for declared variables */
	codegen_patchtohere(fs, prep);
	if (isnum)  /* numeric for? */
		endfor = codegen_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP);
	else {  /* generic for */
		codegen_codeABC(fs, OP_TFORCALL, base, 0, nvars);
		codegen_fixline(fs, line);
		endfor = codegen_codeAsBx(fs, OP_TFORLOOP, base + 2, NO_JUMP);
	}
	codegen_patchlist(fs, endfor, prep + 1);
	codegen_fixline(fs, line);
}

static void fornum(ktap_lexstate *ls, ktap_string *varname, int line)
{
	/* fornum -> NAME = exp1,exp1[,exp1] forbody */
	ktap_funcstate *fs = ls->fs;
	int base = fs->freereg;

	new_localvarliteral(ls, "(for index)");
	new_localvarliteral(ls, "(for limit)");
	new_localvarliteral(ls, "(for step)");
	new_localvar(ls, varname);
	checknext(ls, '=');
	exp1(ls);  /* initial value */
	checknext(ls, ',');
	exp1(ls);  /* limit */
	if (testnext(ls, ','))
		exp1(ls);  /* optional step */
	else {  /* default step = 1 */
		codegen_codek(fs, fs->freereg, codegen_numberK(fs, 1));
		codegen_reserveregs(fs, 1);
	}
	forbody(ls, base, line, 1, 1);
}

static void forlist(ktap_lexstate *ls, ktap_string *indexname)
{
	/* forlist -> NAME {,NAME} IN explist forbody */
	ktap_funcstate *fs = ls->fs;
	ktap_expdesc e;
	int nvars = 4;  /* gen, state, control, plus at least one declared var */
	int line;
	int base = fs->freereg;

	/* create control variables */
	new_localvarliteral(ls, "(for generator)");
	new_localvarliteral(ls, "(for state)");
	new_localvarliteral(ls, "(for control)");
	/* create declared variables */
	new_localvar(ls, indexname);
	while (testnext(ls, ',')) {
		new_localvar(ls, str_checkname(ls));
		nvars++;
	}
	checknext(ls, TK_IN);
	line = ls->linenumber;
	adjust_assign(ls, 3, explist(ls, &e), &e);
	codegen_checkstack(fs, 3);  /* extra space to call generator */
	forbody(ls, base, line, nvars - 3, 0);
}

static void forstat(ktap_lexstate *ls, int line)
{
	/* forstat -> FOR (fornum | forlist) END */
	ktap_funcstate *fs = ls->fs;
	ktap_string *varname;
	ktap_blockcnt bl;

	enterblock(fs, &bl, 1);  /* scope for loop and control variables */
	lex_next(ls);  /* skip `for' */

	checknext(ls, '(');
	varname = str_checkname(ls);  /* first variable name */
	switch (ls->t.token) {
	case '=':
		fornum(ls, varname, line);
		break;
	case ',': case TK_IN:
		forlist(ls, varname);
		break;
	default: 
		lex_syntaxerror(ls, KTAP_QL("=") " or " KTAP_QL("in") " expected");
	}
	//check_match(ls, TK_END, TK_FOR, line);
	checknext(ls, '}');
	leaveblock(fs);  /* loop scope (`break' jumps to this point) */
}

static void test_then_block(ktap_lexstate *ls, int *escapelist)
{
	/* test_then_block -> [IF | ELSEIF] cond THEN block */
	ktap_blockcnt bl;
	ktap_funcstate *fs = ls->fs;
	ktap_expdesc v;
	int jf;  /* instruction to skip 'then' code (if condition is false) */

	lex_next(ls);  /* skip IF or ELSEIF */
	checknext(ls, '(');
	expr(ls, &v);  /* read condition */
	checknext(ls, ')');
	//checknext(ls, TK_THEN);
	checknext(ls, '{');
	if (ls->t.token == TK_GOTO || ls->t.token == TK_BREAK) {
		codegen_goiffalse(ls->fs, &v);  /* will jump to label if condition is true */
		enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
		gotostat(ls, v.t);  /* handle goto/break */
		skipnoopstat(ls);  /* skip other no-op statements */
		if (block_follow(ls, 0)) {  /* 'goto' is the entire block? */
			leaveblock(fs);
			checknext(ls, '}');
			return;  /* and that is it */
		} else  /* must skip over 'then' part if condition is false */
			jf = codegen_jump(fs);
	} else {  /* regular case (not goto/break) */
		codegen_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
		enterblock(fs, &bl, 0);
		jf = v.f;
	}
	statlist(ls);  /* `then' part */
	checknext(ls, '}');
	leaveblock(fs);
	if (ls->t.token == TK_ELSE || ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
		codegen_concat(fs, escapelist, codegen_jump(fs));  /* must jump over it */
	codegen_patchtohere(fs, jf);
}

static void ifstat(ktap_lexstate *ls, int line)
{
	/* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
	ktap_funcstate *fs = ls->fs;
	int escapelist = NO_JUMP;  /* exit list for finished parts */

	test_then_block(ls, &escapelist);  /* IF cond THEN block */
	while (ls->t.token == TK_ELSEIF)
		test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
	if (testnext(ls, TK_ELSE)) {
		checknext(ls, '{');
		block(ls);  /* `else' part */
		checknext(ls, '}');
	}
	//check_match(ls, TK_END, TK_IF, line);
	codegen_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}

static void localfunc(ktap_lexstate *ls)
{
	ktap_expdesc b;
	ktap_funcstate *fs = ls->fs;

	new_localvar(ls, str_checkname(ls));  /* new local variable */
	adjustlocalvars(ls, 1);  /* enter its scope */
	body(ls, &b, 0, ls->linenumber);  /* function created in next register */
	/* debug information will only see the variable after this point! */
	getlocvar(fs, b.u.info)->startpc = fs->pc;
}

static void localstat(ktap_lexstate *ls)
{
	/* stat -> LOCAL NAME {`,' NAME} [`=' explist] */
	int nvars = 0;
	int nexps;
	ktap_expdesc e;

	do {
		new_localvar(ls, str_checkname(ls));
		nvars++;
	} while (testnext(ls, ','));
	if (testnext(ls, '='))
		nexps = explist(ls, &e);
	else {
		e.k = VVOID;
		nexps = 0;
	}
	adjust_assign(ls, nvars, nexps, &e);
	adjustlocalvars(ls, nvars);
}

static int funcname(ktap_lexstate *ls, ktap_expdesc *v)
{
	/* funcname -> NAME {fieldsel} [`:' NAME] */
	int ismethod = 0;

	singlevar(ls, v);
	while (ls->t.token == '.')
		fieldsel(ls, v);
		if (ls->t.token == ':') {
			ismethod = 1;
			fieldsel(ls, v);
	}
	return ismethod;
}

static void funcstat(ktap_lexstate *ls, int line)
{
	/* funcstat -> FUNCTION funcname body */
	int ismethod;
	ktap_expdesc v, b;

	lex_next(ls);  /* skip FUNCTION */
	ismethod = funcname(ls, &v);
	body(ls, &b, ismethod, line);
	codegen_storevar(ls->fs, &v, &b);
	codegen_fixline(ls->fs, line);  /* definition `happens' in the first line */
}

static void exprstat(ktap_lexstate *ls)
{
	/* stat -> func | assignment */
	ktap_funcstate *fs = ls->fs;
	struct LHS_assign v;

	suffixedexp(ls, &v.v);
	/* stat -> assignment ? */
	if (ls->t.token == '=' || ls->t.token == ',' ||
	    ls->t.token == TK_INCR) {
		v.prev = NULL;
		assignment(ls, &v, 1);
	} else {  /* stat -> func */
		check_condition(ls, v.v.k == VCALL, "syntax error");
		SETARG_C(getcode(fs, &v.v), 1);  /* call statement uses no results */
	}
}

static void retstat(ktap_lexstate *ls)
{
	/* stat -> RETURN [explist] [';'] */
	ktap_funcstate *fs = ls->fs;
	ktap_expdesc e;
	int first, nret;  /* registers with returned values */

	if (block_follow(ls, 1) || ls->t.token == ';')
		first = nret = 0;  /* return no values */
	else {
		nret = explist(ls, &e);  /* optional return values */
		if (hasmultret(e.k)) {
			codegen_setmultret(fs, &e);
			if (e.k == VCALL && nret == 1) {  /* tail call? */
				SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
				ktap_assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
			}
			first = fs->nactvar;
			nret = KTAP_MULTRET;  /* return all values */
		} else {
			if (nret == 1)  /* only one single value? */
				first = codegen_exp2anyreg(fs, &e);
			else {
				codegen_exp2nextreg(fs, &e);  /* values must go to the `stack' */
				first = fs->nactvar;  /* return all `active' values */
				ktap_assert(nret == fs->freereg - first);
			}
		}
	}
	codegen_ret(fs, first, nret);
	testnext(ls, ';');  /* skip optional semicolon */
}

static void tracestat(ktap_lexstate *ls)
{
	ktap_expdesc v0, key, args;
	ktap_expdesc *v = &v0;
	ktap_string *kdebug_str = ktapc_ts_new("kdebug");
	ktap_string *probe_str = ktapc_ts_new("probe_by_id");
	ktap_string *probe_end_str = ktapc_ts_new("probe_end");
	ktap_funcstate *fs = ls->fs;
	int token = ls->t.token;
	int line = ls->linenumber;
	int base, nparams;

	if (token == TK_TRACE)
		lex_read_string_until(ls, '{');
	else
		lex_next(ls);  /* skip "trace_end" keyword */

	/* kdebug */
	singlevaraux(fs, ls->envn, v, 1);  /* get environment variable */
	codestring(ls, &key, kdebug_str);  /* key is variable name */
	codegen_indexed(fs, v, &key);  /* env[varname] */

	/* fieldsel: kdebug.probe */
	codegen_exp2anyregup(fs, v);
	if (token == TK_TRACE)
		codestring(ls, &key, probe_str);
	else if (token == TK_TRACE_END)
		codestring(ls, &key, probe_end_str);
	codegen_indexed(fs, v, &key);

	/* funcargs*/
	codegen_exp2nextreg(fs, v);

	if (token == TK_TRACE) {
		/* argument: EVENTDEF string */
		check(ls, TK_STRING);
		enterlevel(ls);
		ktap_string *ts = ktapc_parse_eventdef(ls->t.seminfo.ts);
		check_condition(ls, ts != NULL, "Cannot parse eventdef");
		codestring(ls, &args, ts);
		lex_next(ls);  /* skip EVENTDEF string */
		leavelevel(ls);

		codegen_exp2nextreg(fs, &args); /* for next argument */
	}

	/* argument: callback function */
	enterlevel(ls);
	func_body_no_args(ls, &args, ls->linenumber);
	leavelevel(ls);

	codegen_setmultret(fs, &args);

	base = v->u.info;  /* base register for call */
	if (hasmultret(args.k))
		nparams = KTAP_MULTRET;  /* open call */
	else {
		codegen_exp2nextreg(fs, &args);  /* close last argument */
		nparams = fs->freereg - (base+1);
	}
	init_exp(v, VCALL, codegen_codeABC(fs, OP_CALL, base, nparams+1, 2));
	codegen_fixline(fs, line);
	fs->freereg = base+1;

	check_condition(ls, v->k == VCALL, "syntax error");
	SETARG_C(getcode(fs, v), 1);  /* call statement uses no results */
}

static void timerstat(ktap_lexstate *ls)
{
	ktap_expdesc v0, key, args;
	ktap_expdesc *v = &v0;
	ktap_funcstate *fs = ls->fs;
	ktap_string *token_str = ls->t.seminfo.ts;
	ktap_string *interval_str;
	int line = ls->linenumber;
	int base, nparams;

	lex_next(ls);  /* skip profile/tick keyword */
	check(ls, '-');

	lex_read_string_until(ls, '{');
	interval_str = ls->t.seminfo.ts;

	//printf("timerstat str: %s\n", getstr(interval_str));
	//exit(0);

	/* timer */
	singlevaraux(fs, ls->envn, v, 1);  /* get environment variable */
	codestring(ls, &key, ktapc_ts_new("timer"));  /* key is variable name */
	codegen_indexed(fs, v, &key);  /* env[varname] */

	/* fieldsel: timer.profile, timer.tick */
	codegen_exp2anyregup(fs, v);
	codestring(ls, &key, token_str);
	codegen_indexed(fs, v, &key);

	/* funcargs*/
	codegen_exp2nextreg(fs, v);

	/* argument: interval string */
	check(ls, TK_STRING);
	enterlevel(ls);
	codestring(ls, &args, interval_str);
	lex_next(ls);  /* skip interval string */
	leavelevel(ls);

	codegen_exp2nextreg(fs, &args); /* for next argument */

	/* argument: callback function */
	enterlevel(ls);
	func_body_no_args(ls, &args, ls->linenumber);
	leavelevel(ls);

	codegen_setmultret(fs, &args);

	base = v->u.info;  /* base register for call */
	if (hasmultret(args.k))
		nparams = KTAP_MULTRET;  /* open call */
	else {
		codegen_exp2nextreg(fs, &args);  /* close last argument */
		nparams = fs->freereg - (base+1);
	}
	init_exp(v, VCALL, codegen_codeABC(fs, OP_CALL, base, nparams+1, 2));
	codegen_fixline(fs, line);
	fs->freereg = base+1;

	check_condition(ls, v->k == VCALL, "syntax error");
	SETARG_C(getcode(fs, v), 1);  /* call statement uses no results */
}

static void statement(ktap_lexstate *ls)
{
	int line = ls->linenumber;  /* may be needed for error messages */

	enterlevel(ls);
	switch (ls->t.token) {
	case ';': {  /* stat -> ';' (empty statement) */
		lex_next(ls);  /* skip ';' */
		break;
	}
	case TK_IF: {  /* stat -> ifstat */
		ifstat(ls, line);
		break;
	}
	case TK_WHILE: {  /* stat -> whilestat */
		whilestat(ls, line);
		break;
	}
	case TK_DO: {  /* stat -> DO block END */
		lex_next(ls);  /* skip DO */
		block(ls);
		check_match(ls, TK_END, TK_DO, line);
		break;
	}
	case TK_FOR: {  /* stat -> forstat */
		forstat(ls, line);
		break;
	}
	case TK_REPEAT: {  /* stat -> repeatstat */
		repeatstat(ls, line);
		break;
	}
	case TK_FUNCTION: {  /* stat -> funcstat */
		funcstat(ls, line);
		break;
	}
	case TK_LOCAL: {  /* stat -> localstat */
		lex_next(ls);  /* skip LOCAL */
		if (testnext(ls, TK_FUNCTION))  /* local function? */
			localfunc(ls);
		else
			localstat(ls);
		break;
	}
	case TK_DBCOLON: {  /* stat -> label */
		lex_next(ls);  /* skip double colon */
		labelstat(ls, str_checkname(ls), line);
		break;
	}
	case TK_RETURN: {  /* stat -> retstat */
		lex_next(ls);  /* skip RETURN */
		retstat(ls);
		break;
	}
	case TK_BREAK:   /* stat -> breakstat */
	case TK_GOTO: {  /* stat -> 'goto' NAME */
		gotostat(ls, codegen_jump(ls->fs));
		break;
	}

	case TK_TRACE:
	case TK_TRACE_END:
		tracestat(ls);
		break;
	case TK_PROFILE:
	case TK_TICK:
		timerstat(ls);
		break;
	default: {  /* stat -> func | assignment */
		exprstat(ls);
		break;
	}
	}
	//ktap_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
	//	ls->fs->freereg >= ls->fs->nactvar);
	ls->fs->freereg = ls->fs->nactvar;  /* free registers */
	leavelevel(ls);
}
/* }====================================================================== */

/*
 * compiles the main function, which is a regular vararg function with an upvalue
 */
static void mainfunc(ktap_lexstate *ls, ktap_funcstate *fs)
{
	ktap_blockcnt bl;
	ktap_expdesc v;

	open_func(ls, fs, &bl);
	fs->f->is_vararg = 1;  /* main function is always vararg */
	init_exp(&v, VLOCAL, 0);  /* create and... */
	newupvalue(fs, ls->envn, &v);  /* ...set environment upvalue */
	lex_next(ls);  /* read first token */
	statlist(ls);  /* parse main body */
	check(ls, TK_EOS);
	close_func(ls);
}

ktap_closure *ktapc_parser(char *ptr, const char *name)
{
	ktap_lexstate lexstate;
	ktap_funcstate funcstate;
	ktap_dyndata dyd;
	ktap_mbuffer buff;
	int firstchar = *ptr++;
	ktap_closure *cl = ktapc_newlclosure(1);  /* create main closure */

	memset(&lexstate, 0, sizeof(ktap_lexstate));
	memset(&funcstate, 0, sizeof(ktap_funcstate));
	funcstate.f = cl->l.p = ktapc_newproto();
	funcstate.f->source = ktapc_ts_new(name);  /* create and anchor ktap_string */

	lex_init();

	mbuff_init(&buff);
	memset(&dyd, 0, sizeof(ktap_dyndata));
	lexstate.buff = &buff;
	lexstate.dyd = &dyd;
	lex_setinput(&lexstate, ptr, funcstate.f->source, firstchar);

	mainfunc(&lexstate, &funcstate);

	ktap_assert(!funcstate.prev && funcstate.nups == 1 && !lexstate.fs);

	/* all scopes should be correctly finished */
	ktap_assert(dyd.actvar.n == 0 && dyd.gt.n == 0 && dyd.label.n == 0);
	return cl;
}


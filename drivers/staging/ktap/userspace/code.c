/*
 * code.c - Code generator for ktap
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


#define hasjumps(e)	((e)->t != (e)->f)

void codegen_patchtohere (ktap_funcstate *fs, int list);

static int isnumeral(ktap_expdesc *e)
{
	return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}

void codegen_nil(ktap_funcstate *fs, int from, int n)
{
	ktap_instruction *previous;
	int l = from + n - 1;  /* last register to set nil */

	if (fs->pc > fs->lasttarget) {  /* no jumps to current position? */
		previous = &fs->f->code[fs->pc-1];
		if (GET_OPCODE(*previous) == OP_LOADNIL) {
			int pfrom = GETARG_A(*previous);
			int pl = pfrom + GETARG_B(*previous);

			if ((pfrom <= from && from <= pl + 1) ||
				(from <= pfrom && pfrom <= l + 1)) {  /* can connect both? */
				if (pfrom < from)
					from = pfrom;  /* from = min(from, pfrom) */
				if (pl > l)
					l = pl;  /* l = max(l, pl) */
				SETARG_A(*previous, from);
				SETARG_B(*previous, l - from);
				return;
			}
		}  /* else go through */
	}
	codegen_codeABC(fs, OP_LOADNIL, from, n - 1, 0);  /* else no optimization */
}

int codegen_jump(ktap_funcstate *fs)
{
	int jpc = fs->jpc;  /* save list of jumps to here */
	int j;

	fs->jpc = NO_JUMP;
	j = codegen_codeAsBx(fs, OP_JMP, 0, NO_JUMP);
	codegen_concat(fs, &j, jpc);  /* keep them on hold */
	return j;
}

void codegen_ret(ktap_funcstate *fs, int first, int nret)
{
	codegen_codeABC(fs, OP_RETURN, first, nret+1, 0);
}

static int condjump(ktap_funcstate *fs, OpCode op, int A, int B, int C)
{
	codegen_codeABC(fs, op, A, B, C);
	return codegen_jump(fs);
}

static void fixjump(ktap_funcstate *fs, int pc, int dest)
{
	ktap_instruction *jmp = &fs->f->code[pc];
	int offset = dest-(pc+1);

	ktap_assert(dest != NO_JUMP);
	if (abs(offset) > MAXARG_sBx)
		lex_syntaxerror(fs->ls, "control structure too long");
	SETARG_sBx(*jmp, offset);
}

/*
 * returns current `pc' and marks it as a jump target (to avoid wrong
 * optimizations with consecutive instructions not in the same basic block).
 */
int codegen_getlabel(ktap_funcstate *fs)
{
	fs->lasttarget = fs->pc;
	return fs->pc;
}

static int getjump(ktap_funcstate *fs, int pc)
{
	int offset = GETARG_sBx(fs->f->code[pc]);

	if (offset == NO_JUMP)  /* point to itself represents end of list */
		return NO_JUMP;  /* end of list */
	else
		return (pc+1)+offset;  /* turn offset into absolute position */
}

static ktap_instruction *getjumpcontrol(ktap_funcstate *fs, int pc)
{
	ktap_instruction *pi = &fs->f->code[pc];
	if (pc >= 1 && testTMode(GET_OPCODE(*(pi-1))))
		return pi-1;
	else
		return pi;
}

/*
 * check whether list has any jump that do not produce a value
 * (or produce an inverted value)
 */
static int need_value(ktap_funcstate *fs, int list)
{
	for (; list != NO_JUMP; list = getjump(fs, list)) {
		ktap_instruction i = *getjumpcontrol(fs, list);
		if (GET_OPCODE(i) != OP_TESTSET)
			return 1;
	}
	return 0;  /* not found */
}

static int patchtestreg(ktap_funcstate *fs, int node, int reg)
{
	ktap_instruction *i = getjumpcontrol(fs, node);
	if (GET_OPCODE(*i) != OP_TESTSET)
		return 0;  /* cannot patch other instructions */
	if (reg != NO_REG && reg != GETARG_B(*i))
		SETARG_A(*i, reg);
	else  /* no register to put value or register already has the value */
		*i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));

	return 1;
}

static void removevalues(ktap_funcstate *fs, int list)
{
	for (; list != NO_JUMP; list = getjump(fs, list))
		patchtestreg(fs, list, NO_REG);
}

static void patchlistaux(ktap_funcstate *fs, int list, int vtarget, int reg,
			 int dtarget)
{
	while (list != NO_JUMP) {
		int next = getjump(fs, list);
		if (patchtestreg(fs, list, reg))
			fixjump(fs, list, vtarget);
		else
			fixjump(fs, list, dtarget);  /* jump to default target */
		list = next;
	}
}

static void dischargejpc(ktap_funcstate *fs)
{
	patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
	fs->jpc = NO_JUMP;
}

void codegen_patchlist(ktap_funcstate *fs, int list, int target)
{
	if (target == fs->pc)
		codegen_patchtohere(fs, list);
	else {
		ktap_assert(target < fs->pc);
		patchlistaux(fs, list, target, NO_REG, target);
	}
}

void codegen_patchclose(ktap_funcstate *fs, int list, int level)
{
	level++;  /* argument is +1 to reserve 0 as non-op */
	while (list != NO_JUMP) {
		int next = getjump(fs, list);
		ktap_assert(GET_OPCODE(fs->f->code[list]) == OP_JMP &&
			   (GETARG_A(fs->f->code[list]) == 0 ||
			    GETARG_A(fs->f->code[list]) >= level));
		SETARG_A(fs->f->code[list], level);
		list = next;
	}
}

void codegen_patchtohere(ktap_funcstate *fs, int list)
{
	codegen_getlabel(fs);
	codegen_concat(fs, &fs->jpc, list);
}

void codegen_concat(ktap_funcstate *fs, int *l1, int l2)
{
	if (l2 == NO_JUMP)
		return;
	else if (*l1 == NO_JUMP)
		*l1 = l2;
	else {
		int list = *l1;
		int next;
		while ((next = getjump(fs, list)) != NO_JUMP)  /* find last element */
			list = next;
		fixjump(fs, list, l2);
	}
}

static int codegen_code(ktap_funcstate *fs, ktap_instruction i)
{
	ktap_proto *f = fs->f;

	dischargejpc(fs);  /* `pc' will change */

	/* put new instruction in code array */
	ktapc_growvector(f->code, fs->pc, f->sizecode, ktap_instruction,
			 MAX_INT, "opcodes");
	f->code[fs->pc] = i;

	/* save corresponding line information */
	ktapc_growvector(f->lineinfo, fs->pc, f->sizelineinfo, int,
			 MAX_INT, "opcodes");
	f->lineinfo[fs->pc] = fs->ls->lastline;
	return fs->pc++;
}

int codegen_codeABC(ktap_funcstate *fs, OpCode o, int a, int b, int c)
{
	ktap_assert(getOpMode(o) == iABC);
	//ktap_assert(getBMode(o) != OpArgN || b == 0);
	//ktap_assert(getCMode(o) != OpArgN || c == 0);
	//ktap_assert(a <= MAXARG_A && b <= MAXARG_B && c <= MAXARG_C);
	return codegen_code(fs, CREATE_ABC(o, a, b, c));
}

int codegen_codeABx(ktap_funcstate *fs, OpCode o, int a, unsigned int bc)
{
	ktap_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
	ktap_assert(getCMode(o) == OpArgN);
	ktap_assert(a <= MAXARG_A && bc <= MAXARG_Bx);
	return codegen_code(fs, CREATE_ABx(o, a, bc));
}

static int codeextraarg(ktap_funcstate *fs, int a)
{
	ktap_assert(a <= MAXARG_Ax);
	return codegen_code(fs, CREATE_Ax(OP_EXTRAARG, a));
}

int codegen_codek(ktap_funcstate *fs, int reg, int k)
{
	if (k <= MAXARG_Bx)
		return codegen_codeABx(fs, OP_LOADK, reg, k);
	else {
		int p = codegen_codeABx(fs, OP_LOADKX, reg, 0);
		codeextraarg(fs, k);
		return p;
	}
}

void codegen_checkstack(ktap_funcstate *fs, int n)
{
	int newstack = fs->freereg + n;

	if (newstack > fs->f->maxstacksize) {
		if (newstack >= MAXSTACK)
			lex_syntaxerror(fs->ls, "function or expression too complex");
		fs->f->maxstacksize = (u8)(newstack);
	}
}

void codegen_reserveregs(ktap_funcstate *fs, int n)
{
	codegen_checkstack(fs, n);
	fs->freereg += n;
}

static void freereg(ktap_funcstate *fs, int reg)
{
	if (!ISK(reg) && reg >= fs->nactvar) {
		fs->freereg--;
		ktap_assert(reg == fs->freereg);
	}
}

static void freeexp(ktap_funcstate *fs, ktap_expdesc *e)
{
	if (e->k == VNONRELOC)
		freereg(fs, e->u.info);
}

static int addk(ktap_funcstate *fs, ktap_value *key, ktap_value *v)
{
	const ktap_value *idx = ktapc_table_get(fs->h, key);
	ktap_proto *f = fs->f;
	ktap_value kn;
	int k, oldsize;

	if (ttisnumber(idx)) {
		ktap_number n = nvalue(idx);
		ktap_number2int(k, n);
		if (ktapc_equalobj(&f->k[k], v))
			return k;
		/* else may be a collision (e.g., between 0.0 and "\0\0\0\0\0\0\0\0");
			go through and create a new entry for this value */
	}
	/* constant not found; create a new entry */
	oldsize = f->sizek;
	k = fs->nk;

	/* numerical value does not need GC barrier;
	   table has no metatable, so it does not need to invalidate cache */
	setnvalue(&kn, (ktap_number)k);
	ktapc_table_setvalue(fs->h, key, &kn);
	ktapc_growvector(f->k, k, f->sizek, ktap_value, MAXARG_Ax, "constants");
	while (oldsize < f->sizek)
		setnilvalue(&f->k[oldsize++]);
	setobj(&f->k[k], v);
	fs->nk++;
	return k;
}

int codegen_stringK(ktap_funcstate *fs, ktap_string *s)
{
	ktap_value o;

	setsvalue(&o, s);
	return addk(fs, &o, &o);
}

int codegen_numberK(ktap_funcstate *fs, ktap_number r)
{
	int n;
	ktap_value o, s;

	setnvalue(&o, r);
	if (r == 0 || ktap_numisnan(NULL, r)) {  /* handle -0 and NaN */
		/* use raw representation as key to avoid numeric problems */
		setsvalue(&s, ktapc_ts_newlstr((char *)&r, sizeof(r)));
		//   incr_top(L);
		n = addk(fs, &s, &o);
		//   L->top--;
	} else
		n = addk(fs, &o, &o);  /* regular case */
	return n;
}

static int boolK(ktap_funcstate *fs, int b)
{
	ktap_value o;
	setbvalue(&o, b);
	return addk(fs, &o, &o);
}

static int nilK(ktap_funcstate *fs)
{
	ktap_value k, v;
	setnilvalue(&v);
	/* cannot use nil as key; instead use table itself to represent nil */
	sethvalue(&k, fs->h);
	return addk(fs, &k, &v);
}

void codegen_setreturns(ktap_funcstate *fs, ktap_expdesc *e, int nresults)
{
	if (e->k == VCALL) {  /* expression is an open function call? */
		SETARG_C(getcode(fs, e), nresults+1);
	}
	else if (e->k == VVARARG) {
		SETARG_B(getcode(fs, e), nresults+1);
		SETARG_A(getcode(fs, e), fs->freereg);
		codegen_reserveregs(fs, 1);
	}
}

void codegen_setoneret(ktap_funcstate *fs, ktap_expdesc *e)
{
	if (e->k == VCALL) {  /* expression is an open function call? */
		e->k = VNONRELOC;
		e->u.info = GETARG_A(getcode(fs, e));
	} else if (e->k == VVARARG) {
		SETARG_B(getcode(fs, e), 2);
		e->k = VRELOCABLE;  /* can relocate its simple result */
	}
}

void codegen_dischargevars(ktap_funcstate *fs, ktap_expdesc *e)
{
	switch (e->k) {
	case VLOCAL: {
		e->k = VNONRELOC;
		break;
	}
	case VUPVAL: {
		e->u.info = codegen_codeABC(fs, OP_GETUPVAL, 0, e->u.info, 0);
		e->k = VRELOCABLE;
		break;
	}
	case VINDEXED: {
		OpCode op = OP_GETTABUP;  /* assume 't' is in an upvalue */
		freereg(fs, e->u.ind.idx);
		if (e->u.ind.vt == VLOCAL) {  /* 't' is in a register? */
			freereg(fs, e->u.ind.t);
			op = OP_GETTABLE;
		}
		e->u.info = codegen_codeABC(fs, op, 0, e->u.ind.t, e->u.ind.idx);
		e->k = VRELOCABLE;
		break;
	}
	case VVARARG:
	case VCALL: {
		codegen_setoneret(fs, e);
		break;
	}
	default:
		break;  /* there is one value available (somewhere) */
	}
}

static int code_label(ktap_funcstate *fs, int A, int b, int jump)
{
	codegen_getlabel(fs);  /* those instructions may be jump targets */
	return codegen_codeABC(fs, OP_LOADBOOL, A, b, jump);
}

static void discharge2reg(ktap_funcstate *fs, ktap_expdesc *e, int reg)
{
	codegen_dischargevars(fs, e);
	switch (e->k) {
	case VNIL: {
		codegen_nil(fs, reg, 1);
		break;
	}
	case VFALSE:  case VTRUE: {
		codegen_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
 		break;
	}
	case VEVENT:
		codegen_codeABC(fs, OP_EVENT, reg, 0, 0);
		break;
	case VEVENTNAME:
		codegen_codeABC(fs, OP_EVENTNAME, reg, 0, 0);
		break;
	case VEVENTARG:
		codegen_codeABC(fs, OP_EVENTARG, reg, e->u.info, 0);
		break;
	case VK: {
		codegen_codek(fs, reg, e->u.info);
		break;
	}
	case VKNUM: {
		codegen_codek(fs, reg, codegen_numberK(fs, e->u.nval));
		break;
	}
	case VRELOCABLE: {
		ktap_instruction *pc = &getcode(fs, e);
		SETARG_A(*pc, reg);
		break;
	}
	case VNONRELOC: {
		if (reg != e->u.info)
			codegen_codeABC(fs, OP_MOVE, reg, e->u.info, 0);
		break;
	}
	default:
		ktap_assert(e->k == VVOID || e->k == VJMP);
		return;  /* nothing to do... */
	}

	e->u.info = reg;
	e->k = VNONRELOC;
}

static void discharge2anyreg(ktap_funcstate *fs, ktap_expdesc *e)
{
	if (e->k != VNONRELOC) {
		codegen_reserveregs(fs, 1);
		discharge2reg(fs, e, fs->freereg-1);
	}
}

static void exp2reg(ktap_funcstate *fs, ktap_expdesc *e, int reg)
{
	discharge2reg(fs, e, reg);
	if (e->k == VJMP)
		codegen_concat(fs, &e->t, e->u.info);  /* put this jump in `t' list */
	if (hasjumps(e)) {
		int final;  /* position after whole expression */
		int p_f = NO_JUMP;  /* position of an eventual LOAD false */
		int p_t = NO_JUMP;  /* position of an eventual LOAD true */

		if (need_value(fs, e->t) || need_value(fs, e->f)) {
			int fj = (e->k == VJMP) ? NO_JUMP : codegen_jump(fs);

			p_f = code_label(fs, reg, 0, 1);
			p_t = code_label(fs, reg, 1, 0);
			codegen_patchtohere(fs, fj);
		}
		final = codegen_getlabel(fs);
		patchlistaux(fs, e->f, final, reg, p_f);
		patchlistaux(fs, e->t, final, reg, p_t);
	}
	e->f = e->t = NO_JUMP;
	e->u.info = reg;
	e->k = VNONRELOC;
}

void codegen_exp2nextreg(ktap_funcstate *fs, ktap_expdesc *e)
{
	codegen_dischargevars(fs, e);
	freeexp(fs, e);
	codegen_reserveregs(fs, 1);
	exp2reg(fs, e, fs->freereg - 1);
}

int codegen_exp2anyreg(ktap_funcstate *fs, ktap_expdesc *e)
{
	codegen_dischargevars(fs, e);
	if (e->k == VNONRELOC) {
		if (!hasjumps(e))
			return e->u.info;  /* exp is already in a register */
		if (e->u.info >= fs->nactvar) {  /* reg. is not a local? */
			exp2reg(fs, e, e->u.info);  /* put value on it */
			return e->u.info;
		}
	}
	codegen_exp2nextreg(fs, e);  /* default */
	return e->u.info;
}

void codegen_exp2anyregup(ktap_funcstate *fs, ktap_expdesc *e)
{
	if (e->k != VUPVAL || hasjumps(e))
		codegen_exp2anyreg(fs, e);
}

void codegen_exp2val(ktap_funcstate *fs, ktap_expdesc *e)
{
	if (hasjumps(e))
		codegen_exp2anyreg(fs, e);
	else
		codegen_dischargevars(fs, e);
}

int codegen_exp2RK(ktap_funcstate *fs, ktap_expdesc *e)
{
	codegen_exp2val(fs, e);
	switch (e->k) {
	case VTRUE:
	case VFALSE:
	case VNIL: {
		if (fs->nk <= MAXINDEXRK) {  /* constant fits in RK operand? */
			e->u.info = (e->k == VNIL) ? nilK(fs) :
						     boolK(fs, (e->k == VTRUE));
			e->k = VK;
			return RKASK(e->u.info);
		}
		else
			break;
	}
	case VKNUM: {
		e->u.info = codegen_numberK(fs, e->u.nval);
		e->k = VK;
		/* go through */
	}
	case VK: {
		if (e->u.info <= MAXINDEXRK)  /* constant fits in argC? */
			return RKASK(e->u.info);
		else
			break;
	}
	default:
		break;
	}
	/* not a constant in the right range: put it in a register */
	return codegen_exp2anyreg(fs, e);
}

void codegen_storevar(ktap_funcstate *fs, ktap_expdesc *var, ktap_expdesc *ex)
{
	switch (var->k) {
	case VLOCAL: {
		freeexp(fs, ex);
		exp2reg(fs, ex, var->u.info);
		return;
	}
	case VUPVAL: {
		int e = codegen_exp2anyreg(fs, ex);
		codegen_codeABC(fs, OP_SETUPVAL, e, var->u.info, 0);
		break;
	}
	case VINDEXED: {
		OpCode op = (var->u.ind.vt == VLOCAL) ? OP_SETTABLE : OP_SETTABUP;
		int e = codegen_exp2RK(fs, ex);
		codegen_codeABC(fs, op, var->u.ind.t, var->u.ind.idx, e);
		break;
	}
	default:
		ktap_assert(0);  /* invalid var kind to store */
		break;
	}

	freeexp(fs, ex);
}

void codegen_storeincr(ktap_funcstate *fs, ktap_expdesc *var, ktap_expdesc *ex)
{
	switch (var->k) {
#if 0 /*current not supported */
	case VLOCAL: {
		freeexp(fs, ex);
		exp2reg(fs, ex, var->u.info);
		return;
	}
	case VUPVAL: {
		int e = codegen_exp2anyreg(fs, ex);
		codegen_codeABC(fs, OP_SETUPVAL, e, var->u.info, 0);
		break;
	}
#endif
	case VINDEXED: {
		OpCode op = (var->u.ind.vt == VLOCAL) ? OP_SETTABLE_INCR :
				OP_SETTABUP_INCR;
		int e = codegen_exp2RK(fs, ex);
		codegen_codeABC(fs, op, var->u.ind.t, var->u.ind.idx, e);
		break;
	}
	default:
		ktap_assert(0);  /* invalid var kind to store */
		break;
	}

	freeexp(fs, ex);
}


void codegen_self(ktap_funcstate *fs, ktap_expdesc *e, ktap_expdesc *key)
{
	int ereg;

	codegen_exp2anyreg(fs, e);
	ereg = e->u.info;  /* register where 'e' was placed */
	freeexp(fs, e);
	e->u.info = fs->freereg;  /* base register for op_self */
	e->k = VNONRELOC;
	codegen_reserveregs(fs, 2);  /* function and 'self' produced by op_self */
	codegen_codeABC(fs, OP_SELF, e->u.info, ereg, codegen_exp2RK(fs, key));
	freeexp(fs, key);
}

static void invertjump(ktap_funcstate *fs, ktap_expdesc *e)
{
	ktap_instruction *pc = getjumpcontrol(fs, e->u.info);
	ktap_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
			GET_OPCODE(*pc) != OP_TEST);
	SETARG_A(*pc, !(GETARG_A(*pc)));
}

static int jumponcond(ktap_funcstate *fs, ktap_expdesc *e, int cond)
{
	if (e->k == VRELOCABLE) {
		ktap_instruction ie = getcode(fs, e);
		if (GET_OPCODE(ie) == OP_NOT) {
			fs->pc--;  /* remove previous OP_NOT */
			return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
		}
		/* else go through */
	}
	discharge2anyreg(fs, e);
	freeexp(fs, e);
	return condjump(fs, OP_TESTSET, NO_REG, e->u.info, cond);
}

void codegen_goiftrue(ktap_funcstate *fs, ktap_expdesc *e)
{
	int pc;  /* pc of last jump */

	codegen_dischargevars(fs, e);
	switch (e->k) {
	case VJMP: {
		invertjump(fs, e);
		pc = e->u.info;
		break;
	}
	case VK: case VKNUM: case VTRUE: {
		pc = NO_JUMP;  /* always true; do nothing */
		break;
	}
	default:
		pc = jumponcond(fs, e, 0);
		break;
	}

	codegen_concat(fs, &e->f, pc);  /* insert last jump in `f' list */
	codegen_patchtohere(fs, e->t);
	e->t = NO_JUMP;
}

void codegen_goiffalse(ktap_funcstate *fs, ktap_expdesc *e)
{
	int pc;  /* pc of last jump */
	codegen_dischargevars(fs, e);

	switch (e->k) {
	case VJMP: {
		pc = e->u.info;
		break;
	}
	case VNIL: case VFALSE: {
		pc = NO_JUMP;  /* always false; do nothing */
 		break;
	}
	default:
		pc = jumponcond(fs, e, 1);
		break;
	}
	codegen_concat(fs, &e->t, pc);  /* insert last jump in `t' list */
	codegen_patchtohere(fs, e->f);
	e->f = NO_JUMP;
}

static void codenot(ktap_funcstate *fs, ktap_expdesc *e)
{
	codegen_dischargevars(fs, e);
	switch (e->k) {
	case VNIL: case VFALSE: {
		e->k = VTRUE;
		break;
	}
	case VK: case VKNUM: case VTRUE: {
		e->k = VFALSE;
		break;
	}
	case VJMP: {
		invertjump(fs, e);
		break;
	}
	case VRELOCABLE:
	case VNONRELOC: {
		discharge2anyreg(fs, e);
		freeexp(fs, e);
		e->u.info = codegen_codeABC(fs, OP_NOT, 0, e->u.info, 0);
		e->k = VRELOCABLE;
		break;
	}
	default:
		ktap_assert(0);  /* cannot happen */
		break;
	}

	/* interchange true and false lists */
	{ int temp = e->f; e->f = e->t; e->t = temp; }
	removevalues(fs, e->f);
	removevalues(fs, e->t);
}

void codegen_indexed(ktap_funcstate *fs, ktap_expdesc *t, ktap_expdesc *k)
{
	ktap_assert(!hasjumps(t));
	t->u.ind.t = t->u.info;
	t->u.ind.idx = codegen_exp2RK(fs, k);
	t->u.ind.vt = (t->k == VUPVAL) ? VUPVAL
			: check_exp(vkisinreg(t->k), VLOCAL);
	t->k = VINDEXED;
}

static int constfolding(OpCode op, ktap_expdesc *e1, ktap_expdesc *e2)
{
	ktap_number r;

	if (!isnumeral(e1) || !isnumeral(e2))
		return 0;

	if ((op == OP_DIV || op == OP_MOD) && e2->u.nval == 0)
		return 0;  /* do not attempt to divide by 0 */

	if (op == OP_POW)
		return 0; /* ktap current do not suppor pow arith */

	r = ktapc_arith(op - OP_ADD + KTAP_OPADD, e1->u.nval, e2->u.nval);
	e1->u.nval = r;
	return 1;
}

static void codearith(ktap_funcstate *fs, OpCode op,
		      ktap_expdesc *e1, ktap_expdesc *e2, int line)
{
	if (constfolding(op, e1, e2))
		return;
	else {
		int o2 = (op != OP_UNM && op != OP_LEN) ? codegen_exp2RK(fs, e2) : 0;
		int o1 = codegen_exp2RK(fs, e1);

		if (o1 > o2) {
			freeexp(fs, e1);
			freeexp(fs, e2);
		} else {
			freeexp(fs, e2);
			freeexp(fs, e1);
		}
		e1->u.info = codegen_codeABC(fs, op, 0, o1, o2);
		e1->k = VRELOCABLE;
		codegen_fixline(fs, line);
	}
}

static void codecomp(ktap_funcstate *fs, OpCode op, int cond, ktap_expdesc *e1,
		     ktap_expdesc *e2)
{
	int o1 = codegen_exp2RK(fs, e1);
	int o2 = codegen_exp2RK(fs, e2);

	freeexp(fs, e2);
	freeexp(fs, e1);
	if (cond == 0 && op != OP_EQ) {
		int temp;  /* exchange args to replace by `<' or `<=' */
		temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
		cond = 1;
	}
	e1->u.info = condjump(fs, op, cond, o1, o2);
	e1->k = VJMP;
}

void codegen_prefix(ktap_funcstate *fs, UnOpr op, ktap_expdesc *e, int line)
{
	ktap_expdesc e2;

	e2.t = e2.f = NO_JUMP;
	e2.k = VKNUM;
	e2.u.nval = 0;

	switch (op) {
	case OPR_MINUS: {
		if (isnumeral(e))  /* minus constant? */
			e->u.nval = ktap_numunm(e->u.nval);  /* fold it */
		else {
			codegen_exp2anyreg(fs, e);
			codearith(fs, OP_UNM, e, &e2, line);
		}
		break;
	}
	case OPR_NOT:
		codenot(fs, e);
		break;
	case OPR_LEN: {
		codegen_exp2anyreg(fs, e);  /* cannot operate on constants */
		codearith(fs, OP_LEN, e, &e2, line);
		break;
	}
	default:
		ktap_assert(0);
	}
}

void codegen_infix(ktap_funcstate *fs, BinOpr op, ktap_expdesc *v)
{
	switch (op) {
	case OPR_AND: {
		codegen_goiftrue(fs, v);
		break;
	}
	case OPR_OR: {
		codegen_goiffalse(fs, v);
		break;
	}
	case OPR_CONCAT: {
		codegen_exp2nextreg(fs, v);  /* operand must be on the `stack' */
		break;
	}
	case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
	case OPR_MOD: case OPR_POW: {
		if (!isnumeral(v)) codegen_exp2RK(fs, v);
			break;
	}
	default:
		codegen_exp2RK(fs, v);
		break;
	}
}

void codegen_posfix(ktap_funcstate *fs, BinOpr op, ktap_expdesc *e1, ktap_expdesc *e2, int line)
{
	switch (op) {
	case OPR_AND: {
		ktap_assert(e1->t == NO_JUMP);  /* list must be closed */
		codegen_dischargevars(fs, e2);
		codegen_concat(fs, &e2->f, e1->f);
		*e1 = *e2;
		break;
	}
	case OPR_OR: {
		ktap_assert(e1->f == NO_JUMP);  /* list must be closed */
		codegen_dischargevars(fs, e2);
		codegen_concat(fs, &e2->t, e1->t);
		*e1 = *e2;
		break;
	}
	case OPR_CONCAT: {
		codegen_exp2val(fs, e2);
		if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == OP_CONCAT) {
			ktap_assert(e1->u.info == GETARG_B(getcode(fs, e2))-1);
			freeexp(fs, e1);
			SETARG_B(getcode(fs, e2), e1->u.info);
			e1->k = VRELOCABLE; e1->u.info = e2->u.info;
		} else {
			codegen_exp2nextreg(fs, e2);  /* operand must be on the 'stack' */
			codearith(fs, OP_CONCAT, e1, e2, line);
		}
		break;
	}
	case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
	case OPR_MOD: case OPR_POW: {
		codearith(fs, (OpCode)(op - OPR_ADD + OP_ADD), e1, e2, line);
		break;
	}
	case OPR_EQ: case OPR_LT: case OPR_LE: {
		codecomp(fs, (OpCode)(op - OPR_EQ + OP_EQ), 1, e1, e2);
		break;
	}
	case OPR_NE: case OPR_GT: case OPR_GE: {
		codecomp(fs, (OpCode)(op - OPR_NE + OP_EQ), 0, e1, e2);
		break;
	}
	default:
		ktap_assert(0);
	}
}

void codegen_fixline(ktap_funcstate *fs, int line)
{
	fs->f->lineinfo[fs->pc - 1] = line;
}

void codegen_setlist(ktap_funcstate *fs, int base, int nelems, int tostore)
{
	int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
	int b = (tostore == KTAP_MULTRET) ? 0 : tostore;

	ktap_assert(tostore != 0);
	if (c <= MAXARG_C)
		codegen_codeABC(fs, OP_SETLIST, base, b, c);
	else if (c <= MAXARG_Ax) {
		codegen_codeABC(fs, OP_SETLIST, base, b, 0);
		codeextraarg(fs, c);
	} else
		lex_syntaxerror(fs->ls, "constructor too long");
	fs->freereg = base + 1;  /* free registers with list values */
}


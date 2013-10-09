/*
 * vm.c - ktap script virtual machine in Linux kernel
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

#include <linux/slab.h>
#include <linux/ftrace_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include "../include/ktap.h"

#define KTAP_MINSTACK 20

/* todo: enlarge maxstack for big system like 64-bit */
#define KTAP_MAXSTACK           15000

#define KTAP_STACK_SIZE (BASIC_STACK_SIZE * sizeof(ktap_value))

#define CIST_KTAP	(1 << 0) /* call is running a ktap function */
#define CIST_REENTRY	(1 << 2)

#define isktapfunc(ci)	((ci)->callstatus & CIST_KTAP)

static void ktap_concat(ktap_state *ks, int start, int end)
{
	int i, len = 0;
	StkId top = ks->ci->u.l.base;
	ktap_string *ts;
	char *ptr, *buffer;

	for (i = start; i <= end; i++) {
		if (!ttisstring(top + i)) {
			kp_error(ks, "cannot concat non-string\n");
			setnilvalue(top + start);
			return;
		}

		len += rawtsvalue(top + i)->tsv.len;
	}

	if (len >= KTAP_PERCPU_BUFFER_SIZE) {
		kp_error(ks, "Error: too long string concatenation\n");
		return;
	}

	preempt_disable_notrace();

	buffer = kp_percpu_data(KTAP_PERCPU_DATA_BUFFER);
	ptr = buffer;

	for (i = start; i <= end; i++) {
		int len = rawtsvalue(top + i)->tsv.len;
		strncpy(ptr, svalue(top + i), len);
		ptr += len;
	}
	ts = kp_tstring_newlstr(ks, buffer, len);
	setsvalue(top + start, ts);

	preempt_enable_notrace();
}

/* todo: compare l == r if both is tstring type? */
static int lessthan(ktap_state *ks, const ktap_value *l, const ktap_value *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLT(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return kp_tstring_cmp(rawtsvalue(l), rawtsvalue(r)) < 0;

	return 0;
}

static int lessequal(ktap_state *ks, const ktap_value *l, const ktap_value *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLE(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return kp_tstring_cmp(rawtsvalue(l), rawtsvalue(r)) <= 0;

	return 0;
}

static int fb2int (int x)
{
	int e = (x >> 3) & 0x1f;
	if (e == 0)
		return x;
	else
		return ((x & 7) + 8) << (e - 1);
}

static const ktap_value *ktap_tonumber(const ktap_value *obj, ktap_value *n)
{
	if (ttisnumber(obj))
		return obj;

	return NULL;
}

static ktap_upval *findupval(ktap_state *ks, StkId level)
{
	ktap_global_state *g = G(ks);
	ktap_gcobject **pp = &ks->openupval;
	ktap_upval *p;
	ktap_upval *uv;

	while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
		if (p->v == level) {  /* found a corresponding upvalue? */
			return p;
		}
		pp = &p->next;
	}

	/* not found: create a new one */
	uv = &kp_newobject(ks, KTAP_TUPVAL, sizeof(ktap_upval), pp)->uv;
	uv->v = level;  /* current value lives in the stack */
	uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
	uv->u.l.next = g->uvhead.u.l.next;
	uv->u.l.next->u.l.prev = uv;
	g->uvhead.u.l.next = uv;
	return uv;
}

/* todo: implement this*/
static void function_close (ktap_state *ks, StkId level)
{
}

/* create a new closure */
static void pushclosure(ktap_state *ks, ktap_proto *p, ktap_upval **encup,
			StkId base, StkId ra)
{
	int nup = p->sizeupvalues;
	ktap_upvaldesc *uv = p->upvalues;
	int i;
	ktap_closure *ncl = kp_newlclosure(ks, nup);

	ncl->l.p = p;
	setcllvalue(ra, ncl);  /* anchor new closure in stack */

	/* fill in its upvalues */
	for (i = 0; i < nup; i++) {
		if (uv[i].instack) {
			/* upvalue refers to local variable? */
			ncl->l.upvals[i] = findupval(ks, base + uv[i].idx);
		} else {
			/* get upvalue from enclosing function */
			ncl->l.upvals[i] = encup[uv[i].idx];
		}
	}
	//p->cache = ncl;  /* save it on cache for reuse */
}

static void gettable(ktap_state *ks, const ktap_value *t, ktap_value *key,
		     StkId val)
{
	if (ttistable(t)) {
		setobj(val, kp_table_get(hvalue(t), key));
	} else if (ttisaggrtable(t)) {
		kp_aggrtable_get(ks, ahvalue(t), key, val);
	} else {
		kp_error(ks, "get key from non-table\n");
	}
}

static void settable(ktap_state *ks, const ktap_value *t, ktap_value *key,
		     StkId val)
{
	if (ttistable(t)) {
		kp_table_setvalue(ks, hvalue(t), key, val);
	} else if (ttisaggrtable(t)) {
		kp_aggrtable_set(ks, ahvalue(t), key, val);
	} else {
		kp_error(ks, "set key to non-table\n");
	}
}

static void settable_incr(ktap_state *ks, const ktap_value *t, ktap_value *key,
			  StkId val)
{
	if (unlikely(!ttistable(t))) {
		kp_error(ks, "use += operator for non-table\n");
		return;
	}

	if (unlikely(!ttisnumber(val))) {
		kp_error(ks, "use non-number to += operator\n");
		return;
	}

	kp_table_atomic_inc(ks, hvalue(t), key, nvalue(val));
}


static void growstack(ktap_state *ks, int n)
{
	ktap_value *oldstack;
	int lim;
	ktap_callinfo *ci;
	ktap_gcobject *up;
	int size = ks->stacksize;
	int needed = (int)(ks->top - ks->stack) + n;
	int newsize = 2 * size;

	if (newsize > KTAP_MAXSTACK)
		newsize = KTAP_MAXSTACK;

	if (newsize < needed)
		newsize = needed;

	if (newsize > KTAP_MAXSTACK) {  /* stack overflow? */
		kp_error(ks, "stack overflow\n");
		return;
	}

	/* realloc stack */
	oldstack = ks->stack;
	lim = ks->stacksize;
	kp_realloc(ks, ks->stack, ks->stacksize, newsize, ktap_value);

	for (; lim < newsize; lim++)
		setnilvalue(ks->stack + lim);
	ks->stacksize = newsize;
	ks->stack_last = ks->stack + newsize;

	/* correct stack */
	ks->top = (ks->top - oldstack) + ks->stack;
	for (up = ks->openupval; up != NULL; up = up->gch.next)
		gco2uv(up)->v = (gco2uv(up)->v - oldstack) + ks->stack;

	for (ci = ks->ci; ci != NULL; ci = ci->prev) {
		ci->top = (ci->top - oldstack) + ks->stack;
		ci->func = (ci->func - oldstack) + ks->stack;
		if (isktapfunc(ci))
			ci->u.l.base = (ci->u.l.base - oldstack) + ks->stack;
	}
	
}

static inline void checkstack(ktap_state *ks, int n)
{
	if (ks->stack_last - ks->top <= n)
		growstack(ks, n);
}

static StkId adjust_varargs(ktap_state *ks, ktap_proto *p, int actual)
{
	int i;
	int nfixargs = p->numparams;
	StkId base, fixed;

	/* move fixed parameters to final position */
	fixed = ks->top - actual;  /* first fixed argument */
	base = ks->top;  /* final position of first argument */

	for (i=0; i < nfixargs; i++) {
		setobj(ks->top++, fixed + i);
		setnilvalue(fixed + i);
	}

	return base;
}

static int poscall(ktap_state *ks, StkId first_result)
{
	ktap_callinfo *ci;
	StkId res;
	int wanted, i;

	ci = ks->ci;

	res = ci->func;
	wanted = ci->nresults;

	ks->ci = ci = ci->prev;

	for (i = wanted; i != 0 && first_result < ks->top; i--)
		setobj(res++, first_result++);

	while(i-- > 0)
		setnilvalue(res++);

	ks->top = res;

	return (wanted - (-1));
}

static ktap_callinfo *extend_ci(ktap_state *ks)
{
	ktap_callinfo *ci;

	ci = kp_malloc(ks, sizeof(ktap_callinfo));
	ks->ci->next = ci;
	ci->prev = ks->ci;
	ci->next = NULL;

	return ci;
}

static void free_ci(ktap_state *ks)
{
	ktap_callinfo *ci = ks->ci;
	ktap_callinfo *next;

	if (!ci)
		return;

	next = ci->next;
	ci->next = NULL;
	while ((ci = next) != NULL) {
		next = ci->next;
		kp_free(ks, ci);
	}
}

#define next_ci(ks) (ks->ci = ks->ci->next ? ks->ci->next : extend_ci(ks))
#define savestack(ks, p)	((char *)(p) - (char *)ks->stack)
#define restorestack(ks, n)	((ktap_value *)((char *)ks->stack + (n)))

static int precall(ktap_state *ks, StkId func, int nresults)
{
	ktap_cfunction f;
	ktap_callinfo *ci;
	ktap_proto *p;
	StkId base;
	ptrdiff_t funcr = savestack(ks, func);
	int n;

	switch (ttype(func)) {
	case KTAP_TLCF: /* light C function */
		f = fvalue(func);
		goto CFUNC;
	case KTAP_TCCL: /* C closure */
		f = clcvalue(func)->f;
 CFUNC:
		checkstack(ks, KTAP_MINSTACK);
		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = restorestack(ks, funcr);
		ci->top = ks->top + KTAP_MINSTACK;
		ci->callstatus = 0;
		n = (*f)(ks);
		poscall(ks, ks->top - n);
		return 1;
	case KTAP_TLCL:	
		p = CLVALUE(func)->p;
		checkstack(ks, p->maxstacksize);
		func = restorestack(ks, funcr);
		n = (int)(ks->top - func) - 1; /* number of real arguments */

		/* complete missing arguments */
		for (; n < p->numparams; n++)
			setnilvalue(ks->top++);

		base = (!p->is_vararg) ? func + 1 : adjust_varargs(ks, p, n);
		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = func;
		ci->u.l.base = base;
		ci->top = base + p->maxstacksize;
		ci->u.l.savedpc = p->code; /* starting point */
		ci->callstatus = CIST_KTAP;
		ks->top = ci->top;
		return 0;
	default:
		kp_error(ks, "attempt to call nil function\n");
	}

	return 0;
}

#define RA(i)   (base+GETARG_A(i))
#define RB(i)   (base+GETARG_B(i))
#define ISK(x)  ((x) & BITRK)
#define RC(i)   base+GETARG_C(i)
#define RKB(i) \
        ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i)
#define RKC(i)  \
        ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i)

#define dojump(ci,i,e) { \
	ci->u.l.savedpc += GETARG_sBx(i) + e; }
#define donextjump(ci)  { instr = *ci->u.l.savedpc; dojump(ci, instr, 1); }

#define arith_op(ks, op) { \
	ktap_value *rb = RKB(instr); \
	ktap_value *rc = RKC(instr); \
	if (ttisnumber(rb) && ttisnumber(rc)) { \
		ktap_number nb = nvalue(rb), nc = nvalue(rc); \
		setnvalue(ra, op(nb, nc)); \
	} else {	\
		kp_puts(ks, "Error: Cannot make arith operation\n");	\
		return;	\
	} }

static ktap_value *cfunction_cache_get(ktap_state *ks, int index);

static void ktap_execute(ktap_state *ks)
{
	int exec_count = 0;
	ktap_callinfo *ci;
	ktap_lclosure *cl;
	ktap_value *k;
	unsigned int instr, opcode;
	StkId base; /* stack pointer */
	StkId ra; /* register pointer */
	int res, nresults; /* temp varible */

	ci = ks->ci;

 newframe:
	cl = CLVALUE(ci->func);
	k = cl->p->k;
	base = ci->u.l.base;

 mainloop:
	/* main loop of interpreter */

	/* dead loop detaction */
	if (exec_count++ == kp_max_exec_count) {
		if (G(ks)->mainthread != ks) {
			kp_error(ks, "non-mainthread executed instructions "
				     "exceed max limit(%d)\n",
					kp_max_exec_count);
			return;
		}

		cond_resched();
		if (signal_pending(current)) {
			flush_signals(current);
			return;
		}
		exec_count = 0;
	}

	instr = *(ci->u.l.savedpc++);
	opcode = GET_OPCODE(instr);

	/* ra is target register */
	ra = RA(instr);

	switch (opcode) {
	case OP_MOVE:
		setobj(ra, base + GETARG_B(instr));
		break;
	case OP_LOADK:
		setobj(ra, k + GETARG_Bx(instr));
		break;
	case OP_LOADKX:
		setobj(ra, k + GETARG_Ax(*ci->u.l.savedpc++));
		break;
	case OP_LOADBOOL:
		setbvalue(ra, GETARG_B(instr));
		if (GETARG_C(instr))
			ci->u.l.savedpc++;
		break;
	case OP_LOADNIL: {
		int b = GETARG_B(instr);
		do {
			setnilvalue(ra++);
		} while (b--);
		break;
		}
	case OP_GETUPVAL: {
		int b = GETARG_B(instr);
		setobj(ra, cl->upvals[b]->v);
		break;
		}
	case OP_GETTABUP: {
		int b = GETARG_B(instr);
		gettable(ks, cl->upvals[b]->v, RKC(instr), ra);
		base = ci->u.l.base;
		break;
		}
	case OP_GETTABLE:
		gettable(ks, RB(instr), RKC(instr), ra);
		base = ci->u.l.base;
		break;
	case OP_SETTABUP: {
		int a = GETARG_A(instr);
		settable(ks, cl->upvals[a]->v, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
		}
	case OP_SETTABUP_INCR: {
		int a = GETARG_A(instr);
		settable_incr(ks, cl->upvals[a]->v, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
		}
	case OP_SETUPVAL: {
		ktap_upval *uv = cl->upvals[GETARG_B(instr)];
		setobj(uv->v, ra);
		break;
		}
	case OP_SETTABLE:
		settable(ks, ra, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_SETTABLE_INCR:
		settable_incr(ks, ra, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_NEWTABLE: {
		int b = GETARG_B(instr);
		int c = GETARG_C(instr);
		ktap_table *t = kp_table_new(ks);
		sethvalue(ra, t);
		if (b != 0 || c != 0)
			kp_table_resize(ks, t, fb2int(b), fb2int(c));
		break;
		}
	case OP_SELF: {
		StkId rb = RB(instr);
		setobj(ra+1, rb);
		gettable(ks, rb, RKC(instr), ra);
		base = ci->u.l.base;
		break;
		}
	case OP_ADD:
		arith_op(ks, NUMADD);
		break;
	case OP_SUB:
		arith_op(ks, NUMSUB);
		break;
	case OP_MUL:
		arith_op(ks, NUMMUL);
		break;
	case OP_DIV:
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_error(ks, "divide 0 arith operation\n");
			return;
		}
		arith_op(ks, NUMDIV);
		break;
	case OP_MOD:
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_error(ks, "mod 0 arith operation\n");
			return;
		}
		arith_op(ks, NUMMOD);
		break;
	case OP_POW:
		kp_error(ks, "ktap don't support pow arith in kernel\n");
		return;
	case OP_UNM: {
		ktap_value *rb = RB(instr);
		if (ttisnumber(rb)) {
			ktap_number nb = nvalue(rb);
			setnvalue(ra, NUMUNM(nb));
		}
		break;
		}
	case OP_NOT:
		res = isfalse(RB(instr));
		setbvalue(ra, res);
		break;
	case OP_LEN: {
		int len = kp_objlen(ks, RB(instr));
		if (len < 0)
			return;
		setnvalue(ra, len);
		break;
		}
	case OP_CONCAT: {
		int b = GETARG_B(instr);
		int c = GETARG_C(instr);
		ktap_concat(ks, b, c);
		break;
		}
	case OP_JMP:
		dojump(ci, instr, 0);
		break;
	case OP_EQ: {
		ktap_value *rb = RKB(instr);
		ktap_value *rc = RKC(instr);
		if ((int)equalobj(ks, rb, rc) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);

		base = ci->u.l.base;
		break;
		}
	case OP_LT:
		if (lessthan(ks, RKB(instr), RKC(instr)) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		base = ci->u.l.base;
		break;
	case OP_LE:
		if (lessequal(ks, RKB(instr), RKC(instr)) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		base = ci->u.l.base;
		break;
	case OP_TEST:
		if (GETARG_C(instr) ? isfalse(ra) : !isfalse(ra))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		break;
	case OP_TESTSET: {
		ktap_value *rb = RB(instr);
		if (GETARG_C(instr) ? isfalse(rb) : !isfalse(rb))
			ci->u.l.savedpc++;
		else {
			setobj(ra, rb);
			donextjump(ci);
		}
		break;
		}
	case OP_CALL: {
		int b = GETARG_B(instr);
		int ret;

		nresults = GETARG_C(instr) - 1;

		if (b != 0)
			ks->top = ra + b;

		ret = precall(ks, ra, nresults);
		if (ret) { /* C function */
			if (nresults >= 0)
				ks->top = ci->top;
			base = ci->u.l.base;
			break;
		} else { /* ktap function */
			ci = ks->ci;
			/* this flag is used for return time, see OP_RETURN */
			ci->callstatus |= CIST_REENTRY;
			goto newframe;
		}
		break;
		}
	case OP_TAILCALL: {
		int b = GETARG_B(instr);

		if (b != 0)
			ks->top = ra+b;
		if (precall(ks, ra, -1))  /* C function? */
			base = ci->u.l.base;
		else {
			int aux;

			/* 
			 * tail call: put called frame (n) in place of
			 * caller one (o)
			 */
			ktap_callinfo *nci = ks->ci;  /* called frame */
			ktap_callinfo *oci = nci->prev;  /* caller frame */
			StkId nfunc = nci->func;  /* called function */
			StkId ofunc = oci->func;  /* caller function */
			/* last stack slot filled by 'precall' */
			StkId lim = nci->u.l.base +
				    CLVALUE(nfunc)->p->numparams;

			/* close all upvalues from previous call */
			if (cl->p->sizep > 0)
				function_close(ks, oci->u.l.base);

			/* move new frame into old one */
			for (aux = 0; nfunc + aux < lim; aux++)
				setobj(ofunc + aux, nfunc + aux);
			/* correct base */
			oci->u.l.base = ofunc + (nci->u.l.base - nfunc);
			/* correct top */
			oci->top = ks->top = ofunc + (ks->top - nfunc);
			oci->u.l.savedpc = nci->u.l.savedpc;
			/* remove new frame */
			ci = ks->ci = oci;
			/* restart ktap_execute over new ktap function */
			goto newframe;
		}
		break;
		}
	case OP_RETURN: {
		int b = GETARG_B(instr);
		if (b != 0)
			ks->top = ra+b-1;
		if (cl->p->sizep > 0)
			function_close(ks, base);
		b = poscall(ks, ra);

		/* if it's called from external invocation, just return */
		if (!(ci->callstatus & CIST_REENTRY))
			return;

		ci = ks->ci;
		if (b)
			ks->top = ci->top;
		goto newframe;
		}
	case OP_FORLOOP: {
		ktap_number step = nvalue(ra+2);
		/* increment index */
		ktap_number idx = NUMADD(nvalue(ra), step);
		ktap_number limit = nvalue(ra+1);
		if (NUMLT(0, step) ? NUMLE(idx, limit) : NUMLE(limit, idx)) {
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
			setnvalue(ra, idx);  /* update internal index... */
			setnvalue(ra+3, idx);  /* ...and external index */
		}
		break;
		}
	case OP_FORPREP: {
		const ktap_value *init = ra;
		const ktap_value *plimit = ra + 1;
		const ktap_value *pstep = ra + 2;

		if (!ktap_tonumber(init, ra)) {
			kp_error(ks, KTAP_QL("for")
				 " initial value must be a number\n");
			return;
		} else if (!ktap_tonumber(plimit, ra + 1)) {
			kp_error(ks, KTAP_QL("for")
				 " limit must be a number\n");
			return;
		} else if (!ktap_tonumber(pstep, ra + 2)) {
			kp_error(ks, KTAP_QL("for") " step must be a number\n");
			return;
		}

		setnvalue(ra, NUMSUB(nvalue(ra), nvalue(pstep)));
		ci->u.l.savedpc += GETARG_sBx(instr);
		break;
		}
	case OP_TFORCALL: {
		StkId cb = ra + 3;  /* call base */
		setobj(cb + 2, ra + 2);
		setobj(cb + 1, ra + 1);
		setobj(cb, ra);
		ks->top = cb + 3;  /* func. + 2 args (state and index) */
		kp_call(ks, cb, GETARG_C(instr));
		base = ci->u.l.base;
		ks->top = ci->top;
		instr = *(ci->u.l.savedpc++);  /* go to next instruction */
		ra = RA(instr);
		}
		/*go through */
	case OP_TFORLOOP:
		if (!ttisnil(ra + 1)) {  /* continue loop? */
			setobj(ra, ra + 1);  /* save control variable */
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
		}
		break;
	case OP_SETLIST: {
		int n = GETARG_B(instr);
		int c = GETARG_C(instr);
		int last;
		ktap_table *h;

		if (n == 0)
			n = (int)(ks->top - ra) - 1;
		if (c == 0)
			c = GETARG_Ax(*ci->u.l.savedpc++);

		h = hvalue(ra);
		last = ((c - 1) * LFIELDS_PER_FLUSH) + n;
		if (last > h->sizearray)  /* needs more space? */
			kp_table_resizearray(ks, h, last);

		for (; n > 0; n--) {
			ktap_value *val = ra+n;
			kp_table_setint(ks, h, last--, val);
		}
		/* correct top (in case of previous open call) */
		ks->top = ci->top;
		break;
		}
	case OP_CLOSURE: {
		/* need to use closure cache? (multithread contention issue)*/
		ktap_proto *p = cl->p->p[GETARG_Bx(instr)];
		pushclosure(ks, p, cl->upvals, base, ra);
		break;
		}
	case OP_VARARG: {
		int b = GETARG_B(instr) - 1;
		int j;
		int n = (int)(base - ci->func) - cl->p->numparams - 1;
		if (b < 0) {  /* B == 0? */
			b = n;  /* get all var. arguments */
			checkstack(ks, n);
			/* previous call may change the stack */
			ra = RA(instr);
			ks->top = ra + n;
		}
		for (j = 0; j < b; j++) {
			if (j < n) {
				setobj(ra + j, base - n + j);
			} else
				setnilvalue(ra + j);
		}
		break;
		}
	case OP_EXTRAARG:
		return;

	case OP_EVENT: {
		struct ktap_event *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}
		setevalue(ra, e);
		break;
		}

	case OP_EVENTNAME: {
		struct ktap_event *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}
		setsvalue(ra, kp_tstring_new(ks, e->call->name));
		break;
		}
	case OP_EVENTARG:
		if (unlikely(!ks->current_event)) {
			kp_error(ks, "invalid event context\n");
			return;
		}

		kp_event_getarg(ks, ra, GETARG_B(instr));		
		break;
	case OP_LOAD_GLOBAL: {
		ktap_value *cfunc = cfunction_cache_get(ks, GETARG_C(instr));
		setobj(ra, cfunc);
		}
		break;

	case OP_EXIT:
		return;
	}

	goto mainloop;
}

void kp_call(ktap_state *ks, StkId func, int nresults)
{
	if (!precall(ks, func, nresults))
		ktap_execute(ks);
}

static int cfunction_cache_getindex(ktap_state *ks, ktap_value *fname);

/*
 * This function must be called before all code loaded.
 */
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f)
{
	int i;

	for (i = 0; i < f->sizecode; i++) {
		int instr = f->code[i];
		ktap_value *k = f->k;

		if (GET_OPCODE(instr) == OP_GETTABUP) {
			if ((GETARG_B(instr) == 0) && ISK(GETARG_C(instr))) {
				ktap_value *field = k + INDEXK(GETARG_C(instr));
				if (ttype(field) == KTAP_TSTRING) {
					int index = cfunction_cache_getindex(ks,
									field);
					if (index == -1)
						break;

					SET_OPCODE(instr, OP_LOAD_GLOBAL);
					SETARG_C(instr, index);
					f->code[i] = instr;
					break;
				}
			}
		}
	}

	/* continue optimize sub functions */
	for (i = 0; i < f->sizep; i++)
		kp_optimize_code(ks, level + 1, f->p[i]);
}

static ktap_value *cfunction_cache_get(ktap_state *ks, int index)
{
	return &G(ks)->cfunction_tbl[index];
}

static int cfunction_cache_getindex(ktap_state *ks, ktap_value *fname)
{
	const ktap_value *gt = kp_table_getint(hvalue(&G(ks)->registry),
				KTAP_RIDX_GLOBALS);
	const ktap_value *cfunc;
	int nr, i;

	nr = G(ks)->nr_builtin_cfunction;
	cfunc = kp_table_get(hvalue(gt), fname);

	for (i = 0; i < nr; i++) {
		if (rawequalobj(&G(ks)->cfunction_tbl[i], cfunc))
			return i;
	}

	return -1;
}

static void cfunction_cache_add(ktap_state *ks, ktap_value *func)
{
	int nr = G(ks)->nr_builtin_cfunction;
	setobj(&G(ks)->cfunction_tbl[nr], func);
	G(ks)->nr_builtin_cfunction++;
}

static void cfunction_cache_exit(ktap_state *ks)
{
	kp_free(ks, G(ks)->cfunction_tbl);
}

static int cfunction_cache_init(ktap_state *ks)
{
	G(ks)->cfunction_tbl = kp_zalloc(ks, sizeof(ktap_value) * 128);
	if (!G(ks)->cfunction_tbl)
		return -ENOMEM;

	return 0;
}

/* function for register library */
void kp_register_lib(ktap_state *ks, const char *libname, const ktap_Reg *funcs)
{
	int i;
	ktap_table *target_tbl;
	const ktap_value *gt = kp_table_getint(hvalue(&G(ks)->registry),
					       KTAP_RIDX_GLOBALS);

	/* lib is null when register baselib function */
	if (libname == NULL)
		target_tbl = hvalue(gt);
	else {
		ktap_value key, val;

		target_tbl = kp_table_new(ks);
		kp_table_resize(ks, target_tbl, 0,
				sizeof(*funcs) / sizeof(ktap_Reg));

		setsvalue(&key, kp_tstring_new(ks, libname));
		sethvalue(&val, target_tbl);
		kp_table_setvalue(ks, hvalue(gt), &key, &val);
	}

	for (i = 0; funcs[i].name != NULL; i++) {
		ktap_value func_name, cl;

		setsvalue(&func_name, kp_tstring_new(ks, funcs[i].name));
		setfvalue(&cl, funcs[i].func);
		kp_table_setvalue(ks, target_tbl, &func_name, &cl);

		cfunction_cache_add(ks, &cl);
	}
}

#define BASIC_STACK_SIZE        (2 * KTAP_MINSTACK)

static void kp_init_registry(ktap_state *ks)
{
	ktap_value mt;
	ktap_table *registry = kp_table_new(ks);

	sethvalue(&G(ks)->registry, registry);
	kp_table_resize(ks, registry, KTAP_RIDX_LAST, 0);
	setthvalue(ks, &mt, ks);
	kp_table_setint(ks, registry, KTAP_RIDX_MAINTHREAD, &mt);
	sethvalue(&mt, kp_table_new(ks));
	kp_table_setint(ks, registry, KTAP_RIDX_GLOBALS, &mt);
}

static int kp_init_arguments(ktap_state *ks, int argc, char __user **user_argv)
{
	const ktap_value *gt = kp_table_getint(hvalue(&G(ks)->registry),
			   KTAP_RIDX_GLOBALS);
	ktap_table *global_tbl = hvalue(gt);
	ktap_table *arg_tbl = kp_table_new(ks);
	ktap_value arg_tblval;
	ktap_value arg_tsval;
	char **argv;
	int i, ret;
	
	setsvalue(&arg_tsval, kp_tstring_new(ks, "arg"));
	sethvalue(&arg_tblval, arg_tbl);
	kp_table_setvalue(ks, global_tbl, &arg_tsval, &arg_tblval);

	if (!argc)
		return 0;

	if (argc > 1024)
		return -EINVAL;

	argv = kzalloc(argc * sizeof(char *), GFP_KERNEL);
	if (!argv)
		return -ENOMEM;

	ret = copy_from_user(argv, user_argv, argc * sizeof(char *));
	if (ret < 0) {
		kfree(argv);
		return -EFAULT;
	}

	kp_table_resize(ks, arg_tbl, argc, 1);

	ret = 0;
	for (i = 0; i < argc; i++) {
		ktap_value val;
		char __user *ustr = argv[i];
		char * kstr;
		int len;
		int res;

		len = strlen_user(ustr);
		if (len > 0x1000) {
			ret = -EINVAL;
			break;
		}

		kstr = kmalloc(len + 1, GFP_KERNEL);
		if (!kstr) {
			ret = -ENOMEM;
			break;
		}

		if (strncpy_from_user(kstr, ustr, len) < 0) {
			ret = -EFAULT;
			break;
		}

		kstr[len] = '\0';

		if (!kstrtoint(kstr, 10, &res)) {
			setnvalue(&val, res);
		} else
			setsvalue(&val, kp_tstring_new(ks, kstr));

		kp_table_setint(ks, arg_tbl, i, &val);

		kfree(kstr);
	}

	kfree(argv);
	return ret;
}

DEFINE_PER_CPU(int, kp_recursion_context[PERF_NR_CONTEXTS]);

/* todo: make this per-session aware */
static void __percpu *kp_pcpu_data[KTAP_PERCPU_DATA_MAX][PERF_NR_CONTEXTS];

void *kp_percpu_data(int type)
{
	return this_cpu_ptr(kp_pcpu_data[type][trace_get_context_bit()]);
}

static void free_kp_percpu_data(void)
{
	int i, j;

	for (i = 0; i < KTAP_PERCPU_DATA_MAX; i++) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			free_percpu(kp_pcpu_data[i][j]);
			kp_pcpu_data[i][j] = NULL;
		}
	}
}

static int alloc_kp_percpu_data(void)
{
	int data_size[KTAP_PERCPU_DATA_MAX] = {
		sizeof(ktap_state), KTAP_STACK_SIZE, KTAP_PERCPU_BUFFER_SIZE,
		KTAP_PERCPU_BUFFER_SIZE, sizeof(ktap_btrace)};
	int i, j;

	for (i = 0; i < KTAP_PERCPU_DATA_MAX; i++) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			void __percpu *data = __alloc_percpu(data_size[i],
							     __alignof__(char));
			if (!data)
				goto fail;
			kp_pcpu_data[i][j] = data;
		}
	}

	return 0;

 fail:
	free_kp_percpu_data();
	return -ENOMEM;
}

static void kp_init_state(ktap_state *ks)
{
	ktap_callinfo *ci;
	int i;

	ks->stacksize = BASIC_STACK_SIZE;

	for (i = 0; i < BASIC_STACK_SIZE; i++)
		setnilvalue(ks->stack + i);

	ks->top = ks->stack;
	ks->stack_last = ks->stack + ks->stacksize;

	ci = &ks->baseci;
	ci->callstatus = 0;
	ci->func = ks->top;
	setnilvalue(ks->top++);
	ci->top = ks->top + KTAP_MINSTACK;
	ks->ci = ci;
}

static void free_all_ci(ktap_state *ks)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		ktap_state *ks;
		int j;

		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			if (!kp_pcpu_data[KTAP_PERCPU_DATA_STATE][j])
				break;

			ks = per_cpu_ptr(kp_pcpu_data[KTAP_PERCPU_DATA_STATE][j], cpu);
			if (!ks)
				break;

			free_ci(ks);
		}
	}

	free_ci(ks);
}

void kp_exitthread(ktap_state *ks)
{
	/* free local allocation objects, like annotate strings */
	kp_free_gclist(ks, ks->gclist);
}

ktap_state *kp_newthread(ktap_state *mainthread)
{
	ktap_state *ks;

	ks = kp_percpu_data(KTAP_PERCPU_DATA_STATE);
	ks->stack = kp_percpu_data(KTAP_PERCPU_DATA_STACK);
	G(ks) = G(mainthread);
	ks->gclist = NULL;
	kp_init_state(ks);
	return ks;
}

/*
 * wait ktapio thread read all content in ring buffer.
 *
 * Here we use stupid approach to sync with ktapio thread,
 * note that we cannot use semaphore/completion/other sync method,
 * because ktapio thread could be killed by SIG_KILL in anytime, there
 * have no safe way to up semaphore or wake waitqueue before thread exit.
 *
 * we also cannot use waitqueue of current->signal->wait_chldexit to sync
 * exit, becasue mainthread and ktapio thread are in same thread group.
 *
 * Also ktap mainthread must wait ktapio thread exit, otherwise ktapio
 * thread will oops when access ktap structure.
 */
static void wait_user_completion(ktap_state *ks)
{
	struct task_struct *tsk = G(ks)->task;
	G(ks)->wait_user = 1;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ / 10);

		if (get_nr_threads(tsk) == 1)
			break;
	}
}

/* kp_wait: used for mainthread waiting for exit */
static void kp_wait(ktap_state *ks)
{
	struct task_struct *task = G(ks)->trace_task;

	if (G(ks)->exit)
		return;

	ks->stop = 0;

	/* tell workload process to start executing */
	if (G(ks)->parm->workload)
		send_sig(SIGINT, G(ks)->trace_task, 0);

	while (!ks->stop) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ / 10);

		if (signal_pending(current)) {
			flush_signals(current);

			/* newline for handle CTRL+C display as ^C */
			kp_puts(ks, "\n");
			break;
		}

		/* stop waiting if target pid is exited */
		if (task && task->state == TASK_DEAD)
				break;
	}

}

void kp_exit(ktap_state *ks)
{
	set_next_as_exit(ks);

	G(ks)->mainthread->stop = 1;
	G(ks)->exit = 1;
}

void kp_final_exit(ktap_state *ks)
{
	if (!list_empty(&G(ks)->probe_events_head) ||
	    !list_empty(&G(ks)->timers))
		kp_wait(ks);

	if (G(ks)->trace_task)
		put_task_struct(G(ks)->trace_task);

	kp_exit_timers(ks);
	kp_probe_exit(ks);

	/* free all resources got by ktap */
	kp_tstring_freeall(ks);
	kp_free_all_gcobject(ks);
	cfunction_cache_exit(ks);

	wait_user_completion(ks);

	kp_transport_exit(ks);

	kp_exitthread(ks);
	kp_free(ks, ks->stack);
	free_all_ci(ks);

	free_kp_percpu_data();

	free_cpumask_var(G(ks)->cpumask);
	kp_free(ks, ks);
}

/* ktap mainthread initization, main entry for ktap */
ktap_state *kp_newstate(ktap_parm *parm, struct dentry *dir)
{
	ktap_state *ks;
	pid_t pid;
	int cpu;

	ks = kzalloc(sizeof(ktap_state) + sizeof(ktap_global_state),
		     GFP_KERNEL);
	if (!ks)
		return NULL;

	ks->stack = kp_malloc(ks, KTAP_STACK_SIZE);
	G(ks) = (ktap_global_state *)(ks + 1);
	G(ks)->mainthread = ks;
	G(ks)->seed = 201236; /* todo: make more random in future */
	G(ks)->task = current;
	G(ks)->parm = parm;
	INIT_LIST_HEAD(&(G(ks)->timers));
	INIT_LIST_HEAD(&(G(ks)->probe_events_head));
	G(ks)->exit = 0;

	if (kp_transport_init(ks, dir))
		goto out;

	pid = (pid_t)parm->trace_pid;
	if (pid != -1) {
		struct task_struct *task;

		rcu_read_lock();
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		if (!task) {
			kp_error(ks, "cannot find pid %d\n", pid);
			rcu_read_unlock();
			goto out;
		}
		G(ks)->trace_task = task;
		get_task_struct(task);
		rcu_read_unlock();
	}

	if( !alloc_cpumask_var(&G(ks)->cpumask, GFP_KERNEL))
		goto out;

	cpumask_copy(G(ks)->cpumask, cpu_online_mask);

	cpu = parm->trace_cpu;
	if (cpu != -1) {
		if (!cpu_online(cpu)) {
			printk(KERN_INFO "ktap: cpu %d is not online\n", cpu);
			goto out;
		}

		cpumask_clear(G(ks)->cpumask);
		cpumask_set_cpu(cpu, G(ks)->cpumask);
	}

	if (cfunction_cache_init(ks))
		goto out;

	kp_tstring_resize(ks, 512); /* set inital string hashtable size */

	kp_init_state(ks);
	kp_init_registry(ks);
	kp_init_arguments(ks, parm->argc, parm->argv);

	/* init library */
	kp_init_baselib(ks);
	kp_init_kdebuglib(ks);
	kp_init_timerlib(ks);
	kp_init_ansilib(ks);

	if (alloc_kp_percpu_data())
		goto out;

	if (kp_probe_init(ks))
		goto out;

	return ks;

 out:
	G(ks)->exit = 1;
	kp_final_exit(ks);
	return NULL;
}


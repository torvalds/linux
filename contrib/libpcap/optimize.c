/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  Optimization module for BPF code intermediate representation.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <errno.h>

#include "pcap-int.h"

#include "gencode.h"
#include "optimize.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#ifdef BDEBUG
/*
 * The internal "debug printout" flag for the filter expression optimizer.
 * The code to print that stuff is present only if BDEBUG is defined, so
 * the flag, and the routine to set it, are defined only if BDEBUG is
 * defined.
 */
static int pcap_optimizer_debug;

/*
 * Routine to set that flag.
 *
 * This is intended for libpcap developers, not for general use.
 * If you want to set these in a program, you'll have to declare this
 * routine yourself, with the appropriate DLL import attribute on Windows;
 * it's not declared in any header file, and won't be declared in any
 * header file provided by libpcap.
 */
PCAP_API void pcap_set_optimizer_debug(int value);

PCAP_API_DEF void
pcap_set_optimizer_debug(int value)
{
	pcap_optimizer_debug = value;
}

/*
 * The internal "print dot graph" flag for the filter expression optimizer.
 * The code to print that stuff is present only if BDEBUG is defined, so
 * the flag, and the routine to set it, are defined only if BDEBUG is
 * defined.
 */
static int pcap_print_dot_graph;

/*
 * Routine to set that flag.
 *
 * This is intended for libpcap developers, not for general use.
 * If you want to set these in a program, you'll have to declare this
 * routine yourself, with the appropriate DLL import attribute on Windows;
 * it's not declared in any header file, and won't be declared in any
 * header file provided by libpcap.
 */
PCAP_API void pcap_set_print_dot_graph(int value);

PCAP_API_DEF void
pcap_set_print_dot_graph(int value)
{
	pcap_print_dot_graph = value;
}

#endif

/*
 * lowest_set_bit().
 *
 * Takes a 32-bit integer as an argument.
 *
 * If handed a non-zero value, returns the index of the lowest set bit,
 * counting upwards fro zero.
 *
 * If handed zero, the results are platform- and compiler-dependent.
 * Keep it out of the light, don't give it any water, don't feed it
 * after midnight, and don't pass zero to it.
 *
 * This is the same as the count of trailing zeroes in the word.
 */
#if PCAP_IS_AT_LEAST_GNUC_VERSION(3,4)
  /*
   * GCC 3.4 and later; we have __builtin_ctz().
   */
  #define lowest_set_bit(mask) __builtin_ctz(mask)
#elif defined(_MSC_VER)
  /*
   * Visual Studio; we support only 2005 and later, so use
   * _BitScanForward().
   */
#include <intrin.h>

#ifndef __clang__
#pragma intrinsic(_BitScanForward)
#endif

static __forceinline int
lowest_set_bit(int mask)
{
	unsigned long bit;

	/*
	 * Don't sign-extend mask if long is longer than int.
	 * (It's currently not, in MSVC, even on 64-bit platforms, but....)
	 */
	if (_BitScanForward(&bit, (unsigned int)mask) == 0)
		return -1;	/* mask is zero */
	return (int)bit;
}
#elif defined(MSDOS) && defined(__DJGPP__)
  /*
   * MS-DOS with DJGPP, which declares ffs() in <string.h>, which
   * we've already included.
   */
  #define lowest_set_bit(mask)	(ffs((mask)) - 1)
#elif (defined(MSDOS) && defined(__WATCOMC__)) || defined(STRINGS_H_DECLARES_FFS)
  /*
   * MS-DOS with Watcom C, which has <strings.h> and declares ffs() there,
   * or some other platform (UN*X conforming to a sufficient recent version
   * of the Single UNIX Specification).
   */
  #include <strings.h>
  #define lowest_set_bit(mask)	(ffs((mask)) - 1)
#else
/*
 * None of the above.
 * Use a perfect-hash-function-based function.
 */
static int
lowest_set_bit(int mask)
{
	unsigned int v = (unsigned int)mask;

	static const int MultiplyDeBruijnBitPosition[32] = {
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};

	/*
	 * We strip off all but the lowermost set bit (v & ~v),
	 * and perform a minimal perfect hash on it to look up the
	 * number of low-order zero bits in a table.
	 *
	 * See:
	 *
	 *	http://7ooo.mooo.com/text/ComputingTrailingZerosHOWTO.pdf
	 *
	 *	http://supertech.csail.mit.edu/papers/debruijn.pdf
	 */
	return (MultiplyDeBruijnBitPosition[((v & -v) * 0x077CB531U) >> 27]);
}
#endif

/*
 * Represents a deleted instruction.
 */
#define NOP -1

/*
 * Register numbers for use-def values.
 * 0 through BPF_MEMWORDS-1 represent the corresponding scratch memory
 * location.  A_ATOM is the accumulator and X_ATOM is the index
 * register.
 */
#define A_ATOM BPF_MEMWORDS
#define X_ATOM (BPF_MEMWORDS+1)

/*
 * This define is used to represent *both* the accumulator and
 * x register in use-def computations.
 * Currently, the use-def code assumes only one definition per instruction.
 */
#define AX_ATOM N_ATOMS

/*
 * These data structures are used in a Cocke and Shwarz style
 * value numbering scheme.  Since the flowgraph is acyclic,
 * exit values can be propagated from a node's predecessors
 * provided it is uniquely defined.
 */
struct valnode {
	int code;
	int v0, v1;
	int val;
	struct valnode *next;
};

/* Integer constants mapped with the load immediate opcode. */
#define K(i) F(opt_state, BPF_LD|BPF_IMM|BPF_W, i, 0L)

struct vmapinfo {
	int is_const;
	bpf_int32 const_val;
};

typedef struct {
	/*
	 * A flag to indicate that further optimization is needed.
	 * Iterative passes are continued until a given pass yields no
	 * branch movement.
	 */
	int done;

	int n_blocks;
	struct block **blocks;
	int n_edges;
	struct edge **edges;

	/*
	 * A bit vector set representation of the dominators.
	 * We round up the set size to the next power of two.
	 */
	int nodewords;
	int edgewords;
	struct block **levels;
	bpf_u_int32 *space;

#define BITS_PER_WORD (8*sizeof(bpf_u_int32))
/*
 * True if a is in uset {p}
 */
#define SET_MEMBER(p, a) \
((p)[(unsigned)(a) / BITS_PER_WORD] & (1 << ((unsigned)(a) % BITS_PER_WORD)))

/*
 * Add 'a' to uset p.
 */
#define SET_INSERT(p, a) \
(p)[(unsigned)(a) / BITS_PER_WORD] |= (1 << ((unsigned)(a) % BITS_PER_WORD))

/*
 * Delete 'a' from uset p.
 */
#define SET_DELETE(p, a) \
(p)[(unsigned)(a) / BITS_PER_WORD] &= ~(1 << ((unsigned)(a) % BITS_PER_WORD))

/*
 * a := a intersect b
 */
#define SET_INTERSECT(a, b, n)\
{\
	register bpf_u_int32 *_x = a, *_y = b;\
	register int _n = n;\
	while (--_n >= 0) *_x++ &= *_y++;\
}

/*
 * a := a - b
 */
#define SET_SUBTRACT(a, b, n)\
{\
	register bpf_u_int32 *_x = a, *_y = b;\
	register int _n = n;\
	while (--_n >= 0) *_x++ &=~ *_y++;\
}

/*
 * a := a union b
 */
#define SET_UNION(a, b, n)\
{\
	register bpf_u_int32 *_x = a, *_y = b;\
	register int _n = n;\
	while (--_n >= 0) *_x++ |= *_y++;\
}

	uset all_dom_sets;
	uset all_closure_sets;
	uset all_edge_sets;

#define MODULUS 213
	struct valnode *hashtbl[MODULUS];
	int curval;
	int maxval;

	struct vmapinfo *vmap;
	struct valnode *vnode_base;
	struct valnode *next_vnode;
} opt_state_t;

typedef struct {
	/*
	 * Some pointers used to convert the basic block form of the code,
	 * into the array form that BPF requires.  'fstart' will point to
	 * the malloc'd array while 'ftail' is used during the recursive
	 * traversal.
	 */
	struct bpf_insn *fstart;
	struct bpf_insn *ftail;
} conv_state_t;

static void opt_init(compiler_state_t *, opt_state_t *, struct icode *);
static void opt_cleanup(opt_state_t *);

static void intern_blocks(opt_state_t *, struct icode *);

static void find_inedges(opt_state_t *, struct block *);
#ifdef BDEBUG
static void opt_dump(compiler_state_t *, struct icode *);
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

static void
find_levels_r(opt_state_t *opt_state, struct icode *ic, struct block *b)
{
	int level;

	if (isMarked(ic, b))
		return;

	Mark(ic, b);
	b->link = 0;

	if (JT(b)) {
		find_levels_r(opt_state, ic, JT(b));
		find_levels_r(opt_state, ic, JF(b));
		level = MAX(JT(b)->level, JF(b)->level) + 1;
	} else
		level = 0;
	b->level = level;
	b->link = opt_state->levels[level];
	opt_state->levels[level] = b;
}

/*
 * Level graph.  The levels go from 0 at the leaves to
 * N_LEVELS at the root.  The opt_state->levels[] array points to the
 * first node of the level list, whose elements are linked
 * with the 'link' field of the struct block.
 */
static void
find_levels(opt_state_t *opt_state, struct icode *ic)
{
	memset((char *)opt_state->levels, 0, opt_state->n_blocks * sizeof(*opt_state->levels));
	unMarkAll(ic);
	find_levels_r(opt_state, ic, ic->root);
}

/*
 * Find dominator relationships.
 * Assumes graph has been leveled.
 */
static void
find_dom(opt_state_t *opt_state, struct block *root)
{
	int i;
	struct block *b;
	bpf_u_int32 *x;

	/*
	 * Initialize sets to contain all nodes.
	 */
	x = opt_state->all_dom_sets;
	i = opt_state->n_blocks * opt_state->nodewords;
	while (--i >= 0)
		*x++ = 0xFFFFFFFFU;
	/* Root starts off empty. */
	for (i = opt_state->nodewords; --i >= 0;)
		root->dom[i] = 0;

	/* root->level is the highest level no found. */
	for (i = root->level; i >= 0; --i) {
		for (b = opt_state->levels[i]; b; b = b->link) {
			SET_INSERT(b->dom, b->id);
			if (JT(b) == 0)
				continue;
			SET_INTERSECT(JT(b)->dom, b->dom, opt_state->nodewords);
			SET_INTERSECT(JF(b)->dom, b->dom, opt_state->nodewords);
		}
	}
}

static void
propedom(opt_state_t *opt_state, struct edge *ep)
{
	SET_INSERT(ep->edom, ep->id);
	if (ep->succ) {
		SET_INTERSECT(ep->succ->et.edom, ep->edom, opt_state->edgewords);
		SET_INTERSECT(ep->succ->ef.edom, ep->edom, opt_state->edgewords);
	}
}

/*
 * Compute edge dominators.
 * Assumes graph has been leveled and predecessors established.
 */
static void
find_edom(opt_state_t *opt_state, struct block *root)
{
	int i;
	uset x;
	struct block *b;

	x = opt_state->all_edge_sets;
	for (i = opt_state->n_edges * opt_state->edgewords; --i >= 0; )
		x[i] = 0xFFFFFFFFU;

	/* root->level is the highest level no found. */
	memset(root->et.edom, 0, opt_state->edgewords * sizeof(*(uset)0));
	memset(root->ef.edom, 0, opt_state->edgewords * sizeof(*(uset)0));
	for (i = root->level; i >= 0; --i) {
		for (b = opt_state->levels[i]; b != 0; b = b->link) {
			propedom(opt_state, &b->et);
			propedom(opt_state, &b->ef);
		}
	}
}

/*
 * Find the backwards transitive closure of the flow graph.  These sets
 * are backwards in the sense that we find the set of nodes that reach
 * a given node, not the set of nodes that can be reached by a node.
 *
 * Assumes graph has been leveled.
 */
static void
find_closure(opt_state_t *opt_state, struct block *root)
{
	int i;
	struct block *b;

	/*
	 * Initialize sets to contain no nodes.
	 */
	memset((char *)opt_state->all_closure_sets, 0,
	      opt_state->n_blocks * opt_state->nodewords * sizeof(*opt_state->all_closure_sets));

	/* root->level is the highest level no found. */
	for (i = root->level; i >= 0; --i) {
		for (b = opt_state->levels[i]; b; b = b->link) {
			SET_INSERT(b->closure, b->id);
			if (JT(b) == 0)
				continue;
			SET_UNION(JT(b)->closure, b->closure, opt_state->nodewords);
			SET_UNION(JF(b)->closure, b->closure, opt_state->nodewords);
		}
	}
}

/*
 * Return the register number that is used by s.  If A and X are both
 * used, return AX_ATOM.  If no register is used, return -1.
 *
 * The implementation should probably change to an array access.
 */
static int
atomuse(struct stmt *s)
{
	register int c = s->code;

	if (c == NOP)
		return -1;

	switch (BPF_CLASS(c)) {

	case BPF_RET:
		return (BPF_RVAL(c) == BPF_A) ? A_ATOM :
			(BPF_RVAL(c) == BPF_X) ? X_ATOM : -1;

	case BPF_LD:
	case BPF_LDX:
		return (BPF_MODE(c) == BPF_IND) ? X_ATOM :
			(BPF_MODE(c) == BPF_MEM) ? s->k : -1;

	case BPF_ST:
		return A_ATOM;

	case BPF_STX:
		return X_ATOM;

	case BPF_JMP:
	case BPF_ALU:
		if (BPF_SRC(c) == BPF_X)
			return AX_ATOM;
		return A_ATOM;

	case BPF_MISC:
		return BPF_MISCOP(c) == BPF_TXA ? X_ATOM : A_ATOM;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Return the register number that is defined by 's'.  We assume that
 * a single stmt cannot define more than one register.  If no register
 * is defined, return -1.
 *
 * The implementation should probably change to an array access.
 */
static int
atomdef(struct stmt *s)
{
	if (s->code == NOP)
		return -1;

	switch (BPF_CLASS(s->code)) {

	case BPF_LD:
	case BPF_ALU:
		return A_ATOM;

	case BPF_LDX:
		return X_ATOM;

	case BPF_ST:
	case BPF_STX:
		return s->k;

	case BPF_MISC:
		return BPF_MISCOP(s->code) == BPF_TAX ? X_ATOM : A_ATOM;
	}
	return -1;
}

/*
 * Compute the sets of registers used, defined, and killed by 'b'.
 *
 * "Used" means that a statement in 'b' uses the register before any
 * statement in 'b' defines it, i.e. it uses the value left in
 * that register by a predecessor block of this block.
 * "Defined" means that a statement in 'b' defines it.
 * "Killed" means that a statement in 'b' defines it before any
 * statement in 'b' uses it, i.e. it kills the value left in that
 * register by a predecessor block of this block.
 */
static void
compute_local_ud(struct block *b)
{
	struct slist *s;
	atomset def = 0, use = 0, killed = 0;
	int atom;

	for (s = b->stmts; s; s = s->next) {
		if (s->s.code == NOP)
			continue;
		atom = atomuse(&s->s);
		if (atom >= 0) {
			if (atom == AX_ATOM) {
				if (!ATOMELEM(def, X_ATOM))
					use |= ATOMMASK(X_ATOM);
				if (!ATOMELEM(def, A_ATOM))
					use |= ATOMMASK(A_ATOM);
			}
			else if (atom < N_ATOMS) {
				if (!ATOMELEM(def, atom))
					use |= ATOMMASK(atom);
			}
			else
				abort();
		}
		atom = atomdef(&s->s);
		if (atom >= 0) {
			if (!ATOMELEM(use, atom))
				killed |= ATOMMASK(atom);
			def |= ATOMMASK(atom);
		}
	}
	if (BPF_CLASS(b->s.code) == BPF_JMP) {
		/*
		 * XXX - what about RET?
		 */
		atom = atomuse(&b->s);
		if (atom >= 0) {
			if (atom == AX_ATOM) {
				if (!ATOMELEM(def, X_ATOM))
					use |= ATOMMASK(X_ATOM);
				if (!ATOMELEM(def, A_ATOM))
					use |= ATOMMASK(A_ATOM);
			}
			else if (atom < N_ATOMS) {
				if (!ATOMELEM(def, atom))
					use |= ATOMMASK(atom);
			}
			else
				abort();
		}
	}

	b->def = def;
	b->kill = killed;
	b->in_use = use;
}

/*
 * Assume graph is already leveled.
 */
static void
find_ud(opt_state_t *opt_state, struct block *root)
{
	int i, maxlevel;
	struct block *p;

	/*
	 * root->level is the highest level no found;
	 * count down from there.
	 */
	maxlevel = root->level;
	for (i = maxlevel; i >= 0; --i)
		for (p = opt_state->levels[i]; p; p = p->link) {
			compute_local_ud(p);
			p->out_use = 0;
		}

	for (i = 1; i <= maxlevel; ++i) {
		for (p = opt_state->levels[i]; p; p = p->link) {
			p->out_use |= JT(p)->in_use | JF(p)->in_use;
			p->in_use |= p->out_use &~ p->kill;
		}
	}
}
static void
init_val(opt_state_t *opt_state)
{
	opt_state->curval = 0;
	opt_state->next_vnode = opt_state->vnode_base;
	memset((char *)opt_state->vmap, 0, opt_state->maxval * sizeof(*opt_state->vmap));
	memset((char *)opt_state->hashtbl, 0, sizeof opt_state->hashtbl);
}

/* Because we really don't have an IR, this stuff is a little messy. */
static int
F(opt_state_t *opt_state, int code, int v0, int v1)
{
	u_int hash;
	int val;
	struct valnode *p;

	hash = (u_int)code ^ (v0 << 4) ^ (v1 << 8);
	hash %= MODULUS;

	for (p = opt_state->hashtbl[hash]; p; p = p->next)
		if (p->code == code && p->v0 == v0 && p->v1 == v1)
			return p->val;

	val = ++opt_state->curval;
	if (BPF_MODE(code) == BPF_IMM &&
	    (BPF_CLASS(code) == BPF_LD || BPF_CLASS(code) == BPF_LDX)) {
		opt_state->vmap[val].const_val = v0;
		opt_state->vmap[val].is_const = 1;
	}
	p = opt_state->next_vnode++;
	p->val = val;
	p->code = code;
	p->v0 = v0;
	p->v1 = v1;
	p->next = opt_state->hashtbl[hash];
	opt_state->hashtbl[hash] = p;

	return val;
}

static inline void
vstore(struct stmt *s, int *valp, int newval, int alter)
{
	if (alter && newval != VAL_UNKNOWN && *valp == newval)
		s->code = NOP;
	else
		*valp = newval;
}

/*
 * Do constant-folding on binary operators.
 * (Unary operators are handled elsewhere.)
 */
static void
fold_op(compiler_state_t *cstate, opt_state_t *opt_state,
    struct stmt *s, int v0, int v1)
{
	bpf_u_int32 a, b;

	a = opt_state->vmap[v0].const_val;
	b = opt_state->vmap[v1].const_val;

	switch (BPF_OP(s->code)) {
	case BPF_ADD:
		a += b;
		break;

	case BPF_SUB:
		a -= b;
		break;

	case BPF_MUL:
		a *= b;
		break;

	case BPF_DIV:
		if (b == 0)
			bpf_error(cstate, "division by zero");
		a /= b;
		break;

	case BPF_MOD:
		if (b == 0)
			bpf_error(cstate, "modulus by zero");
		a %= b;
		break;

	case BPF_AND:
		a &= b;
		break;

	case BPF_OR:
		a |= b;
		break;

	case BPF_XOR:
		a ^= b;
		break;

	case BPF_LSH:
		a <<= b;
		break;

	case BPF_RSH:
		a >>= b;
		break;

	default:
		abort();
	}
	s->k = a;
	s->code = BPF_LD|BPF_IMM;
	opt_state->done = 0;
}

static inline struct slist *
this_op(struct slist *s)
{
	while (s != 0 && s->s.code == NOP)
		s = s->next;
	return s;
}

static void
opt_not(struct block *b)
{
	struct block *tmp = JT(b);

	JT(b) = JF(b);
	JF(b) = tmp;
}

static void
opt_peep(opt_state_t *opt_state, struct block *b)
{
	struct slist *s;
	struct slist *next, *last;
	int val;

	s = b->stmts;
	if (s == 0)
		return;

	last = s;
	for (/*empty*/; /*empty*/; s = next) {
		/*
		 * Skip over nops.
		 */
		s = this_op(s);
		if (s == 0)
			break;	/* nothing left in the block */

		/*
		 * Find the next real instruction after that one
		 * (skipping nops).
		 */
		next = this_op(s->next);
		if (next == 0)
			break;	/* no next instruction */
		last = next;

		/*
		 * st  M[k]	-->	st  M[k]
		 * ldx M[k]		tax
		 */
		if (s->s.code == BPF_ST &&
		    next->s.code == (BPF_LDX|BPF_MEM) &&
		    s->s.k == next->s.k) {
			opt_state->done = 0;
			next->s.code = BPF_MISC|BPF_TAX;
		}
		/*
		 * ld  #k	-->	ldx  #k
		 * tax			txa
		 */
		if (s->s.code == (BPF_LD|BPF_IMM) &&
		    next->s.code == (BPF_MISC|BPF_TAX)) {
			s->s.code = BPF_LDX|BPF_IMM;
			next->s.code = BPF_MISC|BPF_TXA;
			opt_state->done = 0;
		}
		/*
		 * This is an ugly special case, but it happens
		 * when you say tcp[k] or udp[k] where k is a constant.
		 */
		if (s->s.code == (BPF_LD|BPF_IMM)) {
			struct slist *add, *tax, *ild;

			/*
			 * Check that X isn't used on exit from this
			 * block (which the optimizer might cause).
			 * We know the code generator won't generate
			 * any local dependencies.
			 */
			if (ATOMELEM(b->out_use, X_ATOM))
				continue;

			/*
			 * Check that the instruction following the ldi
			 * is an addx, or it's an ldxms with an addx
			 * following it (with 0 or more nops between the
			 * ldxms and addx).
			 */
			if (next->s.code != (BPF_LDX|BPF_MSH|BPF_B))
				add = next;
			else
				add = this_op(next->next);
			if (add == 0 || add->s.code != (BPF_ALU|BPF_ADD|BPF_X))
				continue;

			/*
			 * Check that a tax follows that (with 0 or more
			 * nops between them).
			 */
			tax = this_op(add->next);
			if (tax == 0 || tax->s.code != (BPF_MISC|BPF_TAX))
				continue;

			/*
			 * Check that an ild follows that (with 0 or more
			 * nops between them).
			 */
			ild = this_op(tax->next);
			if (ild == 0 || BPF_CLASS(ild->s.code) != BPF_LD ||
			    BPF_MODE(ild->s.code) != BPF_IND)
				continue;
			/*
			 * We want to turn this sequence:
			 *
			 * (004) ldi     #0x2		{s}
			 * (005) ldxms   [14]		{next}  -- optional
			 * (006) addx			{add}
			 * (007) tax			{tax}
			 * (008) ild     [x+0]		{ild}
			 *
			 * into this sequence:
			 *
			 * (004) nop
			 * (005) ldxms   [14]
			 * (006) nop
			 * (007) nop
			 * (008) ild     [x+2]
			 *
			 * XXX We need to check that X is not
			 * subsequently used, because we want to change
			 * what'll be in it after this sequence.
			 *
			 * We know we can eliminate the accumulator
			 * modifications earlier in the sequence since
			 * it is defined by the last stmt of this sequence
			 * (i.e., the last statement of the sequence loads
			 * a value into the accumulator, so we can eliminate
			 * earlier operations on the accumulator).
			 */
			ild->s.k += s->s.k;
			s->s.code = NOP;
			add->s.code = NOP;
			tax->s.code = NOP;
			opt_state->done = 0;
		}
	}
	/*
	 * If the comparison at the end of a block is an equality
	 * comparison against a constant, and nobody uses the value
	 * we leave in the A register at the end of a block, and
	 * the operation preceding the comparison is an arithmetic
	 * operation, we can sometime optimize it away.
	 */
	if (b->s.code == (BPF_JMP|BPF_JEQ|BPF_K) &&
	    !ATOMELEM(b->out_use, A_ATOM)) {
	    	/*
	    	 * We can optimize away certain subtractions of the
	    	 * X register.
	    	 */
		if (last->s.code == (BPF_ALU|BPF_SUB|BPF_X)) {
			val = b->val[X_ATOM];
			if (opt_state->vmap[val].is_const) {
				/*
				 * If we have a subtract to do a comparison,
				 * and the X register is a known constant,
				 * we can merge this value into the
				 * comparison:
				 *
				 * sub x  ->	nop
				 * jeq #y	jeq #(x+y)
				 */
				b->s.k += opt_state->vmap[val].const_val;
				last->s.code = NOP;
				opt_state->done = 0;
			} else if (b->s.k == 0) {
				/*
				 * If the X register isn't a constant,
				 * and the comparison in the test is
				 * against 0, we can compare with the
				 * X register, instead:
				 *
				 * sub x  ->	nop
				 * jeq #0	jeq x
				 */
				last->s.code = NOP;
				b->s.code = BPF_JMP|BPF_JEQ|BPF_X;
				opt_state->done = 0;
			}
		}
		/*
		 * Likewise, a constant subtract can be simplified:
		 *
		 * sub #x ->	nop
		 * jeq #y ->	jeq #(x+y)
		 */
		else if (last->s.code == (BPF_ALU|BPF_SUB|BPF_K)) {
			last->s.code = NOP;
			b->s.k += last->s.k;
			opt_state->done = 0;
		}
		/*
		 * And, similarly, a constant AND can be simplified
		 * if we're testing against 0, i.e.:
		 *
		 * and #k	nop
		 * jeq #0  ->	jset #k
		 */
		else if (last->s.code == (BPF_ALU|BPF_AND|BPF_K) &&
		    b->s.k == 0) {
			b->s.k = last->s.k;
			b->s.code = BPF_JMP|BPF_K|BPF_JSET;
			last->s.code = NOP;
			opt_state->done = 0;
			opt_not(b);
		}
	}
	/*
	 * jset #0        ->   never
	 * jset #ffffffff ->   always
	 */
	if (b->s.code == (BPF_JMP|BPF_K|BPF_JSET)) {
		if (b->s.k == 0)
			JT(b) = JF(b);
		if ((u_int)b->s.k == 0xffffffffU)
			JF(b) = JT(b);
	}
	/*
	 * If we're comparing against the index register, and the index
	 * register is a known constant, we can just compare against that
	 * constant.
	 */
	val = b->val[X_ATOM];
	if (opt_state->vmap[val].is_const && BPF_SRC(b->s.code) == BPF_X) {
		bpf_int32 v = opt_state->vmap[val].const_val;
		b->s.code &= ~BPF_X;
		b->s.k = v;
	}
	/*
	 * If the accumulator is a known constant, we can compute the
	 * comparison result.
	 */
	val = b->val[A_ATOM];
	if (opt_state->vmap[val].is_const && BPF_SRC(b->s.code) == BPF_K) {
		bpf_int32 v = opt_state->vmap[val].const_val;
		switch (BPF_OP(b->s.code)) {

		case BPF_JEQ:
			v = v == b->s.k;
			break;

		case BPF_JGT:
			v = (unsigned)v > (unsigned)b->s.k;
			break;

		case BPF_JGE:
			v = (unsigned)v >= (unsigned)b->s.k;
			break;

		case BPF_JSET:
			v &= b->s.k;
			break;

		default:
			abort();
		}
		if (JF(b) != JT(b))
			opt_state->done = 0;
		if (v)
			JF(b) = JT(b);
		else
			JT(b) = JF(b);
	}
}

/*
 * Compute the symbolic value of expression of 's', and update
 * anything it defines in the value table 'val'.  If 'alter' is true,
 * do various optimizations.  This code would be cleaner if symbolic
 * evaluation and code transformations weren't folded together.
 */
static void
opt_stmt(compiler_state_t *cstate, opt_state_t *opt_state,
    struct stmt *s, int val[], int alter)
{
	int op;
	int v;

	switch (s->code) {

	case BPF_LD|BPF_ABS|BPF_W:
	case BPF_LD|BPF_ABS|BPF_H:
	case BPF_LD|BPF_ABS|BPF_B:
		v = F(opt_state, s->code, s->k, 0L);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_IND|BPF_W:
	case BPF_LD|BPF_IND|BPF_H:
	case BPF_LD|BPF_IND|BPF_B:
		v = val[X_ATOM];
		if (alter && opt_state->vmap[v].is_const) {
			s->code = BPF_LD|BPF_ABS|BPF_SIZE(s->code);
			s->k += opt_state->vmap[v].const_val;
			v = F(opt_state, s->code, s->k, 0L);
			opt_state->done = 0;
		}
		else
			v = F(opt_state, s->code, s->k, v);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_LEN:
		v = F(opt_state, s->code, 0L, 0L);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_IMM:
		v = K(s->k);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LDX|BPF_IMM:
		v = K(s->k);
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_LDX|BPF_MSH|BPF_B:
		v = F(opt_state, s->code, s->k, 0L);
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_ALU|BPF_NEG:
		if (alter && opt_state->vmap[val[A_ATOM]].is_const) {
			s->code = BPF_LD|BPF_IMM;
			s->k = -opt_state->vmap[val[A_ATOM]].const_val;
			val[A_ATOM] = K(s->k);
		}
		else
			val[A_ATOM] = F(opt_state, s->code, val[A_ATOM], 0L);
		break;

	case BPF_ALU|BPF_ADD|BPF_K:
	case BPF_ALU|BPF_SUB|BPF_K:
	case BPF_ALU|BPF_MUL|BPF_K:
	case BPF_ALU|BPF_DIV|BPF_K:
	case BPF_ALU|BPF_MOD|BPF_K:
	case BPF_ALU|BPF_AND|BPF_K:
	case BPF_ALU|BPF_OR|BPF_K:
	case BPF_ALU|BPF_XOR|BPF_K:
	case BPF_ALU|BPF_LSH|BPF_K:
	case BPF_ALU|BPF_RSH|BPF_K:
		op = BPF_OP(s->code);
		if (alter) {
			if (s->k == 0) {
				/* don't optimize away "sub #0"
				 * as it may be needed later to
				 * fixup the generated math code */
				if (op == BPF_ADD ||
				    op == BPF_LSH || op == BPF_RSH ||
				    op == BPF_OR || op == BPF_XOR) {
					s->code = NOP;
					break;
				}
				if (op == BPF_MUL || op == BPF_AND) {
					s->code = BPF_LD|BPF_IMM;
					val[A_ATOM] = K(s->k);
					break;
				}
			}
			if (opt_state->vmap[val[A_ATOM]].is_const) {
				fold_op(cstate, opt_state, s, val[A_ATOM], K(s->k));
				val[A_ATOM] = K(s->k);
				break;
			}
		}
		val[A_ATOM] = F(opt_state, s->code, val[A_ATOM], K(s->k));
		break;

	case BPF_ALU|BPF_ADD|BPF_X:
	case BPF_ALU|BPF_SUB|BPF_X:
	case BPF_ALU|BPF_MUL|BPF_X:
	case BPF_ALU|BPF_DIV|BPF_X:
	case BPF_ALU|BPF_MOD|BPF_X:
	case BPF_ALU|BPF_AND|BPF_X:
	case BPF_ALU|BPF_OR|BPF_X:
	case BPF_ALU|BPF_XOR|BPF_X:
	case BPF_ALU|BPF_LSH|BPF_X:
	case BPF_ALU|BPF_RSH|BPF_X:
		op = BPF_OP(s->code);
		if (alter && opt_state->vmap[val[X_ATOM]].is_const) {
			if (opt_state->vmap[val[A_ATOM]].is_const) {
				fold_op(cstate, opt_state, s, val[A_ATOM], val[X_ATOM]);
				val[A_ATOM] = K(s->k);
			}
			else {
				s->code = BPF_ALU|BPF_K|op;
				s->k = opt_state->vmap[val[X_ATOM]].const_val;
				opt_state->done = 0;
				val[A_ATOM] =
					F(opt_state, s->code, val[A_ATOM], K(s->k));
			}
			break;
		}
		/*
		 * Check if we're doing something to an accumulator
		 * that is 0, and simplify.  This may not seem like
		 * much of a simplification but it could open up further
		 * optimizations.
		 * XXX We could also check for mul by 1, etc.
		 */
		if (alter && opt_state->vmap[val[A_ATOM]].is_const
		    && opt_state->vmap[val[A_ATOM]].const_val == 0) {
			if (op == BPF_ADD || op == BPF_OR || op == BPF_XOR) {
				s->code = BPF_MISC|BPF_TXA;
				vstore(s, &val[A_ATOM], val[X_ATOM], alter);
				break;
			}
			else if (op == BPF_MUL || op == BPF_DIV || op == BPF_MOD ||
				 op == BPF_AND || op == BPF_LSH || op == BPF_RSH) {
				s->code = BPF_LD|BPF_IMM;
				s->k = 0;
				vstore(s, &val[A_ATOM], K(s->k), alter);
				break;
			}
			else if (op == BPF_NEG) {
				s->code = NOP;
				break;
			}
		}
		val[A_ATOM] = F(opt_state, s->code, val[A_ATOM], val[X_ATOM]);
		break;

	case BPF_MISC|BPF_TXA:
		vstore(s, &val[A_ATOM], val[X_ATOM], alter);
		break;

	case BPF_LD|BPF_MEM:
		v = val[s->k];
		if (alter && opt_state->vmap[v].is_const) {
			s->code = BPF_LD|BPF_IMM;
			s->k = opt_state->vmap[v].const_val;
			opt_state->done = 0;
		}
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_MISC|BPF_TAX:
		vstore(s, &val[X_ATOM], val[A_ATOM], alter);
		break;

	case BPF_LDX|BPF_MEM:
		v = val[s->k];
		if (alter && opt_state->vmap[v].is_const) {
			s->code = BPF_LDX|BPF_IMM;
			s->k = opt_state->vmap[v].const_val;
			opt_state->done = 0;
		}
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_ST:
		vstore(s, &val[s->k], val[A_ATOM], alter);
		break;

	case BPF_STX:
		vstore(s, &val[s->k], val[X_ATOM], alter);
		break;
	}
}

static void
deadstmt(opt_state_t *opt_state, register struct stmt *s, register struct stmt *last[])
{
	register int atom;

	atom = atomuse(s);
	if (atom >= 0) {
		if (atom == AX_ATOM) {
			last[X_ATOM] = 0;
			last[A_ATOM] = 0;
		}
		else
			last[atom] = 0;
	}
	atom = atomdef(s);
	if (atom >= 0) {
		if (last[atom]) {
			opt_state->done = 0;
			last[atom]->code = NOP;
		}
		last[atom] = s;
	}
}

static void
opt_deadstores(opt_state_t *opt_state, register struct block *b)
{
	register struct slist *s;
	register int atom;
	struct stmt *last[N_ATOMS];

	memset((char *)last, 0, sizeof last);

	for (s = b->stmts; s != 0; s = s->next)
		deadstmt(opt_state, &s->s, last);
	deadstmt(opt_state, &b->s, last);

	for (atom = 0; atom < N_ATOMS; ++atom)
		if (last[atom] && !ATOMELEM(b->out_use, atom)) {
			last[atom]->code = NOP;
			opt_state->done = 0;
		}
}

static void
opt_blk(compiler_state_t *cstate, opt_state_t *opt_state,
    struct block *b, int do_stmts)
{
	struct slist *s;
	struct edge *p;
	int i;
	bpf_int32 aval, xval;

#if 0
	for (s = b->stmts; s && s->next; s = s->next)
		if (BPF_CLASS(s->s.code) == BPF_JMP) {
			do_stmts = 0;
			break;
		}
#endif

	/*
	 * Initialize the atom values.
	 */
	p = b->in_edges;
	if (p == 0) {
		/*
		 * We have no predecessors, so everything is undefined
		 * upon entry to this block.
		 */
		memset((char *)b->val, 0, sizeof(b->val));
	} else {
		/*
		 * Inherit values from our predecessors.
		 *
		 * First, get the values from the predecessor along the
		 * first edge leading to this node.
		 */
		memcpy((char *)b->val, (char *)p->pred->val, sizeof(b->val));
		/*
		 * Now look at all the other nodes leading to this node.
		 * If, for the predecessor along that edge, a register
		 * has a different value from the one we have (i.e.,
		 * control paths are merging, and the merging paths
		 * assign different values to that register), give the
		 * register the undefined value of 0.
		 */
		while ((p = p->next) != NULL) {
			for (i = 0; i < N_ATOMS; ++i)
				if (b->val[i] != p->pred->val[i])
					b->val[i] = 0;
		}
	}
	aval = b->val[A_ATOM];
	xval = b->val[X_ATOM];
	for (s = b->stmts; s; s = s->next)
		opt_stmt(cstate, opt_state, &s->s, b->val, do_stmts);

	/*
	 * This is a special case: if we don't use anything from this
	 * block, and we load the accumulator or index register with a
	 * value that is already there, or if this block is a return,
	 * eliminate all the statements.
	 *
	 * XXX - what if it does a store?
	 *
	 * XXX - why does it matter whether we use anything from this
	 * block?  If the accumulator or index register doesn't change
	 * its value, isn't that OK even if we use that value?
	 *
	 * XXX - if we load the accumulator with a different value,
	 * and the block ends with a conditional branch, we obviously
	 * can't eliminate it, as the branch depends on that value.
	 * For the index register, the conditional branch only depends
	 * on the index register value if the test is against the index
	 * register value rather than a constant; if nothing uses the
	 * value we put into the index register, and we're not testing
	 * against the index register's value, and there aren't any
	 * other problems that would keep us from eliminating this
	 * block, can we eliminate it?
	 */
	if (do_stmts &&
	    ((b->out_use == 0 &&
	      aval != VAL_UNKNOWN && b->val[A_ATOM] == aval &&
	      xval != VAL_UNKNOWN && b->val[X_ATOM] == xval) ||
	     BPF_CLASS(b->s.code) == BPF_RET)) {
		if (b->stmts != 0) {
			b->stmts = 0;
			opt_state->done = 0;
		}
	} else {
		opt_peep(opt_state, b);
		opt_deadstores(opt_state, b);
	}
	/*
	 * Set up values for branch optimizer.
	 */
	if (BPF_SRC(b->s.code) == BPF_K)
		b->oval = K(b->s.k);
	else
		b->oval = b->val[X_ATOM];
	b->et.code = b->s.code;
	b->ef.code = -b->s.code;
}

/*
 * Return true if any register that is used on exit from 'succ', has
 * an exit value that is different from the corresponding exit value
 * from 'b'.
 */
static int
use_conflict(struct block *b, struct block *succ)
{
	int atom;
	atomset use = succ->out_use;

	if (use == 0)
		return 0;

	for (atom = 0; atom < N_ATOMS; ++atom)
		if (ATOMELEM(use, atom))
			if (b->val[atom] != succ->val[atom])
				return 1;
	return 0;
}

static struct block *
fold_edge(struct block *child, struct edge *ep)
{
	int sense;
	int aval0, aval1, oval0, oval1;
	int code = ep->code;

	if (code < 0) {
		code = -code;
		sense = 0;
	} else
		sense = 1;

	if (child->s.code != code)
		return 0;

	aval0 = child->val[A_ATOM];
	oval0 = child->oval;
	aval1 = ep->pred->val[A_ATOM];
	oval1 = ep->pred->oval;

	if (aval0 != aval1)
		return 0;

	if (oval0 == oval1)
		/*
		 * The operands of the branch instructions are
		 * identical, so the result is true if a true
		 * branch was taken to get here, otherwise false.
		 */
		return sense ? JT(child) : JF(child);

	if (sense && code == (BPF_JMP|BPF_JEQ|BPF_K))
		/*
		 * At this point, we only know the comparison if we
		 * came down the true branch, and it was an equality
		 * comparison with a constant.
		 *
		 * I.e., if we came down the true branch, and the branch
		 * was an equality comparison with a constant, we know the
		 * accumulator contains that constant.  If we came down
		 * the false branch, or the comparison wasn't with a
		 * constant, we don't know what was in the accumulator.
		 *
		 * We rely on the fact that distinct constants have distinct
		 * value numbers.
		 */
		return JF(child);

	return 0;
}

static void
opt_j(opt_state_t *opt_state, struct edge *ep)
{
	register int i, k;
	register struct block *target;

	if (JT(ep->succ) == 0)
		return;

	if (JT(ep->succ) == JF(ep->succ)) {
		/*
		 * Common branch targets can be eliminated, provided
		 * there is no data dependency.
		 */
		if (!use_conflict(ep->pred, ep->succ->et.succ)) {
			opt_state->done = 0;
			ep->succ = JT(ep->succ);
		}
	}
	/*
	 * For each edge dominator that matches the successor of this
	 * edge, promote the edge successor to the its grandchild.
	 *
	 * XXX We violate the set abstraction here in favor a reasonably
	 * efficient loop.
	 */
 top:
	for (i = 0; i < opt_state->edgewords; ++i) {
		register bpf_u_int32 x = ep->edom[i];

		while (x != 0) {
			k = lowest_set_bit(x);
			x &=~ (1 << k);
			k += i * BITS_PER_WORD;

			target = fold_edge(ep->succ, opt_state->edges[k]);
			/*
			 * Check that there is no data dependency between
			 * nodes that will be violated if we move the edge.
			 */
			if (target != 0 && !use_conflict(ep->pred, target)) {
				opt_state->done = 0;
				ep->succ = target;
				if (JT(target) != 0)
					/*
					 * Start over unless we hit a leaf.
					 */
					goto top;
				return;
			}
		}
	}
}


static void
or_pullup(opt_state_t *opt_state, struct block *b)
{
	int val, at_top;
	struct block *pull;
	struct block **diffp, **samep;
	struct edge *ep;

	ep = b->in_edges;
	if (ep == 0)
		return;

	/*
	 * Make sure each predecessor loads the same value.
	 * XXX why?
	 */
	val = ep->pred->val[A_ATOM];
	for (ep = ep->next; ep != 0; ep = ep->next)
		if (val != ep->pred->val[A_ATOM])
			return;

	if (JT(b->in_edges->pred) == b)
		diffp = &JT(b->in_edges->pred);
	else
		diffp = &JF(b->in_edges->pred);

	at_top = 1;
	for (;;) {
		if (*diffp == 0)
			return;

		if (JT(*diffp) != JT(b))
			return;

		if (!SET_MEMBER((*diffp)->dom, b->id))
			return;

		if ((*diffp)->val[A_ATOM] != val)
			break;

		diffp = &JF(*diffp);
		at_top = 0;
	}
	samep = &JF(*diffp);
	for (;;) {
		if (*samep == 0)
			return;

		if (JT(*samep) != JT(b))
			return;

		if (!SET_MEMBER((*samep)->dom, b->id))
			return;

		if ((*samep)->val[A_ATOM] == val)
			break;

		/* XXX Need to check that there are no data dependencies
		   between dp0 and dp1.  Currently, the code generator
		   will not produce such dependencies. */
		samep = &JF(*samep);
	}
#ifdef notdef
	/* XXX This doesn't cover everything. */
	for (i = 0; i < N_ATOMS; ++i)
		if ((*samep)->val[i] != pred->val[i])
			return;
#endif
	/* Pull up the node. */
	pull = *samep;
	*samep = JF(pull);
	JF(pull) = *diffp;

	/*
	 * At the top of the chain, each predecessor needs to point at the
	 * pulled up node.  Inside the chain, there is only one predecessor
	 * to worry about.
	 */
	if (at_top) {
		for (ep = b->in_edges; ep != 0; ep = ep->next) {
			if (JT(ep->pred) == b)
				JT(ep->pred) = pull;
			else
				JF(ep->pred) = pull;
		}
	}
	else
		*diffp = pull;

	opt_state->done = 0;
}

static void
and_pullup(opt_state_t *opt_state, struct block *b)
{
	int val, at_top;
	struct block *pull;
	struct block **diffp, **samep;
	struct edge *ep;

	ep = b->in_edges;
	if (ep == 0)
		return;

	/*
	 * Make sure each predecessor loads the same value.
	 */
	val = ep->pred->val[A_ATOM];
	for (ep = ep->next; ep != 0; ep = ep->next)
		if (val != ep->pred->val[A_ATOM])
			return;

	if (JT(b->in_edges->pred) == b)
		diffp = &JT(b->in_edges->pred);
	else
		diffp = &JF(b->in_edges->pred);

	at_top = 1;
	for (;;) {
		if (*diffp == 0)
			return;

		if (JF(*diffp) != JF(b))
			return;

		if (!SET_MEMBER((*diffp)->dom, b->id))
			return;

		if ((*diffp)->val[A_ATOM] != val)
			break;

		diffp = &JT(*diffp);
		at_top = 0;
	}
	samep = &JT(*diffp);
	for (;;) {
		if (*samep == 0)
			return;

		if (JF(*samep) != JF(b))
			return;

		if (!SET_MEMBER((*samep)->dom, b->id))
			return;

		if ((*samep)->val[A_ATOM] == val)
			break;

		/* XXX Need to check that there are no data dependencies
		   between diffp and samep.  Currently, the code generator
		   will not produce such dependencies. */
		samep = &JT(*samep);
	}
#ifdef notdef
	/* XXX This doesn't cover everything. */
	for (i = 0; i < N_ATOMS; ++i)
		if ((*samep)->val[i] != pred->val[i])
			return;
#endif
	/* Pull up the node. */
	pull = *samep;
	*samep = JT(pull);
	JT(pull) = *diffp;

	/*
	 * At the top of the chain, each predecessor needs to point at the
	 * pulled up node.  Inside the chain, there is only one predecessor
	 * to worry about.
	 */
	if (at_top) {
		for (ep = b->in_edges; ep != 0; ep = ep->next) {
			if (JT(ep->pred) == b)
				JT(ep->pred) = pull;
			else
				JF(ep->pred) = pull;
		}
	}
	else
		*diffp = pull;

	opt_state->done = 0;
}

static void
opt_blks(compiler_state_t *cstate, opt_state_t *opt_state, struct icode *ic,
    int do_stmts)
{
	int i, maxlevel;
	struct block *p;

	init_val(opt_state);
	maxlevel = ic->root->level;

	find_inedges(opt_state, ic->root);
	for (i = maxlevel; i >= 0; --i)
		for (p = opt_state->levels[i]; p; p = p->link)
			opt_blk(cstate, opt_state, p, do_stmts);

	if (do_stmts)
		/*
		 * No point trying to move branches; it can't possibly
		 * make a difference at this point.
		 */
		return;

	for (i = 1; i <= maxlevel; ++i) {
		for (p = opt_state->levels[i]; p; p = p->link) {
			opt_j(opt_state, &p->et);
			opt_j(opt_state, &p->ef);
		}
	}

	find_inedges(opt_state, ic->root);
	for (i = 1; i <= maxlevel; ++i) {
		for (p = opt_state->levels[i]; p; p = p->link) {
			or_pullup(opt_state, p);
			and_pullup(opt_state, p);
		}
	}
}

static inline void
link_inedge(struct edge *parent, struct block *child)
{
	parent->next = child->in_edges;
	child->in_edges = parent;
}

static void
find_inedges(opt_state_t *opt_state, struct block *root)
{
	int i;
	struct block *b;

	for (i = 0; i < opt_state->n_blocks; ++i)
		opt_state->blocks[i]->in_edges = 0;

	/*
	 * Traverse the graph, adding each edge to the predecessor
	 * list of its successors.  Skip the leaves (i.e. level 0).
	 */
	for (i = root->level; i > 0; --i) {
		for (b = opt_state->levels[i]; b != 0; b = b->link) {
			link_inedge(&b->et, JT(b));
			link_inedge(&b->ef, JF(b));
		}
	}
}

static void
opt_root(struct block **b)
{
	struct slist *tmp, *s;

	s = (*b)->stmts;
	(*b)->stmts = 0;
	while (BPF_CLASS((*b)->s.code) == BPF_JMP && JT(*b) == JF(*b))
		*b = JT(*b);

	tmp = (*b)->stmts;
	if (tmp != 0)
		sappend(s, tmp);
	(*b)->stmts = s;

	/*
	 * If the root node is a return, then there is no
	 * point executing any statements (since the bpf machine
	 * has no side effects).
	 */
	if (BPF_CLASS((*b)->s.code) == BPF_RET)
		(*b)->stmts = 0;
}

static void
opt_loop(compiler_state_t *cstate, opt_state_t *opt_state, struct icode *ic,
    int do_stmts)
{

#ifdef BDEBUG
	if (pcap_optimizer_debug > 1 || pcap_print_dot_graph) {
		printf("opt_loop(root, %d) begin\n", do_stmts);
		opt_dump(cstate, ic);
	}
#endif
	do {
		opt_state->done = 1;
		find_levels(opt_state, ic);
		find_dom(opt_state, ic->root);
		find_closure(opt_state, ic->root);
		find_ud(opt_state, ic->root);
		find_edom(opt_state, ic->root);
		opt_blks(cstate, opt_state, ic, do_stmts);
#ifdef BDEBUG
		if (pcap_optimizer_debug > 1 || pcap_print_dot_graph) {
			printf("opt_loop(root, %d) bottom, done=%d\n", do_stmts, opt_state->done);
			opt_dump(cstate, ic);
		}
#endif
	} while (!opt_state->done);
}

/*
 * Optimize the filter code in its dag representation.
 */
void
bpf_optimize(compiler_state_t *cstate, struct icode *ic)
{
	opt_state_t opt_state;

	opt_init(cstate, &opt_state, ic);
	opt_loop(cstate, &opt_state, ic, 0);
	opt_loop(cstate, &opt_state, ic, 1);
	intern_blocks(&opt_state, ic);
#ifdef BDEBUG
	if (pcap_optimizer_debug > 1 || pcap_print_dot_graph) {
		printf("after intern_blocks()\n");
		opt_dump(cstate, ic);
	}
#endif
	opt_root(&ic->root);
#ifdef BDEBUG
	if (pcap_optimizer_debug > 1 || pcap_print_dot_graph) {
		printf("after opt_root()\n");
		opt_dump(cstate, ic);
	}
#endif
	opt_cleanup(&opt_state);
}

static void
make_marks(struct icode *ic, struct block *p)
{
	if (!isMarked(ic, p)) {
		Mark(ic, p);
		if (BPF_CLASS(p->s.code) != BPF_RET) {
			make_marks(ic, JT(p));
			make_marks(ic, JF(p));
		}
	}
}

/*
 * Mark code array such that isMarked(ic->cur_mark, i) is true
 * only for nodes that are alive.
 */
static void
mark_code(struct icode *ic)
{
	ic->cur_mark += 1;
	make_marks(ic, ic->root);
}

/*
 * True iff the two stmt lists load the same value from the packet into
 * the accumulator.
 */
static int
eq_slist(struct slist *x, struct slist *y)
{
	for (;;) {
		while (x && x->s.code == NOP)
			x = x->next;
		while (y && y->s.code == NOP)
			y = y->next;
		if (x == 0)
			return y == 0;
		if (y == 0)
			return x == 0;
		if (x->s.code != y->s.code || x->s.k != y->s.k)
			return 0;
		x = x->next;
		y = y->next;
	}
}

static inline int
eq_blk(struct block *b0, struct block *b1)
{
	if (b0->s.code == b1->s.code &&
	    b0->s.k == b1->s.k &&
	    b0->et.succ == b1->et.succ &&
	    b0->ef.succ == b1->ef.succ)
		return eq_slist(b0->stmts, b1->stmts);
	return 0;
}

static void
intern_blocks(opt_state_t *opt_state, struct icode *ic)
{
	struct block *p;
	int i, j;
	int done1; /* don't shadow global */
 top:
	done1 = 1;
	for (i = 0; i < opt_state->n_blocks; ++i)
		opt_state->blocks[i]->link = 0;

	mark_code(ic);

	for (i = opt_state->n_blocks - 1; --i >= 0; ) {
		if (!isMarked(ic, opt_state->blocks[i]))
			continue;
		for (j = i + 1; j < opt_state->n_blocks; ++j) {
			if (!isMarked(ic, opt_state->blocks[j]))
				continue;
			if (eq_blk(opt_state->blocks[i], opt_state->blocks[j])) {
				opt_state->blocks[i]->link = opt_state->blocks[j]->link ?
					opt_state->blocks[j]->link : opt_state->blocks[j];
				break;
			}
		}
	}
	for (i = 0; i < opt_state->n_blocks; ++i) {
		p = opt_state->blocks[i];
		if (JT(p) == 0)
			continue;
		if (JT(p)->link) {
			done1 = 0;
			JT(p) = JT(p)->link;
		}
		if (JF(p)->link) {
			done1 = 0;
			JF(p) = JF(p)->link;
		}
	}
	if (!done1)
		goto top;
}

static void
opt_cleanup(opt_state_t *opt_state)
{
	free((void *)opt_state->vnode_base);
	free((void *)opt_state->vmap);
	free((void *)opt_state->edges);
	free((void *)opt_state->space);
	free((void *)opt_state->levels);
	free((void *)opt_state->blocks);
}

/*
 * Return the number of stmts in 's'.
 */
static u_int
slength(struct slist *s)
{
	u_int n = 0;

	for (; s; s = s->next)
		if (s->s.code != NOP)
			++n;
	return n;
}

/*
 * Return the number of nodes reachable by 'p'.
 * All nodes should be initially unmarked.
 */
static int
count_blocks(struct icode *ic, struct block *p)
{
	if (p == 0 || isMarked(ic, p))
		return 0;
	Mark(ic, p);
	return count_blocks(ic, JT(p)) + count_blocks(ic, JF(p)) + 1;
}

/*
 * Do a depth first search on the flow graph, numbering the
 * the basic blocks, and entering them into the 'blocks' array.`
 */
static void
number_blks_r(opt_state_t *opt_state, struct icode *ic, struct block *p)
{
	int n;

	if (p == 0 || isMarked(ic, p))
		return;

	Mark(ic, p);
	n = opt_state->n_blocks++;
	p->id = n;
	opt_state->blocks[n] = p;

	number_blks_r(opt_state, ic, JT(p));
	number_blks_r(opt_state, ic, JF(p));
}

/*
 * Return the number of stmts in the flowgraph reachable by 'p'.
 * The nodes should be unmarked before calling.
 *
 * Note that "stmts" means "instructions", and that this includes
 *
 *	side-effect statements in 'p' (slength(p->stmts));
 *
 *	statements in the true branch from 'p' (count_stmts(JT(p)));
 *
 *	statements in the false branch from 'p' (count_stmts(JF(p)));
 *
 *	the conditional jump itself (1);
 *
 *	an extra long jump if the true branch requires it (p->longjt);
 *
 *	an extra long jump if the false branch requires it (p->longjf).
 */
static u_int
count_stmts(struct icode *ic, struct block *p)
{
	u_int n;

	if (p == 0 || isMarked(ic, p))
		return 0;
	Mark(ic, p);
	n = count_stmts(ic, JT(p)) + count_stmts(ic, JF(p));
	return slength(p->stmts) + n + 1 + p->longjt + p->longjf;
}

/*
 * Allocate memory.  All allocation is done before optimization
 * is begun.  A linear bound on the size of all data structures is computed
 * from the total number of blocks and/or statements.
 */
static void
opt_init(compiler_state_t *cstate, opt_state_t *opt_state, struct icode *ic)
{
	bpf_u_int32 *p;
	int i, n, max_stmts;

	/*
	 * First, count the blocks, so we can malloc an array to map
	 * block number to block.  Then, put the blocks into the array.
	 */
	unMarkAll(ic);
	n = count_blocks(ic, ic->root);
	opt_state->blocks = (struct block **)calloc(n, sizeof(*opt_state->blocks));
	if (opt_state->blocks == NULL)
		bpf_error(cstate, "malloc");
	unMarkAll(ic);
	opt_state->n_blocks = 0;
	number_blks_r(opt_state, ic, ic->root);

	opt_state->n_edges = 2 * opt_state->n_blocks;
	opt_state->edges = (struct edge **)calloc(opt_state->n_edges, sizeof(*opt_state->edges));
	if (opt_state->edges == NULL)
		bpf_error(cstate, "malloc");

	/*
	 * The number of levels is bounded by the number of nodes.
	 */
	opt_state->levels = (struct block **)calloc(opt_state->n_blocks, sizeof(*opt_state->levels));
	if (opt_state->levels == NULL)
		bpf_error(cstate, "malloc");

	opt_state->edgewords = opt_state->n_edges / (8 * sizeof(bpf_u_int32)) + 1;
	opt_state->nodewords = opt_state->n_blocks / (8 * sizeof(bpf_u_int32)) + 1;

	/* XXX */
	opt_state->space = (bpf_u_int32 *)malloc(2 * opt_state->n_blocks * opt_state->nodewords * sizeof(*opt_state->space)
				 + opt_state->n_edges * opt_state->edgewords * sizeof(*opt_state->space));
	if (opt_state->space == NULL)
		bpf_error(cstate, "malloc");
	p = opt_state->space;
	opt_state->all_dom_sets = p;
	for (i = 0; i < n; ++i) {
		opt_state->blocks[i]->dom = p;
		p += opt_state->nodewords;
	}
	opt_state->all_closure_sets = p;
	for (i = 0; i < n; ++i) {
		opt_state->blocks[i]->closure = p;
		p += opt_state->nodewords;
	}
	opt_state->all_edge_sets = p;
	for (i = 0; i < n; ++i) {
		register struct block *b = opt_state->blocks[i];

		b->et.edom = p;
		p += opt_state->edgewords;
		b->ef.edom = p;
		p += opt_state->edgewords;
		b->et.id = i;
		opt_state->edges[i] = &b->et;
		b->ef.id = opt_state->n_blocks + i;
		opt_state->edges[opt_state->n_blocks + i] = &b->ef;
		b->et.pred = b;
		b->ef.pred = b;
	}
	max_stmts = 0;
	for (i = 0; i < n; ++i)
		max_stmts += slength(opt_state->blocks[i]->stmts) + 1;
	/*
	 * We allocate at most 3 value numbers per statement,
	 * so this is an upper bound on the number of valnodes
	 * we'll need.
	 */
	opt_state->maxval = 3 * max_stmts;
	opt_state->vmap = (struct vmapinfo *)calloc(opt_state->maxval, sizeof(*opt_state->vmap));
	opt_state->vnode_base = (struct valnode *)calloc(opt_state->maxval, sizeof(*opt_state->vnode_base));
	if (opt_state->vmap == NULL || opt_state->vnode_base == NULL)
		bpf_error(cstate, "malloc");
}

/*
 * This is only used when supporting optimizer debugging.  It is
 * global state, so do *not* do more than one compile in parallel
 * and expect it to provide meaningful information.
 */
#ifdef BDEBUG
int bids[NBIDS];
#endif

/*
 * Returns true if successful.  Returns false if a branch has
 * an offset that is too large.  If so, we have marked that
 * branch so that on a subsequent iteration, it will be treated
 * properly.
 */
static int
convert_code_r(compiler_state_t *cstate, conv_state_t *conv_state,
    struct icode *ic, struct block *p)
{
	struct bpf_insn *dst;
	struct slist *src;
	u_int slen;
	u_int off;
	u_int extrajmps;	/* number of extra jumps inserted */
	struct slist **offset = NULL;

	if (p == 0 || isMarked(ic, p))
		return (1);
	Mark(ic, p);

	if (convert_code_r(cstate, conv_state, ic, JF(p)) == 0)
		return (0);
	if (convert_code_r(cstate, conv_state, ic, JT(p)) == 0)
		return (0);

	slen = slength(p->stmts);
	dst = conv_state->ftail -= (slen + 1 + p->longjt + p->longjf);
		/* inflate length by any extra jumps */

	p->offset = (int)(dst - conv_state->fstart);

	/* generate offset[] for convenience  */
	if (slen) {
		offset = (struct slist **)calloc(slen, sizeof(struct slist *));
		if (!offset) {
			bpf_error(cstate, "not enough core");
			/*NOTREACHED*/
		}
	}
	src = p->stmts;
	for (off = 0; off < slen && src; off++) {
#if 0
		printf("off=%d src=%x\n", off, src);
#endif
		offset[off] = src;
		src = src->next;
	}

	off = 0;
	for (src = p->stmts; src; src = src->next) {
		if (src->s.code == NOP)
			continue;
		dst->code = (u_short)src->s.code;
		dst->k = src->s.k;

		/* fill block-local relative jump */
		if (BPF_CLASS(src->s.code) != BPF_JMP || src->s.code == (BPF_JMP|BPF_JA)) {
#if 0
			if (src->s.jt || src->s.jf) {
				bpf_error(cstate, "illegal jmp destination");
				/*NOTREACHED*/
			}
#endif
			goto filled;
		}
		if (off == slen - 2)	/*???*/
			goto filled;

	    {
		u_int i;
		int jt, jf;
		const char ljerr[] = "%s for block-local relative jump: off=%d";

#if 0
		printf("code=%x off=%d %x %x\n", src->s.code,
			off, src->s.jt, src->s.jf);
#endif

		if (!src->s.jt || !src->s.jf) {
			bpf_error(cstate, ljerr, "no jmp destination", off);
			/*NOTREACHED*/
		}

		jt = jf = 0;
		for (i = 0; i < slen; i++) {
			if (offset[i] == src->s.jt) {
				if (jt) {
					bpf_error(cstate, ljerr, "multiple matches", off);
					/*NOTREACHED*/
				}

				if (i - off - 1 >= 256) {
					bpf_error(cstate, ljerr, "out-of-range jump", off);
					/*NOTREACHED*/
				}
				dst->jt = (u_char)(i - off - 1);
				jt++;
			}
			if (offset[i] == src->s.jf) {
				if (jf) {
					bpf_error(cstate, ljerr, "multiple matches", off);
					/*NOTREACHED*/
				}
				if (i - off - 1 >= 256) {
					bpf_error(cstate, ljerr, "out-of-range jump", off);
					/*NOTREACHED*/
				}
				dst->jf = (u_char)(i - off - 1);
				jf++;
			}
		}
		if (!jt || !jf) {
			bpf_error(cstate, ljerr, "no destination found", off);
			/*NOTREACHED*/
		}
	    }
filled:
		++dst;
		++off;
	}
	if (offset)
		free(offset);

#ifdef BDEBUG
	if (dst - conv_state->fstart < NBIDS)
		bids[dst - conv_state->fstart] = p->id + 1;
#endif
	dst->code = (u_short)p->s.code;
	dst->k = p->s.k;
	if (JT(p)) {
		extrajmps = 0;
		off = JT(p)->offset - (p->offset + slen) - 1;
		if (off >= 256) {
		    /* offset too large for branch, must add a jump */
		    if (p->longjt == 0) {
		    	/* mark this instruction and retry */
			p->longjt++;
			return(0);
		    }
		    /* branch if T to following jump */
		    if (extrajmps >= 256) {
			bpf_error(cstate, "too many extra jumps");
			/*NOTREACHED*/
		    }
		    dst->jt = (u_char)extrajmps;
		    extrajmps++;
		    dst[extrajmps].code = BPF_JMP|BPF_JA;
		    dst[extrajmps].k = off - extrajmps;
		}
		else
		    dst->jt = (u_char)off;
		off = JF(p)->offset - (p->offset + slen) - 1;
		if (off >= 256) {
		    /* offset too large for branch, must add a jump */
		    if (p->longjf == 0) {
		    	/* mark this instruction and retry */
			p->longjf++;
			return(0);
		    }
		    /* branch if F to following jump */
		    /* if two jumps are inserted, F goes to second one */
		    if (extrajmps >= 256) {
			bpf_error(cstate, "too many extra jumps");
			/*NOTREACHED*/
		    }
		    dst->jf = (u_char)extrajmps;
		    extrajmps++;
		    dst[extrajmps].code = BPF_JMP|BPF_JA;
		    dst[extrajmps].k = off - extrajmps;
		}
		else
		    dst->jf = (u_char)off;
	}
	return (1);
}


/*
 * Convert flowgraph intermediate representation to the
 * BPF array representation.  Set *lenp to the number of instructions.
 *
 * This routine does *NOT* leak the memory pointed to by fp.  It *must
 * not* do free(fp) before returning fp; doing so would make no sense,
 * as the BPF array pointed to by the return value of icode_to_fcode()
 * must be valid - it's being returned for use in a bpf_program structure.
 *
 * If it appears that icode_to_fcode() is leaking, the problem is that
 * the program using pcap_compile() is failing to free the memory in
 * the BPF program when it's done - the leak is in the program, not in
 * the routine that happens to be allocating the memory.  (By analogy, if
 * a program calls fopen() without ever calling fclose() on the FILE *,
 * it will leak the FILE structure; the leak is not in fopen(), it's in
 * the program.)  Change the program to use pcap_freecode() when it's
 * done with the filter program.  See the pcap man page.
 */
struct bpf_insn *
icode_to_fcode(compiler_state_t *cstate, struct icode *ic,
    struct block *root, u_int *lenp)
{
	u_int n;
	struct bpf_insn *fp;
	conv_state_t conv_state;

	/*
	 * Loop doing convert_code_r() until no branches remain
	 * with too-large offsets.
	 */
	for (;;) {
	    unMarkAll(ic);
	    n = *lenp = count_stmts(ic, root);

	    fp = (struct bpf_insn *)malloc(sizeof(*fp) * n);
	    if (fp == NULL)
		    bpf_error(cstate, "malloc");
	    memset((char *)fp, 0, sizeof(*fp) * n);
	    conv_state.fstart = fp;
	    conv_state.ftail = fp + n;

	    unMarkAll(ic);
	    if (convert_code_r(cstate, &conv_state, ic, root))
		break;
	    free(fp);
	}

	return fp;
}

/*
 * Make a copy of a BPF program and put it in the "fcode" member of
 * a "pcap_t".
 *
 * If we fail to allocate memory for the copy, fill in the "errbuf"
 * member of the "pcap_t" with an error message, and return -1;
 * otherwise, return 0.
 */
int
install_bpf_program(pcap_t *p, struct bpf_program *fp)
{
	size_t prog_size;

	/*
	 * Validate the program.
	 */
	if (!bpf_validate(fp->bf_insns, fp->bf_len)) {
		pcap_snprintf(p->errbuf, sizeof(p->errbuf),
			"BPF program is not valid");
		return (-1);
	}

	/*
	 * Free up any already installed program.
	 */
	pcap_freecode(&p->fcode);

	prog_size = sizeof(*fp->bf_insns) * fp->bf_len;
	p->fcode.bf_len = fp->bf_len;
	p->fcode.bf_insns = (struct bpf_insn *)malloc(prog_size);
	if (p->fcode.bf_insns == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "malloc");
		return (-1);
	}
	memcpy(p->fcode.bf_insns, fp->bf_insns, prog_size);
	return (0);
}

#ifdef BDEBUG
static void
dot_dump_node(struct icode *ic, struct block *block, struct bpf_program *prog,
    FILE *out)
{
	int icount, noffset;
	int i;

	if (block == NULL || isMarked(ic, block))
		return;
	Mark(ic, block);

	icount = slength(block->stmts) + 1 + block->longjt + block->longjf;
	noffset = min(block->offset + icount, (int)prog->bf_len);

	fprintf(out, "\tblock%d [shape=ellipse, id=\"block-%d\" label=\"BLOCK%d\\n", block->id, block->id, block->id);
	for (i = block->offset; i < noffset; i++) {
		fprintf(out, "\\n%s", bpf_image(prog->bf_insns + i, i));
	}
	fprintf(out, "\" tooltip=\"");
	for (i = 0; i < BPF_MEMWORDS; i++)
		if (block->val[i] != VAL_UNKNOWN)
			fprintf(out, "val[%d]=%d ", i, block->val[i]);
	fprintf(out, "val[A]=%d ", block->val[A_ATOM]);
	fprintf(out, "val[X]=%d", block->val[X_ATOM]);
	fprintf(out, "\"");
	if (JT(block) == NULL)
		fprintf(out, ", peripheries=2");
	fprintf(out, "];\n");

	dot_dump_node(ic, JT(block), prog, out);
	dot_dump_node(ic, JF(block), prog, out);
}

static void
dot_dump_edge(struct icode *ic, struct block *block, FILE *out)
{
	if (block == NULL || isMarked(ic, block))
		return;
	Mark(ic, block);

	if (JT(block)) {
		fprintf(out, "\t\"block%d\":se -> \"block%d\":n [label=\"T\"]; \n",
				block->id, JT(block)->id);
		fprintf(out, "\t\"block%d\":sw -> \"block%d\":n [label=\"F\"]; \n",
			   block->id, JF(block)->id);
	}
	dot_dump_edge(ic, JT(block), out);
	dot_dump_edge(ic, JF(block), out);
}

/* Output the block CFG using graphviz/DOT language
 * In the CFG, block's code, value index for each registers at EXIT,
 * and the jump relationship is show.
 *
 * example DOT for BPF `ip src host 1.1.1.1' is:
    digraph BPF {
    	block0 [shape=ellipse, id="block-0" label="BLOCK0\n\n(000) ldh      [12]\n(001) jeq      #0x800           jt 2	jf 5" tooltip="val[A]=0 val[X]=0"];
    	block1 [shape=ellipse, id="block-1" label="BLOCK1\n\n(002) ld       [26]\n(003) jeq      #0x1010101       jt 4	jf 5" tooltip="val[A]=0 val[X]=0"];
    	block2 [shape=ellipse, id="block-2" label="BLOCK2\n\n(004) ret      #68" tooltip="val[A]=0 val[X]=0", peripheries=2];
    	block3 [shape=ellipse, id="block-3" label="BLOCK3\n\n(005) ret      #0" tooltip="val[A]=0 val[X]=0", peripheries=2];
    	"block0":se -> "block1":n [label="T"];
    	"block0":sw -> "block3":n [label="F"];
    	"block1":se -> "block2":n [label="T"];
    	"block1":sw -> "block3":n [label="F"];
    }
 *
 *  After install graphviz on http://www.graphviz.org/, save it as bpf.dot
 *  and run `dot -Tpng -O bpf.dot' to draw the graph.
 */
static void
dot_dump(compiler_state_t *cstate, struct icode *ic)
{
	struct bpf_program f;
	FILE *out = stdout;

	memset(bids, 0, sizeof bids);
	f.bf_insns = icode_to_fcode(cstate, ic, ic->root, &f.bf_len);

	fprintf(out, "digraph BPF {\n");
	unMarkAll(ic);
	dot_dump_node(ic, ic->root, &f, out);
	unMarkAll(ic);
	dot_dump_edge(ic, ic->root, out);
	fprintf(out, "}\n");

	free((char *)f.bf_insns);
}

static void
plain_dump(compiler_state_t *cstate, struct icode *ic)
{
	struct bpf_program f;

	memset(bids, 0, sizeof bids);
	f.bf_insns = icode_to_fcode(cstate, ic, ic->root, &f.bf_len);
	bpf_dump(&f, 1);
	putchar('\n');
	free((char *)f.bf_insns);
}

static void
opt_dump(compiler_state_t *cstate, struct icode *ic)
{
	/*
	 * If the CFG, in DOT format, is requested, output it rather than
	 * the code that would be generated from that graph.
	 */
	if (pcap_print_dot_graph)
		dot_dump(cstate, ic);
	else
		plain_dump(cstate, ic);
}
#endif

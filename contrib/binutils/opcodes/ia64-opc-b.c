/* ia64-opc-b.c -- IA-64 `B' opcode table.
   Copyright 1998, 1999, 2000, 2002, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

   This file is part of GDB, GAS, and the GNU binutils.

   GDB, GAS, and the GNU binutils are free software; you can redistribute
   them and/or modify them under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version
   2, or (at your option) any later version.

   GDB, GAS, and the GNU binutils are distributed in the hope that they
   will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this file; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "ia64-opc.h"

#define B0	IA64_TYPE_B, 0
#define B	IA64_TYPE_B, 1

/* instruction bit fields: */
#define bBtype(x)	(((ia64_insn) ((x) & 0x7)) << 6)
#define bD(x)		(((ia64_insn) ((x) & 0x1)) << 35)
#define bIh(x)		(((ia64_insn) ((x) & 0x1)) << 35)
#define bPa(x)		(((ia64_insn) ((x) & 0x1)) << 12)
#define bPr(x)		(((ia64_insn) ((x) & 0x3f)) << 0)
#define bWha(x)		(((ia64_insn) ((x) & 0x3)) << 33)
#define bWhb(x)		(((ia64_insn) ((x) & 0x3)) << 3)
#define bWhc(x)		(((ia64_insn) ((x) & 0x7)) << 32)
#define bX6(x)		(((ia64_insn) ((x) & 0x3f)) << 27)

#define mBtype		bBtype (-1)
#define mD		bD (-1)
#define mIh		bIh (-1)
#define mPa		bPa (-1)
#define mPr		bPr (-1)
#define mWha		bWha (-1)
#define mWhb		bWhb (-1)
#define mWhc		bWhc (-1)
#define mX6		bX6 (-1)

#define OpX6(a,b) 	(bOp (a) | bX6 (b)), (mOp | mX6)
#define OpPaWhaD(a,b,c,d) \
	(bOp (a) | bPa (b) | bWha (c) | bD (d)), (mOp | mPa | mWha | mD)
#define OpPaWhcD(a,b,c,d) \
	(bOp (a) | bPa (b) | bWhc (c) | bD (d)), (mOp | mPa | mWhc | mD)
#define OpBtypePaWhaD(a,b,c,d,e) \
	(bOp (a) | bBtype (b) | bPa (c) | bWha (d) | bD (e)), \
	(mOp | mBtype | mPa | mWha | mD)
#define OpBtypePaWhaDPr(a,b,c,d,e,f) \
	(bOp (a) | bBtype (b) | bPa (c) | bWha (d) | bD (e) | bPr (f)), \
	(mOp | mBtype | mPa | mWha | mD | mPr)
#define OpX6BtypePaWhaD(a,b,c,d,e,f) \
	(bOp (a) | bX6 (b) | bBtype (c) | bPa (d) | bWha (e) | bD (f)), \
	(mOp | mX6 | mBtype | mPa | mWha | mD)
#define OpX6BtypePaWhaDPr(a,b,c,d,e,f,g) \
   (bOp (a) | bX6 (b) | bBtype (c) | bPa (d) | bWha (e) | bD (f) | bPr (g)), \
	(mOp | mX6 | mBtype | mPa | mWha | mD | mPr)
#define OpIhWhb(a,b,c) \
	(bOp (a) | bIh (b) | bWhb (c)), \
	(mOp | mIh | mWhb)
#define OpX6IhWhb(a,b,c,d) \
	(bOp (a) | bX6 (b) | bIh (c) | bWhb (d)), \
	(mOp | mX6 | mIh | mWhb)

/* Used to initialise unused fields in ia64_opcode struct,
   in order to stop gcc from complaining.  */
#define EMPTY 0,0,NULL

struct ia64_opcode ia64_opcodes_b[] =
  {
    /* B-type instruction encodings (sorted according to major opcode) */

#define BR(a,b) \
      B0, OpX6BtypePaWhaDPr (0, 0x20, 0, a, 0, b, 0), {B2}, PSEUDO, 0, NULL
    {"br.few",		BR (0, 0)},
    {"br",		BR (0, 0)},
    {"br.few.clr",	BR (0, 1)},
    {"br.clr",		BR (0, 1)},
    {"br.many",		BR (1, 0)},
    {"br.many.clr",	BR (1, 1)},
#undef BR

#define BR(a,b,c,d,e)	B0, OpX6BtypePaWhaD (0, a, b, c, d, e), {B2}, EMPTY
#define BRP(a,b,c,d,e)	B0, OpX6BtypePaWhaD (0, a, b, c, d, e), {B2}, PSEUDO, 0, NULL
#define BRT(a,b,c,d,e,f) B0, OpX6BtypePaWhaD (0, a, b, c, d, e), {B2}, f, 0, NULL
    {"br.cond.sptk.few",	BR (0x20, 0, 0, 0, 0)},
    {"br.cond.sptk",		BRP (0x20, 0, 0, 0, 0)},
    {"br.cond.sptk.few.clr",	BR (0x20, 0, 0, 0, 1)},
    {"br.cond.sptk.clr",	BRP (0x20, 0, 0, 0, 1)},
    {"br.cond.spnt.few",	BR (0x20, 0, 0, 1, 0)},
    {"br.cond.spnt",		BRP (0x20, 0, 0, 1, 0)},
    {"br.cond.spnt.few.clr",	BR (0x20, 0, 0, 1, 1)},
    {"br.cond.spnt.clr",	BRP (0x20, 0, 0, 1, 1)},
    {"br.cond.dptk.few",	BR (0x20, 0, 0, 2, 0)},
    {"br.cond.dptk",		BRP (0x20, 0, 0, 2, 0)},
    {"br.cond.dptk.few.clr",	BR (0x20, 0, 0, 2, 1)},
    {"br.cond.dptk.clr",	BRP (0x20, 0, 0, 2, 1)},
    {"br.cond.dpnt.few",	BR (0x20, 0, 0, 3, 0)},
    {"br.cond.dpnt",		BRP (0x20, 0, 0, 3, 0)},
    {"br.cond.dpnt.few.clr",	BR (0x20, 0, 0, 3, 1)},
    {"br.cond.dpnt.clr",	BRP (0x20, 0, 0, 3, 1)},
    {"br.cond.sptk.many",	BR (0x20, 0, 1, 0, 0)},
    {"br.cond.sptk.many.clr",	BR (0x20, 0, 1, 0, 1)},
    {"br.cond.spnt.many",	BR (0x20, 0, 1, 1, 0)},
    {"br.cond.spnt.many.clr",	BR (0x20, 0, 1, 1, 1)},
    {"br.cond.dptk.many",	BR (0x20, 0, 1, 2, 0)},
    {"br.cond.dptk.many.clr",	BR (0x20, 0, 1, 2, 1)},
    {"br.cond.dpnt.many",	BR (0x20, 0, 1, 3, 0)},
    {"br.cond.dpnt.many.clr",	BR (0x20, 0, 1, 3, 1)},
    {"br.sptk.few",		BR (0x20, 0, 0, 0, 0)},
    {"br.sptk",			BRP (0x20, 0, 0, 0, 0)},
    {"br.sptk.few.clr",		BR (0x20, 0, 0, 0, 1)},
    {"br.sptk.clr",		BRP (0x20, 0, 0, 0, 1)},
    {"br.spnt.few",		BR (0x20, 0, 0, 1, 0)},
    {"br.spnt",			BRP (0x20, 0, 0, 1, 0)},
    {"br.spnt.few.clr",		BR (0x20, 0, 0, 1, 1)},
    {"br.spnt.clr",		BRP (0x20, 0, 0, 1, 1)},
    {"br.dptk.few",		BR (0x20, 0, 0, 2, 0)},
    {"br.dptk",			BRP (0x20, 0, 0, 2, 0)},
    {"br.dptk.few.clr",		BR (0x20, 0, 0, 2, 1)},
    {"br.dptk.clr",		BRP (0x20, 0, 0, 2, 1)},
    {"br.dpnt.few",		BR (0x20, 0, 0, 3, 0)},
    {"br.dpnt",			BRP (0x20, 0, 0, 3, 0)},
    {"br.dpnt.few.clr",		BR (0x20, 0, 0, 3, 1)},
    {"br.dpnt.clr",		BRP (0x20, 0, 0, 3, 1)},
    {"br.sptk.many",		BR (0x20, 0, 1, 0, 0)},
    {"br.sptk.many.clr",	BR (0x20, 0, 1, 0, 1)},
    {"br.spnt.many",		BR (0x20, 0, 1, 1, 0)},
    {"br.spnt.many.clr",	BR (0x20, 0, 1, 1, 1)},
    {"br.dptk.many",		BR (0x20, 0, 1, 2, 0)},
    {"br.dptk.many.clr",	BR (0x20, 0, 1, 2, 1)},
    {"br.dpnt.many",		BR (0x20, 0, 1, 3, 0)},
    {"br.dpnt.many.clr",	BR (0x20, 0, 1, 3, 1)},
    {"br.ia.sptk.few",		BR (0x20, 1, 0, 0, 0)},
    {"br.ia.sptk",		BRP (0x20, 1, 0, 0, 0)},
    {"br.ia.sptk.few.clr",	BR (0x20, 1, 0, 0, 1)},
    {"br.ia.sptk.clr",		BRP (0x20, 1, 0, 0, 1)},
    {"br.ia.spnt.few",		BR (0x20, 1, 0, 1, 0)},
    {"br.ia.spnt",		BRP (0x20, 1, 0, 1, 0)},
    {"br.ia.spnt.few.clr",	BR (0x20, 1, 0, 1, 1)},
    {"br.ia.spnt.clr",		BRP (0x20, 1, 0, 1, 1)},
    {"br.ia.dptk.few",		BR (0x20, 1, 0, 2, 0)},
    {"br.ia.dptk",		BRP (0x20, 1, 0, 2, 0)},
    {"br.ia.dptk.few.clr",	BR (0x20, 1, 0, 2, 1)},
    {"br.ia.dptk.clr",		BRP (0x20, 1, 0, 2, 1)},
    {"br.ia.dpnt.few",		BR (0x20, 1, 0, 3, 0)},
    {"br.ia.dpnt",		BRP (0x20, 1, 0, 3, 0)},
    {"br.ia.dpnt.few.clr",	BR (0x20, 1, 0, 3, 1)},
    {"br.ia.dpnt.clr",		BRP (0x20, 1, 0, 3, 1)},
    {"br.ia.sptk.many",		BR (0x20, 1, 1, 0, 0)},
    {"br.ia.sptk.many.clr",	BR (0x20, 1, 1, 0, 1)},
    {"br.ia.spnt.many",		BR (0x20, 1, 1, 1, 0)},
    {"br.ia.spnt.many.clr",	BR (0x20, 1, 1, 1, 1)},
    {"br.ia.dptk.many",		BR (0x20, 1, 1, 2, 0)},
    {"br.ia.dptk.many.clr",	BR (0x20, 1, 1, 2, 1)},
    {"br.ia.dpnt.many",		BR (0x20, 1, 1, 3, 0)},
    {"br.ia.dpnt.many.clr",	BR (0x20, 1, 1, 3, 1)},
    {"br.ret.sptk.few",		BRT (0x21, 4, 0, 0, 0, MOD_RRBS)},
    {"br.ret.sptk",		BRT (0x21, 4, 0, 0, 0, PSEUDO | MOD_RRBS)},
    {"br.ret.sptk.few.clr",	BRT (0x21, 4, 0, 0, 1, MOD_RRBS)},
    {"br.ret.sptk.clr",		BRT (0x21, 4, 0, 0, 1, PSEUDO | MOD_RRBS)},
    {"br.ret.spnt.few",		BRT (0x21, 4, 0, 1, 0, MOD_RRBS)},
    {"br.ret.spnt",		BRT (0x21, 4, 0, 1, 0, PSEUDO | MOD_RRBS)},
    {"br.ret.spnt.few.clr",	BRT (0x21, 4, 0, 1, 1, MOD_RRBS)},
    {"br.ret.spnt.clr",		BRT (0x21, 4, 0, 1, 1, PSEUDO | MOD_RRBS)},
    {"br.ret.dptk.few",		BRT (0x21, 4, 0, 2, 0, MOD_RRBS)},
    {"br.ret.dptk",		BRT (0x21, 4, 0, 2, 0, PSEUDO | MOD_RRBS)},
    {"br.ret.dptk.few.clr",	BRT (0x21, 4, 0, 2, 1, MOD_RRBS)},
    {"br.ret.dptk.clr",		BRT (0x21, 4, 0, 2, 1, PSEUDO | MOD_RRBS)},
    {"br.ret.dpnt.few",		BRT (0x21, 4, 0, 3, 0, MOD_RRBS)},
    {"br.ret.dpnt",		BRT (0x21, 4, 0, 3, 0, PSEUDO | MOD_RRBS)},
    {"br.ret.dpnt.few.clr",	BRT (0x21, 4, 0, 3, 1, MOD_RRBS)},
    {"br.ret.dpnt.clr",		BRT (0x21, 4, 0, 3, 1, PSEUDO | MOD_RRBS)},
    {"br.ret.sptk.many",	BRT (0x21, 4, 1, 0, 0, MOD_RRBS)},
    {"br.ret.sptk.many.clr",	BRT (0x21, 4, 1, 0, 1, MOD_RRBS)},
    {"br.ret.spnt.many",	BRT (0x21, 4, 1, 1, 0, MOD_RRBS)},
    {"br.ret.spnt.many.clr",	BRT (0x21, 4, 1, 1, 1, MOD_RRBS)},
    {"br.ret.dptk.many",	BRT (0x21, 4, 1, 2, 0, MOD_RRBS)},
    {"br.ret.dptk.many.clr",	BRT (0x21, 4, 1, 2, 1, MOD_RRBS)},
    {"br.ret.dpnt.many",	BRT (0x21, 4, 1, 3, 0, MOD_RRBS)},
    {"br.ret.dpnt.many.clr",	BRT (0x21, 4, 1, 3, 1, MOD_RRBS)},
#undef BR
#undef BRP
#undef BRT

    {"cover",		B0, OpX6 (0, 0x02), {0, }, NO_PRED | LAST | MOD_RRBS, 0, NULL},
    {"clrrrb",		B0, OpX6 (0, 0x04), {0, }, NO_PRED | LAST | MOD_RRBS, 0, NULL},
    {"clrrrb.pr",	B0, OpX6 (0, 0x05), {0, }, NO_PRED | LAST | MOD_RRBS, 0, NULL},
    {"rfi",		B0, OpX6 (0, 0x08), {0, }, NO_PRED | LAST | PRIV | MOD_RRBS, 0, NULL},
    {"bsw.0",		B0, OpX6 (0, 0x0c), {0, }, NO_PRED | LAST | PRIV, 0, NULL},
    {"bsw.1",		B0, OpX6 (0, 0x0d), {0, }, NO_PRED | LAST | PRIV, 0, NULL},
    {"epc",		B0, OpX6 (0, 0x10), {0, }, NO_PRED, 0, NULL},
    {"vmsw.0",		B0, OpX6 (0, 0x18), {0, }, NO_PRED | PRIV, 0, NULL},
    {"vmsw.1",		B0, OpX6 (0, 0x19), {0, }, NO_PRED | PRIV, 0, NULL},

    {"break.b",		B0, OpX6 (0, 0x00), {IMMU21}, EMPTY},

    {"br.call.sptk.few",	B, OpPaWhcD (1, 0, 1, 0), {B1, B2}, EMPTY},
    {"br.call.sptk",		B, OpPaWhcD (1, 0, 1, 0), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.sptk.few.clr",	B, OpPaWhcD (1, 0, 1, 1), {B1, B2}, EMPTY},
    {"br.call.sptk.clr",	B, OpPaWhcD (1, 0, 1, 1), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.spnt.few",	B, OpPaWhcD (1, 0, 3, 0), {B1, B2}, EMPTY},
    {"br.call.spnt",		B, OpPaWhcD (1, 0, 3, 0), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.spnt.few.clr",	B, OpPaWhcD (1, 0, 3, 1), {B1, B2}, EMPTY},
    {"br.call.spnt.clr",	B, OpPaWhcD (1, 0, 3, 1), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.dptk.few",	B, OpPaWhcD (1, 0, 5, 0), {B1, B2}, EMPTY},
    {"br.call.dptk",		B, OpPaWhcD (1, 0, 5, 0), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.dptk.few.clr",	B, OpPaWhcD (1, 0, 5, 1), {B1, B2}, EMPTY},
    {"br.call.dptk.clr",	B, OpPaWhcD (1, 0, 5, 1), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.dpnt.few",	B, OpPaWhcD (1, 0, 7, 0), {B1, B2}, EMPTY},
    {"br.call.dpnt",		B, OpPaWhcD (1, 0, 7, 0), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.dpnt.few.clr",	B, OpPaWhcD (1, 0, 7, 1), {B1, B2}, EMPTY},
    {"br.call.dpnt.clr",	B, OpPaWhcD (1, 0, 7, 1), {B1, B2}, PSEUDO, 0, NULL},
    {"br.call.sptk.many",	B, OpPaWhcD (1, 1, 1, 0), {B1, B2}, EMPTY},
    {"br.call.sptk.many.clr",	B, OpPaWhcD (1, 1, 1, 1), {B1, B2}, EMPTY},
    {"br.call.spnt.many",	B, OpPaWhcD (1, 1, 3, 0), {B1, B2}, EMPTY},
    {"br.call.spnt.many.clr",	B, OpPaWhcD (1, 1, 3, 1), {B1, B2}, EMPTY},
    {"br.call.dptk.many",	B, OpPaWhcD (1, 1, 5, 0), {B1, B2}, EMPTY},
    {"br.call.dptk.many.clr",	B, OpPaWhcD (1, 1, 5, 1), {B1, B2}, EMPTY},
    {"br.call.dpnt.many",	B, OpPaWhcD (1, 1, 7, 0), {B1, B2}, EMPTY},
    {"br.call.dpnt.many.clr",	B, OpPaWhcD (1, 1, 7, 1), {B1, B2}, EMPTY},

#define BRP(a,b,c) \
      B0, OpX6IhWhb (2, a, b, c), {B2, TAG13}, NO_PRED, 0, NULL
    {"brp.sptk",		BRP (0x10, 0, 0)},
    {"brp.dptk",		BRP (0x10, 0, 2)},
    {"brp.sptk.imp",		BRP (0x10, 1, 0)},
    {"brp.dptk.imp",		BRP (0x10, 1, 2)},
    {"brp.ret.sptk",		BRP (0x11, 0, 0)},
    {"brp.ret.dptk",		BRP (0x11, 0, 2)},
    {"brp.ret.sptk.imp",	BRP (0x11, 1, 0)},
    {"brp.ret.dptk.imp",	BRP (0x11, 1, 2)},
#undef BRP

    {"nop.b",		B0, OpX6 (2, 0x00), {IMMU21}, EMPTY},
    {"hint.b",		B0, OpX6 (2, 0x01), {IMMU21}, EMPTY},

#define BR(a,b) \
      B0, OpBtypePaWhaDPr (4, 0, a, 0, b, 0), {TGT25c}, PSEUDO, 0, NULL
    {"br.few",		BR (0, 0)},
    {"br",		BR (0, 0)},
    {"br.few.clr",	BR (0, 1)},
    {"br.clr",		BR (0, 1)},
    {"br.many",		BR (1, 0)},
    {"br.many.clr",	BR (1, 1)},
#undef BR

#define BR(a,b,c) \
      B0, OpBtypePaWhaD (4, 0, a, b, c), {TGT25c}, EMPTY
#define BRP(a,b,c) \
      B0, OpBtypePaWhaD (4, 0, a, b, c), {TGT25c}, PSEUDO, 0, NULL
    {"br.cond.sptk.few",	BR (0, 0, 0)},
    {"br.cond.sptk",		BRP (0, 0, 0)},
    {"br.cond.sptk.few.clr",	BR (0, 0, 1)},
    {"br.cond.sptk.clr",	BRP (0, 0, 1)},
    {"br.cond.spnt.few",	BR (0, 1, 0)},
    {"br.cond.spnt",		BRP (0, 1, 0)},
    {"br.cond.spnt.few.clr",	BR (0, 1, 1)},
    {"br.cond.spnt.clr",	BRP (0, 1, 1)},
    {"br.cond.dptk.few",	BR (0, 2, 0)},
    {"br.cond.dptk",		BRP (0, 2, 0)},
    {"br.cond.dptk.few.clr",	BR (0, 2, 1)},
    {"br.cond.dptk.clr",	BRP (0, 2, 1)},
    {"br.cond.dpnt.few",	BR (0, 3, 0)},
    {"br.cond.dpnt",		BRP (0, 3, 0)},
    {"br.cond.dpnt.few.clr",	BR (0, 3, 1)},
    {"br.cond.dpnt.clr",	BRP (0, 3, 1)},
    {"br.cond.sptk.many",	BR (1, 0, 0)},
    {"br.cond.sptk.many.clr",	BR (1, 0, 1)},
    {"br.cond.spnt.many",	BR (1, 1, 0)},
    {"br.cond.spnt.many.clr",	BR (1, 1, 1)},
    {"br.cond.dptk.many",	BR (1, 2, 0)},
    {"br.cond.dptk.many.clr",	BR (1, 2, 1)},
    {"br.cond.dpnt.many",	BR (1, 3, 0)},
    {"br.cond.dpnt.many.clr",	BR (1, 3, 1)},
    {"br.sptk.few",		BR (0, 0, 0)},
    {"br.sptk",			BRP (0, 0, 0)},
    {"br.sptk.few.clr",		BR (0, 0, 1)},
    {"br.sptk.clr",		BRP (0, 0, 1)},
    {"br.spnt.few",		BR (0, 1, 0)},
    {"br.spnt",			BRP (0, 1, 0)},
    {"br.spnt.few.clr",		BR (0, 1, 1)},
    {"br.spnt.clr",		BRP (0, 1, 1)},
    {"br.dptk.few",		BR (0, 2, 0)},
    {"br.dptk",			BRP (0, 2, 0)},
    {"br.dptk.few.clr",		BR (0, 2, 1)},
    {"br.dptk.clr",		BRP (0, 2, 1)},
    {"br.dpnt.few",		BR (0, 3, 0)},
    {"br.dpnt",			BRP (0, 3, 0)},
    {"br.dpnt.few.clr",		BR (0, 3, 1)},
    {"br.dpnt.clr",		BRP (0, 3, 1)},
    {"br.sptk.many",		BR (1, 0, 0)},
    {"br.sptk.many.clr",	BR (1, 0, 1)},
    {"br.spnt.many",		BR (1, 1, 0)},
    {"br.spnt.many.clr",	BR (1, 1, 1)},
    {"br.dptk.many",		BR (1, 2, 0)},
    {"br.dptk.many.clr",	BR (1, 2, 1)},
    {"br.dpnt.many",		BR (1, 3, 0)},
    {"br.dpnt.many.clr",	BR (1, 3, 1)},
#undef BR
#undef BRP

#define BR(a,b,c,d, e) \
	B0, OpBtypePaWhaD (4, a, b, c, d), {TGT25c}, SLOT2 | e, 0, NULL
    {"br.wexit.sptk.few",	BR (2, 0, 0, 0, MOD_RRBS)},
    {"br.wexit.sptk",		BR (2, 0, 0, 0, PSEUDO | MOD_RRBS)},
    {"br.wexit.sptk.few.clr",	BR (2, 0, 0, 1, MOD_RRBS)},
    {"br.wexit.sptk.clr",	BR (2, 0, 0, 1, PSEUDO | MOD_RRBS)},
    {"br.wexit.spnt.few",	BR (2, 0, 1, 0, MOD_RRBS)},
    {"br.wexit.spnt",		BR (2, 0, 1, 0, PSEUDO | MOD_RRBS)},
    {"br.wexit.spnt.few.clr",	BR (2, 0, 1, 1, MOD_RRBS)},
    {"br.wexit.spnt.clr",	BR (2, 0, 1, 1, PSEUDO | MOD_RRBS)},
    {"br.wexit.dptk.few",	BR (2, 0, 2, 0, MOD_RRBS)},
    {"br.wexit.dptk",		BR (2, 0, 2, 0, PSEUDO | MOD_RRBS)},
    {"br.wexit.dptk.few.clr",	BR (2, 0, 2, 1, MOD_RRBS)},
    {"br.wexit.dptk.clr",	BR (2, 0, 2, 1, PSEUDO | MOD_RRBS)},
    {"br.wexit.dpnt.few",	BR (2, 0, 3, 0, MOD_RRBS)},
    {"br.wexit.dpnt",		BR (2, 0, 3, 0, PSEUDO | MOD_RRBS)},
    {"br.wexit.dpnt.few.clr",	BR (2, 0, 3, 1, MOD_RRBS)},
    {"br.wexit.dpnt.clr",	BR (2, 0, 3, 1, PSEUDO | MOD_RRBS)},
    {"br.wexit.sptk.many",	BR (2, 1, 0, 0, MOD_RRBS)},
    {"br.wexit.sptk.many.clr",	BR (2, 1, 0, 1, MOD_RRBS)},
    {"br.wexit.spnt.many",	BR (2, 1, 1, 0, MOD_RRBS)},
    {"br.wexit.spnt.many.clr",	BR (2, 1, 1, 1, MOD_RRBS)},
    {"br.wexit.dptk.many",	BR (2, 1, 2, 0, MOD_RRBS)},
    {"br.wexit.dptk.many.clr",	BR (2, 1, 2, 1, MOD_RRBS)},
    {"br.wexit.dpnt.many",	BR (2, 1, 3, 0, MOD_RRBS)},
    {"br.wexit.dpnt.many.clr",	BR (2, 1, 3, 1, MOD_RRBS)},
    {"br.wtop.sptk.few",	BR (3, 0, 0, 0, MOD_RRBS)},
    {"br.wtop.sptk",		BR (3, 0, 0, 0, PSEUDO | MOD_RRBS)},
    {"br.wtop.sptk.few.clr",	BR (3, 0, 0, 1, MOD_RRBS)},
    {"br.wtop.sptk.clr",	BR (3, 0, 0, 1, PSEUDO | MOD_RRBS)},
    {"br.wtop.spnt.few",	BR (3, 0, 1, 0, MOD_RRBS)},
    {"br.wtop.spnt",		BR (3, 0, 1, 0, PSEUDO | MOD_RRBS)},
    {"br.wtop.spnt.few.clr",	BR (3, 0, 1, 1, MOD_RRBS)},
    {"br.wtop.spnt.clr",	BR (3, 0, 1, 1, PSEUDO | MOD_RRBS)},
    {"br.wtop.dptk.few",	BR (3, 0, 2, 0, MOD_RRBS)},
    {"br.wtop.dptk",		BR (3, 0, 2, 0, PSEUDO | MOD_RRBS)},
    {"br.wtop.dptk.few.clr",	BR (3, 0, 2, 1, MOD_RRBS)},
    {"br.wtop.dptk.clr",	BR (3, 0, 2, 1, PSEUDO | MOD_RRBS)},
    {"br.wtop.dpnt.few",	BR (3, 0, 3, 0, MOD_RRBS)},
    {"br.wtop.dpnt",		BR (3, 0, 3, 0, PSEUDO | MOD_RRBS)},
    {"br.wtop.dpnt.few.clr",	BR (3, 0, 3, 1, MOD_RRBS)},
    {"br.wtop.dpnt.clr",	BR (3, 0, 3, 1, PSEUDO | MOD_RRBS)},
    {"br.wtop.sptk.many",	BR (3, 1, 0, 0, MOD_RRBS)},
    {"br.wtop.sptk.many.clr",	BR (3, 1, 0, 1, MOD_RRBS)},
    {"br.wtop.spnt.many",	BR (3, 1, 1, 0, MOD_RRBS)},
    {"br.wtop.spnt.many.clr",	BR (3, 1, 1, 1, MOD_RRBS)},
    {"br.wtop.dptk.many",	BR (3, 1, 2, 0, MOD_RRBS)},
    {"br.wtop.dptk.many.clr",	BR (3, 1, 2, 1, MOD_RRBS)},
    {"br.wtop.dpnt.many",	BR (3, 1, 3, 0, MOD_RRBS)},
    {"br.wtop.dpnt.many.clr",	BR (3, 1, 3, 1, MOD_RRBS)},

#undef BR
#define BR(a,b,c,d) \
	B0, OpBtypePaWhaD (4, a, b, c, d), {TGT25c}, SLOT2 | NO_PRED, 0, NULL
#define BRT(a,b,c,d,e) \
	B0, OpBtypePaWhaD (4, a, b, c, d), {TGT25c}, SLOT2 | NO_PRED | e, 0, NULL
    {"br.cloop.sptk.few",	BR (5, 0, 0, 0)},
    {"br.cloop.sptk",		BRT (5, 0, 0, 0, PSEUDO)},
    {"br.cloop.sptk.few.clr",	BR (5, 0, 0, 1)},
    {"br.cloop.sptk.clr",	BRT (5, 0, 0, 1, PSEUDO)},
    {"br.cloop.spnt.few",	BR (5, 0, 1, 0)},
    {"br.cloop.spnt",		BRT (5, 0, 1, 0, PSEUDO)},
    {"br.cloop.spnt.few.clr",	BR (5, 0, 1, 1)},
    {"br.cloop.spnt.clr",	BRT (5, 0, 1, 1, PSEUDO)},
    {"br.cloop.dptk.few",	BR (5, 0, 2, 0)},
    {"br.cloop.dptk",		BRT (5, 0, 2, 0, PSEUDO)},
    {"br.cloop.dptk.few.clr",	BR (5, 0, 2, 1)},
    {"br.cloop.dptk.clr",	BRT (5, 0, 2, 1, PSEUDO)},
    {"br.cloop.dpnt.few",	BR (5, 0, 3, 0)},
    {"br.cloop.dpnt",		BRT (5, 0, 3, 0, PSEUDO)},
    {"br.cloop.dpnt.few.clr",	BR (5, 0, 3, 1)},
    {"br.cloop.dpnt.clr",	BRT (5, 0, 3, 1, PSEUDO)},
    {"br.cloop.sptk.many",	BR (5, 1, 0, 0)},
    {"br.cloop.sptk.many.clr",	BR (5, 1, 0, 1)},
    {"br.cloop.spnt.many",	BR (5, 1, 1, 0)},
    {"br.cloop.spnt.many.clr",	BR (5, 1, 1, 1)},
    {"br.cloop.dptk.many",	BR (5, 1, 2, 0)},
    {"br.cloop.dptk.many.clr",	BR (5, 1, 2, 1)},
    {"br.cloop.dpnt.many",	BR (5, 1, 3, 0)},
    {"br.cloop.dpnt.many.clr",	BR (5, 1, 3, 1)},
    {"br.cexit.sptk.few",	BRT (6, 0, 0, 0, MOD_RRBS)},
    {"br.cexit.sptk",		BRT (6, 0, 0, 0, PSEUDO | MOD_RRBS)},
    {"br.cexit.sptk.few.clr",	BRT (6, 0, 0, 1, MOD_RRBS)},
    {"br.cexit.sptk.clr",	BRT (6, 0, 0, 1, PSEUDO | MOD_RRBS)},
    {"br.cexit.spnt.few",	BRT (6, 0, 1, 0, MOD_RRBS)},
    {"br.cexit.spnt",		BRT (6, 0, 1, 0, PSEUDO | MOD_RRBS)},
    {"br.cexit.spnt.few.clr",	BRT (6, 0, 1, 1, MOD_RRBS)},
    {"br.cexit.spnt.clr",	BRT (6, 0, 1, 1, PSEUDO | MOD_RRBS)},
    {"br.cexit.dptk.few",	BRT (6, 0, 2, 0, MOD_RRBS)},
    {"br.cexit.dptk",		BRT (6, 0, 2, 0, PSEUDO | MOD_RRBS)},
    {"br.cexit.dptk.few.clr",	BRT (6, 0, 2, 1, MOD_RRBS)},
    {"br.cexit.dptk.clr",	BRT (6, 0, 2, 1, PSEUDO | MOD_RRBS)},
    {"br.cexit.dpnt.few",	BRT (6, 0, 3, 0, MOD_RRBS)},
    {"br.cexit.dpnt",		BRT (6, 0, 3, 0, PSEUDO | MOD_RRBS)},
    {"br.cexit.dpnt.few.clr",	BRT (6, 0, 3, 1, MOD_RRBS)},
    {"br.cexit.dpnt.clr",	BRT (6, 0, 3, 1, PSEUDO | MOD_RRBS)},
    {"br.cexit.sptk.many",	BRT (6, 1, 0, 0, MOD_RRBS)},
    {"br.cexit.sptk.many.clr",	BRT (6, 1, 0, 1, MOD_RRBS)},
    {"br.cexit.spnt.many",	BRT (6, 1, 1, 0, MOD_RRBS)},
    {"br.cexit.spnt.many.clr",	BRT (6, 1, 1, 1, MOD_RRBS)},
    {"br.cexit.dptk.many",	BRT (6, 1, 2, 0, MOD_RRBS)},
    {"br.cexit.dptk.many.clr",	BRT (6, 1, 2, 1, MOD_RRBS)},
    {"br.cexit.dpnt.many",	BRT (6, 1, 3, 0, MOD_RRBS)},
    {"br.cexit.dpnt.many.clr",	BRT (6, 1, 3, 1, MOD_RRBS)},
    {"br.ctop.sptk.few",	BRT (7, 0, 0, 0, MOD_RRBS)},
    {"br.ctop.sptk",		BRT (7, 0, 0, 0, PSEUDO | MOD_RRBS)},
    {"br.ctop.sptk.few.clr",	BRT (7, 0, 0, 1, MOD_RRBS)},
    {"br.ctop.sptk.clr",	BRT (7, 0, 0, 1, PSEUDO | MOD_RRBS)},
    {"br.ctop.spnt.few",	BRT (7, 0, 1, 0, MOD_RRBS)},
    {"br.ctop.spnt",		BRT (7, 0, 1, 0, PSEUDO | MOD_RRBS)},
    {"br.ctop.spnt.few.clr",	BRT (7, 0, 1, 1, MOD_RRBS)},
    {"br.ctop.spnt.clr",	BRT (7, 0, 1, 1, PSEUDO | MOD_RRBS)},
    {"br.ctop.dptk.few",	BRT (7, 0, 2, 0, MOD_RRBS)},
    {"br.ctop.dptk",		BRT (7, 0, 2, 0, PSEUDO | MOD_RRBS)},
    {"br.ctop.dptk.few.clr",	BRT (7, 0, 2, 1, MOD_RRBS)},
    {"br.ctop.dptk.clr",	BRT (7, 0, 2, 1, PSEUDO | MOD_RRBS)},
    {"br.ctop.dpnt.few",	BRT (7, 0, 3, 0, MOD_RRBS)},
    {"br.ctop.dpnt",		BRT (7, 0, 3, 0, PSEUDO | MOD_RRBS)},
    {"br.ctop.dpnt.few.clr",	BRT (7, 0, 3, 1, MOD_RRBS)},
    {"br.ctop.dpnt.clr",	BRT (7, 0, 3, 1, PSEUDO | MOD_RRBS)},
    {"br.ctop.sptk.many",	BRT (7, 1, 0, 0, MOD_RRBS)},
    {"br.ctop.sptk.many.clr",	BRT (7, 1, 0, 1, MOD_RRBS)},
    {"br.ctop.spnt.many",	BRT (7, 1, 1, 0, MOD_RRBS)},
    {"br.ctop.spnt.many.clr",	BRT (7, 1, 1, 1, MOD_RRBS)},
    {"br.ctop.dptk.many",	BRT (7, 1, 2, 0, MOD_RRBS)},
    {"br.ctop.dptk.many.clr",	BRT (7, 1, 2, 1, MOD_RRBS)},
    {"br.ctop.dpnt.many",	BRT (7, 1, 3, 0, MOD_RRBS)},
    {"br.ctop.dpnt.many.clr",	BRT (7, 1, 3, 1, MOD_RRBS)},
#undef BR
#undef BRT

    {"br.call.sptk.few",	B, OpPaWhaD (5, 0, 0, 0), {B1, TGT25c}, EMPTY},
    {"br.call.sptk",		B, OpPaWhaD (5, 0, 0, 0), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.sptk.few.clr",	B, OpPaWhaD (5, 0, 0, 1), {B1, TGT25c}, EMPTY},
    {"br.call.sptk.clr",	B, OpPaWhaD (5, 0, 0, 1), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.spnt.few",	B, OpPaWhaD (5, 0, 1, 0), {B1, TGT25c}, EMPTY},
    {"br.call.spnt",		B, OpPaWhaD (5, 0, 1, 0), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.spnt.few.clr",	B, OpPaWhaD (5, 0, 1, 1), {B1, TGT25c}, EMPTY},
    {"br.call.spnt.clr",	B, OpPaWhaD (5, 0, 1, 1), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.dptk.few",	B, OpPaWhaD (5, 0, 2, 0), {B1, TGT25c}, EMPTY},
    {"br.call.dptk",		B, OpPaWhaD (5, 0, 2, 0), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.dptk.few.clr",	B, OpPaWhaD (5, 0, 2, 1), {B1, TGT25c}, EMPTY},
    {"br.call.dptk.clr",	B, OpPaWhaD (5, 0, 2, 1), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.dpnt.few",	B, OpPaWhaD (5, 0, 3, 0), {B1, TGT25c}, EMPTY},
    {"br.call.dpnt",		B, OpPaWhaD (5, 0, 3, 0), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.dpnt.few.clr",	B, OpPaWhaD (5, 0, 3, 1), {B1, TGT25c}, EMPTY},
    {"br.call.dpnt.clr",	B, OpPaWhaD (5, 0, 3, 1), {B1, TGT25c}, PSEUDO, 0, NULL},
    {"br.call.sptk.many",	B, OpPaWhaD (5, 1, 0, 0), {B1, TGT25c}, EMPTY},
    {"br.call.sptk.many.clr",	B, OpPaWhaD (5, 1, 0, 1), {B1, TGT25c}, EMPTY},
    {"br.call.spnt.many",	B, OpPaWhaD (5, 1, 1, 0), {B1, TGT25c}, EMPTY},
    {"br.call.spnt.many.clr",	B, OpPaWhaD (5, 1, 1, 1), {B1, TGT25c}, EMPTY},
    {"br.call.dptk.many",	B, OpPaWhaD (5, 1, 2, 0), {B1, TGT25c}, EMPTY},
    {"br.call.dptk.many.clr",	B, OpPaWhaD (5, 1, 2, 1), {B1, TGT25c}, EMPTY},
    {"br.call.dpnt.many",	B, OpPaWhaD (5, 1, 3, 0), {B1, TGT25c}, EMPTY},
    {"br.call.dpnt.many.clr",	B, OpPaWhaD (5, 1, 3, 1), {B1, TGT25c}, EMPTY},

    /* Branch predict.  */
#define BRP(a,b) \
      B0, OpIhWhb (7, a, b), {TGT25c, TAG13}, NO_PRED, 0, NULL
    {"brp.sptk",		BRP (0, 0)},
    {"brp.loop",		BRP (0, 1)},
    {"brp.dptk",		BRP (0, 2)},
    {"brp.exit",		BRP (0, 3)},
    {"brp.sptk.imp",		BRP (1, 0)},
    {"brp.loop.imp",		BRP (1, 1)},
    {"brp.dptk.imp",		BRP (1, 2)},
    {"brp.exit.imp",		BRP (1, 3)},
#undef BRP

    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };

#undef B0
#undef B
#undef bBtype
#undef bD
#undef bIh
#undef bPa
#undef bPr
#undef bWha
#undef bWhb
#undef bWhc
#undef bX6
#undef mBtype
#undef mD
#undef mIh
#undef mPa
#undef mPr
#undef mWha
#undef mWhb
#undef mWhc
#undef mX6
#undef OpX6
#undef OpPaWhaD
#undef OpPaWhcD
#undef OpBtypePaWhaD
#undef OpBtypePaWhaDPr
#undef OpX6BtypePaWhaD
#undef OpX6BtypePaWhaDPr
#undef OpIhWhb
#undef OpX6IhWhb
#undef EMPTY

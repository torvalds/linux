/* ia64-opc-x.c -- IA-64 `X' opcode table.
   Copyright 1998, 1999, 2000, 2002 Free Software Foundation, Inc.
   Contributed by Timothy Wall <twall@cygnus.com>

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

/* Identify the specific X-unit type.  */
#define X0      IA64_TYPE_X, 0
#define X	IA64_TYPE_X, 1

/* Instruction bit fields:  */
#define bBtype(x)	(((ia64_insn) ((x) & 0x7)) << 6)
#define bD(x)		(((ia64_insn) ((x) & 0x1)) << 35)
#define bPa(x)		(((ia64_insn) ((x) & 0x1)) << 12)
#define bPr(x)		(((ia64_insn) ((x) & 0x3f)) << 0)
#define bVc(x)		(((ia64_insn) ((x) & 0x1)) << 20)
#define bWha(x)		(((ia64_insn) ((x) & 0x3)) << 33)
#define bX3(x)		(((ia64_insn) ((x) & 0x7)) << 33)
#define bX6(x)		(((ia64_insn) ((x) & 0x3f)) << 27)
#define bY(x)		(((ia64_insn) ((x) & 0x1)) << 26)

#define mBtype		bBtype (-1)
#define mD		bD (-1)
#define mPa		bPa (-1)
#define mPr		bPr (-1)
#define mVc             bVc (-1)
#define mWha		bWha (-1)
#define mX3             bX3 (-1)
#define mX6		bX6 (-1)
#define mY		bY (-1)

#define OpX3X6(a,b,c)		(bOp (a) | bX3 (b) | bX6(c)), \
				(mOp | mX3 | mX6)
#define OpX3X6Y(a,b,c,d)	(bOp (a) | bX3 (b) | bX6(c) | bY(d)), \
				(mOp | mX3 | mX6 | mY)
#define OpVc(a,b)		(bOp (a) | bVc (b)), (mOp | mVc)
#define OpPaWhaD(a,b,c,d) \
	(bOp (a) | bPa (b) | bWha (c) | bD (d)), (mOp | mPa | mWha | mD)
#define OpBtypePaWhaD(a,b,c,d,e) \
	(bOp (a) | bBtype (b) | bPa (c) | bWha (d) | bD (e)), \
	(mOp | mBtype | mPa | mWha | mD)
#define OpBtypePaWhaDPr(a,b,c,d,e,f) \
	(bOp (a) | bBtype (b) | bPa (c) | bWha (d) | bD (e) | bPr (f)), \
	(mOp | mBtype | mPa | mWha | mD | mPr)

struct ia64_opcode ia64_opcodes_x[] =
  {
    {"break.x",	X0, OpX3X6 (0, 0, 0x00), {IMMU62}, 0, 0, NULL},
    {"nop.x",	X0, OpX3X6Y (0, 0, 0x01, 0), {IMMU62}, 0, 0, NULL},
    {"hint.x",	X0, OpX3X6Y (0, 0, 0x01, 1), {IMMU62}, 0, 0, NULL},
    {"movl",	X,  OpVc (6, 0), {R1, IMMU64}, 0, 0, NULL},
#define BRL(a,b) \
      X0, OpBtypePaWhaDPr (0xC, 0, a, 0, b, 0), {TGT64}, PSEUDO, 0, NULL
    {"brl.few",         BRL (0, 0)},
    {"brl",             BRL (0, 0)},
    {"brl.few.clr",	BRL (0, 1)},
    {"brl.clr",		BRL (0, 1)},
    {"brl.many",	BRL (1, 0)},
    {"brl.many.clr",	BRL (1, 1)},
#undef BRL
#define BRL(a,b,c) \
      X0, OpBtypePaWhaD (0xC, 0, a, b, c), {TGT64}, 0, 0, NULL
#define BRLP(a,b,c) \
      X0, OpBtypePaWhaD (0xC, 0, a, b, c), {TGT64}, PSEUDO, 0, NULL
    {"brl.cond.sptk.few",	BRL (0, 0, 0)},
    {"brl.cond.sptk",		BRLP (0, 0, 0)},
    {"brl.cond.sptk.few.clr",	BRL (0, 0, 1)},
    {"brl.cond.sptk.clr",	BRLP (0, 0, 1)},
    {"brl.cond.spnt.few",	BRL (0, 1, 0)},
    {"brl.cond.spnt",		BRLP (0, 1, 0)},
    {"brl.cond.spnt.few.clr",	BRL (0, 1, 1)},
    {"brl.cond.spnt.clr",	BRLP (0, 1, 1)},
    {"brl.cond.dptk.few",	BRL (0, 2, 0)},
    {"brl.cond.dptk",		BRLP (0, 2, 0)},
    {"brl.cond.dptk.few.clr",	BRL (0, 2, 1)},
    {"brl.cond.dptk.clr",	BRLP (0, 2, 1)},
    {"brl.cond.dpnt.few",	BRL (0, 3, 0)},
    {"brl.cond.dpnt",		BRLP (0, 3, 0)},
    {"brl.cond.dpnt.few.clr",	BRL (0, 3, 1)},
    {"brl.cond.dpnt.clr",	BRLP (0, 3, 1)},
    {"brl.cond.sptk.many",	BRL (1, 0, 0)},
    {"brl.cond.sptk.many.clr",	BRL (1, 0, 1)},
    {"brl.cond.spnt.many",	BRL (1, 1, 0)},
    {"brl.cond.spnt.many.clr",	BRL (1, 1, 1)},
    {"brl.cond.dptk.many",	BRL (1, 2, 0)},
    {"brl.cond.dptk.many.clr",	BRL (1, 2, 1)},
    {"brl.cond.dpnt.many",	BRL (1, 3, 0)},
    {"brl.cond.dpnt.many.clr",	BRL (1, 3, 1)},
    {"brl.sptk.few",		BRL (0, 0, 0)},
    {"brl.sptk",		BRLP (0, 0, 0)},
    {"brl.sptk.few.clr",	BRL (0, 0, 1)},
    {"brl.sptk.clr",		BRLP (0, 0, 1)},
    {"brl.spnt.few",		BRL (0, 1, 0)},
    {"brl.spnt",		BRLP (0, 1, 0)},
    {"brl.spnt.few.clr",	BRL (0, 1, 1)},
    {"brl.spnt.clr",		BRLP (0, 1, 1)},
    {"brl.dptk.few",		BRL (0, 2, 0)},
    {"brl.dptk",		BRLP (0, 2, 0)},
    {"brl.dptk.few.clr",	BRL (0, 2, 1)},
    {"brl.dptk.clr",		BRLP (0, 2, 1)},
    {"brl.dpnt.few",		BRL (0, 3, 0)},
    {"brl.dpnt",		BRLP (0, 3, 0)},
    {"brl.dpnt.few.clr",	BRL (0, 3, 1)},
    {"brl.dpnt.clr",		BRLP (0, 3, 1)},
    {"brl.sptk.many",		BRL (1, 0, 0)},
    {"brl.sptk.many.clr",	BRL (1, 0, 1)},
    {"brl.spnt.many",		BRL (1, 1, 0)},
    {"brl.spnt.many.clr",	BRL (1, 1, 1)},
    {"brl.dptk.many",		BRL (1, 2, 0)},
    {"brl.dptk.many.clr",	BRL (1, 2, 1)},
    {"brl.dpnt.many",		BRL (1, 3, 0)},
    {"brl.dpnt.many.clr",	BRL (1, 3, 1)},
#undef BRL
#undef BRLP
#define BRL(a,b,c) X, OpPaWhaD (0xD, a, b, c), {B1, TGT64}, 0, 0, NULL
#define BRLP(a,b,c) X, OpPaWhaD (0xD, a, b, c), {B1, TGT64}, PSEUDO, 0, NULL
    {"brl.call.sptk.few",	BRL (0, 0, 0)},
    {"brl.call.sptk",		BRLP (0, 0, 0)},
    {"brl.call.sptk.few.clr",	BRL (0, 0, 1)},
    {"brl.call.sptk.clr",	BRLP (0, 0, 1)},
    {"brl.call.spnt.few",	BRL (0, 1, 0)},
    {"brl.call.spnt",		BRLP (0, 1, 0)},
    {"brl.call.spnt.few.clr",	BRL (0, 1, 1)},
    {"brl.call.spnt.clr",	BRLP (0, 1, 1)},
    {"brl.call.dptk.few",	BRL (0, 2, 0)},
    {"brl.call.dptk",		BRLP (0, 2, 0)},
    {"brl.call.dptk.few.clr",	BRL (0, 2, 1)},
    {"brl.call.dptk.clr",	BRLP (0, 2, 1)},
    {"brl.call.dpnt.few",	BRL (0, 3, 0)},
    {"brl.call.dpnt",		BRLP (0, 3, 0)},
    {"brl.call.dpnt.few.clr",	BRL (0, 3, 1)},
    {"brl.call.dpnt.clr",	BRLP (0, 3, 1)},
    {"brl.call.sptk.many",	BRL (1, 0, 0)},
    {"brl.call.sptk.many.clr",	BRL (1, 0, 1)},
    {"brl.call.spnt.many",	BRL (1, 1, 0)},
    {"brl.call.spnt.many.clr",	BRL (1, 1, 1)},
    {"brl.call.dptk.many",	BRL (1, 2, 0)},
    {"brl.call.dptk.many.clr",	BRL (1, 2, 1)},
    {"brl.call.dpnt.many",	BRL (1, 3, 0)},
    {"brl.call.dpnt.many.clr",	BRL (1, 3, 1)},
#undef BRL
#undef BRLP
    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };

#undef X0
#undef X

#undef bBtype
#undef bD
#undef bPa
#undef bPr
#undef bVc
#undef bWha
#undef bX3
#undef bX6

#undef mBtype
#undef mD
#undef mPa
#undef mPr
#undef mVc
#undef mWha
#undef mX3
#undef mX6

#undef OpX3X6
#undef OpVc
#undef OpPaWhaD
#undef OpBtypePaWhaD
#undef OpBtypePaWhaDPr

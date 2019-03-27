/* ia64-opc-a.c -- IA-64 `A' opcode table.
   Copyright 1998, 1999, 2000, 2001, 2002, 2004
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

#define A	IA64_TYPE_A, 1
#define A2	IA64_TYPE_A, 2

/* instruction bit fields: */
#define bC(x)		(((ia64_insn) ((x) & 0x1)) << 12)
#define bImm14(x)	((((ia64_insn) (((x) >>  0) & 0x7f)) << 13) | \
			 (((ia64_insn) (((x) >>  7) & 0x3f)) << 27) | \
			 (((ia64_insn) (((x) >> 13) & 0x01)) << 36))
#define bR3a(x)		(((ia64_insn) ((x) & 0x7f)) << 20)
#define bR3b(x)		(((ia64_insn) ((x) & 0x3)) << 20)
#define bTa(x)		(((ia64_insn) ((x) & 0x1)) << 33)
#define bTb(x)		(((ia64_insn) ((x) & 0x1)) << 36)
#define bVe(x)		(((ia64_insn) ((x) & 0x1)) << 33)
#define bX(x)		(((ia64_insn) ((x) & 0x1)) << 33)
#define bX2(x)		(((ia64_insn) ((x) & 0x3)) << 34)
#define bX2a(x)		(((ia64_insn) ((x) & 0x3)) << 34)
#define bX2b(x)		(((ia64_insn) ((x) & 0x3)) << 27)
#define bX4(x)		(((ia64_insn) ((x) & 0xf)) << 29)
#define bZa(x)		(((ia64_insn) ((x) & 0x1)) << 36)
#define bZb(x)		(((ia64_insn) ((x) & 0x1)) << 33)

/* instruction bit masks: */
#define mC	bC (-1)
#define mImm14	bImm14 (-1)
#define mR3a	bR3a (-1)
#define mR3b	bR3b (-1)
#define mTa	bTa (-1)
#define mTb	bTb (-1)
#define mVe	bVe (-1)
#define mX	bX (-1)
#define mX2	bX2 (-1)
#define mX2a	bX2a (-1)
#define mX2b	bX2b (-1)
#define mX4	bX4 (-1)
#define mZa	bZa (-1)
#define mZb	bZb (-1)

#define OpR3b(a,b)		(bOp (a) | bR3b (b)), (mOp | mR3b)
#define OpX2aVe(a,b,c)		(bOp (a) | bX2a (b) | bVe (c)), \
				(mOp | mX2a | mVe)
#define OpX2aVeR3a(a,b,c,d)	(bOp (a) | bX2a (b) | bVe (c) | bR3a (d)), \
				(mOp | mX2a | mVe | mR3a)
#define OpX2aVeImm14(a,b,c,d)	(bOp (a) | bX2a (b) | bVe (c) | bImm14 (d)), \
				(mOp | mX2a | mVe | mImm14)
#define OpX2aVeX4(a,b,c,d)	(bOp (a) | bX2a (b) | bVe (c) | bX4 (d)), \
				(mOp | mX2a | mVe | mX4)
#define OpX2aVeX4X2b(a,b,c,d,e)	\
	(bOp (a) | bX2a (b) | bVe (c) | bX4 (d) | bX2b (e)), \
	(mOp | mX2a | mVe | mX4 | mX2b)
#define OpX2TbTaC(a,b,c,d,e) \
	(bOp (a) | bX2 (b) | bTb (c) | bTa (d) | bC (e)), \
	(mOp | mX2 | mTb | mTa | mC)
#define OpX2TaC(a,b,c,d)	(bOp (a) | bX2 (b) | bTa (c) | bC (d)), \
				(mOp | mX2 | mTa | mC)
#define OpX2aZaZbX4(a,b,c,d,e) \
	(bOp (a) | bX2a (b) | bZa (c) | bZb (d) | bX4 (e)), \
	(mOp | mX2a | mZa | mZb | mX4)
#define OpX2aZaZbX4X2b(a,b,c,d,e,f) \
	(bOp (a) | bX2a (b) | bZa (c) | bZb (d) | bX4 (e) | bX2b (f)), \
	(mOp | mX2a | mZa | mZb | mX4 | mX2b)

/* Used to initialise unused fields in ia64_opcode struct,
   in order to stop gcc from complaining.  */
#define EMPTY 0,0,NULL

struct ia64_opcode ia64_opcodes_a[] =
  {
    /* A-type instruction encodings (sorted according to major opcode).  */

    {"add",	 A, OpX2aVeX4X2b (8, 0, 0, 0, 0), {R1, R2, R3}, EMPTY},
    {"add",	 A, OpX2aVeX4X2b (8, 0, 0, 0, 1), {R1, R2, R3, C1}, EMPTY},
    {"sub",	 A, OpX2aVeX4X2b (8, 0, 0, 1, 1), {R1, R2, R3}, EMPTY},
    {"sub",	 A, OpX2aVeX4X2b (8, 0, 0, 1, 0), {R1, R2, R3, C1}, EMPTY},
    {"addp4",	 A, OpX2aVeX4X2b (8, 0, 0, 2, 0), {R1, R2, R3}, EMPTY},
    {"and",	 A, OpX2aVeX4X2b (8, 0, 0, 3, 0), {R1, R2, R3}, EMPTY},
    {"andcm",	 A, OpX2aVeX4X2b (8, 0, 0, 3, 1), {R1, R2, R3}, EMPTY},
    {"or",	 A, OpX2aVeX4X2b (8, 0, 0, 3, 2), {R1, R2, R3}, EMPTY},
    {"xor",	 A, OpX2aVeX4X2b (8, 0, 0, 3, 3), {R1, R2, R3}, EMPTY},
    {"shladd",	 A, OpX2aVeX4 (8, 0, 0, 4), {R1, R2, CNT2a, R3}, EMPTY},
    {"shladdp4", A, OpX2aVeX4 (8, 0, 0, 6), {R1, R2, CNT2a, R3}, EMPTY},
    {"sub",	 A, OpX2aVeX4X2b (8, 0, 0, 9, 1), {R1, IMM8, R3}, EMPTY},
    {"and",	 A, OpX2aVeX4X2b (8, 0, 0, 0xb, 0), {R1, IMM8, R3}, EMPTY},
    {"andcm",	 A, OpX2aVeX4X2b (8, 0, 0, 0xb, 1), {R1, IMM8, R3}, EMPTY},
    {"or",	 A, OpX2aVeX4X2b (8, 0, 0, 0xb, 2), {R1, IMM8, R3}, EMPTY},
    {"xor",	 A, OpX2aVeX4X2b (8, 0, 0, 0xb, 3), {R1, IMM8, R3}, EMPTY},
    {"mov",	 A, OpX2aVeImm14 (8, 2, 0, 0), {R1, R3}, EMPTY},
    /* A mov immediate pseudo for adds was deleted.  It failed for immediate
       operands requiring relocs, e.g. @pltoff(a).  */
    {"adds",	 A, OpX2aVe (8, 2, 0), {R1, IMM14, R3}, EMPTY},
    {"addp4",	 A, OpX2aVe (8, 3, 0), {R1, IMM14, R3}, EMPTY},
    {"padd1",		 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 0, 0), {R1, R2, R3}, EMPTY},
    {"padd2",		 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 0, 0), {R1, R2, R3}, EMPTY},
    {"padd4",		 A, OpX2aZaZbX4X2b (8, 1, 1, 0, 0, 0), {R1, R2, R3}, EMPTY},
    {"padd1.sss",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 0, 1), {R1, R2, R3}, EMPTY},
    {"padd2.sss",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 0, 1), {R1, R2, R3}, EMPTY},
    {"padd1.uuu",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 0, 2), {R1, R2, R3}, EMPTY},
    {"padd2.uuu",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 0, 2), {R1, R2, R3}, EMPTY},
    {"padd1.uus",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 0, 3), {R1, R2, R3}, EMPTY},
    {"padd2.uus",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 0, 3), {R1, R2, R3}, EMPTY},
    {"psub1",		 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 1, 0), {R1, R2, R3}, EMPTY},
    {"psub2",		 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 1, 0), {R1, R2, R3}, EMPTY},
    {"psub4",		 A, OpX2aZaZbX4X2b (8, 1, 1, 0, 1, 0), {R1, R2, R3}, EMPTY},
    {"psub1.sss",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 1, 1), {R1, R2, R3}, EMPTY},
    {"psub2.sss",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 1, 1), {R1, R2, R3}, EMPTY},
    {"psub1.uuu",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 1, 2), {R1, R2, R3}, EMPTY},
    {"psub2.uuu",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 1, 2), {R1, R2, R3}, EMPTY},
    {"psub1.uus",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 1, 3), {R1, R2, R3}, EMPTY},
    {"psub2.uus",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 1, 3), {R1, R2, R3}, EMPTY},
    {"pavg1",		 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 2, 2), {R1, R2, R3}, EMPTY},
    {"pavg2",		 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 2, 2), {R1, R2, R3}, EMPTY},
    {"pavg1.raz",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 2, 3), {R1, R2, R3}, EMPTY},
    {"pavg2.raz",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 2, 3), {R1, R2, R3}, EMPTY},
    {"pavgsub1",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 3, 2), {R1, R2, R3}, EMPTY},
    {"pavgsub2",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 3, 2), {R1, R2, R3}, EMPTY},
    {"pcmp1.eq",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 9, 0), {R1, R2, R3}, EMPTY},
    {"pcmp2.eq",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 9, 0), {R1, R2, R3}, EMPTY},
    {"pcmp4.eq",	 A, OpX2aZaZbX4X2b (8, 1, 1, 0, 9, 0), {R1, R2, R3}, EMPTY},
    {"pcmp1.gt",	 A, OpX2aZaZbX4X2b (8, 1, 0, 0, 9, 1), {R1, R2, R3}, EMPTY},
    {"pcmp2.gt",	 A, OpX2aZaZbX4X2b (8, 1, 0, 1, 9, 1), {R1, R2, R3}, EMPTY},
    {"pcmp4.gt",	 A, OpX2aZaZbX4X2b (8, 1, 1, 0, 9, 1), {R1, R2, R3}, EMPTY},
    {"pshladd2",	 A, OpX2aZaZbX4 (8, 1, 0, 1, 4), {R1, R2, CNT2b, R3}, EMPTY},
    {"pshradd2",	 A, OpX2aZaZbX4 (8, 1, 0, 1, 6), {R1, R2, CNT2b, R3}, EMPTY},

    {"mov",		 A, OpR3b (9, 0), {R1, IMM22}, PSEUDO, 0, NULL},
    {"addl",		 A, Op    (9),	  {R1, IMM22, R3_2}, EMPTY},

    {"cmp.lt",		 A2, OpX2TbTaC (0xc, 0, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.le",		 A2, OpX2TbTaC (0xc, 0, 0, 0, 0), {P2, P1, R3, R2}, EMPTY},
    {"cmp.gt",		 A2, OpX2TbTaC (0xc, 0, 0, 0, 0), {P1, P2, R3, R2}, EMPTY},
    {"cmp.ge",		 A2, OpX2TbTaC (0xc, 0, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp.lt.unc",	 A2, OpX2TbTaC (0xc, 0, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.le.unc",	 A2, OpX2TbTaC (0xc, 0, 0, 0, 1), {P2, P1, R3, R2}, EMPTY},
    {"cmp.gt.unc",	 A2, OpX2TbTaC (0xc, 0, 0, 0, 1), {P1, P2, R3, R2}, EMPTY},
    {"cmp.ge.unc",	 A2, OpX2TbTaC (0xc, 0, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp.eq.and",	 A2, OpX2TbTaC (0xc, 0, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.ne.andcm",	 A2, OpX2TbTaC (0xc, 0, 0, 1, 0), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.and",	 A2, OpX2TbTaC (0xc, 0, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.eq.andcm",	 A2, OpX2TbTaC (0xc, 0, 0, 1, 1), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.lt",		 A2, OpX2TbTaC (0xc, 1, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.le",		 A2, OpX2TbTaC (0xc, 1, 0, 0, 0), {P2, P1, R3, R2}, EMPTY},
    {"cmp4.gt",		 A2, OpX2TbTaC (0xc, 1, 0, 0, 0), {P1, P2, R3, R2}, EMPTY},
    {"cmp4.ge",		 A2, OpX2TbTaC (0xc, 1, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.lt.unc",	 A2, OpX2TbTaC (0xc, 1, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.le.unc",	 A2, OpX2TbTaC (0xc, 1, 0, 0, 1), {P2, P1, R3, R2}, EMPTY},
    {"cmp4.gt.unc",	 A2, OpX2TbTaC (0xc, 1, 0, 0, 1), {P1, P2, R3, R2}, EMPTY},
    {"cmp4.ge.unc",	 A2, OpX2TbTaC (0xc, 1, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.eq.and",	 A2, OpX2TbTaC (0xc, 1, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.ne.andcm",	 A2, OpX2TbTaC (0xc, 1, 0, 1, 0), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.and",	 A2, OpX2TbTaC (0xc, 1, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.eq.andcm",	 A2, OpX2TbTaC (0xc, 1, 0, 1, 1), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.and",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.lt.and",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.ge.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.and",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.ge.and",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.gt.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.lt.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.and",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.le.and",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.and",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.gt.and",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.le.andcm",	 A2, OpX2TbTaC (0xc, 0, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.and",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.lt.and",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.ge.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.and",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.ge.and",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.lt.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.and",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.le.and",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.gt.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.and",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.gt.and",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.le.andcm",	 A2, OpX2TbTaC (0xc, 1, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt",		 A2, OpX2TaC   (0xc, 2, 0, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.le",		 A2, OpX2TaC   (0xc, 2, 0, 0), {P1, P2, IMM8M1, R3}, EMPTY},
    {"cmp.gt",		 A2, OpX2TaC   (0xc, 2, 0, 0), {P2, P1, IMM8M1, R3}, EMPTY},
    {"cmp.ge",		 A2, OpX2TaC   (0xc, 2, 0, 0), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.lt.unc",	 A2, OpX2TaC   (0xc, 2, 0, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.le.unc",	 A2, OpX2TaC   (0xc, 2, 0, 1), {P1, P2, IMM8M1, R3}, EMPTY},
    {"cmp.gt.unc",	 A2, OpX2TaC   (0xc, 2, 0, 1), {P2, P1, IMM8M1, R3}, EMPTY},
    {"cmp.ge.unc",	 A2, OpX2TaC   (0xc, 2, 0, 1), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.eq.and",	 A2, OpX2TaC   (0xc, 2, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.ne.andcm",	 A2, OpX2TaC   (0xc, 2, 1, 0), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.and",	 A2, OpX2TaC   (0xc, 2, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.eq.andcm",	 A2, OpX2TaC   (0xc, 2, 1, 1), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.lt",		 A2, OpX2TaC   (0xc, 3, 0, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.le",		 A2, OpX2TaC   (0xc, 3, 0, 0), {P1, P2, IMM8M1, R3}, EMPTY},
    {"cmp4.gt",		 A2, OpX2TaC   (0xc, 3, 0, 0), {P2, P1, IMM8M1, R3}, EMPTY},
    {"cmp4.ge",		 A2, OpX2TaC   (0xc, 3, 0, 0), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp4.lt.unc",	 A2, OpX2TaC   (0xc, 3, 0, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.le.unc",	 A2, OpX2TaC   (0xc, 3, 0, 1), {P1, P2, IMM8M1, R3}, EMPTY},
    {"cmp4.gt.unc",	 A2, OpX2TaC   (0xc, 3, 0, 1), {P2, P1, IMM8M1, R3}, EMPTY},
    {"cmp4.ge.unc",	 A2, OpX2TaC   (0xc, 3, 0, 1), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp4.eq.and",	 A2, OpX2TaC   (0xc, 3, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.ne.andcm",	 A2, OpX2TaC   (0xc, 3, 1, 0), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.and",	 A2, OpX2TaC   (0xc, 3, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.eq.andcm",	 A2, OpX2TaC   (0xc, 3, 1, 1), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp.ltu",		 A2, OpX2TbTaC (0xd, 0, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.leu",		 A2, OpX2TbTaC (0xd, 0, 0, 0, 0), {P2, P1, R3, R2}, EMPTY},
    {"cmp.gtu",		 A2, OpX2TbTaC (0xd, 0, 0, 0, 0), {P1, P2, R3, R2}, EMPTY},
    {"cmp.geu",		 A2, OpX2TbTaC (0xd, 0, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp.ltu.unc",	 A2, OpX2TbTaC (0xd, 0, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.leu.unc",	 A2, OpX2TbTaC (0xd, 0, 0, 0, 1), {P2, P1, R3, R2}, EMPTY},
    {"cmp.gtu.unc",	 A2, OpX2TbTaC (0xd, 0, 0, 0, 1), {P1, P2, R3, R2}, EMPTY},
    {"cmp.geu.unc",	 A2, OpX2TbTaC (0xd, 0, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp.eq.or",	 A2, OpX2TbTaC (0xd, 0, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.ne.orcm",	 A2, OpX2TbTaC (0xd, 0, 0, 1, 0), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.or",	 A2, OpX2TbTaC (0xd, 0, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.eq.orcm",	 A2, OpX2TbTaC (0xd, 0, 0, 1, 1), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.ltu",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.leu",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 0), {P2, P1, R3, R2}, EMPTY},
    {"cmp4.gtu",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 0), {P1, P2, R3, R2}, EMPTY},
    {"cmp4.geu",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.ltu.unc",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.leu.unc",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 1), {P2, P1, R3, R2}, EMPTY},
    {"cmp4.gtu.unc",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 1), {P1, P2, R3, R2}, EMPTY},
    {"cmp4.geu.unc",	 A2, OpX2TbTaC (0xd, 1, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.eq.or",	 A2, OpX2TbTaC (0xd, 1, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.ne.orcm",	 A2, OpX2TbTaC (0xd, 1, 0, 1, 0), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.or",	 A2, OpX2TbTaC (0xd, 1, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.eq.orcm",	 A2, OpX2TbTaC (0xd, 1, 0, 1, 1), {P1, P2, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.or",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.lt.or",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.ge.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.or",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.ge.or",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.gt.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.lt.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.or",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.le.or",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.or",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.gt.or",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.le.orcm",	 A2, OpX2TbTaC (0xd, 0, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.or",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.lt.or",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.ge.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.or",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.ge.or",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.lt.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.or",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.le.or",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 0), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.gt.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.or",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.gt.or",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 1), {P1, P2, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.le.orcm",	 A2, OpX2TbTaC (0xd, 1, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ltu",		 A2, OpX2TaC   (0xd, 2, 0, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.leu",		 A2, OpX2TaC   (0xd, 2, 0, 0), {P1, P2, IMM8M1U8, R3}, EMPTY},
    {"cmp.gtu",		 A2, OpX2TaC   (0xd, 2, 0, 0), {P2, P1, IMM8M1U8, R3}, EMPTY},
    {"cmp.geu",		 A2, OpX2TaC   (0xd, 2, 0, 0), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.ltu.unc",	 A2, OpX2TaC   (0xd, 2, 0, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.leu.unc",	 A2, OpX2TaC   (0xd, 2, 0, 1), {P1, P2, IMM8M1U8, R3}, EMPTY},
    {"cmp.gtu.unc",	 A2, OpX2TaC   (0xd, 2, 0, 1), {P2, P1, IMM8M1U8, R3}, EMPTY},
    {"cmp.geu.unc",	 A2, OpX2TaC   (0xd, 2, 0, 1), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.eq.or",	 A2, OpX2TaC   (0xd, 2, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.ne.orcm",	 A2, OpX2TaC   (0xd, 2, 1, 0), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.or",	 A2, OpX2TaC   (0xd, 2, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.eq.orcm",	 A2, OpX2TaC   (0xd, 2, 1, 1), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.ltu",	 A2, OpX2TaC   (0xd, 3, 0, 0), {P1, P2, IMM8U4, R3}, EMPTY},
    {"cmp4.leu",	 A2, OpX2TaC   (0xd, 3, 0, 0), {P1, P2, IMM8M1U4, R3}, EMPTY},
    {"cmp4.gtu",	 A2, OpX2TaC   (0xd, 3, 0, 0), {P2, P1, IMM8M1U4, R3}, EMPTY},
    {"cmp4.geu",	 A2, OpX2TaC   (0xd, 3, 0, 0), {P2, P1, IMM8U4, R3}, EMPTY},
    {"cmp4.ltu.unc",	 A2, OpX2TaC   (0xd, 3, 0, 1), {P1, P2, IMM8U4, R3}, EMPTY},
    {"cmp4.leu.unc",	 A2, OpX2TaC   (0xd, 3, 0, 1), {P1, P2, IMM8M1U4, R3}, EMPTY},
    {"cmp4.gtu.unc",	 A2, OpX2TaC   (0xd, 3, 0, 1), {P2, P1, IMM8M1U4, R3}, EMPTY},
    {"cmp4.geu.unc",	 A2, OpX2TaC   (0xd, 3, 0, 1), {P2, P1, IMM8U4, R3}, EMPTY},
    {"cmp4.eq.or",	 A2, OpX2TaC   (0xd, 3, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.ne.orcm",	 A2, OpX2TaC   (0xd, 3, 1, 0), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.or",	 A2, OpX2TaC   (0xd, 3, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.eq.orcm",	 A2, OpX2TaC   (0xd, 3, 1, 1), {P1, P2, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp.eq",		 A2, OpX2TbTaC (0xe, 0, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.ne",		 A2, OpX2TbTaC (0xe, 0, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp.eq.unc",	 A2, OpX2TbTaC (0xe, 0, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.ne.unc",	 A2, OpX2TbTaC (0xe, 0, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp.eq.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp.ne.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 0, 1, 0), {P2, P1, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp.eq.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 0, 1, 1), {P2, P1, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.eq",		 A2, OpX2TbTaC (0xe, 1, 0, 0, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.ne",		 A2, OpX2TbTaC (0xe, 1, 0, 0, 0), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.eq.unc",	 A2, OpX2TbTaC (0xe, 1, 0, 0, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.ne.unc",	 A2, OpX2TbTaC (0xe, 1, 0, 0, 1), {P2, P1, R2, R3}, EMPTY},
    {"cmp4.eq.or.andcm", A2, OpX2TbTaC (0xe, 1, 0, 1, 0), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.ne.and.orcm", A2, OpX2TbTaC (0xe, 1, 0, 1, 0), {P2, P1, R2, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.or.andcm", A2, OpX2TbTaC (0xe, 1, 0, 1, 1), {P1, P2, R2, R3}, EMPTY},
    {"cmp4.eq.and.orcm", A2, OpX2TbTaC (0xe, 1, 0, 1, 1), {P2, P1, R2, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.lt.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 0), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.ge.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 0), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.le.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.ge.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.gt.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 1), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.lt.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 0, 1), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.le.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 0), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.gt.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 0), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.lt.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp.gt.or.andcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.ge.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 1), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp.le.and.orcm",	 A2, OpX2TbTaC (0xe, 0, 1, 1, 1), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.lt.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 0), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.ge.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 0), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.le.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.ge.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.gt.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 1), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.lt.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 0, 1), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 0), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.le.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 0), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 0), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.gt.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 0), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.lt.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 1), {P1, P2, GR0, R3}, EMPTY},
    {"cmp4.gt.or.andcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 1), {P1, P2, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp4.ge.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 1), {P2, P1, GR0, R3}, PSEUDO, 0, NULL},
    {"cmp4.le.and.orcm", A2, OpX2TbTaC (0xe, 1, 1, 1, 1), {P2, P1, R3, GR0}, PSEUDO, 0, NULL},
    {"cmp.eq",		 A2, OpX2TaC   (0xe, 2, 0, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.ne",		 A2, OpX2TaC   (0xe, 2, 0, 0), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.eq.unc",	 A2, OpX2TaC   (0xe, 2, 0, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.ne.unc",	 A2, OpX2TaC   (0xe, 2, 0, 1), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp.eq.or.andcm",	 A2, OpX2TaC   (0xe, 2, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.ne.and.orcm",	 A2, OpX2TaC   (0xe, 2, 1, 0), {P2, P1, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp.ne.or.andcm",	 A2, OpX2TaC   (0xe, 2, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp.eq.and.orcm",	 A2, OpX2TaC   (0xe, 2, 1, 1), {P2, P1, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.eq",		 A2, OpX2TaC   (0xe, 3, 0, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.ne",		 A2, OpX2TaC   (0xe, 3, 0, 0), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp4.eq.unc",	 A2, OpX2TaC   (0xe, 3, 0, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.ne.unc",	 A2, OpX2TaC   (0xe, 3, 0, 1), {P2, P1, IMM8, R3}, EMPTY},
    {"cmp4.eq.or.andcm", A2, OpX2TaC   (0xe, 3, 1, 0), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.ne.and.orcm", A2, OpX2TaC   (0xe, 3, 1, 0), {P2, P1, IMM8, R3}, PSEUDO, 0, NULL},
    {"cmp4.ne.or.andcm", A2, OpX2TaC   (0xe, 3, 1, 1), {P1, P2, IMM8, R3}, EMPTY},
    {"cmp4.eq.and.orcm", A2, OpX2TaC   (0xe, 3, 1, 1), {P2, P1, IMM8, R3}, PSEUDO, 0, NULL},

    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };

#undef A
#undef A2
#undef bC
#undef bImm14
#undef bR3a
#undef bR3b
#undef bTa
#undef bTb
#undef bVe
#undef bX
#undef bX2
#undef bX2a
#undef bX2b
#undef bX4
#undef bZa
#undef bZb
#undef mC
#undef mImm14
#undef mR3a
#undef mR3b
#undef mTa
#undef mTb
#undef mVe
#undef mX
#undef mX2
#undef mX2a
#undef mX2b
#undef mX4
#undef mZa
#undef mZb
#undef OpR3a
#undef OpR3b
#undef OpX2aVe
#undef OpX2aVeImm14
#undef OpX2aVeX4
#undef OpX2aVeX4X2b
#undef OpX2TbTaC
#undef OpX2TaC
#undef OpX2aZaZbX4
#undef OpX2aZaZbX4X2b
#undef EMPTY

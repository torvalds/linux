/* ia64-opc-i.c -- IA-64 `I' opcode table.
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

#define I0	IA64_TYPE_I, 0
#define I	IA64_TYPE_I, 1
#define I2	IA64_TYPE_I, 2

/* instruction bit fields: */
#define bC(x)		(((ia64_insn) ((x) & 0x1)) << 12)
#define bIh(x)		(((ia64_insn) ((x) & 0x1)) << 23)
#define bTa(x)		(((ia64_insn) ((x) & 0x1)) << 33)
#define bTag13(x)	(((ia64_insn) ((x) & 0x1)) << 33)
#define bTb(x)		(((ia64_insn) ((x) & 0x1)) << 36)
#define bVc(x)		(((ia64_insn) ((x) & 0x1)) << 20)
#define bVe(x)		(((ia64_insn) ((x) & 0x1)) << 32)
#define bWh(x)		(((ia64_insn) ((x) & 0x3)) << 20)
#define bX(x)		(((ia64_insn) ((x) & 0x1)) << 33)
#define bXb(x)		(((ia64_insn) ((x) & 0x1)) << 22)
#define bXc(x)		(((ia64_insn) ((x) & 0x1)) << 19)
#define bX2(x)		(((ia64_insn) ((x) & 0x3)) << 34)
#define bX2a(x)		(((ia64_insn) ((x) & 0x3)) << 34)
#define bX2b(x)		(((ia64_insn) ((x) & 0x3)) << 28)
#define bX2c(x)		(((ia64_insn) ((x) & 0x3)) << 30)
#define bX3(x)		(((ia64_insn) ((x) & 0x7)) << 33)
#define bX6(x)		(((ia64_insn) ((x) & 0x3f)) << 27)
#define bYa(x)		(((ia64_insn) ((x) & 0x1)) << 13)
#define bYb(x)		(((ia64_insn) ((x) & 0x1)) << 26)
#define bZa(x)		(((ia64_insn) ((x) & 0x1)) << 36)
#define bZb(x)		(((ia64_insn) ((x) & 0x1)) << 33)

/* instruction bit masks: */
#define mC	bC (-1)
#define mIh	bIh (-1)
#define mTa	bTa (-1)
#define mTag13	bTag13 (-1)
#define mTb	bTb (-1)
#define mVc	bVc (-1)
#define mVe	bVe (-1)
#define mWh	bWh (-1)
#define mX	bX (-1)
#define mXb	bXb (-1)
#define mXc	bXc (-1)
#define mX2	bX2 (-1)
#define mX2a	bX2a (-1)
#define mX2b	bX2b (-1)
#define mX2c	bX2c (-1)
#define mX3	bX3 (-1)
#define mX6	bX6 (-1)
#define mYa	bYa (-1)
#define mYb	bYb (-1)
#define mZa	bZa (-1)
#define mZb	bZb (-1)

#define OpZaZbVeX2aX2b(a,b,c,d,e,f) \
	(bOp (a) | bZa (b) | bZb (c) | bVe (d) | bX2a (e) | bX2b (f)), \
	(mOp | mZa | mZb | mVe | mX2a | mX2b)
#define OpZaZbVeX2aX2bX2c(a,b,c,d,e,f,g) \
  (bOp (a) | bZa (b) | bZb (c) | bVe (d) | bX2a (e) | bX2b (f) | bX2c (g)), \
	(mOp | mZa | mZb | mVe | mX2a | mX2b | mX2c)
#define OpX2X(a,b,c)		(bOp (a) | bX2 (b) | bX (c)), (mOp | mX2 | mX)
#define OpX2XYa(a,b,c,d)	(bOp (a) | bX2 (b) | bX (c) | bYa (d)), \
				(mOp | mX2 | mX | mYa)
#define OpX2XYb(a,b,c,d)	(bOp (a) | bX2 (b) | bX (c) | bYb (d)), \
				(mOp | mX2 | mX | mYb)
#define OpX2TaTbYaC(a,b,c,d,e,f) \
	(bOp (a) | bX2 (b) | bTa (c) | bTb (d) | bYa (e) | bC (f)), \
	(mOp | mX2 | mTa | mTb | mYa | mC)
#define OpX2TaTbYaXcC(a,b,c,d,e,f,g) \
	(bOp (a) | bX2 (b) | bTa (c) | bTb (d) | bYa (e) | bXc (f) | bC (g)), \
	(mOp | mX2 | mTa | mTb | mYa | mXc | mC)
#define OpX3(a,b)		(bOp (a) | bX3 (b)), (mOp | mX3)
#define OpX3X6(a,b,c)		(bOp (a) | bX3 (b) | bX6(c)), \
				(mOp | mX3 | mX6)
#define OpX3X6Yb(a,b,c,d)	(bOp (a) | bX3 (b) | bX6(c) | bYb(d)), \
				(mOp | mX3 | mX6 | mYb)
#define OpX3XbIhWh(a,b,c,d,e) \
  (bOp (a) | bX3 (b) | bXb (c) | bIh (d) | bWh (e)), \
  (mOp | mX3 | mXb | mIh | mWh)
#define OpX3XbIhWhTag13(a,b,c,d,e,f) \
     (bOp (a) | bX3 (b) | bXb (c) | bIh (d) | bWh (e) | bTag13 (f)), \
     (mOp | mX3 | mXb | mIh | mWh | mTag13)

#define FULL17 ((ia64_insn)0x10ff001fc0LL)

/* Used to initialise unused fields in ia64_opcode struct,
   in order to stop gcc from complaining.  */
#define EMPTY 0,0,NULL

struct ia64_opcode ia64_opcodes_i[] =
  {
    /* I-type instruction encodings (sorted according to major opcode).  */

    {"break.i",	I0, OpX3X6 (0, 0, 0x00), {IMMU21}, X_IN_MLX, 0, NULL},
    {"nop.i",	I0, OpX3X6Yb (0, 0, 0x01, 0), {IMMU21}, X_IN_MLX, 0, NULL},
    {"hint.i",	I0, OpX3X6Yb (0, 0, 0x01, 1), {IMMU21}, X_IN_MLX, 0, NULL},
    {"chk.s.i",	I0, OpX3 (0, 1), {R2, TGT25b}, EMPTY},

    {"mov", I, OpX3XbIhWhTag13 (0, 7, 0, 0, 1, 0), {B1, R2}, PSEUDO, 0, NULL},
#define MOV(a,b,c,d) \
    I, OpX3XbIhWh (0, a, b, c, d), {B1, R2, TAG13b}, EMPTY
    {"mov.sptk",		MOV (7, 0, 0, 0)},
    {"mov.sptk.imp",		MOV (7, 0, 1, 0)},
    {"mov",			MOV (7, 0, 0, 1)},
    {"mov.imp",			MOV (7, 0, 1, 1)},
    {"mov.dptk",		MOV (7, 0, 0, 2)},
    {"mov.dptk.imp",		MOV (7, 0, 1, 2)},
    {"mov.ret.sptk",		MOV (7, 1, 0, 0)},
    {"mov.ret.sptk.imp",	MOV (7, 1, 1, 0)},
    {"mov.ret",			MOV (7, 1, 0, 1)},
    {"mov.ret.imp",		MOV (7, 1, 1, 1)},
    {"mov.ret.dptk",		MOV (7, 1, 0, 2)},
    {"mov.ret.dptk.imp",	MOV (7, 1, 1, 2)},
#undef MOV
    {"mov",	I, OpX3X6 (0, 0, 0x31), {R1, B2}, EMPTY},
    {"mov",	I, OpX3 (0, 3), {PR, R2, IMM17}, EMPTY},
    /* Don't remove one of the seemingly redundant FULL17-s.  */
    {"mov",	I, FULL17 | OpX3 (0, 3) | FULL17, {PR, R2}, PSEUDO, 0, NULL},
    {"mov",	I, OpX3 (0, 2), {PR_ROT, IMM44}, EMPTY},
    {"mov",	I, OpX3X6 (0, 0, 0x30), {R1, IP}, EMPTY},
    {"mov",	I, OpX3X6 (0, 0, 0x33), {R1, PR}, EMPTY},
    {"mov.i",	I, OpX3X6 (0, 0, 0x2a), {AR3, R2}, EMPTY},
    {"mov.i",	I, OpX3X6 (0, 0, 0x0a), {AR3, IMM8}, EMPTY},
    {"mov.i",	I, OpX3X6 (0, 0, 0x32), {R1, AR3}, EMPTY},
    {"zxt1",	I, OpX3X6 (0, 0, 0x10), {R1, R3}, EMPTY},
    {"zxt2",	I, OpX3X6 (0, 0, 0x11), {R1, R3}, EMPTY},
    {"zxt4",	I, OpX3X6 (0, 0, 0x12), {R1, R3}, EMPTY},
    {"sxt1",	I, OpX3X6 (0, 0, 0x14), {R1, R3}, EMPTY},
    {"sxt2",	I, OpX3X6 (0, 0, 0x15), {R1, R3}, EMPTY},
    {"sxt4",	I, OpX3X6 (0, 0, 0x16), {R1, R3}, EMPTY},
    {"czx1.l",	I, OpX3X6 (0, 0, 0x18), {R1, R3}, EMPTY},
    {"czx2.l",	I, OpX3X6 (0, 0, 0x19), {R1, R3}, EMPTY},
    {"czx1.r",	I, OpX3X6 (0, 0, 0x1c), {R1, R3}, EMPTY},
    {"czx2.r",	I, OpX3X6 (0, 0, 0x1d), {R1, R3}, EMPTY},

    {"dep",	I, Op (4), {R1, R2, R3, CPOS6c, LEN4}, EMPTY},

    {"shrp",	I, OpX2X (5, 3, 0), {R1, R2, R3, CNT6}, EMPTY},

    {"shr.u",	I, OpX2XYa (5, 1, 0, 0), {R1, R3, POS6},
     PSEUDO | LEN_EQ_64MCNT, 0, NULL},
    {"extr.u",	I, OpX2XYa (5, 1, 0, 0), {R1, R3, POS6, LEN6}, EMPTY},

    {"shr",	I, OpX2XYa (5, 1, 0, 1), {R1, R3, POS6},
     PSEUDO | LEN_EQ_64MCNT, 0, NULL},
    {"extr",	I, OpX2XYa (5, 1, 0, 1), {R1, R3, POS6, LEN6}, EMPTY},

    {"shl",	I, OpX2XYb (5, 1, 1, 0), {R1, R2, CPOS6a},
     PSEUDO | LEN_EQ_64MCNT, 0, NULL},
    {"dep.z",	I, OpX2XYb (5, 1, 1, 0), {R1, R2, CPOS6a, LEN6}, EMPTY},
    {"dep.z",	I, OpX2XYb (5, 1, 1, 1), {R1, IMM8, CPOS6a, LEN6}, EMPTY},
    {"dep",	I, OpX2X (5, 3, 1), {R1, IMM1, R3, CPOS6b, LEN6}, EMPTY},
#define TF(a,b,c) \
	I2, OpX2TaTbYaXcC (5, 0, a, b, 1, 1, c), {P1, P2, IMMU5b}, EMPTY
#define TFCM(a,b,c) \
	I2, OpX2TaTbYaXcC (5, 0, a, b, 1, 1, c), {P2, P1, IMMU5b}, PSEUDO, 0, NULL
    {"tf.z",		 TF   (0, 0, 0)},
    {"tf.nz",		 TFCM (0, 0, 0)},
    {"tf.z.unc",	 TF   (0, 0, 1)},
    {"tf.nz.unc",	 TFCM (0, 0, 1)},
    {"tf.z.and",	 TF   (0, 1, 0)},
    {"tf.nz.andcm",	 TFCM (0, 1, 0)},
    {"tf.nz.and",	 TF   (0, 1, 1)},
    {"tf.z.andcm",	 TFCM (0, 1, 1)},
    {"tf.z.or",		 TF   (1, 0, 0)},
    {"tf.nz.orcm",	 TFCM (1, 0, 0)},
    {"tf.nz.or",	 TF   (1, 0, 1)},
    {"tf.z.orcm",	 TFCM (1, 0, 1)},
    {"tf.z.or.andcm",	 TF   (1, 1, 0)},
    {"tf.nz.and.orcm",	 TFCM (1, 1, 0)},
    {"tf.nz.or.andcm",	 TF   (1, 1, 1)},
    {"tf.z.and.orcm",	 TFCM (1, 1, 1)},
#undef TF
#undef TFCM
#define TBIT(a,b,c,d) \
        I2, OpX2TaTbYaC (5, 0, a, b, c, d), {P1, P2, R3, POS6}, EMPTY
#define TBITCM(a,b,c,d)	\
        I2, OpX2TaTbYaC (5, 0, a, b, c, d), {P2, P1, R3, POS6}, PSEUDO, 0, NULL
    {"tbit.z",		 TBIT   (0, 0, 0, 0)},
    {"tbit.nz",		 TBITCM (0, 0, 0, 0)},
    {"tbit.z.unc",	 TBIT   (0, 0, 0, 1)},
    {"tbit.nz.unc",	 TBITCM (0, 0, 0, 1)},
    {"tbit.z.and",	 TBIT   (0, 1, 0, 0)},
    {"tbit.nz.andcm",	 TBITCM (0, 1, 0, 0)},
    {"tbit.nz.and",	 TBIT   (0, 1, 0, 1)},
    {"tbit.z.andcm",	 TBITCM (0, 1, 0, 1)},
    {"tbit.z.or",	 TBIT   (1, 0, 0, 0)},
    {"tbit.nz.orcm",	 TBITCM (1, 0, 0, 0)},
    {"tbit.nz.or",	 TBIT   (1, 0, 0, 1)},
    {"tbit.z.orcm",	 TBITCM (1, 0, 0, 1)},
    {"tbit.z.or.andcm",	 TBIT   (1, 1, 0, 0)},
    {"tbit.nz.and.orcm", TBITCM (1, 1, 0, 0)},
    {"tbit.nz.or.andcm", TBIT   (1, 1, 0, 1)},
    {"tbit.z.and.orcm",  TBITCM (1, 1, 0, 1)},
#undef TBIT
#undef TBITCM
#define TNAT(a,b,c,d) \
	I2, OpX2TaTbYaC (5, 0, a, b, c, d), {P1, P2, R3}, EMPTY
#define TNATCM(a,b,c,d) \
	I2, OpX2TaTbYaC (5, 0, a, b, c, d), {P2, P1, R3}, PSEUDO, 0, NULL
    {"tnat.z",		 TNAT   (0, 0, 1, 0)},
    {"tnat.nz",		 TNATCM (0, 0, 1, 0)},
    {"tnat.z.unc",	 TNAT   (0, 0, 1, 1)},
    {"tnat.nz.unc",	 TNATCM (0, 0, 1, 1)},
    {"tnat.z.and",	 TNAT   (0, 1, 1, 0)},
    {"tnat.nz.andcm",	 TNATCM (0, 1, 1, 0)},
    {"tnat.nz.and",	 TNAT   (0, 1, 1, 1)},
    {"tnat.z.andcm",	 TNATCM (0, 1, 1, 1)},
    {"tnat.z.or",	 TNAT   (1, 0, 1, 0)},
    {"tnat.nz.orcm",	 TNATCM (1, 0, 1, 0)},
    {"tnat.nz.or",	 TNAT   (1, 0, 1, 1)},
    {"tnat.z.orcm",	 TNATCM (1, 0, 1, 1)},
    {"tnat.z.or.andcm",	 TNAT   (1, 1, 1, 0)},
    {"tnat.nz.and.orcm", TNATCM (1, 1, 1, 0)},
    {"tnat.nz.or.andcm", TNAT   (1, 1, 1, 1)},
    {"tnat.z.and.orcm",  TNATCM (1, 1, 1, 1)},
#undef TNAT
#undef TNATCM

    {"pmpyshr2",   I, OpZaZbVeX2aX2b (7, 0, 1, 0, 0, 3), {R1, R2, R3, CNT2c}, EMPTY},
    {"pmpyshr2.u", I, OpZaZbVeX2aX2b (7, 0, 1, 0, 0, 1), {R1, R2, R3, CNT2c}, EMPTY},
    {"pmpy2.r",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 1, 3), {R1, R2, R3}, EMPTY},
    {"pmpy2.l",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 3, 3), {R1, R2, R3}, EMPTY},
    {"mix1.r",	   I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 0, 2), {R1, R2, R3}, EMPTY},
    {"mix2.r",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 0, 2), {R1, R2, R3}, EMPTY},
    {"mix4.r",	   I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 2, 0, 2), {R1, R2, R3}, EMPTY},
    {"mix1.l",	   I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 2, 2), {R1, R2, R3}, EMPTY},
    {"mix2.l",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 2, 2), {R1, R2, R3}, EMPTY},
    {"mix4.l",	   I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 2, 2, 2), {R1, R2, R3}, EMPTY},
    {"pack2.uss",  I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 0, 0), {R1, R2, R3}, EMPTY},
    {"pack2.sss",  I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 2, 0), {R1, R2, R3}, EMPTY},
    {"pack4.sss",  I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 2, 2, 0), {R1, R2, R3}, EMPTY},
    {"unpack1.h",  I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 0, 1), {R1, R2, R3}, EMPTY},
    {"unpack2.h",  I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 0, 1), {R1, R2, R3}, EMPTY},
    {"unpack4.h",  I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 2, 0, 1), {R1, R2, R3}, EMPTY},
    {"unpack1.l",  I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 2, 1), {R1, R2, R3}, EMPTY},
    {"unpack2.l",  I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 2, 1), {R1, R2, R3}, EMPTY},
    {"unpack4.l",  I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 2, 2, 1), {R1, R2, R3}, EMPTY},
    {"pmin1.u",	   I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 1, 0), {R1, R2, R3}, EMPTY},
    {"pmax1.u",	   I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 1, 1), {R1, R2, R3}, EMPTY},
    {"pmin2",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 3, 0), {R1, R2, R3}, EMPTY},
    {"pmax2",	   I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 2, 3, 1), {R1, R2, R3}, EMPTY},
    {"psad1",	   I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 2, 3, 2), {R1, R2, R3}, EMPTY},
    {"mux1", I, OpZaZbVeX2aX2bX2c (7, 0, 0, 0, 3, 2, 2), {R1, R2, MBTYPE4}, EMPTY},
    {"mux2", I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 3, 2, 2), {R1, R2, MHTYPE8}, EMPTY},
    {"pshr2",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 0, 2, 0), {R1, R3, R2}, EMPTY},
    {"pshr4",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 0, 2, 0), {R1, R3, R2}, EMPTY},
    {"shr",	I, OpZaZbVeX2aX2bX2c (7, 1, 1, 0, 0, 2, 0), {R1, R3, R2}, EMPTY},
    {"pshr2.u",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 0, 0, 0), {R1, R3, R2}, EMPTY},
    {"pshr4.u",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 0, 0, 0), {R1, R3, R2}, EMPTY},
    {"shr.u",	I, OpZaZbVeX2aX2bX2c (7, 1, 1, 0, 0, 0, 0), {R1, R3, R2}, EMPTY},
    {"pshr2",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 1, 3, 0), {R1, R3, CNT5}, EMPTY},
    {"pshr4",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 1, 3, 0), {R1, R3, CNT5}, EMPTY},
    {"pshr2.u",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 1, 1, 0), {R1, R3, CNT5}, EMPTY},
    {"pshr4.u",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 1, 1, 0), {R1, R3, CNT5}, EMPTY},
    {"pshl2",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 0, 0, 1), {R1, R2, R3}, EMPTY},
    {"pshl4",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 0, 0, 1), {R1, R2, R3}, EMPTY},
    {"shl",	I, OpZaZbVeX2aX2bX2c (7, 1, 1, 0, 0, 0, 1), {R1, R2, R3}, EMPTY},
    {"pshl2",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 3, 1, 1), {R1, R2, CCNT5}, EMPTY},
    {"pshl4",	I, OpZaZbVeX2aX2bX2c (7, 1, 0, 0, 3, 1, 1), {R1, R2, CCNT5}, EMPTY},
    {"popcnt",	I, OpZaZbVeX2aX2bX2c (7, 0, 1, 0, 1, 1, 2), {R1, R3}, EMPTY},

    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };

#undef I0
#undef I
#undef I2
#undef L
#undef bC
#undef bIh
#undef bTa
#undef bTag13
#undef bTb
#undef bVc
#undef bVe
#undef bWh
#undef bX
#undef bXb
#undef bX2
#undef bX2a
#undef bX2b
#undef bX2c
#undef bX3
#undef bX6
#undef bY
#undef bZa
#undef bZb
#undef mC
#undef mIh
#undef mTa
#undef mTag13
#undef mTb
#undef mVc
#undef mVe
#undef mWh
#undef mX
#undef mXb
#undef mX2
#undef mX2a
#undef mX2b
#undef mX2c
#undef mX3
#undef mX6
#undef mY
#undef mZa
#undef mZb
#undef OpZaZbVeX2aX2b
#undef OpZaZbVeX2aX2bX2c
#undef OpX2X
#undef OpX2XYa
#undef OpX2XYb
#undef OpX2TaTbYaC
#undef OpX3
#undef OpX3X6
#undef OpX3XbIhWh
#undef OpX3XbIhWhTag13
#undef EMPTY

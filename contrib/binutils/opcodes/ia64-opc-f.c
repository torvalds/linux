/* ia64-opc-f.c -- IA-64 `F' opcode table.
   Copyright 1998, 1999, 2000, 2002 Free Software Foundation, Inc.
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

#define f0	IA64_TYPE_F, 0
#define f	IA64_TYPE_F, 1
#define f2	IA64_TYPE_F, 2

#define bF2(x)	(((ia64_insn) ((x) & 0x7f)) << 13)
#define bF4(x)	(((ia64_insn) ((x) & 0x7f)) << 27)
#define bQ(x)	(((ia64_insn) ((x) & 0x1)) << 36)
#define bRa(x)	(((ia64_insn) ((x) & 0x1)) << 33)
#define bRb(x)	(((ia64_insn) ((x) & 0x1)) << 36)
#define bSf(x)	(((ia64_insn) ((x) & 0x3)) << 34)
#define bTa(x)	(((ia64_insn) ((x) & 0x1)) << 12)
#define bXa(x)	(((ia64_insn) ((x) & 0x1)) << 36)
#define bXb(x)	(((ia64_insn) ((x) & 0x1)) << 33)
#define bX2(x)	(((ia64_insn) ((x) & 0x3)) << 34)
#define bX6(x)	(((ia64_insn) ((x) & 0x3f)) << 27)
#define bY(x)	(((ia64_insn) ((x) & 0x1)) << 26)

#define mF2	bF2 (-1)
#define mF4	bF4 (-1)
#define mQ	bQ (-1)
#define mRa	bRa (-1)
#define mRb	bRb (-1)
#define mSf	bSf (-1)
#define mTa	bTa (-1)
#define mXa	bXa (-1)
#define mXb	bXb (-1)
#define mX2	bX2 (-1)
#define mX6	bX6 (-1)
#define mY	bY (-1)

#define OpXa(a,b)	(bOp (a) | bXa (b)), (mOp | mXa)
#define OpXaSf(a,b,c)	(bOp (a) | bXa (b) | bSf (c)), (mOp | mXa | mSf)
#define OpXaSfF2(a,b,c,d) \
	(bOp (a) | bXa (b) | bSf (c) | bF2 (d)), (mOp | mXa | mSf | mF2)
#define OpXaSfF4(a,b,c,d) \
	(bOp (a) | bXa (b) | bSf (c) | bF4 (d)), (mOp | mXa | mSf | mF4)
#define OpXaSfF2F4(a,b,c,d,e) \
	(bOp (a) | bXa (b) | bSf (c) | bF2 (d) | bF4 (e)), \
	(mOp | mXa | mSf | mF2 | mF4)
#define OpXaX2(a,b,c)	(bOp (a) | bXa (b) | bX2 (c)), (mOp | mXa | mX2)
#define OpXaX2F2(a,b,c,d) \
	(bOp (a) | bXa (b) | bX2 (c) | bF2 (d)), (mOp | mXa | mX2 | mF2)
#define OpRaRbTaSf(a,b,c,d,e) \
	(bOp (a) | bRa (b) | bRb (c) | bTa (d) | bSf (e)), \
	(mOp | mRa | mRb | mTa | mSf)
#define OpTa(a,b)	(bOp (a) | bTa (b)), (mOp | mTa)
#define OpXbQSf(a,b,c,d) \
	(bOp (a) | bXb (b) | bQ (c) | bSf (d)), (mOp | mXb | mQ | mSf)
#define OpXbX6(a,b,c) \
	(bOp (a) | bXb (b) | bX6 (c)), (mOp | mXb | mX6)
#define OpXbX6Y(a,b,c,d) \
	(bOp (a) | bXb (b) | bX6 (c) | bY (d)), (mOp | mXb | mX6 | mY)
#define OpXbX6F2(a,b,c,d) \
	(bOp (a) | bXb (b) | bX6 (c) | bF2 (d)), (mOp | mXb | mX6 | mF2)
#define OpXbX6Sf(a,b,c,d) \
	(bOp (a) | bXb (b) | bX6 (c) | bSf (d)), (mOp | mXb | mX6 | mSf)

/* Used to initialise unused fields in ia64_opcode struct,
   in order to stop gcc from complaining.  */
#define EMPTY 0,0,NULL

struct ia64_opcode ia64_opcodes_f[] =
  {
    /* F-type instruction encodings (sorted according to major opcode).  */

    {"frcpa.s0",	f2, OpXbQSf (0, 1, 0, 0), {F1, P2, F2, F3}, EMPTY},
    {"frcpa",		f2, OpXbQSf (0, 1, 0, 0), {F1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"frcpa.s1",	f2, OpXbQSf (0, 1, 0, 1), {F1, P2, F2, F3}, EMPTY},
    {"frcpa.s2",	f2, OpXbQSf (0, 1, 0, 2), {F1, P2, F2, F3}, EMPTY},
    {"frcpa.s3",	f2, OpXbQSf (0, 1, 0, 3), {F1, P2, F2, F3}, EMPTY},

    {"frsqrta.s0",	f2, OpXbQSf (0, 1, 1, 0), {F1, P2, F3}, EMPTY},
    {"frsqrta",		f2, OpXbQSf (0, 1, 1, 0), {F1, P2, F3}, PSEUDO, 0, NULL},
    {"frsqrta.s1",	f2, OpXbQSf (0, 1, 1, 1), {F1, P2, F3}, EMPTY},
    {"frsqrta.s2",	f2, OpXbQSf (0, 1, 1, 2), {F1, P2, F3}, EMPTY},
    {"frsqrta.s3",	f2, OpXbQSf (0, 1, 1, 3), {F1, P2, F3}, EMPTY},

    {"fmin.s0",		f, OpXbX6Sf (0, 0, 0x14, 0), {F1, F2, F3}, EMPTY},
    {"fmin",		f, OpXbX6Sf (0, 0, 0x14, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fmin.s1",		f, OpXbX6Sf (0, 0, 0x14, 1), {F1, F2, F3}, EMPTY},
    {"fmin.s2",		f, OpXbX6Sf (0, 0, 0x14, 2), {F1, F2, F3}, EMPTY},
    {"fmin.s3",		f, OpXbX6Sf (0, 0, 0x14, 3), {F1, F2, F3}, EMPTY},
    {"fmax.s0",		f, OpXbX6Sf (0, 0, 0x15, 0), {F1, F2, F3}, EMPTY},
    {"fmax",		f, OpXbX6Sf (0, 0, 0x15, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fmax.s1",		f, OpXbX6Sf (0, 0, 0x15, 1), {F1, F2, F3}, EMPTY},
    {"fmax.s2",		f, OpXbX6Sf (0, 0, 0x15, 2), {F1, F2, F3}, EMPTY},
    {"fmax.s3",		f, OpXbX6Sf (0, 0, 0x15, 3), {F1, F2, F3}, EMPTY},
    {"famin.s0",	f, OpXbX6Sf (0, 0, 0x16, 0), {F1, F2, F3}, EMPTY},
    {"famin",		f, OpXbX6Sf (0, 0, 0x16, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"famin.s1",	f, OpXbX6Sf (0, 0, 0x16, 1), {F1, F2, F3}, EMPTY},
    {"famin.s2",	f, OpXbX6Sf (0, 0, 0x16, 2), {F1, F2, F3}, EMPTY},
    {"famin.s3",	f, OpXbX6Sf (0, 0, 0x16, 3), {F1, F2, F3}, EMPTY},
    {"famax.s0",	f, OpXbX6Sf (0, 0, 0x17, 0), {F1, F2, F3}, EMPTY},
    {"famax",		f, OpXbX6Sf (0, 0, 0x17, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"famax.s1",	f, OpXbX6Sf (0, 0, 0x17, 1), {F1, F2, F3}, EMPTY},
    {"famax.s2",	f, OpXbX6Sf (0, 0, 0x17, 2), {F1, F2, F3}, EMPTY},
    {"famax.s3",	f, OpXbX6Sf (0, 0, 0x17, 3), {F1, F2, F3}, EMPTY},

    {"mov",		f, OpXbX6 (0, 0, 0x10), {F1, F3}, PSEUDO | F2_EQ_F3, 0, NULL},
    {"fabs",		f, OpXbX6F2 (0, 0, 0x10, 0), {F1, F3}, PSEUDO, 0, NULL},
    {"fneg",		f, OpXbX6   (0, 0, 0x11), {F1, F3}, PSEUDO | F2_EQ_F3, 0, NULL},
    {"fnegabs",		f, OpXbX6F2 (0, 0, 0x11, 0), {F1, F3}, PSEUDO, 0, NULL},
    {"fmerge.s",	f, OpXbX6   (0, 0, 0x10), {F1, F2, F3}, EMPTY},
    {"fmerge.ns",	f, OpXbX6   (0, 0, 0x11), {F1, F2, F3}, EMPTY},

    {"fmerge.se",	f, OpXbX6 (0, 0, 0x12), {F1, F2, F3}, EMPTY},
    {"fmix.lr",		f, OpXbX6 (0, 0, 0x39), {F1, F2, F3}, EMPTY},
    {"fmix.r",		f, OpXbX6 (0, 0, 0x3a), {F1, F2, F3}, EMPTY},
    {"fmix.l",		f, OpXbX6 (0, 0, 0x3b), {F1, F2, F3}, EMPTY},
    {"fsxt.r",		f, OpXbX6 (0, 0, 0x3c), {F1, F2, F3}, EMPTY},
    {"fsxt.l",		f, OpXbX6 (0, 0, 0x3d), {F1, F2, F3}, EMPTY},
    {"fpack",		f, OpXbX6 (0, 0, 0x28), {F1, F2, F3}, EMPTY},
    {"fswap",		f, OpXbX6 (0, 0, 0x34), {F1, F2, F3}, EMPTY},
    {"fswap.nl",	f, OpXbX6 (0, 0, 0x35), {F1, F2, F3}, EMPTY},
    {"fswap.nr",	f, OpXbX6 (0, 0, 0x36), {F1, F2, F3}, EMPTY},
    {"fand",		f, OpXbX6 (0, 0, 0x2c), {F1, F2, F3}, EMPTY},
    {"fandcm",		f, OpXbX6 (0, 0, 0x2d), {F1, F2, F3}, EMPTY},
    {"for",		f, OpXbX6 (0, 0, 0x2e), {F1, F2, F3}, EMPTY},
    {"fxor",		f, OpXbX6 (0, 0, 0x2f), {F1, F2, F3}, EMPTY},

    {"fcvt.fx.s0",		f, OpXbX6Sf (0, 0, 0x18, 0), {F1, F2}, EMPTY},
    {"fcvt.fx",			f, OpXbX6Sf (0, 0, 0x18, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fcvt.fx.s1",		f, OpXbX6Sf (0, 0, 0x18, 1), {F1, F2}, EMPTY},
    {"fcvt.fx.s2",		f, OpXbX6Sf (0, 0, 0x18, 2), {F1, F2}, EMPTY},
    {"fcvt.fx.s3",		f, OpXbX6Sf (0, 0, 0x18, 3), {F1, F2}, EMPTY},
    {"fcvt.fxu.s0",		f, OpXbX6Sf (0, 0, 0x19, 0), {F1, F2}, EMPTY},
    {"fcvt.fxu",		f, OpXbX6Sf (0, 0, 0x19, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fcvt.fxu.s1",		f, OpXbX6Sf (0, 0, 0x19, 1), {F1, F2}, EMPTY},
    {"fcvt.fxu.s2",		f, OpXbX6Sf (0, 0, 0x19, 2), {F1, F2}, EMPTY},
    {"fcvt.fxu.s3",		f, OpXbX6Sf (0, 0, 0x19, 3), {F1, F2}, EMPTY},
    {"fcvt.fx.trunc.s0",	f, OpXbX6Sf (0, 0, 0x1a, 0), {F1, F2}, EMPTY},
    {"fcvt.fx.trunc",		f, OpXbX6Sf (0, 0, 0x1a, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fcvt.fx.trunc.s1",	f, OpXbX6Sf (0, 0, 0x1a, 1), {F1, F2}, EMPTY},
    {"fcvt.fx.trunc.s2",	f, OpXbX6Sf (0, 0, 0x1a, 2), {F1, F2}, EMPTY},
    {"fcvt.fx.trunc.s3",	f, OpXbX6Sf (0, 0, 0x1a, 3), {F1, F2}, EMPTY},
    {"fcvt.fxu.trunc.s0",	f, OpXbX6Sf (0, 0, 0x1b, 0), {F1, F2}, EMPTY},
    {"fcvt.fxu.trunc",		f, OpXbX6Sf (0, 0, 0x1b, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fcvt.fxu.trunc.s1",	f, OpXbX6Sf (0, 0, 0x1b, 1), {F1, F2}, EMPTY},
    {"fcvt.fxu.trunc.s2",	f, OpXbX6Sf (0, 0, 0x1b, 2), {F1, F2}, EMPTY},
    {"fcvt.fxu.trunc.s3",	f, OpXbX6Sf (0, 0, 0x1b, 3), {F1, F2}, EMPTY},

    {"fcvt.xf",		f, OpXbX6 (0, 0, 0x1c), {F1, F2}, EMPTY},

    {"fsetc.s0",	f0, OpXbX6Sf (0, 0, 0x04, 0), {IMMU7a, IMMU7b}, EMPTY},
    {"fsetc",		f0, OpXbX6Sf (0, 0, 0x04, 0), {IMMU7a, IMMU7b}, PSEUDO, 0, NULL},
    {"fsetc.s1",	f0, OpXbX6Sf (0, 0, 0x04, 1), {IMMU7a, IMMU7b}, EMPTY},
    {"fsetc.s2",	f0, OpXbX6Sf (0, 0, 0x04, 2), {IMMU7a, IMMU7b}, EMPTY},
    {"fsetc.s3",	f0, OpXbX6Sf (0, 0, 0x04, 3), {IMMU7a, IMMU7b}, EMPTY},
    {"fclrf.s0",	f0, OpXbX6Sf (0, 0, 0x05, 0), {}, EMPTY},
    {"fclrf",		f0, OpXbX6Sf (0, 0, 0x05, 0), {0}, PSEUDO, 0, NULL},
    {"fclrf.s1",	f0, OpXbX6Sf (0, 0, 0x05, 1), {}, EMPTY},
    {"fclrf.s2",	f0, OpXbX6Sf (0, 0, 0x05, 2), {}, EMPTY},
    {"fclrf.s3",	f0, OpXbX6Sf (0, 0, 0x05, 3), {}, EMPTY},
    {"fchkf.s0",	f0, OpXbX6Sf (0, 0, 0x08, 0), {TGT25}, EMPTY},
    {"fchkf",		f0, OpXbX6Sf (0, 0, 0x08, 0), {TGT25}, PSEUDO, 0, NULL},
    {"fchkf.s1",	f0, OpXbX6Sf (0, 0, 0x08, 1), {TGT25}, EMPTY},
    {"fchkf.s2",	f0, OpXbX6Sf (0, 0, 0x08, 2), {TGT25}, EMPTY},
    {"fchkf.s3",	f0, OpXbX6Sf (0, 0, 0x08, 3), {TGT25}, EMPTY},

    {"break.f",		f0, OpXbX6 (0, 0, 0x00), {IMMU21}, EMPTY},
    {"nop.f",		f0, OpXbX6Y (0, 0, 0x01, 0), {IMMU21}, EMPTY},
    {"hint.f",		f0, OpXbX6Y (0, 0, 0x01, 1), {IMMU21}, EMPTY},

    {"fprcpa.s0",	f2, OpXbQSf (1, 1, 0, 0), {F1, P2, F2, F3}, EMPTY},
    {"fprcpa",		f2, OpXbQSf (1, 1, 0, 0), {F1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fprcpa.s1",	f2, OpXbQSf (1, 1, 0, 1), {F1, P2, F2, F3}, EMPTY},
    {"fprcpa.s2",	f2, OpXbQSf (1, 1, 0, 2), {F1, P2, F2, F3}, EMPTY},
    {"fprcpa.s3",	f2, OpXbQSf (1, 1, 0, 3), {F1, P2, F2, F3}, EMPTY},

    {"fprsqrta.s0",	f2, OpXbQSf (1, 1, 1, 0), {F1, P2, F3}, EMPTY},
    {"fprsqrta",	f2, OpXbQSf (1, 1, 1, 0), {F1, P2, F3}, PSEUDO, 0, NULL},
    {"fprsqrta.s1",	f2, OpXbQSf (1, 1, 1, 1), {F1, P2, F3}, EMPTY},
    {"fprsqrta.s2",	f2, OpXbQSf (1, 1, 1, 2), {F1, P2, F3}, EMPTY},
    {"fprsqrta.s3",	f2, OpXbQSf (1, 1, 1, 3), {F1, P2, F3}, EMPTY},

    {"fpmin.s0",	f, OpXbX6Sf (1, 0, 0x14, 0), {F1, F2, F3}, EMPTY},
    {"fpmin",		f, OpXbX6Sf (1, 0, 0x14, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpmin.s1",	f, OpXbX6Sf (1, 0, 0x14, 1), {F1, F2, F3}, EMPTY},
    {"fpmin.s2",	f, OpXbX6Sf (1, 0, 0x14, 2), {F1, F2, F3}, EMPTY},
    {"fpmin.s3",	f, OpXbX6Sf (1, 0, 0x14, 3), {F1, F2, F3}, EMPTY},
    {"fpmax.s0",	f, OpXbX6Sf (1, 0, 0x15, 0), {F1, F2, F3}, EMPTY},
    {"fpmax",		f, OpXbX6Sf (1, 0, 0x15, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpmax.s1",	f, OpXbX6Sf (1, 0, 0x15, 1), {F1, F2, F3}, EMPTY},
    {"fpmax.s2",	f, OpXbX6Sf (1, 0, 0x15, 2), {F1, F2, F3}, EMPTY},
    {"fpmax.s3",	f, OpXbX6Sf (1, 0, 0x15, 3), {F1, F2, F3}, EMPTY},
    {"fpamin.s0",	f, OpXbX6Sf (1, 0, 0x16, 0), {F1, F2, F3}, EMPTY},
    {"fpamin",		f, OpXbX6Sf (1, 0, 0x16, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpamin.s1",	f, OpXbX6Sf (1, 0, 0x16, 1), {F1, F2, F3}, EMPTY},
    {"fpamin.s2",	f, OpXbX6Sf (1, 0, 0x16, 2), {F1, F2, F3}, EMPTY},
    {"fpamin.s3",	f, OpXbX6Sf (1, 0, 0x16, 3), {F1, F2, F3}, EMPTY},
    {"fpamax.s0",	f, OpXbX6Sf (1, 0, 0x17, 0), {F1, F2, F3}, EMPTY},
    {"fpamax",		f, OpXbX6Sf (1, 0, 0x17, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpamax.s1",	f, OpXbX6Sf (1, 0, 0x17, 1), {F1, F2, F3}, EMPTY},
    {"fpamax.s2",	f, OpXbX6Sf (1, 0, 0x17, 2), {F1, F2, F3}, EMPTY},
    {"fpamax.s3",	f, OpXbX6Sf (1, 0, 0x17, 3), {F1, F2, F3}, EMPTY},

    {"fpcmp.eq.s0",	f, OpXbX6Sf (1, 0, 0x30, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.eq",	f, OpXbX6Sf (1, 0, 0x30, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.eq.s1",	f, OpXbX6Sf (1, 0, 0x30, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.eq.s2",	f, OpXbX6Sf (1, 0, 0x30, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.eq.s3",	f, OpXbX6Sf (1, 0, 0x30, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.lt.s0",	f, OpXbX6Sf (1, 0, 0x31, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.lt",	f, OpXbX6Sf (1, 0, 0x31, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.lt.s1",	f, OpXbX6Sf (1, 0, 0x31, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.lt.s2",	f, OpXbX6Sf (1, 0, 0x31, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.lt.s3",	f, OpXbX6Sf (1, 0, 0x31, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.le.s0",	f, OpXbX6Sf (1, 0, 0x32, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.le",	f, OpXbX6Sf (1, 0, 0x32, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.le.s1",	f, OpXbX6Sf (1, 0, 0x32, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.le.s2",	f, OpXbX6Sf (1, 0, 0x32, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.le.s3",	f, OpXbX6Sf (1, 0, 0x32, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.gt.s0",	f, OpXbX6Sf (1, 0, 0x31, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.gt",	f, OpXbX6Sf (1, 0, 0x31, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.gt.s1",	f, OpXbX6Sf (1, 0, 0x31, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.gt.s2",	f, OpXbX6Sf (1, 0, 0x31, 2), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.gt.s3",	f, OpXbX6Sf (1, 0, 0x31, 3), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ge.s0",	f, OpXbX6Sf (1, 0, 0x32, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ge",	f, OpXbX6Sf (1, 0, 0x32, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ge.s1",	f, OpXbX6Sf (1, 0, 0x32, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ge.s2",	f, OpXbX6Sf (1, 0, 0x32, 2), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ge.s3",	f, OpXbX6Sf (1, 0, 0x32, 3), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.unord.s0",	f, OpXbX6Sf (1, 0, 0x33, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.unord",	f, OpXbX6Sf (1, 0, 0x33, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.unord.s1",	f, OpXbX6Sf (1, 0, 0x33, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.unord.s2",	f, OpXbX6Sf (1, 0, 0x33, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.unord.s3",	f, OpXbX6Sf (1, 0, 0x33, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.neq.s0",	f, OpXbX6Sf (1, 0, 0x34, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.neq",	f, OpXbX6Sf (1, 0, 0x34, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.neq.s1",	f, OpXbX6Sf (1, 0, 0x34, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.neq.s2",	f, OpXbX6Sf (1, 0, 0x34, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.neq.s3",	f, OpXbX6Sf (1, 0, 0x34, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.nlt.s0",	f, OpXbX6Sf (1, 0, 0x35, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.nlt",	f, OpXbX6Sf (1, 0, 0x35, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.nlt.s1",	f, OpXbX6Sf (1, 0, 0x35, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.nlt.s2",	f, OpXbX6Sf (1, 0, 0x35, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.nlt.s3",	f, OpXbX6Sf (1, 0, 0x35, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.nle.s0",	f, OpXbX6Sf (1, 0, 0x36, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.nle",	f, OpXbX6Sf (1, 0, 0x36, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.nle.s1",	f, OpXbX6Sf (1, 0, 0x36, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.nle.s2",	f, OpXbX6Sf (1, 0, 0x36, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.nle.s3",	f, OpXbX6Sf (1, 0, 0x36, 3), {F1, F2, F3}, EMPTY},
    {"fpcmp.ngt.s0",	f, OpXbX6Sf (1, 0, 0x35, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ngt",	f, OpXbX6Sf (1, 0, 0x35, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ngt.s1",	f, OpXbX6Sf (1, 0, 0x35, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ngt.s2",	f, OpXbX6Sf (1, 0, 0x35, 2), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ngt.s3",	f, OpXbX6Sf (1, 0, 0x35, 3), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.nge.s0",	f, OpXbX6Sf (1, 0, 0x36, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.nge",	f, OpXbX6Sf (1, 0, 0x36, 0), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.nge.s1",	f, OpXbX6Sf (1, 0, 0x36, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.nge.s2",	f, OpXbX6Sf (1, 0, 0x36, 2), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.nge.s3",	f, OpXbX6Sf (1, 0, 0x36, 3), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fpcmp.ord.s0",	f, OpXbX6Sf (1, 0, 0x37, 0), {F1, F2, F3}, EMPTY},
    {"fpcmp.ord",	f, OpXbX6Sf (1, 0, 0x37, 0), {F1, F2, F3}, PSEUDO, 0, NULL},
    {"fpcmp.ord.s1",	f, OpXbX6Sf (1, 0, 0x37, 1), {F1, F2, F3}, EMPTY},
    {"fpcmp.ord.s2",	f, OpXbX6Sf (1, 0, 0x37, 2), {F1, F2, F3}, EMPTY},
    {"fpcmp.ord.s3",	f, OpXbX6Sf (1, 0, 0x37, 3), {F1, F2, F3}, EMPTY},

    {"fpabs",		f, OpXbX6F2 (1, 0, 0x10, 0), {F1, F3}, PSEUDO, 0, NULL},
    {"fpneg",		f, OpXbX6   (1, 0, 0x11), {F1, F3}, PSEUDO | F2_EQ_F3, 0, NULL},
    {"fpnegabs",	f, OpXbX6F2 (1, 0, 0x11, 0), {F1, F3}, PSEUDO, 0, NULL},
    {"fpmerge.s",	f, OpXbX6   (1, 0, 0x10), {F1, F2, F3}, EMPTY},
    {"fpmerge.ns",	f, OpXbX6   (1, 0, 0x11), {F1, F2, F3}, EMPTY},
    {"fpmerge.se",	f, OpXbX6 (1, 0, 0x12), {F1, F2, F3}, EMPTY},

    {"fpcvt.fx.s0",		f, OpXbX6Sf (1, 0, 0x18, 0), {F1, F2}, EMPTY},
    {"fpcvt.fx",		f, OpXbX6Sf (1, 0, 0x18, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fpcvt.fx.s1",		f, OpXbX6Sf (1, 0, 0x18, 1), {F1, F2}, EMPTY},
    {"fpcvt.fx.s2",		f, OpXbX6Sf (1, 0, 0x18, 2), {F1, F2}, EMPTY},
    {"fpcvt.fx.s3",		f, OpXbX6Sf (1, 0, 0x18, 3), {F1, F2}, EMPTY},
    {"fpcvt.fxu.s0",		f, OpXbX6Sf (1, 0, 0x19, 0), {F1, F2}, EMPTY},
    {"fpcvt.fxu",		f, OpXbX6Sf (1, 0, 0x19, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fpcvt.fxu.s1",		f, OpXbX6Sf (1, 0, 0x19, 1), {F1, F2}, EMPTY},
    {"fpcvt.fxu.s2",		f, OpXbX6Sf (1, 0, 0x19, 2), {F1, F2}, EMPTY},
    {"fpcvt.fxu.s3",		f, OpXbX6Sf (1, 0, 0x19, 3), {F1, F2}, EMPTY},
    {"fpcvt.fx.trunc.s0",	f, OpXbX6Sf (1, 0, 0x1a, 0), {F1, F2}, EMPTY},
    {"fpcvt.fx.trunc",		f, OpXbX6Sf (1, 0, 0x1a, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fpcvt.fx.trunc.s1",	f, OpXbX6Sf (1, 0, 0x1a, 1), {F1, F2}, EMPTY},
    {"fpcvt.fx.trunc.s2",	f, OpXbX6Sf (1, 0, 0x1a, 2), {F1, F2}, EMPTY},
    {"fpcvt.fx.trunc.s3",	f, OpXbX6Sf (1, 0, 0x1a, 3), {F1, F2}, EMPTY},
    {"fpcvt.fxu.trunc.s0",	f, OpXbX6Sf (1, 0, 0x1b, 0), {F1, F2}, EMPTY},
    {"fpcvt.fxu.trunc",		f, OpXbX6Sf (1, 0, 0x1b, 0), {F1, F2}, PSEUDO, 0, NULL},
    {"fpcvt.fxu.trunc.s1",	f, OpXbX6Sf (1, 0, 0x1b, 1), {F1, F2}, EMPTY},
    {"fpcvt.fxu.trunc.s2",	f, OpXbX6Sf (1, 0, 0x1b, 2), {F1, F2}, EMPTY},
    {"fpcvt.fxu.trunc.s3",	f, OpXbX6Sf (1, 0, 0x1b, 3), {F1, F2}, EMPTY},

    {"fcmp.eq.s0",	  f2, OpRaRbTaSf (4, 0, 0, 0, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq",		  f2, OpRaRbTaSf (4, 0, 0, 0, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.eq.s1",	  f2, OpRaRbTaSf (4, 0, 0, 0, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.s2",	  f2, OpRaRbTaSf (4, 0, 0, 0, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.s3",	  f2, OpRaRbTaSf (4, 0, 0, 0, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.s0",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt",		  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.lt.s1",	  f2, OpRaRbTaSf (4, 0, 1, 0, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.s2",	  f2, OpRaRbTaSf (4, 0, 1, 0, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.s3",	  f2, OpRaRbTaSf (4, 0, 1, 0, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.s0",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le",		  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.le.s1",	  f2, OpRaRbTaSf (4, 1, 0, 0, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.s2",	  f2, OpRaRbTaSf (4, 1, 0, 0, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.s3",	  f2, OpRaRbTaSf (4, 1, 0, 0, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.s0",	  f2, OpRaRbTaSf (4, 1, 1, 0, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord",	  f2, OpRaRbTaSf (4, 1, 1, 0, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.unord.s1",	  f2, OpRaRbTaSf (4, 1, 1, 0, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.s2",	  f2, OpRaRbTaSf (4, 1, 1, 0, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.s3",	  f2, OpRaRbTaSf (4, 1, 1, 0, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.unc.s0",	  f2, OpRaRbTaSf (4, 0, 0, 1, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.unc",	  f2, OpRaRbTaSf (4, 0, 0, 1, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.eq.unc.s1",	  f2, OpRaRbTaSf (4, 0, 0, 1, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.unc.s2",	  f2, OpRaRbTaSf (4, 0, 0, 1, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.eq.unc.s3",	  f2, OpRaRbTaSf (4, 0, 0, 1, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.unc.s0",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.unc",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.lt.unc.s1",	  f2, OpRaRbTaSf (4, 0, 1, 1, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.unc.s2",	  f2, OpRaRbTaSf (4, 0, 1, 1, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.lt.unc.s3",	  f2, OpRaRbTaSf (4, 0, 1, 1, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.unc.s0",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.unc",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.le.unc.s1",	  f2, OpRaRbTaSf (4, 1, 0, 1, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.unc.s2",	  f2, OpRaRbTaSf (4, 1, 0, 1, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.le.unc.s3",	  f2, OpRaRbTaSf (4, 1, 0, 1, 3), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.unc.s0", f2, OpRaRbTaSf (4, 1, 1, 1, 0), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.unc",    f2, OpRaRbTaSf (4, 1, 1, 1, 0), {P1, P2, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.unord.unc.s1", f2, OpRaRbTaSf (4, 1, 1, 1, 1), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.unc.s2", f2, OpRaRbTaSf (4, 1, 1, 1, 2), {P1, P2, F2, F3}, EMPTY},
    {"fcmp.unord.unc.s3", f2, OpRaRbTaSf (4, 1, 1, 1, 3), {P1, P2, F2, F3}, EMPTY},

    /* pseudo-ops of the above */
    {"fcmp.gt.s0",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt",		  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P1, P2, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.gt.s1",	  f2, OpRaRbTaSf (4, 0, 1, 0, 1), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt.s2",	  f2, OpRaRbTaSf (4, 0, 1, 0, 2), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt.s3",	  f2, OpRaRbTaSf (4, 0, 1, 0, 3), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.s0",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge",		  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P1, P2, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.ge.s1",	  f2, OpRaRbTaSf (4, 1, 0, 0, 1), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.s2",	  f2, OpRaRbTaSf (4, 1, 0, 0, 2), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.s3",	  f2, OpRaRbTaSf (4, 1, 0, 0, 3), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.neq.s0",	  f2, OpRaRbTaSf (4, 0, 0, 0, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq",	  f2, OpRaRbTaSf (4, 0, 0, 0, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.neq.s1",	  f2, OpRaRbTaSf (4, 0, 0, 0, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq.s2",	  f2, OpRaRbTaSf (4, 0, 0, 0, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq.s3",	  f2, OpRaRbTaSf (4, 0, 0, 0, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.s0",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.nlt.s1",	  f2, OpRaRbTaSf (4, 0, 1, 0, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.s2",	  f2, OpRaRbTaSf (4, 0, 1, 0, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.s3",	  f2, OpRaRbTaSf (4, 0, 1, 0, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.s0",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.nle.s1",	  f2, OpRaRbTaSf (4, 1, 0, 0, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.s2",	  f2, OpRaRbTaSf (4, 1, 0, 0, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.s3",	  f2, OpRaRbTaSf (4, 1, 0, 0, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ngt.s0",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt",	  f2, OpRaRbTaSf (4, 0, 1, 0, 0), {P2, P1, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.ngt.s1",	  f2, OpRaRbTaSf (4, 0, 1, 0, 1), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt.s2",	  f2, OpRaRbTaSf (4, 0, 1, 0, 2), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt.s3",	  f2, OpRaRbTaSf (4, 0, 1, 0, 3), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.s0",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge",	  f2, OpRaRbTaSf (4, 1, 0, 0, 0), {P2, P1, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.nge.s1",	  f2, OpRaRbTaSf (4, 1, 0, 0, 1), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.s2",	  f2, OpRaRbTaSf (4, 1, 0, 0, 2), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.s3",	  f2, OpRaRbTaSf (4, 1, 0, 0, 3), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ord.s0",	  f2, OpRaRbTaSf (4, 1, 1, 0, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord",	  f2, OpRaRbTaSf (4, 1, 1, 0, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.ord.s1",	  f2, OpRaRbTaSf (4, 1, 1, 0, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord.s2",	  f2, OpRaRbTaSf (4, 1, 1, 0, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord.s3",	  f2, OpRaRbTaSf (4, 1, 1, 0, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.gt.unc.s0",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt.unc",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P1, P2, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.gt.unc.s1",	  f2, OpRaRbTaSf (4, 0, 1, 1, 1), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt.unc.s2",	  f2, OpRaRbTaSf (4, 0, 1, 1, 2), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.gt.unc.s3",	  f2, OpRaRbTaSf (4, 0, 1, 1, 3), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.unc.s0",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.unc",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P1, P2, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.ge.unc.s1",	  f2, OpRaRbTaSf (4, 1, 0, 1, 1), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.unc.s2",	  f2, OpRaRbTaSf (4, 1, 0, 1, 2), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.ge.unc.s3",	  f2, OpRaRbTaSf (4, 1, 0, 1, 3), {P1, P2, F3, F2}, EMPTY},
    {"fcmp.neq.unc.s0",	  f2, OpRaRbTaSf (4, 0, 0, 1, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq.unc",	  f2, OpRaRbTaSf (4, 0, 0, 1, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.neq.unc.s1",	  f2, OpRaRbTaSf (4, 0, 0, 1, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq.unc.s2",	  f2, OpRaRbTaSf (4, 0, 0, 1, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.neq.unc.s3",	  f2, OpRaRbTaSf (4, 0, 0, 1, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.unc.s0",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.unc",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.nlt.unc.s1",	  f2, OpRaRbTaSf (4, 0, 1, 1, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.unc.s2",	  f2, OpRaRbTaSf (4, 0, 1, 1, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nlt.unc.s3",	  f2, OpRaRbTaSf (4, 0, 1, 1, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.unc.s0",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.unc",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.nle.unc.s1",	  f2, OpRaRbTaSf (4, 1, 0, 1, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.unc.s2",	  f2, OpRaRbTaSf (4, 1, 0, 1, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.nle.unc.s3",	  f2, OpRaRbTaSf (4, 1, 0, 1, 3), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ngt.unc.s0",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt.unc",	  f2, OpRaRbTaSf (4, 0, 1, 1, 0), {P2, P1, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.ngt.unc.s1",	  f2, OpRaRbTaSf (4, 0, 1, 1, 1), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt.unc.s2",	  f2, OpRaRbTaSf (4, 0, 1, 1, 2), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ngt.unc.s3",	  f2, OpRaRbTaSf (4, 0, 1, 1, 3), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.unc.s0",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.unc",	  f2, OpRaRbTaSf (4, 1, 0, 1, 0), {P2, P1, F3, F2}, PSEUDO, 0, NULL},
    {"fcmp.nge.unc.s1",	  f2, OpRaRbTaSf (4, 1, 0, 1, 1), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.unc.s2",	  f2, OpRaRbTaSf (4, 1, 0, 1, 2), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.nge.unc.s3",	  f2, OpRaRbTaSf (4, 1, 0, 1, 3), {P2, P1, F3, F2}, EMPTY},
    {"fcmp.ord.unc.s0",	  f2, OpRaRbTaSf (4, 1, 1, 1, 0), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord.unc",	  f2, OpRaRbTaSf (4, 1, 1, 1, 0), {P2, P1, F2, F3}, PSEUDO, 0, NULL},
    {"fcmp.ord.unc.s1",   f2, OpRaRbTaSf (4, 1, 1, 1, 1), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord.unc.s2",   f2, OpRaRbTaSf (4, 1, 1, 1, 2), {P2, P1, F2, F3}, EMPTY},
    {"fcmp.ord.unc.s3",   f2, OpRaRbTaSf (4, 1, 1, 1, 3), {P2, P1, F2, F3}, EMPTY},

    {"fclass.m",	f2, OpTa (5, 0), {P1, P2, F2, IMMU9}, EMPTY},
    {"fclass.nm",	f2, OpTa (5, 0), {P2, P1, F2, IMMU9}, PSEUDO, 0, NULL},
    {"fclass.m.unc",	f2, OpTa (5, 1), {P1, P2, F2, IMMU9}, EMPTY},
    {"fclass.nm.unc",	f2, OpTa (5, 1), {P2, P1, F2, IMMU9}, PSEUDO, 0, NULL},

    /* note: fnorm and fcvt.xuf have identical encodings! */
    {"fnorm.s0",	f, OpXaSfF2F4 (0x8, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm",		f, OpXaSfF2F4 (0x8, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s1",	f, OpXaSfF2F4 (0x8, 0, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s2",	f, OpXaSfF2F4 (0x8, 0, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s3",	f, OpXaSfF2F4 (0x8, 0, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s.s0",	f, OpXaSfF2F4 (0x8, 1, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s",		f, OpXaSfF2F4 (0x8, 1, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s.s1",	f, OpXaSfF2F4 (0x8, 1, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s.s2",	f, OpXaSfF2F4 (0x8, 1, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.s.s3",	f, OpXaSfF2F4 (0x8, 1, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s0",	f, OpXaSfF2F4 (0x8, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf",	f, OpXaSfF2F4 (0x8, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s1",	f, OpXaSfF2F4 (0x8, 0, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s2",	f, OpXaSfF2F4 (0x8, 0, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s3",	f, OpXaSfF2F4 (0x8, 0, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s.s0",	f, OpXaSfF2F4 (0x8, 1, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s",	f, OpXaSfF2F4 (0x8, 1, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s.s1",	f, OpXaSfF2F4 (0x8, 1, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s.s2",	f, OpXaSfF2F4 (0x8, 1, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.s.s3",	f, OpXaSfF2F4 (0x8, 1, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fadd.s0",		f, OpXaSfF4 (0x8, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd",		f, OpXaSfF4 (0x8, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s1",		f, OpXaSfF4 (0x8, 0, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s2",		f, OpXaSfF4 (0x8, 0, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s3",		f, OpXaSfF4 (0x8, 0, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s.s0",	f, OpXaSfF4 (0x8, 1, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s",		f, OpXaSfF4 (0x8, 1, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s.s1",	f, OpXaSfF4 (0x8, 1, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s.s2",	f, OpXaSfF4 (0x8, 1, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.s.s3",	f, OpXaSfF4 (0x8, 1, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fmpy.s0",		f, OpXaSfF2 (0x8, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy",		f, OpXaSfF2 (0x8, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s1",		f, OpXaSfF2 (0x8, 0, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s2",		f, OpXaSfF2 (0x8, 0, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s3",		f, OpXaSfF2 (0x8, 0, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s.s0",	f, OpXaSfF2 (0x8, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s",		f, OpXaSfF2 (0x8, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s.s1",	f, OpXaSfF2 (0x8, 1, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s.s2",	f, OpXaSfF2 (0x8, 1, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.s.s3",	f, OpXaSfF2 (0x8, 1, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fma.s0",		f, OpXaSf (0x8, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fma",		f, OpXaSf (0x8, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fma.s1",		f, OpXaSf (0x8, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fma.s2",		f, OpXaSf (0x8, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fma.s3",		f, OpXaSf (0x8, 0, 3), {F1, F3, F4, F2}, EMPTY},
    {"fma.s.s0",	f, OpXaSf (0x8, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fma.s",		f, OpXaSf (0x8, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fma.s.s1",	f, OpXaSf (0x8, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fma.s.s2",	f, OpXaSf (0x8, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fma.s.s3",	f, OpXaSf (0x8, 1, 3), {F1, F3, F4, F2}, EMPTY},

    {"fnorm.d.s0",	f, OpXaSfF2F4 (0x9, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.d",		f, OpXaSfF2F4 (0x9, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.d.s1",	f, OpXaSfF2F4 (0x9, 0, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.d.s2",	f, OpXaSfF2F4 (0x9, 0, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fnorm.d.s3",	f, OpXaSfF2F4 (0x9, 0, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.d.s0",	f, OpXaSfF2F4 (0x9, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.d",	f, OpXaSfF2F4 (0x9, 0, 0, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.d.s1",	f, OpXaSfF2F4 (0x9, 0, 1, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.d.s2",	f, OpXaSfF2F4 (0x9, 0, 2, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fcvt.xuf.d.s3",	f, OpXaSfF2F4 (0x9, 0, 3, 0, 1), {F1, F3}, PSEUDO, 0, NULL},
    {"fadd.d.s0",	f, OpXaSfF4 (0x9, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.d",		f, OpXaSfF4 (0x9, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.d.s1",	f, OpXaSfF4 (0x9, 0, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.d.s2",	f, OpXaSfF4 (0x9, 0, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fadd.d.s3",	f, OpXaSfF4 (0x9, 0, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fmpy.d.s0",	f, OpXaSfF2 (0x9, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.d",		f, OpXaSfF2 (0x9, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.d.s1",	f, OpXaSfF2 (0x9, 0, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.d.s2",	f, OpXaSfF2 (0x9, 0, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fmpy.d.s3",	f, OpXaSfF2 (0x9, 0, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fma.d.s0",	f, OpXaSf (0x9, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fma.d",		f, OpXaSf (0x9, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fma.d.s1",	f, OpXaSf (0x9, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fma.d.s2",	f, OpXaSf (0x9, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fma.d.s3",	f, OpXaSf (0x9, 0, 3), {F1, F3, F4, F2}, EMPTY},

    {"fpmpy.s0",	f, OpXaSfF2 (0x9, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpmpy",		f, OpXaSfF2 (0x9, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpmpy.s1",	f, OpXaSfF2 (0x9, 1, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpmpy.s2",	f, OpXaSfF2 (0x9, 1, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpmpy.s3",	f, OpXaSfF2 (0x9, 1, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpma.s0",		f, OpXaSf   (0x9, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fpma",		f, OpXaSf   (0x9, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fpma.s1",		f, OpXaSf   (0x9, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fpma.s2",		f, OpXaSf   (0x9, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fpma.s3",		f, OpXaSf   (0x9, 1, 3), {F1, F3, F4, F2}, EMPTY},

    {"fsub.s0",		f, OpXaSfF4 (0xa, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub",		f, OpXaSfF4 (0xa, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s1",		f, OpXaSfF4 (0xa, 0, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s2",		f, OpXaSfF4 (0xa, 0, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s3",		f, OpXaSfF4 (0xa, 0, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s.s0",	f, OpXaSfF4 (0xa, 1, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s",		f, OpXaSfF4 (0xa, 1, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s.s1",	f, OpXaSfF4 (0xa, 1, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s.s2",	f, OpXaSfF4 (0xa, 1, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.s.s3",	f, OpXaSfF4 (0xa, 1, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fms.s0",		f, OpXaSf   (0xa, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fms",		f, OpXaSf   (0xa, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fms.s1",		f, OpXaSf   (0xa, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fms.s2",		f, OpXaSf   (0xa, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fms.s3",		f, OpXaSf   (0xa, 0, 3), {F1, F3, F4, F2}, EMPTY},
    {"fms.s.s0",	f, OpXaSf   (0xa, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fms.s",		f, OpXaSf   (0xa, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fms.s.s1",	f, OpXaSf   (0xa, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fms.s.s2",	f, OpXaSf   (0xa, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fms.s.s3",	f, OpXaSf   (0xa, 1, 3), {F1, F3, F4, F2}, EMPTY},
    {"fsub.d.s0",	f, OpXaSfF4 (0xb, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.d",		f, OpXaSfF4 (0xb, 0, 0, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.d.s1",	f, OpXaSfF4 (0xb, 0, 1, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.d.s2",	f, OpXaSfF4 (0xb, 0, 2, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fsub.d.s3",	f, OpXaSfF4 (0xb, 0, 3, 1), {F1, F3, F2}, PSEUDO, 0, NULL},
    {"fms.d.s0",	f, OpXaSf   (0xb, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fms.d",		f, OpXaSf   (0xb, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fms.d.s1",	f, OpXaSf   (0xb, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fms.d.s2",	f, OpXaSf   (0xb, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fms.d.s3",	f, OpXaSf   (0xb, 0, 3), {F1, F3, F4, F2}, EMPTY},

    {"fpms.s0",		f, OpXaSf (0xb, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fpms",		f, OpXaSf (0xb, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fpms.s1",		f, OpXaSf (0xb, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fpms.s2",		f, OpXaSf (0xb, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fpms.s3",		f, OpXaSf (0xb, 1, 3), {F1, F3, F4, F2}, EMPTY},

    {"fnmpy.s0",	f, OpXaSfF2 (0xc, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy",		f, OpXaSfF2 (0xc, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s1",	f, OpXaSfF2 (0xc, 0, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s2",	f, OpXaSfF2 (0xc, 0, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s3",	f, OpXaSfF2 (0xc, 0, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s.s0",	f, OpXaSfF2 (0xc, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s",		f, OpXaSfF2 (0xc, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s.s1",	f, OpXaSfF2 (0xc, 1, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s.s2",	f, OpXaSfF2 (0xc, 1, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.s.s3",	f, OpXaSfF2 (0xc, 1, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnma.s0",		f, OpXaSf (0xc, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fnma",		f, OpXaSf (0xc, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fnma.s1",		f, OpXaSf (0xc, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s2",		f, OpXaSf (0xc, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s3",		f, OpXaSf (0xc, 0, 3), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s.s0",	f, OpXaSf (0xc, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s",		f, OpXaSf (0xc, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fnma.s.s1",	f, OpXaSf (0xc, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s.s2",	f, OpXaSf (0xc, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fnma.s.s3",	f, OpXaSf (0xc, 1, 3), {F1, F3, F4, F2}, EMPTY},
    {"fnmpy.d.s0",	f, OpXaSfF2 (0xd, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.d",		f, OpXaSfF2 (0xd, 0, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.d.s1",	f, OpXaSfF2 (0xd, 0, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.d.s2",	f, OpXaSfF2 (0xd, 0, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnmpy.d.s3",	f, OpXaSfF2 (0xd, 0, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fnma.d.s0",	f, OpXaSf (0xd, 0, 0), {F1, F3, F4, F2}, EMPTY},
    {"fnma.d",		f, OpXaSf (0xd, 0, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fnma.d.s1",	f, OpXaSf (0xd, 0, 1), {F1, F3, F4, F2}, EMPTY},
    {"fnma.d.s2",	f, OpXaSf (0xd, 0, 2), {F1, F3, F4, F2}, EMPTY},
    {"fnma.d.s3",	f, OpXaSf (0xd, 0, 3), {F1, F3, F4, F2}, EMPTY},

    {"fpnmpy.s0",	f, OpXaSfF2 (0xd, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpnmpy",		f, OpXaSfF2 (0xd, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpnmpy.s1",	f, OpXaSfF2 (0xd, 1, 1, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpnmpy.s2",	f, OpXaSfF2 (0xd, 1, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpnmpy.s3",	f, OpXaSfF2 (0xd, 1, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"fpnma.s0",	f, OpXaSf   (0xd, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"fpnma",		f, OpXaSf   (0xd, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"fpnma.s1",	f, OpXaSf   (0xd, 1, 1), {F1, F3, F4, F2}, EMPTY},
    {"fpnma.s2",	f, OpXaSf   (0xd, 1, 2), {F1, F3, F4, F2}, EMPTY},
    {"fpnma.s3",	f, OpXaSf   (0xd, 1, 3), {F1, F3, F4, F2}, EMPTY},

    {"xmpy.l",		f, OpXaX2F2 (0xe, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"xmpy.lu",		f, OpXaX2F2 (0xe, 1, 0, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"xmpy.h",		f, OpXaX2F2 (0xe, 1, 3, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"xmpy.hu",		f, OpXaX2F2 (0xe, 1, 2, 0), {F1, F3, F4}, PSEUDO, 0, NULL},
    {"xma.l",		f, OpXaX2 (0xe, 1, 0), {F1, F3, F4, F2}, EMPTY},
    {"xma.lu",		f, OpXaX2 (0xe, 1, 0), {F1, F3, F4, F2}, PSEUDO, 0, NULL},
    {"xma.h",		f, OpXaX2 (0xe, 1, 3), {F1, F3, F4, F2}, EMPTY},
    {"xma.hu",		f, OpXaX2 (0xe, 1, 2), {F1, F3, F4, F2}, EMPTY},

    {"fselect",		f, OpXa (0xe, 0), {F1, F3, F4, F2}, EMPTY},

    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };

#undef f0
#undef f
#undef f2
#undef bF2
#undef bF4
#undef bQ
#undef bRa
#undef bRb
#undef bSf
#undef bTa
#undef bXa
#undef bXb
#undef bX2
#undef bX6
#undef mF2
#undef mF4
#undef mQ
#undef mRa
#undef mRb
#undef mSf
#undef mTa
#undef mXa
#undef mXb
#undef mX2
#undef mX6
#undef OpXa
#undef OpXaSf
#undef OpXaSfF2
#undef OpXaSfF4
#undef OpXaSfF2F4
#undef OpXaX2
#undef OpRaRbTaSf
#undef OpTa
#undef OpXbQSf
#undef OpXbX6
#undef OpXbX6F2
#undef OpXbX6Sf
#undef EMPTY

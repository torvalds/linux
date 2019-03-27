/* ia64-opc.h -- IA-64 opcode table.
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

#ifndef IA64_OPC_H
#define IA64_OPC_H

#include "opcode/ia64.h"

/* define a couple of abbreviations: */

#define bOp(x)	(((ia64_insn) ((x) & 0xf)) << 37)
#define mOp	bOp (-1)
#define Op(x)	bOp (x), mOp

#define FIRST		IA64_OPCODE_FIRST
#define X_IN_MLX	IA64_OPCODE_X_IN_MLX
#define LAST		IA64_OPCODE_LAST
#define PRIV		IA64_OPCODE_PRIV
#define NO_PRED		IA64_OPCODE_NO_PRED
#define SLOT2		IA64_OPCODE_SLOT2
#define PSEUDO		IA64_OPCODE_PSEUDO
#define F2_EQ_F3	IA64_OPCODE_F2_EQ_F3
#define LEN_EQ_64MCNT	IA64_OPCODE_LEN_EQ_64MCNT
#define MOD_RRBS        IA64_OPCODE_MOD_RRBS
#define POSTINC		IA64_OPCODE_POSTINC

#define AR_CCV	IA64_OPND_AR_CCV
#define AR_PFS	IA64_OPND_AR_PFS
#define AR_CSD	IA64_OPND_AR_CSD
#define C1	IA64_OPND_C1
#define C8	IA64_OPND_C8
#define C16	IA64_OPND_C16
#define GR0	IA64_OPND_GR0
#define IP	IA64_OPND_IP
#define PR	IA64_OPND_PR
#define PR_ROT	IA64_OPND_PR_ROT
#define PSR	IA64_OPND_PSR
#define PSR_L	IA64_OPND_PSR_L
#define PSR_UM	IA64_OPND_PSR_UM

#define AR3	IA64_OPND_AR3
#define B1	IA64_OPND_B1
#define B2	IA64_OPND_B2
#define CR3	IA64_OPND_CR3
#define F1	IA64_OPND_F1
#define F2	IA64_OPND_F2
#define F3	IA64_OPND_F3
#define F4	IA64_OPND_F4
#define P1	IA64_OPND_P1
#define P2	IA64_OPND_P2
#define R1	IA64_OPND_R1
#define R2	IA64_OPND_R2
#define R3	IA64_OPND_R3
#define R3_2	IA64_OPND_R3_2

#define CPUID_R3 IA64_OPND_CPUID_R3
#define DBR_R3	IA64_OPND_DBR_R3
#define DTR_R3	IA64_OPND_DTR_R3
#define ITR_R3	IA64_OPND_ITR_R3
#define IBR_R3	IA64_OPND_IBR_R3
#define MR3	IA64_OPND_MR3
#define MSR_R3	IA64_OPND_MSR_R3
#define PKR_R3	IA64_OPND_PKR_R3
#define PMC_R3	IA64_OPND_PMC_R3
#define PMD_R3	IA64_OPND_PMD_R3
#define RR_R3	IA64_OPND_RR_R3

#define CCNT5	IA64_OPND_CCNT5
#define CNT2a	IA64_OPND_CNT2a
#define CNT2b	IA64_OPND_CNT2b
#define CNT2c	IA64_OPND_CNT2c
#define CNT5	IA64_OPND_CNT5
#define CNT6	IA64_OPND_CNT6
#define CPOS6a	IA64_OPND_CPOS6a
#define CPOS6b	IA64_OPND_CPOS6b
#define CPOS6c	IA64_OPND_CPOS6c
#define IMM1	IA64_OPND_IMM1
#define IMM14	IA64_OPND_IMM14
#define IMM17	IA64_OPND_IMM17
#define IMM22	IA64_OPND_IMM22
#define IMM44	IA64_OPND_IMM44
#define SOF	IA64_OPND_SOF
#define SOL	IA64_OPND_SOL
#define SOR	IA64_OPND_SOR
#define IMM8	IA64_OPND_IMM8
#define IMM8U4	IA64_OPND_IMM8U4
#define IMM8M1	IA64_OPND_IMM8M1
#define IMM8M1U4 IA64_OPND_IMM8M1U4
#define IMM8M1U8 IA64_OPND_IMM8M1U8
#define IMM9a	IA64_OPND_IMM9a
#define IMM9b	IA64_OPND_IMM9b
#define IMMU2	IA64_OPND_IMMU2
#define IMMU21	IA64_OPND_IMMU21
#define IMMU24	IA64_OPND_IMMU24
#define IMMU62	IA64_OPND_IMMU62
#define IMMU64	IA64_OPND_IMMU64
#define IMMU5b	IA64_OPND_IMMU5b
#define IMMU7a	IA64_OPND_IMMU7a
#define IMMU7b	IA64_OPND_IMMU7b
#define IMMU9	IA64_OPND_IMMU9
#define INC3	IA64_OPND_INC3
#define LEN4	IA64_OPND_LEN4
#define LEN6	IA64_OPND_LEN6
#define MBTYPE4	IA64_OPND_MBTYPE4
#define MHTYPE8	IA64_OPND_MHTYPE8
#define POS6	IA64_OPND_POS6
#define TAG13	IA64_OPND_TAG13
#define TAG13b	IA64_OPND_TAG13b
#define TGT25	IA64_OPND_TGT25
#define TGT25b	IA64_OPND_TGT25b
#define TGT25c	IA64_OPND_TGT25c
#define TGT64   IA64_OPND_TGT64

#endif

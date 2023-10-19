// SPDX-License-Identifier: GPL-2.0-or-later
/* SPU opcode list

   Copyright 2006 Free Software Foundation, Inc.

   This file is part of GDB, GAS, and the GNU binutils.

 */

#include <linux/kernel.h>
#include <linux/bug.h>
#include "spu.h"

/* This file holds the Spu opcode table */


/*
   Example contents of spu-insn.h
      id_tag	mode	mode	type	opcode	mnemonic	asmtype	    dependency		FPU	L/S?	branch?	instruction   
                QUAD	WORD                                               (0,RC,RB,RA,RT)    latency  			              		
   APUOP(M_LQD,	1,	0,	RI9,	0x1f8,	"lqd",		ASM_RI9IDX,	00012,		FXU,	1,	0)	Load Quadword d-form 
 */

const struct spu_opcode spu_opcodes[] = {
#define APUOP(TAG,MACFORMAT,OPCODE,MNEMONIC,ASMFORMAT,DEP,PIPE) \
	{ MACFORMAT, OPCODE, MNEMONIC, ASMFORMAT },
#define APUOPFB(TAG,MACFORMAT,OPCODE,FB,MNEMONIC,ASMFORMAT,DEP,PIPE) \
	{ MACFORMAT, OPCODE, MNEMONIC, ASMFORMAT },
#include "spu-insns.h"
#undef APUOP
#undef APUOPFB
};

const int spu_num_opcodes = ARRAY_SIZE(spu_opcodes);

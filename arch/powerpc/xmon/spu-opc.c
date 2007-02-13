/* SPU opcode list

   Copyright 2006 Free Software Foundation, Inc.

   This file is part of GDB, GAS, and the GNU binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <linux/kernel.h>
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

/* Declarations for Intel 80386 opcode table
   Copyright 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "opcode/i386.h"

typedef struct template
{
  /* instruction name sans width suffix ("mov" for movl insns) */
  char *name;

  /* how many operands */
  unsigned int operands;

  /* base_opcode is the fundamental opcode byte without optional
     prefix(es).  */
  unsigned int base_opcode;
#define Opcode_D	0x2 /* Direction bit:
			       set if Reg --> Regmem;
			       unset if Regmem --> Reg. */
#define Opcode_FloatR	0x8 /* Bit to swap src/dest for float insns. */
#define Opcode_FloatD 0x400 /* Direction bit for float insns. */

  /* extension_opcode is the 3 bit extension for group <n> insns.
     This field is also used to store the 8-bit opcode suffix for the
     AMD 3DNow! instructions.
     If this template has no extension opcode (the usual case) use None */
  unsigned int extension_opcode;
#define None 0xffff		/* If no extension_opcode is possible.  */

  /* cpu feature flags */
  unsigned int cpu_flags;
#define Cpu186		  0x1	/* i186 or better required */
#define Cpu286		  0x2	/* i286 or better required */
#define Cpu386		  0x4	/* i386 or better required */
#define Cpu486		  0x8	/* i486 or better required */
#define Cpu586		 0x10	/* i585 or better required */
#define Cpu686		 0x20	/* i686 or better required */
#define CpuP4		 0x40	/* Pentium4 or better required */
#define CpuK6		 0x80	/* AMD K6 or better required*/
#define CpuSledgehammer 0x100	/* Sledgehammer or better required */
#define CpuMMX		0x200	/* MMX support required */
#define CpuMMX2		0x400	/* extended MMX support (with SSE or 3DNow!Ext) required */
#define CpuSSE		0x800	/* Streaming SIMD extensions required */
#define CpuSSE2	       0x1000	/* Streaming SIMD extensions 2 required */
#define Cpu3dnow       0x2000	/* 3dnow! support required */
#define Cpu3dnowA      0x4000	/* 3dnow!Extensions support required */
#define CpuSSE3	       0x8000	/* Streaming SIMD extensions 3 required */
#define CpuPadLock    0x10000	/* VIA PadLock required */
#define CpuSVME	      0x20000	/* AMD Secure Virtual Machine Ext-s required */
#define CpuVMX	      0x40000	/* VMX Instructions required */
#define CpuSSSE3      0x80000	/* Supplemental Streaming SIMD extensions 3 required */
#define CpuSSE4a     0x100000   /* SSE4a New Instuctions required */
#define CpuABM       0x200000   /* ABM New Instructions required */
#define CpuSSE4_1    0x400000	/* SSE4.1 Instructions required */
#define CpuSSE4_2    0x800000	/* SSE4.2 Instructions required */
#define CpuXSAVE    0x1000000	/* XSAVE Instructions required */
#define CpuAES      0x2000000	/* AES Instructions required */

  /* These flags are set by gas depending on the flag_code.  */
#define Cpu64	     0x4000000   /* 64bit support required  */
#define CpuNo64      0x8000000   /* Not supported in the 64bit mode  */

#define CpuPCLMUL   0x10000000	/* Carry-less Multiplication extensions */
#define CpuRdRnd    0x20000000	/* Intel Random Number Generator extensions */
#define CpuSMAP     0x40000000	/* Intel Supervisor Mode Access Prevention */
#define CpuFSGSBase 0x80000000	/* Read/write fs/gs segment base registers */

/* SSE4.1/4.2 Instructions required */
#define CpuSSE4	     (CpuSSE4_1|CpuSSE4_2)

  /* The default value for unknown CPUs - enable all features to avoid problems.  */
#define CpuUnknownFlags (Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686 \
	|CpuP4|CpuSledgehammer|CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuVMX \
	|Cpu3dnow|Cpu3dnowA|CpuK6|CpuPadLock|CpuSVME|CpuSSSE3|CpuSSE4_1 \
	|CpuSSE4_2|CpuABM|CpuSSE4a|CpuXSAVE|CpuAES|CpuPCLMUL|CpuRdRnd|CpuSMAP \
	|CpuFSGSBase)

  /* the bits in opcode_modifier are used to generate the final opcode from
     the base_opcode.  These bits also are used to detect alternate forms of
     the same instruction */
  unsigned int opcode_modifier;

  /* opcode_modifier bits: */
#define D		   0x1	/* has direction bit. */
#define W		   0x2	/* set if operands can be words or dwords
				   encoded the canonical way */
#define Modrm		   0x4	/* insn has a modrm byte. */
#define ShortForm	   0x8	/* register is in low 3 bits of opcode */
#define Jump		  0x10	/* special case for jump insns.  */
#define JumpDword	  0x20  /* call and jump */
#define JumpByte	  0x40  /* loop and jecxz */
#define JumpInterSegment  0x80	/* special case for intersegment leaps/calls */
#define FloatMF		 0x100	/* FP insn memory format bit, sized by 0x4 */
#define FloatR		 0x200	/* src/dest swap for floats. */
#define FloatD		 0x400	/* has float insn direction bit. */
#define Size16		 0x800	/* needs size prefix if in 32-bit mode */
#define Size32		0x1000	/* needs size prefix if in 16-bit mode */
#define Size64		0x2000	/* needs size prefix if in 64-bit mode */
#define IgnoreSize      0x4000  /* instruction ignores operand size prefix */
#define DefaultSize     0x8000  /* default insn size depends on mode */
#define No_bSuf	       0x10000	/* b suffix on instruction illegal */
#define No_wSuf	       0x20000	/* w suffix on instruction illegal */
#define No_lSuf	       0x40000 	/* l suffix on instruction illegal */
#define No_sSuf	       0x80000	/* s suffix on instruction illegal */
#define No_qSuf       0x100000  /* q suffix on instruction illegal */
#define No_xSuf       0x200000  /* x suffix on instruction illegal */
#define FWait	      0x400000	/* instruction needs FWAIT */
#define IsString      0x800000	/* quick test for string instructions */
#define RegKludge    0x1000000	/* fake an extra reg operand for clr, imul
				   and special register processing for
				   some instructions.  */
#define IsPrefix     0x2000000	/* opcode is a prefix */
#define ImmExt	     0x4000000	/* instruction has extension in 8 bit imm */
#define NoRex64	     0x8000000  /* instruction don't need Rex64 prefix.  */
#define Rex64	    0x10000000  /* instruction require Rex64 prefix.  */
#define Ugh	    0x20000000	/* deprecated fp insn, gets a warning */

#define NoSuf		(No_bSuf|No_wSuf|No_lSuf|No_sSuf|No_qSuf|No_xSuf)

  /* operand_types[i] describes the type of operand i.  This is made
     by OR'ing together all of the possible type masks.  (e.g.
     'operand_types[i] = Reg|Imm' specifies that operand i can be
     either a register or an immediate operand.  */
  unsigned int operand_types[MAX_OPERANDS];

  /* operand_types[i] bits */
  /* register */
#define Reg8		   0x1	/* 8 bit reg */
#define Reg16		   0x2	/* 16 bit reg */
#define Reg32		   0x4	/* 32 bit reg */
#define Reg64		   0x8	/* 64 bit reg */
  /* immediate */
#define Imm8		  0x10	/* 8 bit immediate */
#define Imm8S		  0x20	/* 8 bit immediate sign extended */
#define Imm16		  0x40	/* 16 bit immediate */
#define Imm32		  0x80	/* 32 bit immediate */
#define Imm32S		 0x100	/* 32 bit immediate sign extended */
#define Imm64		 0x200	/* 64 bit immediate */
#define Imm1		 0x400	/* 1 bit immediate */
  /* memory */
#define BaseIndex	 0x800
  /* Disp8,16,32 are used in different ways, depending on the
     instruction.  For jumps, they specify the size of the PC relative
     displacement, for baseindex type instructions, they specify the
     size of the offset relative to the base register, and for memory
     offset instructions such as `mov 1234,%al' they specify the size of
     the offset relative to the segment base.  */
#define Disp8		0x1000	/* 8 bit displacement */
#define Disp16		0x2000	/* 16 bit displacement */
#define Disp32		0x4000	/* 32 bit displacement */
#define Disp32S	        0x8000	/* 32 bit signed displacement */
#define Disp64	       0x10000	/* 64 bit displacement */
  /* specials */
#define InOutPortReg   0x20000	/* register to hold in/out port addr = dx */
#define ShiftCount     0x40000	/* register to hold shift count = cl */
#define Control	       0x80000	/* Control register */
#define Debug	      0x100000	/* Debug register */
#define Test	      0x200000	/* Test register */
#define FloatReg      0x400000	/* Float register */
#define FloatAcc      0x800000	/* Float stack top %st(0) */
#define SReg2	     0x1000000	/* 2 bit segment register */
#define SReg3	     0x2000000	/* 3 bit segment register */
#define Acc	     0x4000000	/* Accumulator %al or %ax or %eax */
#define JumpAbsolute 0x8000000
#define RegMMX	    0x10000000	/* MMX register */
#define RegXMM	    0x20000000	/* XMM registers in PIII */
#define EsSeg	    0x40000000	/* String insn operand with fixed es segment */

  /* RegMem is for instructions with a modrm byte where the register
     destination operand should be encoded in the mod and regmem fields.
     Normally, it will be encoded in the reg field. We add a RegMem
     flag to the destination register operand to indicate that it should
     be encoded in the regmem field.  */
#define RegMem	    0x80000000

#define Reg	(Reg8|Reg16|Reg32|Reg64) /* gen'l register */
#define WordReg (Reg16|Reg32|Reg64)
#define ImplicitRegister (InOutPortReg|ShiftCount|Acc|FloatAcc)
#define Imm	(Imm8|Imm8S|Imm16|Imm32S|Imm32|Imm64) /* gen'l immediate */
#define EncImm	(Imm8|Imm16|Imm32|Imm32S) /* Encodable gen'l immediate */
#define Disp	(Disp8|Disp16|Disp32|Disp32S|Disp64) /* General displacement */
#define AnyMem	(Disp8|Disp16|Disp32|Disp32S|BaseIndex)	/* General memory */
  /* The following aliases are defined because the opcode table
     carefully specifies the allowed memory types for each instruction.
     At the moment we can only tell a memory reference size by the
     instruction suffix, so there's not much point in defining Mem8,
     Mem16, Mem32 and Mem64 opcode modifiers - We might as well just use
     the suffix directly to check memory operands.  */
#define LLongMem AnyMem		/* 64 bits (or more) */
#define LongMem AnyMem		/* 32 bit memory ref */
#define ShortMem AnyMem		/* 16 bit memory ref */
#define WordMem AnyMem		/* 16, 32 or 64 bit memory ref */
#define ByteMem AnyMem		/* 8 bit memory ref */
}
template;

extern const template i386_optab[];

/* these are for register name --> number & type hash lookup */
typedef struct
{
  char *reg_name;
  unsigned int reg_type;
  unsigned int reg_flags;
#define RegRex	    0x1  /* Extended register.  */
#define RegRex64    0x2  /* Extended 8 bit register.  */
  unsigned int reg_num;
}
reg_entry;

/* Entries in i386_regtab.  */
#define REGNAM_AL 1
#define REGNAM_AX 25
#define REGNAM_EAX 41

extern const reg_entry i386_regtab[];
extern const unsigned int i386_regtab_size;

typedef struct
{
  char *seg_name;
  unsigned int seg_prefix;
}
seg_entry;

extern const seg_entry cs;
extern const seg_entry ds;
extern const seg_entry ss;
extern const seg_entry es;
extern const seg_entry fs;
extern const seg_entry gs;

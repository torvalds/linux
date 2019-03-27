/* opcode/i386.h -- Intel 80386 opcode macros
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler, and GDB, the GNU Debugger.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* The SystemV/386 SVR3.2 assembler, and probably all AT&T derived
   ix86 Unix assemblers, generate floating point instructions with
   reversed source and destination registers in certain cases.
   Unfortunately, gcc and possibly many other programs use this
   reversed syntax, so we're stuck with it.

   eg. `fsub %st(3),%st' results in st = st - st(3) as expected, but
   `fsub %st,%st(3)' results in st(3) = st - st(3), rather than
   the expected st(3) = st(3) - st

   This happens with all the non-commutative arithmetic floating point
   operations with two register operands, where the source register is
   %st, and destination register is %st(i).

   The affected opcode map is dceX, dcfX, deeX, defX.  */

#ifndef SYSV386_COMPAT
/* Set non-zero for broken, compatible instructions.  Set to zero for
   non-broken opcodes at your peril.  gcc generates SystemV/386
   compatible instructions.  */
#define SYSV386_COMPAT 1
#endif
#ifndef OLDGCC_COMPAT
/* Set non-zero to cater for old (<= 2.8.1) versions of gcc that could
   generate nonsense fsubp, fsubrp, fdivp and fdivrp with operands
   reversed.  */
#define OLDGCC_COMPAT SYSV386_COMPAT
#endif

#define MOV_AX_DISP32 0xa0
#define POP_SEG_SHORT 0x07
#define JUMP_PC_RELATIVE 0xeb
#define INT_OPCODE  0xcd
#define INT3_OPCODE 0xcc
/* The opcode for the fwait instruction, which disassembler treats as a
   prefix when it can.  */
#define FWAIT_OPCODE 0x9b
#define ADDR_PREFIX_OPCODE 0x67
#define DATA_PREFIX_OPCODE 0x66
#define LOCK_PREFIX_OPCODE 0xf0
#define CS_PREFIX_OPCODE 0x2e
#define DS_PREFIX_OPCODE 0x3e
#define ES_PREFIX_OPCODE 0x26
#define FS_PREFIX_OPCODE 0x64
#define GS_PREFIX_OPCODE 0x65
#define SS_PREFIX_OPCODE 0x36
#define REPNE_PREFIX_OPCODE 0xf2
#define REPE_PREFIX_OPCODE  0xf3

#define TWO_BYTE_OPCODE_ESCAPE 0x0f
#define NOP_OPCODE (char) 0x90

/* register numbers */
#define EBP_REG_NUM 5
#define ESP_REG_NUM 4

/* modrm_byte.regmem for twobyte escape */
#define ESCAPE_TO_TWO_BYTE_ADDRESSING ESP_REG_NUM
/* index_base_byte.index for no index register addressing */
#define NO_INDEX_REGISTER ESP_REG_NUM
/* index_base_byte.base for no base register addressing */
#define NO_BASE_REGISTER EBP_REG_NUM
#define NO_BASE_REGISTER_16 6

/* modrm.mode = REGMEM_FIELD_HAS_REG when a register is in there */
#define REGMEM_FIELD_HAS_REG 0x3/* always = 0x3 */
#define REGMEM_FIELD_HAS_MEM (~REGMEM_FIELD_HAS_REG)

/* x86-64 extension prefix.  */
#define REX_OPCODE	0x40

/* Indicates 64 bit operand size.  */
#define REX_W	8
/* High extension to reg field of modrm byte.  */
#define REX_R	4
/* High extension to SIB index field.  */
#define REX_X	2
/* High extension to base field of modrm or SIB, or reg field of opcode.  */
#define REX_B	1

/* max operands per insn */
#define MAX_OPERANDS 4

/* max immediates per insn (lcall, ljmp, insertq, extrq) */
#define MAX_IMMEDIATE_OPERANDS 2

/* max memory refs per insn (string ops) */
#define MAX_MEMORY_OPERANDS 2

/* max size of insn mnemonics.  */
#define MAX_MNEM_SIZE 16

/* max size of register name in insn mnemonics.  */
#define MAX_REG_NAME_SIZE 8

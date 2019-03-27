/* mips16-opc.c.  Mips16 opcode table.
   Copyright 1996, 1997, 1998, 2000, 2005 Free Software Foundation, Inc.
   Contributed by Ian Lance Taylor, Cygnus Support

This file is part of GDB, GAS, and the GNU binutils.

GDB, GAS, and the GNU binutils are free software; you can redistribute
them and/or modify them under the terms of the GNU General Public
License as published by the Free Software Foundation; either version
1, or (at your option) any later version.

GDB, GAS, and the GNU binutils are distributed in the hope that they
will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this file; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

#include <stdio.h>
#include "sysdep.h"
#include "opcode/mips.h"

/* This is the opcodes table for the mips16 processor.  The format of
   this table is intentionally identical to the one in mips-opc.c.
   However, the special letters that appear in the argument string are
   different, and the table uses some different flags.  */

/* Use some short hand macros to keep down the length of the lines in
   the opcodes table.  */

#define UBD     INSN_UNCOND_BRANCH_DELAY
#define BR      MIPS16_INSN_BRANCH

#define WR_x	MIPS16_INSN_WRITE_X
#define WR_y	MIPS16_INSN_WRITE_Y
#define WR_z	MIPS16_INSN_WRITE_Z
#define WR_T	MIPS16_INSN_WRITE_T
#define WR_SP	MIPS16_INSN_WRITE_SP
#define WR_31	MIPS16_INSN_WRITE_31
#define WR_Y	MIPS16_INSN_WRITE_GPR_Y

#define RD_x	MIPS16_INSN_READ_X
#define RD_y	MIPS16_INSN_READ_Y
#define RD_Z	MIPS16_INSN_READ_Z
#define RD_T	MIPS16_INSN_READ_T
#define RD_SP	MIPS16_INSN_READ_SP
#define RD_31	MIPS16_INSN_READ_31
#define RD_PC	MIPS16_INSN_READ_PC
#define RD_X	MIPS16_INSN_READ_GPR_X

#define WR_HI	INSN_WRITE_HI
#define WR_LO	INSN_WRITE_LO
#define RD_HI	INSN_READ_HI
#define RD_LO	INSN_READ_LO

#define TRAP	INSN_TRAP

#define I1	INSN_ISA1
#define I3	INSN_ISA3
#define I32	INSN_ISA32
#define I64	INSN_ISA64
#define T3	INSN_3900

const struct mips_opcode mips16_opcodes[] =
{
/* name,    args,	match,	mask,	pinfo,         	pinfo2, membership */
{"nop",	    "",		0x6500, 0xffff, RD_Z,		0,	I1 }, /* move $0,$Z */
{"la",	    "x,A",	0x0800, 0xf800, WR_x|RD_PC,	0,	I1 },
{"abs",	    "x,w",	0, (int) M_ABS, INSN_MACRO,	0,	I1 },
{"addiu",   "y,x,4",	0x4000, 0xf810, WR_y|RD_x,	0,	I1 },
{"addiu",   "x,k",	0x4800, 0xf800, WR_x|RD_x,	0,	I1 },
{"addiu",   "S,K",	0x6300, 0xff00, WR_SP|RD_SP,	0,	I1 },
{"addiu",   "S,S,K",	0x6300, 0xff00, WR_SP|RD_SP,	0,	I1 },
{"addiu",   "x,P,V",	0x0800, 0xf800, WR_x|RD_PC,	0,	I1 },
{"addiu",   "x,S,V",	0x0000, 0xf800, WR_x|RD_SP,	0,	I1 },
{"addu",    "z,v,y",	0xe001, 0xf803, WR_z|RD_x|RD_y,	0,	I1 },
{"addu",    "y,x,4",	0x4000, 0xf810, WR_y|RD_x,	0,	I1 },
{"addu",    "x,k",	0x4800, 0xf800, WR_x|RD_x,	0,	I1 },
{"addu",    "S,K",	0x6300, 0xff00, WR_SP|RD_SP,	0,	I1 },
{"addu",    "S,S,K",	0x6300, 0xff00, WR_SP|RD_SP,	0,	I1 },
{"addu",    "x,P,V",	0x0800, 0xf800, WR_x|RD_PC,	0,	I1 },
{"addu",    "x,S,V",	0x0000, 0xf800, WR_x|RD_SP,	0,	I1 },
{"and",	    "x,y",	0xe80c, 0xf81f, WR_x|RD_x|RD_y,	0,	I1 },
{"b",	    "q",	0x1000, 0xf800, BR,		0,	I1 },
{"beq",	    "x,y,p",	0, (int) M_BEQ, INSN_MACRO,	0,	I1 },
{"beq",     "x,U,p",	0, (int) M_BEQ_I, INSN_MACRO,	0,	I1 },
{"beqz",    "x,p",	0x2000, 0xf800, BR|RD_x,	0,	I1 },
{"bge",	    "x,y,p",	0, (int) M_BGE, INSN_MACRO,	0,	I1 },
{"bge",     "x,8,p",	0, (int) M_BGE_I, INSN_MACRO,	0,	I1 },
{"bgeu",    "x,y,p",	0, (int) M_BGEU, INSN_MACRO,	0,	I1 },
{"bgeu",    "x,8,p",	0, (int) M_BGEU_I, INSN_MACRO,	0,	I1 },
{"bgt",	    "x,y,p",	0, (int) M_BGT, INSN_MACRO,	0,	I1 },
{"bgt",     "x,8,p",	0, (int) M_BGT_I, INSN_MACRO,	0,	I1 },
{"bgtu",    "x,y,p",	0, (int) M_BGTU, INSN_MACRO,	0,	I1 },
{"bgtu",    "x,8,p",	0, (int) M_BGTU_I, INSN_MACRO,	0,	I1 },
{"ble",	    "x,y,p",	0, (int) M_BLE, INSN_MACRO,	0,	I1 },
{"ble",     "x,8,p",	0, (int) M_BLE_I, INSN_MACRO,	0,	I1 },
{"bleu",    "x,y,p",	0, (int) M_BLEU, INSN_MACRO,	0,	I1 },
{"bleu",    "x,8,p",	0, (int) M_BLEU_I, INSN_MACRO,	0,	I1 },
{"blt",	    "x,y,p",	0, (int) M_BLT, INSN_MACRO,	0,	I1 },
{"blt",     "x,8,p",	0, (int) M_BLT_I, INSN_MACRO,	0,	I1 },
{"bltu",    "x,y,p",	0, (int) M_BLTU, INSN_MACRO,	0,	I1 },
{"bltu",    "x,8,p",	0, (int) M_BLTU_I, INSN_MACRO,	0,	I1 },
{"bne",	    "x,y,p",	0, (int) M_BNE, INSN_MACRO,	0,	I1 },
{"bne",     "x,U,p",	0, (int) M_BNE_I, INSN_MACRO,	0,	I1 },
{"bnez",    "x,p",	0x2800, 0xf800, BR|RD_x,	0,	I1 },
{"break",   "6",	0xe805, 0xf81f, TRAP,		0,	I1 },
{"bteqz",   "p",	0x6000, 0xff00, BR|RD_T,	0,	I1 },
{"btnez",   "p",	0x6100, 0xff00, BR|RD_T,	0,	I1 },
{"cmpi",    "x,U",	0x7000, 0xf800, WR_T|RD_x,	0,	I1 },
{"cmp",	    "x,y",	0xe80a, 0xf81f, WR_T|RD_x|RD_y,	0,	I1 },
{"cmp",     "x,U",	0x7000, 0xf800, WR_T|RD_x,	0,	I1 },
{"dla",	    "y,E",	0xfe00, 0xff00, WR_y|RD_PC, 	0,	I3 },
{"daddiu",  "y,x,4",	0x4010, 0xf810, WR_y|RD_x, 	0,	I3 },
{"daddiu",  "y,j",	0xfd00, 0xff00, WR_y|RD_y, 	0,	I3 },
{"daddiu",  "S,K",	0xfb00, 0xff00, WR_SP|RD_SP, 	0,	I3 },
{"daddiu",  "S,S,K",	0xfb00, 0xff00, WR_SP|RD_SP, 	0,	I3 },
{"daddiu",  "y,P,W",	0xfe00, 0xff00, WR_y|RD_PC, 	0,	I3 },
{"daddiu",  "y,S,W",	0xff00, 0xff00, WR_y|RD_SP, 	0,	I3 },
{"daddu",   "z,v,y",	0xe000, 0xf803, WR_z|RD_x|RD_y, 0,	I3 },
{"daddu",   "y,x,4",	0x4010, 0xf810, WR_y|RD_x, 	0,	I3 },
{"daddu",   "y,j",	0xfd00, 0xff00, WR_y|RD_y, 	0,	I3 },
{"daddu",   "S,K",	0xfb00, 0xff00, WR_SP|RD_SP, 	0,	I3 },
{"daddu",   "S,S,K",	0xfb00, 0xff00, WR_SP|RD_SP, 	0,	I3 },
{"daddu",   "y,P,W",	0xfe00, 0xff00, WR_y|RD_PC, 	0,	I3 },
{"daddu",   "y,S,W",	0xff00, 0xff00, WR_y|RD_SP, 	0,	I3 },
{"ddiv",    "0,x,y",	0xe81e, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 	0,	I3 },
{"ddiv",    "z,v,y",	0, (int) M_DDIV_3, INSN_MACRO,	0,	I1 },
{"ddivu",   "0,x,y",	0xe81f, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 0,	I3 },
{"ddivu",   "z,v,y",	0, (int) M_DDIVU_3, INSN_MACRO,	0,	I1 },
{"div",     "0,x,y",	0xe81a, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"div",     "z,v,y",	0, (int) M_DIV_3, INSN_MACRO,	0,	I1 },
{"divu",    "0,x,y",	0xe81b, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"divu",    "z,v,y",	0, (int) M_DIVU_3, INSN_MACRO,	0,	I1 },
{"dmul",    "z,v,y",	0, (int) M_DMUL, INSN_MACRO, 	0,	I3 },
{"dmult",   "x,y",	0xe81c, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 0,	I3 },
{"dmultu",  "x,y",	0xe81d, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 0,	I3 },
{"drem",    "0,x,y",	0xe81e, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 0,	I3 },
{"drem",    "z,v,y",	0, (int) M_DREM_3, INSN_MACRO,	0,	I1 },
{"dremu",   "0,x,y",	0xe81f, 0xf81f, RD_x|RD_y|WR_HI|WR_LO, 0,	I3 },
{"dremu",   "z,v,y",	0, (int) M_DREMU_3, INSN_MACRO,	0,	I1 },
{"dsllv",   "y,x",	0xe814, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsll",    "x,w,[",	0x3001, 0xf803, WR_x|RD_y, 	0,	I3 },
{"dsll",    "y,x",	0xe814, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsrav",   "y,x",	0xe817, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsra",    "y,]",	0xe813, 0xf81f, WR_y|RD_y, 	0,	I3 },
{"dsra",    "y,x",	0xe817, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsrlv",   "y,x",	0xe816, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsrl",    "y,]",	0xe808, 0xf81f, WR_y|RD_y, 	0,	I3 },
{"dsrl",    "y,x",	0xe816, 0xf81f, WR_y|RD_y|RD_x, 0,	I3 },
{"dsubu",   "z,v,y",	0xe002, 0xf803, WR_z|RD_x|RD_y, 0,	I3 },
{"dsubu",   "y,x,4",	0, (int) M_DSUBU_I, INSN_MACRO,	0,	I1 },
{"dsubu",   "y,j",	0, (int) M_DSUBU_I_2, INSN_MACRO, 0,	I1 },
{"exit",    "L",	0xed09, 0xff1f, TRAP,		0,	I1 },
{"exit",    "L",	0xee09, 0xff1f, TRAP,		0,	I1 },
{"exit",    "L",	0xef09, 0xff1f, TRAP,		0,	I1 },
{"entry",   "l",	0xe809, 0xf81f, TRAP,		0,	I1 },
{"extend",  "e",	0xf000, 0xf800, 0,		0,	I1 },
{"jalr",    "x",	0xe840, 0xf8ff, UBD|WR_31|RD_x,	0,	I1 },
{"jalr",    "R,x",	0xe840, 0xf8ff, UBD|WR_31|RD_x,	0,	I1 },
{"jal",     "x",	0xe840, 0xf8ff, UBD|WR_31|RD_x,	0,	I1 },
{"jal",     "R,x",	0xe840, 0xf8ff, UBD|WR_31|RD_x,	0,	I1 },
{"jal",	    "a",	0x1800, 0xfc00, UBD|WR_31,	0,	I1 },
{"jalx",    "a",	0x1c00, 0xfc00, UBD|WR_31,	0,	I1 },
{"jr",	    "x",	0xe800, 0xf8ff, UBD|RD_x,	0,	I1 },
{"jr",	    "R",	0xe820, 0xffff, UBD|RD_31,	0,	I1 },
{"j",	    "x",	0xe800, 0xf8ff, UBD|RD_x,	0,	I1 },
{"j",	    "R",	0xe820, 0xffff, UBD|RD_31,	0,	I1 },
{"lb",	    "y,5(x)",	0x8000, 0xf800, WR_y|RD_x,	0,	I1 },
{"lbu",	    "y,5(x)",	0xa000, 0xf800, WR_y|RD_x,	0,	I1 },
{"ld",	    "y,D(x)",	0x3800, 0xf800, WR_y|RD_x, 	0,	I3 },
{"ld",	    "y,B",	0xfc00, 0xff00, WR_y|RD_PC, 	0,	I3 },
{"ld",	    "y,D(P)",	0xfc00, 0xff00, WR_y|RD_PC, 	0,	I3 },
{"ld",	    "y,D(S)",	0xf800, 0xff00, WR_y|RD_SP, 	0,	I3 },
{"lh",	    "y,H(x)",	0x8800, 0xf800, WR_y|RD_x,	0,	I1 },
{"lhu",	    "y,H(x)",	0xa800, 0xf800, WR_y|RD_x,	0,	I1 },
{"li",	    "x,U",	0x6800, 0xf800, WR_x,		0,	I1 },
{"lw",	    "y,W(x)",	0x9800, 0xf800, WR_y|RD_x,	0,	I1 },
{"lw",	    "x,A",	0xb000, 0xf800, WR_x|RD_PC,	0,	I1 },
{"lw",	    "x,V(P)",	0xb000, 0xf800, WR_x|RD_PC,	0,	I1 },
{"lw",	    "x,V(S)",	0x9000, 0xf800, WR_x|RD_SP,	0,	I1 },
{"lwu",     "y,W(x)",	0xb800, 0xf800, WR_y|RD_x, 	0,	I3 },
{"mfhi",    "x",	0xe810, 0xf8ff, WR_x|RD_HI,	0,	I1 },
{"mflo",    "x",	0xe812, 0xf8ff, WR_x|RD_LO,	0,	I1 },
{"move",    "y,X",	0x6700, 0xff00, WR_y|RD_X, 	0,	I1 },
{"move",    "Y,Z",	0x6500, 0xff00, WR_Y|RD_Z,	0,	I1 },
{"mul",     "z,v,y",	0, (int) M_MUL, INSN_MACRO,	0,	I1 },
{"mult",    "x,y",	0xe818, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"multu",   "x,y",	0xe819, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"neg",	    "x,w",	0xe80b, 0xf81f, WR_x|RD_y,	0,	I1 },
{"not",	    "x,w",	0xe80f, 0xf81f, WR_x|RD_y,	0,	I1 },
{"or",	    "x,y",	0xe80d, 0xf81f, WR_x|RD_x|RD_y,	0,	I1 },
{"rem",     "0,x,y",	0xe81a, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"rem",     "z,v,y",	0, (int) M_REM_3, INSN_MACRO,	0,	I1 },
{"remu",    "0,x,y",	0xe81b, 0xf81f, RD_x|RD_y|WR_HI|WR_LO,	0,	I1 },
{"remu",    "z,v,y",	0, (int) M_REMU_3, INSN_MACRO,	0,	I1 },
{"sb",	    "y,5(x)",	0xc000, 0xf800, RD_y|RD_x,	0,	I1 },
{"sd",	    "y,D(x)",	0x7800, 0xf800, RD_y|RD_x, 	0,	I3 },
{"sd",	    "y,D(S)",	0xf900, 0xff00, RD_y|RD_PC, 	0,	I3 },
{"sd",	    "R,C(S)",	0xfa00, 0xff00, RD_31|RD_PC,	0,	I1 },
{"sh",	    "y,H(x)",	0xc800, 0xf800, RD_y|RD_x,	0,	I1 },
{"sllv",    "y,x",	0xe804, 0xf81f, WR_y|RD_y|RD_x, 0,	I1 },
{"sll",	    "x,w,<",	0x3000, 0xf803, WR_x|RD_y,	0,	I1 },
{"sll",     "y,x",	0xe804, 0xf81f, WR_y|RD_y|RD_x,	0,	I1 },
{"slti",    "x,8",	0x5000, 0xf800, WR_T|RD_x,	0,	I1 },
{"slt",	    "x,y",	0xe802, 0xf81f, WR_T|RD_x|RD_y,	0,	I1 },
{"slt",     "x,8",	0x5000, 0xf800, WR_T|RD_x,	0,	I1 },
{"sltiu",   "x,8",	0x5800, 0xf800, WR_T|RD_x,	0,	I1 },
{"sltu",    "x,y",	0xe803, 0xf81f, WR_T|RD_x|RD_y,	0,	I1 },
{"sltu",    "x,8",	0x5800, 0xf800, WR_T|RD_x,	0,	I1 },
{"srav",    "y,x",	0xe807, 0xf81f, WR_y|RD_y|RD_x,	0,	I1 },
{"sra",	    "x,w,<",	0x3003, 0xf803, WR_x|RD_y,	0,	I1 },
{"sra",     "y,x",	0xe807, 0xf81f, WR_y|RD_y|RD_x,	0,	I1 },
{"srlv",    "y,x",	0xe806, 0xf81f, WR_y|RD_y|RD_x,	0,	I1 },
{"srl",	    "x,w,<",	0x3002, 0xf803, WR_x|RD_y,	0,	I1 },
{"srl",     "y,x",	0xe806, 0xf81f, WR_y|RD_y|RD_x,	0,	I1 },
{"subu",    "z,v,y",	0xe003, 0xf803, WR_z|RD_x|RD_y,	0,	I1 },
{"subu",    "y,x,4",	0, (int) M_SUBU_I, INSN_MACRO,	0,	I1 },
{"subu",    "x,k",	0, (int) M_SUBU_I_2, INSN_MACRO,0,	I1 },
{"sw",	    "y,W(x)",	0xd800, 0xf800, RD_y|RD_x,	0,	I1 },
{"sw",	    "x,V(S)",	0xd000, 0xf800, RD_x|RD_SP,	0,	I1 },
{"sw",	    "R,V(S)",	0x6200, 0xff00, RD_31|RD_SP,	0,	I1 },
{"xor",	    "x,y",	0xe80e, 0xf81f, WR_x|RD_x|RD_y, 0,	I1 },
  /* MIPS16e additions */
{"jalrc",   "x",	0xe8c0, 0xf8ff, WR_31|RD_x|TRAP, 0,     I32 },
{"jalrc",   "R,x",	0xe8c0, 0xf8ff, WR_31|RD_x|TRAP, 0,     I32 },
{"jrc",     "x",	0xe880, 0xf8ff, RD_x|TRAP,	0,      I32 },
{"jrc",     "R",	0xe8a0, 0xffff, RD_31|TRAP,	0,      I32 },
{"restore", "M",	0x6400, 0xff80, WR_31|RD_SP|WR_SP|TRAP,	0,	I32 },
{"save",    "m",	0x6480, 0xff80, RD_31|RD_SP|WR_SP|TRAP,	0,	I32 },
{"sdbbp",   "6",	0xe801, 0xf81f, TRAP,		0,	I32 },
{"seb",	    "x",	0xe891, 0xf8ff, WR_x|RD_x,	0,      I32 },
{"seh",	    "x",	0xe8b1, 0xf8ff, WR_x|RD_x,	0,      I32 },
{"sew",	    "x",	0xe8d1, 0xf8ff, WR_x|RD_x,	0,      I64 },
{"zeb",	    "x",	0xe811, 0xf8ff, WR_x|RD_x,	0,      I32 },
{"zeh",	    "x",	0xe831, 0xf8ff, WR_x|RD_x,	0,      I32 },
{"zew",	    "x",	0xe851, 0xf8ff, WR_x|RD_x,	0,      I64 },
};

const int bfd_mips16_num_opcodes =
  ((sizeof mips16_opcodes) / (sizeof (mips16_opcodes[0])));

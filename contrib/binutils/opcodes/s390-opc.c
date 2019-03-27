/* s390-opc.c -- S390 opcode list
   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

   This file is part of GDB, GAS, and the GNU binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdio.h>
#include "ansidecl.h"
#include "opcode/s390.h"

/* This file holds the S390 opcode table.  The opcode table
   includes almost all of the extended instruction mnemonics.  This
   permits the disassembler to use them, and simplifies the assembler
   logic, at the cost of increasing the table size.  The table is
   strictly constant data, so the compiler should be able to put it in
   the .text section.

   This file also holds the operand table.  All knowledge about
   inserting operands into instructions and vice-versa is kept in this
   file.  */

/* The operands table.
   The fields are bits, shift, insert, extract, flags.  */

const struct s390_operand s390_operands[] =
{
#define UNUSED 0
  { 0, 0, 0 },                    /* Indicates the end of the operand list */

#define R_8    1                  /* GPR starting at position 8 */
  { 4, 8, S390_OPERAND_GPR },
#define R_12   2                  /* GPR starting at position 12 */
  { 4, 12, S390_OPERAND_GPR },
#define R_16   3                  /* GPR starting at position 16 */
  { 4, 16, S390_OPERAND_GPR },
#define R_20   4                  /* GPR starting at position 20 */
  { 4, 20, S390_OPERAND_GPR },
#define R_24   5                  /* GPR starting at position 24 */
  { 4, 24, S390_OPERAND_GPR },
#define R_28   6                  /* GPR starting at position 28 */
  { 4, 28, S390_OPERAND_GPR },
#define R_32   7                  /* GPR starting at position 32 */
  { 4, 32, S390_OPERAND_GPR },

#define F_8    8                  /* FPR starting at position 8 */
  { 4, 8, S390_OPERAND_FPR },
#define F_12   9                  /* FPR starting at position 12 */
  { 4, 12, S390_OPERAND_FPR },
#define F_16   10                 /* FPR starting at position 16 */
  { 4, 16, S390_OPERAND_FPR },
#define F_20   11                 /* FPR starting at position 16 */
  { 4, 16, S390_OPERAND_FPR },
#define F_24   12                 /* FPR starting at position 24 */
  { 4, 24, S390_OPERAND_FPR },
#define F_28   13                 /* FPR starting at position 28 */
  { 4, 28, S390_OPERAND_FPR },
#define F_32   14                 /* FPR starting at position 32 */
  { 4, 32, S390_OPERAND_FPR },

#define A_8    15                 /* Access reg. starting at position 8 */
  { 4, 8, S390_OPERAND_AR },
#define A_12   16                 /* Access reg. starting at position 12 */
  { 4, 12, S390_OPERAND_AR },
#define A_24   17                 /* Access reg. starting at position 24 */
  { 4, 24, S390_OPERAND_AR },
#define A_28   18                 /* Access reg. starting at position 28 */
  { 4, 28, S390_OPERAND_AR },

#define C_8    19                 /* Control reg. starting at position 8 */
  { 4, 8, S390_OPERAND_CR },
#define C_12   20                 /* Control reg. starting at position 12 */
  { 4, 12, S390_OPERAND_CR },

#define B_16   21                 /* Base register starting at position 16 */
  { 4, 16, S390_OPERAND_BASE|S390_OPERAND_GPR },
#define B_32   22                 /* Base register starting at position 32 */
  { 4, 32, S390_OPERAND_BASE|S390_OPERAND_GPR },

#define X_12   23                 /* Index register starting at position 12 */
  { 4, 12, S390_OPERAND_INDEX|S390_OPERAND_GPR },

#define D_20   24                 /* Displacement starting at position 20 */
  { 12, 20, S390_OPERAND_DISP },
#define D_36   25                 /* Displacement starting at position 36 */
  { 12, 36, S390_OPERAND_DISP },
#define D20_20 26		  /* 20 bit displacement starting at 20 */
  { 20, 20, S390_OPERAND_DISP|S390_OPERAND_SIGNED },

#define L4_8   27                 /* 4 bit length starting at position 8 */
  { 4, 8, S390_OPERAND_LENGTH },
#define L4_12  28                 /* 4 bit length starting at position 12 */
  { 4, 12, S390_OPERAND_LENGTH },
#define L8_8   29                 /* 8 bit length starting at position 8 */
  { 8, 8, S390_OPERAND_LENGTH },

#define U4_8   30                 /* 4 bit unsigned value starting at 8 */
  { 4, 8, 0 },
#define U4_12  31                 /* 4 bit unsigned value starting at 12 */
  { 4, 12, 0 },
#define U4_16  32                 /* 4 bit unsigned value starting at 16 */
  { 4, 16, 0 },
#define U4_20  33                 /* 4 bit unsigned value starting at 20 */
  { 4, 20, 0 },
#define U8_8   34                 /* 8 bit unsigned value starting at 8 */
  { 8, 8, 0 },
#define U8_16  35                 /* 8 bit unsigned value starting at 16 */
  { 8, 16, 0 },
#define I16_16 36                 /* 16 bit signed value starting at 16 */
  { 16, 16, S390_OPERAND_SIGNED },
#define U16_16 37                 /* 16 bit unsigned value starting at 16 */
  { 16, 16, 0 },
#define J16_16 38                 /* PC relative jump offset at 16 */
  { 16, 16, S390_OPERAND_PCREL },
#define J32_16 39                 /* PC relative long offset at 16 */
  { 32, 16, S390_OPERAND_PCREL },
#define I32_16 40		  /* 32 bit signed value starting at 16 */
  { 32, 16, S390_OPERAND_SIGNED },
#define U32_16 41		  /* 32 bit unsigned value starting at 16 */
  { 32, 16, 0 },
#define M_16   42                 /* 4 bit optional mask starting at 16 */
  { 4, 16, S390_OPERAND_OPTIONAL },
#define RO_28  43                 /* optional GPR starting at position 28 */
  { 4, 28, (S390_OPERAND_GPR | S390_OPERAND_OPTIONAL) }

};


/* Macros used to form opcodes.  */

/* 8/16/48 bit opcodes.  */
#define OP8(x) { x, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define OP16(x) { x >> 8, x & 255, 0x00, 0x00, 0x00, 0x00 }
#define OP48(x) { x >> 40, (x >> 32) & 255, (x >> 24) & 255, \
                  (x >> 16) & 255, (x >> 8) & 255, x & 255}

/* The new format of the INSTR_x_y and MASK_x_y defines is based
   on the following rules:
   1) the middle part of the definition (x in INSTR_x_y) is the official
      names of the instruction format that you can find in the principals
      of operation.
   2) the last part of the definition (y in INSTR_x_y) gives you an idea
      which operands the binary represenation of the instruction has.
      The meanings of the letters in y are:
      a - access register
      c - control register
      d - displacement, 12 bit
      f - floating pointer register
      i - signed integer, 4, 8, 16 or 32 bit
      l - length, 4 or 8 bit
      p - pc relative
      r - general purpose register
      u - unsigned integer, 4, 8, 16 or 32 bit
      m - mode field, 4 bit
      0 - operand skipped.
      The order of the letters reflects the layout of the format in
      storage and not the order of the paramaters of the instructions.
      The use of the letters is not a 100% match with the PoP but it is
      quite close.

      For example the instruction "mvo" is defined in the PoP as follows:
      
      MVO  D1(L1,B1),D2(L2,B2)   [SS]

      --------------------------------------
      | 'F1' | L1 | L2 | B1 | D1 | B2 | D2 |
      --------------------------------------
       0      8    12   16   20   32   36

      The instruction format is: INSTR_SS_LLRDRD / MASK_SS_LLRDRD.  */

#define INSTR_E          2, { 0,0,0,0,0,0 }                    /* e.g. pr    */
#define INSTR_RIE_RRP    6, { R_8,R_12,J16_16,0,0,0 }          /* e.g. brxhg */
#define INSTR_RIL_0P     6, { J32_16,0,0,0,0 }                 /* e.g. jg    */
#define INSTR_RIL_RP     6, { R_8,J32_16,0,0,0,0 }             /* e.g. brasl */
#define INSTR_RIL_UP     6, { U4_8,J32_16,0,0,0,0 }            /* e.g. brcl  */
#define INSTR_RIL_RI     6, { R_8,I32_16,0,0,0,0 }             /* e.g. afi   */
#define INSTR_RIL_RU     6, { R_8,U32_16,0,0,0,0 }             /* e.g. alfi  */
#define INSTR_RI_0P      4, { J16_16,0,0,0,0,0 }               /* e.g. j     */
#define INSTR_RI_RI      4, { R_8,I16_16,0,0,0,0 }             /* e.g. ahi   */
#define INSTR_RI_RP      4, { R_8,J16_16,0,0,0,0 }             /* e.g. brct  */
#define INSTR_RI_RU      4, { R_8,U16_16,0,0,0,0 }             /* e.g. tml   */
#define INSTR_RI_UP      4, { U4_8,J16_16,0,0,0,0 }            /* e.g. brc   */
#define INSTR_RRE_00     4, { 0,0,0,0,0,0 }                    /* e.g. palb  */
#define INSTR_RRE_0R     4, { R_28,0,0,0,0,0 }                 /* e.g. tb    */
#define INSTR_RRE_AA     4, { A_24,A_28,0,0,0,0 }              /* e.g. cpya  */
#define INSTR_RRE_AR     4, { A_24,R_28,0,0,0,0 }              /* e.g. sar   */
#define INSTR_RRE_F0     4, { F_24,0,0,0,0,0 }                 /* e.g. sqer  */
#define INSTR_RRE_FF     4, { F_24,F_28,0,0,0,0 }              /* e.g. debr  */
#define INSTR_RRE_R0     4, { R_24,0,0,0,0,0 }                 /* e.g. ipm   */
#define INSTR_RRE_RA     4, { R_24,A_28,0,0,0,0 }              /* e.g. ear   */
#define INSTR_RRE_RF     4, { R_24,F_28,0,0,0,0 }              /* e.g. cefbr */
#define INSTR_RRE_RR     4, { R_24,R_28,0,0,0,0 }              /* e.g. lura  */
#define INSTR_RRE_FR     4, { F_24,R_28,0,0,0,0 }              /* e.g. ldgr  */
/* Actually efpc and sfpc do not take an optional operand.
   This is just a workaround for existing code e.g. glibc.  */
#define INSTR_RRE_RR_OPT 4, { R_24,RO_28,0,0,0,0 }             /* efpc, sfpc */
#define INSTR_RRF_F0FF   4, { F_16,F_24,F_28,0,0,0 }           /* e.g. madbr */
#define INSTR_RRF_F0FF2  4, { F_24,F_16,F_28,0,0,0 }           /* e.g. cpsdr */
#define INSTR_RRF_F0FR   4, { F_24,F_16,R_28,0,0,0 }           /* e.g. iedtr */
#define INSTR_RRF_FUFF   4, { F_24,F_16,F_28,U4_20,0,0 }       /* e.g. didbr */
#define INSTR_RRF_RURR   4, { R_24,R_28,R_16,U4_20,0,0 }       /* e.g. .insn */
#define INSTR_RRF_R0RR   4, { R_24,R_28,R_16,0,0,0 }           /* e.g. idte  */
#define INSTR_RRF_U0FF   4, { F_24,U4_16,F_28,0,0,0 }          /* e.g. fixr  */
#define INSTR_RRF_U0RF   4, { R_24,U4_16,F_28,0,0,0 }          /* e.g. cfebr */
#define INSTR_RRF_UUFF   4, { F_24,U4_16,F_28,U4_20,0,0 }      /* e.g. fidtr */
#define INSTR_RRF_0UFF   4, { F_24,F_28,U4_20,0,0,0 }          /* e.g. ldetr */
#define INSTR_RRF_FFFU   4, { F_24,F_16,F_28,U4_20,0,0 }       /* e.g. qadtr */
#define INSTR_RRF_M0RR   4, { R_24,R_28,M_16,0,0,0 }           /* e.g. sske  */
#define INSTR_RR_0R      2, { R_12, 0,0,0,0,0 }                /* e.g. br    */
#define INSTR_RR_FF      2, { F_8,F_12,0,0,0,0 }               /* e.g. adr   */
#define INSTR_RR_R0      2, { R_8, 0,0,0,0,0 }                 /* e.g. spm   */
#define INSTR_RR_RR      2, { R_8,R_12,0,0,0,0 }               /* e.g. lr    */
#define INSTR_RR_U0      2, { U8_8, 0,0,0,0,0 }                /* e.g. svc   */
#define INSTR_RR_UR      2, { U4_8,R_12,0,0,0,0 }              /* e.g. bcr   */
#define INSTR_RRR_F0FF   4, { F_24,F_28,F_16,0,0,0 }           /* e.g. ddtr  */
#define INSTR_RSE_RRRD   6, { R_8,R_12,D_20,B_16,0,0 }         /* e.g. lmh   */
#define INSTR_RSE_CCRD   6, { C_8,C_12,D_20,B_16,0,0 }         /* e.g. lmh   */
#define INSTR_RSE_RURD   6, { R_8,U4_12,D_20,B_16,0,0 }        /* e.g. icmh  */
#define INSTR_RSL_R0RD   6, { R_8,D_20,B_16,0,0,0 }            /* e.g. tp    */
#define INSTR_RSI_RRP    4, { R_8,R_12,J16_16,0,0,0 }          /* e.g. brxh  */
#define INSTR_RSY_RRRD   6, { R_8,R_12,D20_20,B_16,0,0 }       /* e.g. stmy  */
#define INSTR_RSY_RURD   6, { R_8,U4_12,D20_20,B_16,0,0 }      /* e.g. icmh  */
#define INSTR_RSY_AARD   6, { A_8,A_12,D20_20,B_16,0,0 }       /* e.g. lamy  */
#define INSTR_RSY_CCRD   6, { C_8,C_12,D20_20,B_16,0,0 }       /* e.g. lamy  */
#define INSTR_RS_AARD    4, { A_8,A_12,D_20,B_16,0,0 }         /* e.g. lam   */
#define INSTR_RS_CCRD    4, { C_8,C_12,D_20,B_16,0,0 }         /* e.g. lctl  */
#define INSTR_RS_R0RD    4, { R_8,D_20,B_16,0,0,0 }            /* e.g. sll   */
#define INSTR_RS_RRRD    4, { R_8,R_12,D_20,B_16,0,0 }         /* e.g. cs    */
#define INSTR_RS_RURD    4, { R_8,U4_12,D_20,B_16,0,0 }        /* e.g. icm   */
#define INSTR_RXE_FRRD   6, { F_8,D_20,X_12,B_16,0,0 }         /* e.g. axbr  */
#define INSTR_RXE_RRRD   6, { R_8,D_20,X_12,B_16,0,0 }         /* e.g. lg    */
#define INSTR_RXF_FRRDF  6, { F_32,F_8,D_20,X_12,B_16,0 }      /* e.g. madb  */
#define INSTR_RXF_RRRDR  6, { R_32,R_8,D_20,X_12,B_16,0 }      /* e.g. .insn */
#define INSTR_RXY_RRRD   6, { R_8,D20_20,X_12,B_16,0,0 }       /* e.g. ly    */
#define INSTR_RXY_FRRD   6, { F_8,D20_20,X_12,B_16,0,0 }       /* e.g. ley   */
#define INSTR_RX_0RRD    4, { D_20,X_12,B_16,0,0,0 }           /* e.g. be    */
#define INSTR_RX_FRRD    4, { F_8,D_20,X_12,B_16,0,0 }         /* e.g. ae    */
#define INSTR_RX_RRRD    4, { R_8,D_20,X_12,B_16,0,0 }         /* e.g. l     */
#define INSTR_RX_URRD    4, { U4_8,D_20,X_12,B_16,0,0 }        /* e.g. bc    */
#define INSTR_SI_URD     4, { D_20,B_16,U8_8,0,0,0 }           /* e.g. cli   */
#define INSTR_SIY_URD    6, { D20_20,B_16,U8_8,0,0,0 }         /* e.g. tmy   */
#define INSTR_SSE_RDRD   6, { D_20,B_16,D_36,B_32,0,0 }        /* e.g. mvsdk */
#define INSTR_SS_L0RDRD  6, { D_20,L8_8,B_16,D_36,B_32,0     } /* e.g. mvc   */
#define INSTR_SS_L2RDRD  6, { D_20,B_16,D_36,L8_8,B_32,0     } /* e.g. pka   */
#define INSTR_SS_LIRDRD  6, { D_20,L4_8,B_16,D_36,B_32,U4_12 } /* e.g. srp   */
#define INSTR_SS_LLRDRD  6, { D_20,L4_8,B_16,D_36,L4_12,B_32 } /* e.g. pack  */
#define INSTR_SS_RRRDRD  6, { D_20,R_8,B_16,D_36,B_32,R_12 }   /* e.g. mvck  */
#define INSTR_SS_RRRDRD2 6, { R_8,D_20,B_16,R_12,D_36,B_32 }   /* e.g. plo   */
#define INSTR_SS_RRRDRD3 6, { R_8,R_12,D_20,B_16,D_36,B_32 }   /* e.g. lmd   */
#define INSTR_S_00       4, { 0,0,0,0,0,0 }                    /* e.g. hsch  */
#define INSTR_S_RD       4, { D_20,B_16,0,0,0,0 }              /* e.g. lpsw  */
#define INSTR_SSF_RRDRD  6, { D_20,B_16,D_36,B_32,R_8,0 }      /* e.g. mvcos */

#define MASK_E           { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIE_RRP     { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RIL_0P      { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RP      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_UP      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RI      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RU      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_0P       { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RI       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RP       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RU       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_UP       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRE_00      { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 }
#define MASK_RRE_0R      { 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00 }
#define MASK_RRE_AA      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_AR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_F0      { 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00 }
#define MASK_RRE_FF      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_R0      { 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00 }
#define MASK_RRE_RA      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RF      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_FR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RR_OPT  { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FF2   { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FR    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_FUFF    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_RURR    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_R0RR    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_U0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_U0RF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_UUFF    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_0UFF    { 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00 }
#define MASK_RRF_FFFU    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_M0RR    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RR_0R       { 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_FF       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_R0       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_RR       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_U0       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_UR       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRR_F0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RSE_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSE_CCRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSE_RURD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSL_R0RD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSI_RRP     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_AARD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_CCRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_R0RD     { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_RRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_RURD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RSY_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_RURD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_AARD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_CCRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXE_FRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXE_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXF_FRRDF   { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXF_RRRDR   { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXY_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXY_FRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RX_0RRD     { 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_FRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_RRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_URRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SI_URD      { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SIY_URD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_SSE_RDRD    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_L0RDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_L2RDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_LIRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_LLRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD2  { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD3  { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_S_00        { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 }
#define MASK_S_RD        { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SSF_RRDRD   { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }

/* The opcode formats table (blueprints for .insn pseudo mnemonic).  */

const struct s390_opcode s390_opformats[] =
  {
  { "e",	OP8(0x00LL),	MASK_E,		INSTR_E,	3, 0 },
  { "ri",	OP8(0x00LL),	MASK_RI_RI,	INSTR_RI_RI,	3, 0 },
  { "rie",	OP8(0x00LL),	MASK_RIE_RRP,	INSTR_RIE_RRP,	3, 0 },
  { "ril",	OP8(0x00LL),	MASK_RIL_RP,	INSTR_RIL_RP,	3, 0 },
  { "rilu",	OP8(0x00LL),	MASK_RIL_RU,	INSTR_RIL_RU,	3, 0 },
  { "rr",	OP8(0x00LL),	MASK_RR_RR,	INSTR_RR_RR,	3, 0 },
  { "rre",	OP8(0x00LL),	MASK_RRE_RR,	INSTR_RRE_RR,	3, 0 },
  { "rrf",	OP8(0x00LL),	MASK_RRF_RURR,	INSTR_RRF_RURR,	3, 0 },
  { "rs",	OP8(0x00LL),	MASK_RS_RRRD,	INSTR_RS_RRRD,	3, 0 },
  { "rse",	OP8(0x00LL),	MASK_RSE_RRRD,	INSTR_RSE_RRRD,	3, 0 },
  { "rsi",	OP8(0x00LL),	MASK_RSI_RRP,	INSTR_RSI_RRP,	3, 0 },
  { "rsy",	OP8(0x00LL),	MASK_RSY_RRRD,	INSTR_RSY_RRRD,	3, 3 },
  { "rx",	OP8(0x00LL),	MASK_RX_RRRD,	INSTR_RX_RRRD,	3, 0 },
  { "rxe",	OP8(0x00LL),	MASK_RXE_RRRD,	INSTR_RXE_RRRD,	3, 0 },
  { "rxf",	OP8(0x00LL),	MASK_RXF_RRRDR,	INSTR_RXF_RRRDR,3, 0 },
  { "rxy",	OP8(0x00LL),	MASK_RXY_RRRD,	INSTR_RXY_RRRD,	3, 3 },
  { "s",	OP8(0x00LL),	MASK_S_RD,	INSTR_S_RD,	3, 0 },
  { "si",	OP8(0x00LL),	MASK_SI_URD,	INSTR_SI_URD,	3, 0 },
  { "siy",	OP8(0x00LL),	MASK_SIY_URD,	INSTR_SIY_URD,	3, 3 },
  { "ss",	OP8(0x00LL),	MASK_SS_RRRDRD,	INSTR_SS_RRRDRD,3, 0 },
  { "sse",	OP8(0x00LL),	MASK_SSE_RDRD,	INSTR_SSE_RDRD,	3, 0 },
  { "ssf",	OP8(0x00LL),	MASK_SSF_RRDRD,	INSTR_SSF_RRDRD,3, 0 },
};

const int s390_num_opformats =
  sizeof (s390_opformats) / sizeof (s390_opformats[0]);

#include "s390-opc.tab"

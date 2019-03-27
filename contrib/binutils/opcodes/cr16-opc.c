/* cr16-opc.c -- Table of opcodes for the CR16 processor.
   Copyright 2007 Free Software Foundation, Inc.
   Contributed by M R Swami Reddy (MR.Swami.Reddy@nsc.com)

   This file is part of GAS, GDB and the GNU binutils.

   GAS, GDB, and GNU binutils is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GAS, GDB, and GNU binutils are distributed in the hope that they will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <stdio.h>
#include "libiberty.h"
#include "symcat.h"
#include "opcode/cr16.h"

const inst cr16_instruction[] =
{
/* Create an arithmetic instruction - INST[bw].  */
#define  ARITH_BYTE_INST(NAME, OPC, OP1)                             \
  /* opc8 imm4 r */                                                  \
  {NAME, 1, OPC, 24, ARITH_BYTE_INS, {{uimm4_1,20}, {regr,16}}},     \
  /* opc8 imm16 r */                                                 \
  {NAME, 2, (OPC<<4)+0xB, 20, ARITH_BYTE_INS, {{OP1,0}, {regr,16}}}, \
  /* opc8 r r */                                                     \
  {NAME, 1, OPC+0x1, 24, ARITH_BYTE_INS, {{regr,20}, {regr,16}}}

/* for Logincal operations, allow unsinged imm16 also */
#define  ARITH1_BYTE_INST(NAME, OPC, OP1)                            \
  /* opc8 imm16 r */                                                 \
  {NAME, 2, (OPC<<4)+0xB, 20, ARITH_BYTE_INS, {{OP1,0}, {regr,16}}}


  ARITH_BYTE_INST ("andb", 0x20, uimm16),
  ARITH1_BYTE_INST ("andb", 0x20, imm16),
  ARITH_BYTE_INST ("andw", 0x22, uimm16),
  ARITH1_BYTE_INST ("andw", 0x22, imm16),

  ARITH_BYTE_INST ("orb",  0x24, uimm16),
  ARITH1_BYTE_INST ("orb",  0x24, imm16),
  ARITH_BYTE_INST ("orw",  0x26, uimm16),
  ARITH1_BYTE_INST ("orw",  0x26, imm16),

  ARITH_BYTE_INST ("xorb", 0x28, uimm16),
  ARITH1_BYTE_INST ("xorb", 0x28, imm16),
  ARITH_BYTE_INST ("xorw", 0x2A, uimm16),
  ARITH1_BYTE_INST ("xorw", 0x2A, imm16),

  ARITH_BYTE_INST ("addub", 0x2C, imm16),
  ARITH_BYTE_INST ("adduw", 0x2E, imm16),
  ARITH_BYTE_INST ("addb",  0x30, imm16),
  ARITH_BYTE_INST ("addw",  0x32, imm16),
  ARITH_BYTE_INST ("addcb", 0x34, imm16),
  ARITH_BYTE_INST ("addcw", 0x36, imm16),

  ARITH_BYTE_INST ("subb",  0x38, imm16),
  ARITH_BYTE_INST ("subw",  0x3A, imm16),
  ARITH_BYTE_INST ("subcb", 0x3C, imm16),
  ARITH_BYTE_INST ("subcw", 0x3E, imm16),

  ARITH_BYTE_INST ("cmpb",  0x50, imm16),
  ARITH_BYTE_INST ("cmpw",  0x52, imm16),

  ARITH_BYTE_INST ("movb",  0x58, imm16),
  ARITH_BYTE_INST ("movw",  0x5A, imm16),

  ARITH_BYTE_INST ("mulb",  0x64, imm16),
  ARITH_BYTE_INST ("mulw",  0x66, imm16),

#define  ARITH_BYTE_INST1(NAME, OPC)                       \
  /* opc8 r r */                                           \
  {NAME, 1, OPC, 24, ARITH_BYTE_INS, {{regr,20}, {regr,16}}}

  ARITH_BYTE_INST1 ("movxb",  0x5C),
  ARITH_BYTE_INST1 ("movzb",  0x5D),
  ARITH_BYTE_INST1 ("mulsb",  0x0B),

#define  ARITH_BYTE_INST2(NAME, OPC)                       \
  /* opc8 r rp */                                          \
  {NAME, 1, OPC, 24, ARITH_BYTE_INS, {{regr,20}, {regp,16}}}

  ARITH_BYTE_INST2 ("movxw",  0x5E),
  ARITH_BYTE_INST2 ("movzw",  0x5F),
  ARITH_BYTE_INST2 ("mulsw",  0x62),
  ARITH_BYTE_INST2 ("muluw",  0x63),

/* Create an arithmetic instruction - INST[d]- with 3 types.  */
#define  ARITH_INST_D(NAME, OPC)                                     \
  /* opc8 imm4 rp */                                                 \
  {NAME, 1, OPC, 24, ARITH_INS, {{uimm4_1,20}, {regp,16}}},          \
  /* opc8 imm16 rp */                                                \
  {NAME, 2, (OPC<<4)+0xB, 20, ARITH_INS, {{imm16,0}, {regp,16}}},    \
  /* opc8 rp rp */                                                   \
  {NAME, 1, OPC+1, 24, ARITH_INS, {{regp,20}, {regp,16}}}

/* Create an arithmetic instruction - INST[d]-20 bit types.  */
#define  ARITH_INST20(NAME, OPC)                        \
  /* opc8 uimm20 rp */                                  \
  {NAME, 2, OPC, 24, ARITH_INS, {{uimm20,0},  {regp,20}}}

/* Create an arithmetic instruction - INST[d]-32 bit types.  */
#define  ARITH_INST32(NAME, OPC, OP1)                \
  /* opc12 imm32 rp */                               \
  {NAME, 3, OPC, 20, ARITH_INS, {{OP1,0},  {regp,16}}}

/* Create an arithmetic instruction - INST[d]-32bit types(reg pairs).*/
#define  ARITH_INST32RP(NAME, OPC)                   \
  /* opc24 rp rp */                                  \
  {NAME, 2, OPC, 12, ARITH_INS, {{regp,4},  {regp,0}}}

  ARITH_INST_D   ("movd", 0x54),
  ARITH_INST20   ("movd", 0x05),
  ARITH_INST32   ("movd", 0x007, imm32),
  ARITH_INST_D   ("addd", 0x60),
  ARITH_INST20   ("addd", 0x04),
  ARITH_INST32   ("addd", 0x002, imm32),
  ARITH_INST32   ("subd", 0x003, imm32),
  ARITH_INST32RP ("subd", 0x0014C),
  ARITH_INST_D   ("cmpd", 0x56),
  ARITH_INST32   ("cmpd", 0x009, imm32),
  ARITH_INST32   ("andd", 0x004, uimm32),
  ARITH_INST32RP ("andd", 0x0014B),
  ARITH_INST32   ("ord",  0x005, uimm32),
  ARITH_INST32RP ("ord",  0x00149),
  ARITH_INST32   ("xord", 0x006, uimm32),
  ARITH_INST32RP ("xord", 0x0014A),

/* Create a shift instruction.  */
#define  SHIFT_INST_A(NAME, OPC1, OPC2, SHIFT, OP1, OP2)    \
  /* opc imm r */                                           \
  {NAME, 1, OPC1, SHIFT, SHIFT_INS, {{OP1,20}, {OP2,16}}},  \
  /* opc imm r */                                           \
  {NAME, 1, OPC1+1, SHIFT, SHIFT_INS, {{OP1,20}, {OP2,16}}},\
  /* opc r r */                                             \
  {NAME, 1, OPC2, 24, SHIFT_INS, {{regr,20}, {OP2,16}}}

  SHIFT_INST_A("ashub", 0x80, 0x41, 23, imm4, regr),
  SHIFT_INST_A("ashud", 0x26, 0x48, 25, imm6, regp),
  SHIFT_INST_A("ashuw", 0x42, 0x45, 24, imm5, regr),

#define  SHIFT_INST_L(NAME, OPC1, OPC2, SHIFT, OP1, OP2)    \
  /* opc imm r */                                           \
  {NAME, 1, OPC1, SHIFT, SHIFT_INS, {{OP1,20}, {OP2,16}}},  \
  /* opc r r */                                             \
  {NAME, 1, OPC2, 24, SHIFT_INS, {{regr,20}, {OP2,16}}}

  SHIFT_INST_L("lshb", 0x13, 0x44, 23, imm4, regr),
  SHIFT_INST_L("lshd", 0x25, 0x47, 25, imm6, regp),
  SHIFT_INST_L("lshw", 0x49, 0x46, 24, imm5, regr),

/* Create a conditional branch instruction.  */
#define  BRANCH_INST(NAME, OPC)                                       \
  /* opc4 c4 dispe9 */                                                \
  {NAME,  1, OPC, 28, BRANCH_INS, {{cc,20}, {dispe9,16}}},            \
  /* opc4 c4 disps17 */                                               \
  {NAME,  2, ((OPC<<4)+0x8), 24, BRANCH_INS, {{cc,20}, {disps17,0}}}, \
  /* opc4 c4 disps25 */                                               \
  {NAME,  3, (OPC<<4), 16 , BRANCH_INS, {{cc,4}, {disps25,16}}}

  BRANCH_INST ("b", 0x1),

/* Create a 'Branch if Equal to 0' instruction.  */
#define  BRANCH_NEQ_INST(NAME, OPC)                           \
  /* opc8 disps5 r */                                         \
  {NAME,  1, OPC, 24, BRANCH_NEQ_INS, {{regr,16}, {disps5,20}}}

  BRANCH_NEQ_INST ("beq0b",  0x0C),
  BRANCH_NEQ_INST ("bne0b",  0x0D),
  BRANCH_NEQ_INST ("beq0w",  0x0E),
  BRANCH_NEQ_INST ("bne0w",  0x0F),


/* Create an instruction using a single register operand.  */
#define  REG1_INST(NAME, OPC)                  \
  /* opc8 c4 r */                              \
  {NAME,  1, OPC, 20, NO_TYPE_INS, {{regr,16}}}

#define  REGP1_INST(NAME, OPC)                \
  /* opc8 c4 r */                             \
  {NAME,  1, OPC, 20, NO_TYPE_INS, {{regp,16}}}

/* Same as REG1_INST, with additional FLAGS.  */
#define  REG1_FLAG_INST(NAME, OPC, FLAGS)             \
  /* opc8 c4 r */                                     \
  {NAME,  1, OPC, 20, NO_TYPE_INS | FLAGS, {{regp,16}}}

  /* JCond instructions */
  REGP1_INST ("jeq",  0x0A0),
  REGP1_INST ("jne",  0x0A1),
  REGP1_INST ("jcs",  0x0A2),
  REGP1_INST ("jcc",  0x0A3),
  REGP1_INST ("jhi",  0x0A4),
  REGP1_INST ("jls",  0x0A5),
  REGP1_INST ("jgt",  0x0A6),
  REGP1_INST ("jle",  0x0A7),
  REGP1_INST ("jfs",  0x0A8),
  REGP1_INST ("jfc",  0x0A9),
  REGP1_INST ("jlo",  0x0AA),
  REGP1_INST ("jhs",  0x0AB),
  REGP1_INST ("jlt",  0x0AC),
  REGP1_INST ("jge",  0x0AD),
  REGP1_INST ("jump", 0x0AE),
  REGP1_INST ("jusr", 0x0AF),

  /* SCond instructions */
  REG1_INST ("seq",  0x080),
  REG1_INST ("sne",  0x081),
  REG1_INST ("scs",  0x082),
  REG1_INST ("scc",  0x083),
  REG1_INST ("shi",  0x084),
  REG1_INST ("sls",  0x085),
  REG1_INST ("sgt",  0x086),
  REG1_INST ("sle",  0x087),
  REG1_INST ("sfs",  0x088),
  REG1_INST ("sfc",  0x089),
  REG1_INST ("slo",  0x08A),
  REG1_INST ("shs",  0x08B),
  REG1_INST ("slt",  0x08C),
  REG1_INST ("sge",  0x08D),


/* Create an instruction using two register operands.  */
#define  REG3_INST(NAME, OPC)                                    \
  /* opc24 r r rp  */                                            \
  {NAME,  2, OPC,  12, NO_TYPE_INS, {{regr,4}, {regr,0}, {regp,8}}}

  /* MULTIPLY INSTRUCTIONS */
  REG3_INST ("macqw",  0x0014d),
  REG3_INST ("macuw",  0x0014e),
  REG3_INST ("macsw",  0x0014f),

/* Create a branch instruction.  */
#define  BR_INST(NAME, OPC)                               \
  /* opc12 ra disps25 */                                  \
  {NAME,  2, OPC,  24, NO_TYPE_INS, {{rra,0}, {disps25,0}}}

#define  BR_INST_RP(NAME, OPC)                              \
  /* opc8 rp disps25 */                                     \
  {NAME,  3, OPC,  12, NO_TYPE_INS, {{regp,4}, {disps25,16}}}

  BR_INST    ("bal", 0xC0),
  BR_INST_RP ("bal", 0x00102),

#define  REGPP2_INST(NAME, OPC)                         \
  /* opc16 rp rp  */                                    \
  {NAME,  2, OPC,  12, NO_TYPE_INS, {{regp,0}, {regp,4}}}
 /* Jump and link instructions.  */
  REGP1_INST  ("jal",0x00D),
  REGPP2_INST ("jal",0x00148),


/* Instructions including a register list (opcode is represented as a mask). */
#define  REGLIST_INST(NAME, OPC, TYPE)                               \
  /* opc7 r count3 RA */                                             \
  {NAME,1, (OPC<<1)+1, 23, TYPE, {{uimm3_1,20},{regr,16},{regr,0}}}, \
  /* opc8 r count3 */                                                \
  {NAME,  1, OPC, 24, TYPE, {{uimm3_1,20}, {regr,16}}},              \
  /* opc12 RA  */                                                    \
  {NAME,  1, (OPC<<8)+0x1E, 16, TYPE, {{regr,0}}}

   REGLIST_INST   ("push",   0x01, (NO_TYPE_INS | REG_LIST)),
   REGLIST_INST   ("pop",    0x02, (NO_TYPE_INS | REG_LIST)),
   REGLIST_INST   ("popret", 0x03, (NO_TYPE_INS | REG_LIST)),

  {"loadm",  1, 0x14, 19, NO_TYPE_INS | REG_LIST, {{uimm3_1,16}}},
  {"loadmp", 1, 0x15, 19, NO_TYPE_INS | REG_LIST, {{uimm3_1,16}}},
  {"storm",  1, 0x16, 19, NO_TYPE_INS | REG_LIST, {{uimm3_1,16}}},
  {"stormp", 1, 0x17, 19, NO_TYPE_INS | REG_LIST, {{uimm3_1,16}}},

 /* Processor Regsiter Manipulation instructions  */
  /* opc16 reg, preg */
  {"lpr",  2, 0x00140, 12, NO_TYPE_INS, {{regr,0}, {pregr,4}}},
  /* opc16 regp, pregp */
  {"lprd", 2, 0x00141, 12, NO_TYPE_INS, {{regp,0}, {pregrp,4}}},
  /* opc16 preg, reg */
  {"spr",  2, 0x00142, 12, NO_TYPE_INS, {{pregr,4}, {regr,0}}},
  /* opc16 pregp, regp */
  {"sprd", 2, 0x00143, 12, NO_TYPE_INS, {{pregrp,4}, {regp,0}}},

 /* Miscellaneous.  */
  /* opc12 ui4 */
  {"excp", 1, 0x00C, 20, NO_TYPE_INS, {{uimm4,16}}},

/* Create a bit-b instruction.  */
#define  CSTBIT_INST_B(NAME, OP, OPC1, OPC2, OPC3, OPC4)               \
  /* opcNN iN abs20 */                                                 \
  {NAME,  2, (OPC3+1), 23, CSTBIT_INS, {{OP,20},{abs20,0}}},           \
  /* opcNN iN abs24 */                                                 \
  {NAME,  3, (OPC2+3), 12, CSTBIT_INS, {{OP,4},{abs24,16}}},           \
  /* opcNN iN (Rindex)abs20 */                                         \
  {NAME,  2, OPC1, 24, CSTBIT_INS, {{OP,20}, {rindex7_abs20,0}}},      \
  /* opcNN iN (prp) disps14(RPbase) */                                 \
  {NAME,  2, OPC4, 22, CSTBIT_INS, {{OP,4},{rpindex_disps14,0}}},      \
  /* opcNN iN disps20(Rbase) */                                        \
  {NAME,  3, OPC2, 12, CSTBIT_INS, {{OP,4}, {rbase_disps20,16}}},      \
  /* opcNN iN (rp) disps0(RPbase) */                                   \
  {NAME,  1, OPC3-2, 23, CSTBIT_INS, {{OP,20}, {rpbase_disps0,16}}},   \
  /* opcNN iN (rp) disps16(RPBase) */                                  \
  {NAME,  2, OPC3,  23, CSTBIT_INS, {{OP,20}, {rpbase_disps16,0}}},    \
  /* opcNN iN (rp) disps20(RPBase) */                                  \
  {NAME,  3, (OPC2+1), 12, CSTBIT_INS, {{OP,4}, {rpbase_disps20,16}}}, \
  /* opcNN iN rrp (Rindex)disps20(RPbase) */                           \
  {NAME,  3, (OPC2+2), 12, CSTBIT_INS, {{OP,4}, {rpindex_disps20,16}}}

  CSTBIT_INST_B ("cbitb", uimm3, 0x68, 0x00104, 0xD6, 0x1AA),
  CSTBIT_INST_B ("sbitb", uimm3, 0x70, 0x00108, 0xE6, 0x1CA),
  CSTBIT_INST_B ("tbitb", uimm3, 0x78, 0x0010C, 0xF6, 0x1EA),

/* Create a bit-w instruction.  */
#define  CSTBIT_INST_W(NAME, OP, OPC1, OPC2, OPC3, OPC4)               \
  /* opcNN iN abs20 */                                                 \
  {NAME,  2, OPC1+6, 24, CSTBIT_INS, {{OP,20},{abs20,0}}},             \
  /* opcNN iN abs24 */                                                 \
  {NAME,  3, OPC2+3, 12, CSTBIT_INS, {{OP,4},{abs24,16}}},             \
  /* opcNN iN (Rindex)abs20 */                                         \
  {NAME,  2, OPC3, 25, CSTBIT_INS, {{OP,20}, {rindex8_abs20,0}}},      \
  /* opcNN iN (prp) disps14(RPbase) */                                 \
  {NAME,  2, OPC4, 22, CSTBIT_INS, {{OP,4},{rpindex_disps14,0}}},      \
  /* opcNN iN disps20(Rbase) */                                        \
  {NAME,  3, OPC2, 12, CSTBIT_INS, {{OP,4}, {rbase_disps20,16}}},      \
  /* opcNN iN (rp) disps0(RPbase) */                                   \
  {NAME,  1, OPC1+5, 24, CSTBIT_INS, {{OP,20}, {rpbase_disps0,16}}},   \
  /* opcNN iN (rp) disps16(RPBase) */                                  \
  {NAME,  2, OPC1,  24, CSTBIT_INS, {{OP,20}, {rpbase_disps16,0}}},    \
  /* opcNN iN (rp) disps20(RPBase) */                                  \
  {NAME,  3, OPC2+1, 12, CSTBIT_INS, {{OP,4}, {rpbase_disps20,16}}},   \
  /* opcNN iN rrp (Rindex)disps20(RPbase) */                           \
  {NAME,  3, OPC2+2, 12, CSTBIT_INS, {{OP,4}, {rpindex_disps20,16}}}

  CSTBIT_INST_W ("cbitw", uimm4, 0x69, 0x00114, 0x36, 0x1AB),
  CSTBIT_INST_W ("sbitw", uimm4, 0x71, 0x00118, 0x3A, 0x1CB),
  CSTBIT_INST_W ("tbitw", uimm4, 0x79, 0x0011C, 0x3E, 0x1EB),

  /* tbit cnt */
  {"tbit", 1, 0x06, 24, CSTBIT_INS, {{uimm4,20}, {regr,16}}},
  /* tbit reg reg */
  {"tbit", 1, 0x07, 24, CSTBIT_INS, {{regr,20},  {regr,16}}},


/* Load instructions (from memory to register).  */
#define  LD_REG_INST(NAME, OPC1, OPC2, OPC3, OPC4, OPC5, OP_S, OP_D)     \
 /* opc8 reg abs20 */                                                    \
 {NAME, 2, OPC3,  24, LD_STOR_INS, {{abs20,0}, {OP_D,20}}},              \
 /* opc20 reg abs24 */                                                   \
 {NAME, 3, OPC1+3, 12, LD_STOR_INS, {{abs24,16}, {OP_D,4}}},             \
 /* opc7 reg rindex8_abs20 */                                            \
 {NAME, 2, OPC5, 25, LD_STOR_INS, {{rindex8_abs20,0}, {OP_D,20}}},       \
  /* opc4 reg  disps4(RPbase) */                                         \
 {NAME, 1, (OPC2>>4), 28, LD_STOR_INS, {{OP_S,24}, {OP_D,20}}},          \
 /* opcNN reg  disps0(RPbase) */                                         \
 {NAME, 1, OPC2, 24, LD_STOR_INS, {{rpindex_disps0,0}, {OP_D,20}}},      \
 /* opc reg  disps14(RPbase) */                                          \
 {NAME, 2, OPC4, 22, LD_STOR_INS, {{rpindex_disps14,0}, {OP_D,20}}},     \
 /* opc reg -disps20(Rbase) */                                           \
 {NAME, 3, OPC1+0x60, 12, LD_STOR_INS, {{rbase_dispe20,16}, {OP_D,4}}},  \
 /* opc reg disps20(Rbase) */                                            \
 {NAME, 3, OPC1, 12, LD_STOR_INS, {{rbase_disps20,16}, {OP_D,4}}},       \
  /* opc reg (rp) disps16(RPbase) */                                     \
 {NAME, 2, OPC2+1, 24, LD_STOR_INS, {{rpbase_disps16,0}, {OP_D,20}}},    \
  /* opc16 reg (rp) disps20(RPbase) */                                   \
 {NAME, 3, OPC1+1, 12, LD_STOR_INS, {{rpbase_disps20,16}, {OP_D,4}}},    \
  /* op reg (rp) -disps20(RPbase) */                                     \
 {NAME, 3, OPC1+0x61, 12, LD_STOR_INS, {{rpbase_dispe20,16}, {OP_D,4}}}, \
 /* opc reg rrp (Rindex)disps20(RPbase) */                               \
 {NAME, 3, (OPC1+2), 12, LD_STOR_INS, {{rpindex_disps20,16}, {OP_D,4}}}

  LD_REG_INST ("loadb", 0x00124, 0xBE, 0x88, 0x219, 0x45, rpbase_disps4, regr),
  LD_REG_INST ("loadd", 0x00128, 0xAE, 0x87, 0x21A, 0x46, rpbase_dispe4, regp),
  LD_REG_INST ("loadw", 0x0012C, 0x9E, 0x89, 0x21B, 0x47, rpbase_dispe4, regr),

/* Store instructions (from reg to memory).  */
#define  ST_REG_INST(NAME, OPC1, OPC2, OPC3, OPC4, OPC5, OP_D, OP_S)     \
 /* opc8 reg abs20 */                                                    \
 {NAME, 2, OPC3,  24, LD_STOR_INS, {{OP_S,20}, {abs20,0}}},              \
 /* opc20 reg abs24 */                                                   \
 {NAME, 3, OPC1+3, 12, LD_STOR_INS, {{OP_S,4}, {abs24,16}}},             \
 /* opc7 reg rindex8_abs20 */                                            \
 {NAME, 2, OPC5, 25, LD_STOR_INS, {{OP_S,20}, {rindex8_abs20,0}}},       \
  /* opc4 reg disps4(RPbase) */                                          \
 {NAME, 1, (OPC2>>4), 28, LD_STOR_INS, {{OP_S,20}, {OP_D,24}}},          \
 /* opcNN reg  disps0(RPbase) */                                         \
 {NAME, 1, OPC2, 24, LD_STOR_INS, {{OP_S,20}, {rpindex_disps0,0}}},      \
 /* opc reg  disps14(RPbase) */                                          \
 {NAME, 2, OPC4, 22, LD_STOR_INS, {{OP_S,20}, {rpindex_disps14,0}}},     \
 /* opc reg -disps20(Rbase) */                                           \
 {NAME, 3, OPC1+0x60, 12, LD_STOR_INS, {{OP_S,4}, {rbase_dispe20,16}}},  \
 /* opc reg disps20(Rbase) */                                            \
 {NAME, 3, OPC1, 12, LD_STOR_INS, {{OP_S,4}, {rbase_disps20,16}}},       \
  /* opc reg  disps16(RPbase) */                                         \
 {NAME, 2, OPC2+1, 24, LD_STOR_INS, {{OP_S,20}, {rpbase_disps16,0}}},    \
  /* opc16 reg disps20(RPbase) */                                        \
 {NAME, 3, OPC1+1, 12, LD_STOR_INS, {{OP_S,4}, {rpbase_disps20,16}}},    \
  /* op reg (rp) -disps20(RPbase) */                                     \
 {NAME, 3, OPC1+0x61, 12, LD_STOR_INS, {{OP_S,4}, {rpbase_dispe20,16}}}, \
 /* opc reg rrp (Rindex)disps20(RPbase) */                               \
 {NAME, 3, OPC1+2, 12, LD_STOR_INS, {{OP_S,4}, {rpindex_disps20,16}}}


/* Store instructions (from imm to memory).  */
#define  ST_IMM_INST(NAME, OPC1, OPC2, OPC3, OPC4)                       \
  /* opcNN iN abs20 */                                                   \
  {NAME,  2, OPC1, 24, LD_STOR_INS, {{uimm4,20},{abs20,0}}},             \
  /* opcNN iN abs24 */                                                   \
  {NAME,  3, OPC2+3, 12, LD_STOR_INS, {{uimm4,4},{abs24,16}}},           \
  /* opcNN iN (Rindex)abs20 */                                           \
  {NAME,  2, OPC3, 25, LD_STOR_INS, {{uimm4,20}, {rindex8_abs20,0}}},    \
  /* opcNN iN (prp) disps14(RPbase) */                                   \
  {NAME,  2, OPC4, 22, LD_STOR_INS, {{uimm4,4},{rpindex_disps14,0}}},    \
  /* opcNN iN (rp) disps0(RPbase) */                                     \
  {NAME,  1, OPC1+1, 24, LD_STOR_INS, {{uimm4,20}, {rpbase_disps0,16}}}, \
  /* opcNN iN disps20(Rbase) */                                          \
  {NAME,  3, OPC2, 12, LD_STOR_INS, {{uimm4,4}, {rbase_disps20,16}}},    \
  /* opcNN iN (rp) disps16(RPBase) */                                    \
  {NAME,  2, OPC1+2, 24, LD_STOR_INS, {{uimm4,20}, {rpbase_disps16,0}}}, \
  /* opcNN iN (rp) disps20(RPBase) */                                    \
  {NAME,  3, OPC2+1, 12, LD_STOR_INS, {{uimm4,4}, {rpbase_disps20,16}}}, \
  /* opcNN iN rrp (Rindex)disps20(RPbase) */                             \
  {NAME,  3, OPC2+2, 12, LD_STOR_INS, {{uimm4,4}, {rpindex_disps20,16}}}

  ST_REG_INST ("storb", 0x00134, 0xFE, 0xC8, 0x319, 0x65, rpbase_disps4, regr),
  ST_IMM_INST ("storb", 0x81, 0x00120, 0x42, 0x218),
  ST_REG_INST ("stord", 0x00138, 0xEE, 0xC7, 0x31A, 0x66, rpbase_dispe4, regp),
  ST_REG_INST ("storw", 0x0013C, 0xDE, 0xC9, 0x31B, 0x67, rpbase_dispe4, regr),
  ST_IMM_INST ("storw", 0xC1, 0x00130, 0x62, 0x318),

/* Create instruction with no operands.  */
#define  NO_OP_INST(NAME, OPC)   \
  /* opc16 */                    \
  {NAME,  1, OPC, 16, 0, {{0, 0}}}

  NO_OP_INST ("cinv[i]",     0x000A),
  NO_OP_INST ("cinv[i,u]",   0x000B),
  NO_OP_INST ("cinv[d]",     0x000C),
  NO_OP_INST ("cinv[d,u]",   0x000D),
  NO_OP_INST ("cinv[d,i]",   0x000E),
  NO_OP_INST ("cinv[d,i,u]", 0x000F),
  NO_OP_INST ("nop",         0x2C00),
  NO_OP_INST ("retx",        0x0003),
  NO_OP_INST ("di",          0x0004),
  NO_OP_INST ("ei",          0x0005),
  NO_OP_INST ("wait",        0x0006),
  NO_OP_INST ("eiwait",      0x0007),

  {NULL,      0, 0, 0,    0, {{0, 0}}}
};

const unsigned int cr16_num_opcodes = ARRAY_SIZE (cr16_instruction);

/* Macro to build a reg_entry, which have an opcode image :
   For example :
      REG(u4, 0x84, CR16_U_REGTYPE)
   is interpreted as :
      {"u4",  u4, 0x84, CR16_U_REGTYPE}  */
#define REG(NAME, N, TYPE)    {STRINGX(NAME), {NAME}, N, TYPE}

#define REGP(NAME, BNAME, N, TYPE)    {STRINGX(NAME), {BNAME}, N, TYPE}

const reg_entry cr16_regtab[] =
{ /* Build a general purpose register r<N>.  */
#define REG_R(N)    REG(CONCAT2(r,N), N, CR16_R_REGTYPE)

  REG_R(0), REG_R(1), REG_R(2), REG_R(3),
  REG_R(4), REG_R(5), REG_R(6), REG_R(7),
  REG_R(8), REG_R(9), REG_R(10), REG_R(11),
  REG_R(12), REG_R(13), REG_R(14), REG_R(15),
  REG(r12_L, 12,  CR16_R_REGTYPE),
  REG(r13_L, 13,  CR16_R_REGTYPE),
  REG(ra,    0xe, CR16_R_REGTYPE),
  REG(sp,    0xf, CR16_R_REGTYPE),
  REG(sp_L,  0xf, CR16_R_REGTYPE),
  REG(RA,    0xe, CR16_R_REGTYPE),
};

const reg_entry cr16_regptab[] =
{ /* Build a general purpose register r<N>.  */

#define REG_RP(M,N) REGP((CONCAT2(r,M),CONCAT2(r,N)), CONCAT2(r,N), N, CR16_RP_REGTYPE)

  REG_RP(1,0), REG_RP(2,1), REG_RP(3,2), REG_RP(4,3),
  REG_RP(5,4), REG_RP(6,5), REG_RP(7,6), REG_RP(8,7),
  REG_RP(9,8), REG_RP(10,9), REG_RP(11,10), REG_RP(12,11),
  REG((r12), 0xc, CR16_RP_REGTYPE),
  REG((r13), 0xd, CR16_RP_REGTYPE),
  //REG((r14), 0xe, CR16_RP_REGTYPE),
  REG((ra), 0xe, CR16_RP_REGTYPE),
  REG((sp), 0xf, CR16_RP_REGTYPE),
};


const unsigned int cr16_num_regs = ARRAY_SIZE (cr16_regtab) ;
const unsigned int cr16_num_regps = ARRAY_SIZE (cr16_regptab) ;

const reg_entry cr16_pregtab[] =
{
/* Build a processor register.  */
  REG(dbs,   0x0, CR16_P_REGTYPE),
  REG(dsr,   0x1, CR16_P_REGTYPE),
  REG(dcrl,  0x2, CR16_P_REGTYPE),
  REG(dcrh,  0x3, CR16_P_REGTYPE),
  REG(car0l, 0x4, CR16_P_REGTYPE),
  REG(car0h, 0x5, CR16_P_REGTYPE),
  REG(car1l, 0x6, CR16_P_REGTYPE),
  REG(car1h, 0x7, CR16_P_REGTYPE),
  REG(cfg,   0x8, CR16_P_REGTYPE),
  REG(psr,   0x9, CR16_P_REGTYPE),
  REG(intbasel, 0xa, CR16_P_REGTYPE),
  REG(intbaseh, 0xb, CR16_P_REGTYPE),
  REG(ispl,  0xc, CR16_P_REGTYPE),
  REG(isph,  0xd, CR16_P_REGTYPE),
  REG(uspl,  0xe, CR16_P_REGTYPE),
  REG(usph,  0xf, CR16_P_REGTYPE),
};

const reg_entry cr16_pregptab[] =
{
  REG(dbs,   0, CR16_P_REGTYPE),
  REG(dsr,   1, CR16_P_REGTYPE),
  REG(dcr,   2, CR16_P_REGTYPE),
  REG(car0,  4, CR16_P_REGTYPE),
  REG(car1,  6, CR16_P_REGTYPE),
  REG(cfg,   8, CR16_P_REGTYPE),
  REG(psr,   9, CR16_P_REGTYPE),
  REG(intbase, 10, CR16_P_REGTYPE),
  REG(isp,   12, CR16_P_REGTYPE),
  REG(usp,   14, CR16_P_REGTYPE),
};

const unsigned int cr16_num_pregs =  ARRAY_SIZE (cr16_pregtab);
const unsigned int cr16_num_pregps =  ARRAY_SIZE (cr16_pregptab);

const char *cr16_b_cond_tab[]=
{
  "eq","ne","cs","cc","hi","ls","gt","le","fs","fc",
  "lo","hs","lt","ge","r", "???"
};

const unsigned int cr16_num_cc =  ARRAY_SIZE (cr16_b_cond_tab);

/* CR16 operands table.  */
const operand_entry cr16_optab[] =
{
  /* Index 0 is dummy, so we can count the instruction's operands.  */
  {0,    nullargs,     0},                        /* dummy */
  {3,    arg_ic,       OP_SIGNED},                /* imm3 */
  {4,    arg_ic,       OP_SIGNED},                /* imm4 */
  {5,    arg_ic,       OP_SIGNED},                /* imm5 */
  {6,    arg_ic,       OP_SIGNED},                /* imm6 */
  {16,   arg_ic,       OP_SIGNED},                /* imm16 */
  {20,   arg_ic,       OP_SIGNED},                /* imm20 */
  {32,   arg_ic,       OP_SIGNED},                /* imm32 */
  {3,    arg_ic,       OP_UNSIGNED},              /* uimm3 */
  {3,    arg_ic,       OP_UNSIGNED|OP_DEC},       /* uimm3_1 */
  {4,    arg_ic,       OP_UNSIGNED},              /* uimm4 */
  {4,    arg_ic,       OP_UNSIGNED|OP_ESC},       /* uimm4_1 */
  {5,    arg_ic,       OP_UNSIGNED},              /* uimm5 */
  {16,   arg_ic,       OP_UNSIGNED},              /* uimm16 */
  {20,   arg_ic,       OP_UNSIGNED},              /* uimm20 */
  {32,   arg_ic,       OP_UNSIGNED},              /* uimm32 */
  {5,    arg_c,        OP_EVEN|OP_SHIFT_DEC|OP_SIGNED},      /* disps5 */
  {16,   arg_c,        OP_EVEN|OP_UNSIGNED},      /* disps17 */
  {24,   arg_c,        OP_EVEN|OP_UNSIGNED},      /* disps25 */
  {8,    arg_c,        OP_EVEN|OP_UNSIGNED},      /* dispe9 */
  {20,   arg_c,        OP_UNSIGNED|OP_ABS20},     /* abs20 */
  {24,   arg_c,        OP_UNSIGNED|OP_ABS24},     /* abs24 */
  {4,    arg_rp,       0},                        /* rra */
  {4,    arg_rbase,    0},                        /* rbase */
  {20,   arg_cr,       OP_UNSIGNED},              /* rbase_disps20 */
  {21,   arg_cr,       OP_NEG},                   /* rbase_dispe20 */
  {0,    arg_crp,      0},                        /* rpbase_disps0 */
  {4,    arg_crp,      OP_EVEN|OP_SHIFT|OP_UNSIGNED|OP_ESC1},/* rpbase_dispe4 */
  {4,    arg_crp,      OP_UNSIGNED|OP_ESC1},      /* rpbase_disps4 */
  {16,   arg_crp,      OP_UNSIGNED},              /* rpbase_disps16 */
  {20,   arg_crp,      OP_UNSIGNED},              /* rpbase_disps20 */
  {21,   arg_crp,      OP_NEG},                   /* rpbase_dispe20 */
  {20,   arg_idxr,     OP_UNSIGNED},              /* rindex7_abs20  */
  {20,   arg_idxr,     OP_UNSIGNED},              /* rindex8_abs20  */
  {0,    arg_idxrp,    OP_UNSIGNED},              /* rpindex_disps0 */
  {14,   arg_idxrp,    OP_UNSIGNED},              /* rpindex_disps14 */
  {20,   arg_idxrp,    OP_UNSIGNED},              /* rpindex_disps20 */
  {4,    arg_r,        0},                        /* regr */
  {4,    arg_rp,       0},                        /* reg pair */
  {4,    arg_pr,       0},                        /* proc reg */
  {4,    arg_prp,      0},                        /* 32 bit proc reg  */
  {4,    arg_cc,       OP_UNSIGNED}               /* cc - code */
};


/* CR16 traps/interrupts.  */
const trap_entry cr16_traps[] =
{
  {"svc", 5}, {"dvz",  6}, {"flg", 7}, {"bpt", 8}, {"trc", 9},
  {"und", 10}, {"iad", 12}, {"dbg",14}, {"ise",15}
};

const unsigned int cr16_num_traps = ARRAY_SIZE (cr16_traps);

/* CR16 instructions that don't have arguments.  */
const char * cr16_no_op_insn[] =
{
  "cinv[i]", "cinv[i,u]", "cinv[d]", "cinv[d,u]", "cinv[d,i]", "cinv[d,i,u]",
  "di", "ei", "eiwait", "nop", "retx", "wait", NULL
};

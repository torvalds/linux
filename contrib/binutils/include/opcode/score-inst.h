/* score-inst.h -- Score Instructions Table
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by: 
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)

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
   Software Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef SCORE_INST_H
#define SCORE_INST_H

#define LDST_UNALIGN_MASK 0x0000007f
#define UA_LCB		  0x00000060
#define UA_LCW		  0x00000062
#define UA_LCE		  0x00000066
#define UA_SCB		  0x00000068
#define UA_SCW		  0x0000006a
#define UA_SCE		  0x0000006e
#define UA_LL		  0x0000000c
#define UA_SC		  0x0000000e
#define LDST16_RR_MASK   0x0000000f
#define N16_LW           8
#define N16_LH           9
#define N16_POP          10
#define N16_LBU          11
#define N16_SW           12
#define N16_SH           13
#define N16_PUSH         14
#define N16_SB           15
#define LDST16_RI_MASK   0x7007
#define N16_LWP          0x7000
#define N16_LHP          0x7001
#define N16_LBUP         0x7003
#define N16_SWP          0x7004
#define N16_SHP          0x7005
#define N16_SBP          0x7007
#define N16_LIU          0x5000

#define OPC_PSEUDOLDST_MASK	0x00000007

enum
{
  INSN_LW = 0,
  INSN_LH = 1,
  INSN_LHU = 2,
  INSN_LB = 3,
  INSN_SW = 4,
  INSN_SH = 5,
  INSN_LBU = 6,
  INSN_SB = 7,
};

/* Sub opcdoe opcode.  */
enum
{
  INSN16_LBU = 11,
  INSN16_LH = 9,
  INSN16_LW = 8,
  INSN16_SB = 15,
  INSN16_SH = 13,
  INSN16_SW = 12,
};

enum
{
  LDST_NOUPDATE = 0,
  LDST_PRE = 1,
  LDST_POST = 2,
};

enum score_insn_type
{
  Rd_I4,
  Rd_I5,
  Rd_rvalueBP_I5,
  Rd_lvalueBP_I5,
  Rd_Rs_I5,
  x_Rs_I5,
  x_I5_x,
  Rd_I8,
  Rd_Rs_I14,
  I15,
  Rd_I16,
  Rd_rvalueRs_SI10,
  Rd_lvalueRs_SI10,
  Rd_rvalueRs_preSI12,
  Rd_rvalueRs_postSI12,
  Rd_lvalueRs_preSI12,
  Rd_lvalueRs_postSI12,
  Rd_Rs_SI14,
  Rd_rvalueRs_SI15,
  Rd_lvalueRs_SI15,
  Rd_SI16,
  PC_DISP8div2,
  PC_DISP11div2,
  PC_DISP19div2,
  PC_DISP24div2,
  Rd_Rs_Rs,
  x_Rs_x,
  x_Rs_Rs,
  Rd_Rs_x,
  Rd_x_Rs,
  Rd_x_x,
  Rd_Rs,
  Rd_HighRs,
  Rd_lvalueRs,
  Rd_rvalueRs,
  Rd_lvalue32Rs,
  Rd_rvalue32Rs,
  x_Rs,
  NO_OPD,
  NO16_OPD,
  OP5_rvalueRs_SI15,
  I5_Rs_Rs_I5_OP5,
  x_rvalueRs_post4,
  Rd_rvalueRs_post4,
  Rd_x_I5,
  Rd_lvalueRs_post4,
  x_lvalueRs_post4,
  Rd_LowRs,
  Rd_Rs_Rs_imm,
  Insn_Type_PCE,
  Insn_Type_SYN,
  Insn_GP,
  Insn_PIC,
  Insn_internal,
};

enum score_data_type
{
  _IMM4 = 0,
  _IMM5,
  _IMM8,
  _IMM14,
  _IMM15,
  _IMM16,
  _SIMM10 = 6,
  _SIMM12,
  _SIMM14,
  _SIMM15,
  _SIMM16,
  _SIMM14_NEG = 11,
  _IMM16_NEG,
  _SIMM16_NEG,
  _IMM20,
  _IMM25,
  _DISP8div2 = 16,
  _DISP11div2,
  _DISP19div2,
  _DISP24div2,
  _VALUE,
  _VALUE_HI16,
  _VALUE_LO16,
  _VALUE_LDST_LO16 = 23,
  _SIMM16_LA,
  _IMM5_RSHIFT_1,
  _IMM5_RSHIFT_2,
  _SIMM16_LA_POS,
  _IMM5_RANGE_8_31,
  _IMM10_RSHIFT_2,
  _GP_IMM15 = 30,
  _GP_IMM14 = 31,
  _SIMM16_pic = 42,   /* Index in score_df_range.  */
  _IMM16_LO16_pic = 43,
  _IMM16_pic = 44,
};

#define REG_TMP			  1

#define OP_REG_TYPE             (1 << 6)
#define OP_IMM_TYPE             (1 << 7)
#define OP_SH_REGD              (OP_REG_TYPE |20)
#define	OP_SH_REGS1             (OP_REG_TYPE |15)
#define OP_SH_REGS2             (OP_REG_TYPE |10)
#define OP_SH_I                 (OP_IMM_TYPE | 1)
#define OP_SH_RI15              (OP_IMM_TYPE | 0)
#define OP_SH_I12               (OP_IMM_TYPE | 3)
#define OP_SH_DISP24            (OP_IMM_TYPE | 1)
#define OP_SH_DISP19_p1         (OP_IMM_TYPE |15)
#define OP_SH_DISP19_p2         (OP_IMM_TYPE | 1)
#define OP_SH_I5                (OP_IMM_TYPE |10)
#define OP_SH_I10               (OP_IMM_TYPE | 5)
#define OP_SH_COPID             (OP_IMM_TYPE | 5)
#define OP_SH_TRAPI5            (OP_IMM_TYPE |15)
#define OP_SH_I15               (OP_IMM_TYPE |10)

#define OP16_SH_REGD            (OP_REG_TYPE | 8)
#define	OP16_SH_REGS1           (OP_REG_TYPE | 4)
#define	OP16_SH_I45             (OP_IMM_TYPE | 3)
#define	OP16_SH_I8              (OP_IMM_TYPE | 0)
#define OP16_SH_DISP8           (OP_IMM_TYPE | 0)
#define OP16_SH_DISP11          (OP_IMM_TYPE | 1)

struct datafield_range
{
  int data_type;
  int bits;
  int range[2];
};

struct datafield_range score_df_range[] =
{
  {_IMM4,             4,  {0, (1 << 4) - 1}},	        /* (     0 ~ 15   ) */
  {_IMM5,             5,  {0, (1 << 5) - 1}},	        /* (     0 ~ 31   ) */
  {_IMM8,             8,  {0, (1 << 8) - 1}},	        /* (     0 ~ 255  ) */
  {_IMM14,            14, {0, (1 << 14) - 1}},	        /* (     0 ~ 16383) */
  {_IMM15,            15, {0, (1 << 15) - 1}},	        /* (     0 ~ 32767) */
  {_IMM16,            16, {0, (1 << 16) - 1}},	        /* (     0 ~ 65535) */
  {_SIMM10,           10, {-(1 << 9), (1 << 9) - 1}},	/* (  -512 ~ 511  ) */
  {_SIMM12,           12, {-(1 << 11), (1 << 11) - 1}},	/* ( -2048 ~ 2047 ) */
  {_SIMM14,           14, {-(1 << 13), (1 << 13) - 1}},	/* ( -8192 ~ 8191 ) */
  {_SIMM15,           15, {-(1 << 14), (1 << 14) - 1}},	/* (-16384 ~ 16383) */
  {_SIMM16,           16, {-(1 << 15), (1 << 15) - 1}},	/* (-32768 ~ 32767) */
  {_SIMM14_NEG,       14, {-(1 << 13), (1 << 13) - 1}},	/* ( -8191 ~ 8192 ) */
  {_IMM16_NEG,        16, {0, (1 << 16) - 1}},	        /* (-65535 ~ 0    ) */
  {_SIMM16_NEG,       16, {-(1 << 15), (1 << 15) - 1}},	/* (-32768 ~ 32767) */
  {_IMM20,            20, {0, (1 << 20) - 1}},
  {_IMM25,            25, {0, (1 << 25) - 1}},
  {_DISP8div2,        8,  {-(1 << 8), (1 << 8) - 1}},	/* (  -256 ~ 255  ) */
  {_DISP11div2,       11, {0, 0}},
  {_DISP19div2,       19, {-(1 << 19), (1 << 19) - 1}},	/* (-524288 ~ 524287) */
  {_DISP24div2,       24, {0, 0}},
  {_VALUE,            32, {0, ((unsigned int)1 << 31) - 1}},
  {_VALUE_HI16,       16, {0, (1 << 16) - 1}},
  {_VALUE_LO16,       16, {0, (1 << 16) - 1}},
  {_VALUE_LDST_LO16,  16, {0, (1 << 16) - 1}},
  {_SIMM16_LA,        16, {-(1 << 15), (1 << 15) - 1}},	/* (-32768 ~ 32767) */
  {_IMM5_RSHIFT_1,    5,  {0, (1 << 6) - 1}},	        /* (     0 ~ 63   ) */
  {_IMM5_RSHIFT_2,    5,  {0, (1 << 7) - 1}},	        /* (     0 ~ 127  ) */
  {_SIMM16_LA_POS,    16, {0, (1 << 15) - 1}},	        /* (     0 ~ 32767) */
  {_IMM5_RANGE_8_31,  5,  {8, 31}},	                /* But for cop0 the valid data : (8 ~ 31). */
  {_IMM10_RSHIFT_2,   10, {-(1 << 11), (1 << 11) - 1}},	/* For ldc#, stc#. */
  {_SIMM10,           10, {0, (1 << 10) - 1}},	        /* ( -1024 ~ 1023 ) */
  {_SIMM12,           12, {0, (1 << 12) - 1}},	        /* ( -2048 ~ 2047 ) */
  {_SIMM14,           14, {0, (1 << 14) - 1}},          /* ( -8192 ~ 8191 ) */
  {_SIMM15,           15, {0, (1 << 15) - 1}},	        /* (-16384 ~ 16383) */
  {_SIMM16,           16, {0, (1 << 16) - 1}},	        /* (-65536 ~ 65536) */
  {_SIMM14_NEG,       14, {0, (1 << 16) - 1}},          /* ( -8191 ~ 8192 ) */
  {_IMM16_NEG,        16, {0, (1 << 16) - 1}},	        /* ( 65535 ~ 0    ) */
  {_SIMM16_NEG,       16, {0, (1 << 16) - 1}},	        /* ( 65535 ~ 0    ) */
  {_IMM20,            20, {0, (1 << 20) - 1}},	        /* (-32768 ~ 32767) */
  {_IMM25,            25, {0, (1 << 25) - 1}},	        /* (-32768 ~ 32767) */
  {_GP_IMM15,         15, {0, (1 << 15) - 1}},	        /* (     0 ~ 65535) */
  {_GP_IMM14,         14, {0, (1 << 14) - 1}},	        /* (     0 ~ 65535) */
  {_SIMM16_pic,       16, {-(1 << 15), (1 << 15) - 1}},	/* (-32768 ~ 32767) */
  {_IMM16_LO16_pic,   16, {0, (1 << 16) - 1}},	        /* ( 65535 ~ 0    ) */
  {_IMM16_pic,        16, {0, (1 << 16) - 1}},	        /* (     0 ~ 65535) */
};

struct shift_bitmask
{
  int opd_type;
  int opd_num;
  struct datafield_range *df_range;
  int sh[4];
  long fieldbits[4];
};

struct shift_bitmask score_sh_bits_map[] =
{
  {
   Rd_I4, 2, &score_df_range[_IMM4],
   {OP16_SH_REGD, OP16_SH_I45, 0, 0},
   {0xf, 0xf, 0, 0},
   },
  {
   Rd_I5, 2, &score_df_range[_IMM5],
   {OP16_SH_REGD, OP16_SH_I45, 0, 0},
   {0xf, 0x1f, 0, 0},
   },
  {
   Rd_rvalueBP_I5, 2, &score_df_range[_IMM5],
   {OP16_SH_REGD, OP16_SH_I45, 0, 0},
   {0xf, 0x1f, 0, 0},
   },
  {
   Rd_lvalueBP_I5, 2, &score_df_range[_IMM5],
   {OP16_SH_REGD, OP16_SH_I45, 0, 0},
   {0xf, 0x1f, 0, 0},
   },
  {
   Rd_Rs_I5, 3, &score_df_range[_IMM5],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I5, 0},
   {0x1f, 0x1f, 0x1f, 0},
   },
  {
   x_Rs_I5, 2, &score_df_range[_IMM5],
   {OP_SH_REGS1, OP_SH_I5, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
  {
   x_I5_x, 1, &score_df_range[_IMM5],
   {OP_SH_TRAPI5, 0, 0, 0},
   {0x1f, 0, 0, 0},
   },
  {
   Rd_I8, 2, &score_df_range[_IMM8],
   {OP16_SH_REGD, OP16_SH_I8, 0, 0},
   {0xf, 0xff, 0, 0},
   },
  {
   Rd_Rs_I14, 3, &score_df_range[_IMM14],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I, 0},
   {0x1f, 0x1f, 0x3fff, 0},
   },
  {
   I15, 1, &score_df_range[_IMM15],
   {OP_SH_I15, 0, 0, 0},
   {0x7fff, 0, 0, 0},
   },
  {
   Rd_I16, 2, &score_df_range[_IMM16],
   {OP_SH_REGD, OP_SH_I, 0, 0},
   {0x1f, 0xffff, 0, 0},
   },
  {
   Rd_rvalueRs_SI10, 3, &score_df_range[_SIMM10],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I10, 0},
   {0x1f, 0x1f, 0x3ff, 0},
   },
  {
   Rd_lvalueRs_SI10, 3, &score_df_range[_SIMM10],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I10, 0},
   {0x1f, 0x1f, 0x3ff, 0},
   },
  {
   Rd_rvalueRs_preSI12, 3, &score_df_range[_SIMM12],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I12, 0},
   {0xf, 0xf, 0xfff, 0},
   },
  {
   Rd_rvalueRs_postSI12, 3, &score_df_range[_SIMM12],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I12, 0},
   {0xf, 0xf, 0xfff, 0},
   },
  {
   Rd_lvalueRs_preSI12, 3, &score_df_range[_SIMM12],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I12, 0},
   {0xf, 0xf, 0xfff, 0},
   },
  {
   Rd_lvalueRs_postSI12, 3, &score_df_range[_SIMM12],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I12, 0},
   {0xf, 0xf, 0xfff, 0},
   },
  {
   Rd_Rs_SI14, 3, &score_df_range[_SIMM14],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_I, 0},
   {0x1f, 0x1f, 0x3fff, 0},
   },
  {
   Rd_rvalueRs_SI15, 3, &score_df_range[_SIMM15],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_RI15, 0},
   {0x1f, 0x1f, 0x7fff, 0},
   },
  {
   Rd_lvalueRs_SI15, 3, &score_df_range[_SIMM15],
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_RI15, 0},
   {0x1f, 0x1f, 0x7fff, 0},
   },
  {
   Rd_SI16, 2, &score_df_range[_SIMM16],
   {OP_SH_REGD, OP_SH_I, 0, 0},
   {0x1f, 0xffff, 0, 0},
   },
  {
   PC_DISP8div2, 1, &score_df_range[_DISP8div2],
   {OP16_SH_DISP8, 0, 0, 0},
   {0xff, 0, 0, 0},
   },
  {
   PC_DISP11div2, 1, &score_df_range[_DISP11div2],
   {OP16_SH_DISP11, 0, 0, 0},
   {0x7ff, 0, 0, 0},
   },
  {
   PC_DISP19div2, 2, &score_df_range[_DISP19div2],
   {OP_SH_DISP19_p1, OP_SH_DISP19_p2, 0, 0},
   {0x3ff, 0x1ff, 0, 0},
   },
  {
   PC_DISP24div2, 1, &score_df_range[_DISP24div2],
   {OP_SH_DISP24, 0, 0, 0},
   {0xffffff, 0, 0, 0},
   },
  {
   Rd_Rs_Rs, 3, NULL,
   {OP_SH_REGD, OP_SH_REGS1, OP_SH_REGS2, 0},
   {0x1f, 0x1f, 0x1f, 0}
   },
  {
   Rd_Rs_x, 2, NULL,
   {OP_SH_REGD, OP_SH_REGS1, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
  {
   Rd_x_Rs, 2, NULL,
   {OP_SH_REGD, OP_SH_REGS2, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
  {
   Rd_x_x, 1, NULL,
   {OP_SH_REGD, 0, 0, 0},
   {0x1f, 0, 0, 0},
   },
  {
   x_Rs_Rs, 2, NULL,
   {OP_SH_REGS1, OP_SH_REGS2, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
  {
   x_Rs_x, 1, NULL,
   {OP_SH_REGS1, 0, 0, 0},
   {0x1f, 0, 0, 0},
   },
  {
   Rd_Rs, 2, NULL,
   {OP16_SH_REGD, OP16_SH_REGS1, 0, 0},
   {0xf, 0xf, 0, 0},
   },
  {
   Rd_HighRs, 2, NULL,
   {OP16_SH_REGD, OP16_SH_REGS1, 0, 0},
   {0xf, 0xf, 0x1f, 0},
   },
  {
   Rd_rvalueRs, 2, NULL,
   {OP16_SH_REGD, OP16_SH_REGS1, 0, 0},
   {0xf, 0xf, 0, 0},
   },
  {
   Rd_lvalueRs, 2, NULL,
   {OP16_SH_REGD, OP16_SH_REGS1, 0, 0},
   {0xf, 0xf, 0, 0}
   },
   {
   Rd_lvalue32Rs, 2, NULL,
   {OP_SH_REGD, OP_SH_REGS1, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
   {
   Rd_rvalue32Rs, 2, NULL,
   {OP_SH_REGD, OP_SH_REGS1, 0, 0},
   {0x1f, 0x1f, 0, 0},
   },
  {
   x_Rs, 1, NULL,
   {OP16_SH_REGS1, 0, 0, 0},
   {0xf, 0, 0, 0},
   },
  {
   NO_OPD, 0, NULL,
   {0, 0, 0, 0},
   {0, 0, 0, 0},
   },
  {
   NO16_OPD, 0, NULL,
   {0, 0, 0, 0},
   {0, 0, 0, 0},
   },
};

struct asm_opcode
{
  /* Instruction name.  */
  const char *template;

  /* Instruction Opcode.  */
  unsigned long value;

  /* Instruction bit mask.  */
  unsigned long bitmask;

  /* Relax instruction opcode.  0x8000 imply no relaxation.  */
  unsigned long relax_value;

  /* Instruction type.  */
  enum score_insn_type type;

  /* Function to call to parse args.  */
  void (*parms) (char *);
};

enum insn_class
{
  INSN_CLASS_16,
  INSN_CLASS_32,
  INSN_CLASS_PCE,
  INSN_CLASS_SYN
};

#endif

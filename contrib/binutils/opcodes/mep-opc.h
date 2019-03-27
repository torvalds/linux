/* Instruction opcode header for mep.

THIS FILE IS MACHINE GENERATED WITH CGEN.

Copyright 1996-2005 Free Software Foundation, Inc.

This file is part of the GNU Binutils and/or GDB, the GNU debugger.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.

*/

#ifndef MEP_OPC_H
#define MEP_OPC_H

/* -- opc.h */

#undef  CGEN_DIS_HASH_SIZE
#define CGEN_DIS_HASH_SIZE 1

#undef  CGEN_DIS_HASH
#define CGEN_DIS_HASH(buffer, insn) 0

#define CGEN_VERBOSE_ASSEMBLER_ERRORS

typedef struct
{
  char * name;
  int    config_enum;
  unsigned cpu_flag;
  int    big_endian;
  int    vliw_bits;
  CGEN_ATTR_VALUE_BITSET_TYPE cop16_isa;
  CGEN_ATTR_VALUE_BITSET_TYPE cop32_isa;
  CGEN_ATTR_VALUE_BITSET_TYPE cop48_isa;
  CGEN_ATTR_VALUE_BITSET_TYPE cop64_isa;
  CGEN_ATTR_VALUE_BITSET_TYPE cop_isa;
  CGEN_ATTR_VALUE_BITSET_TYPE core_isa;
  unsigned int option_mask;
} mep_config_map_struct;

extern mep_config_map_struct mep_config_map[];
extern int mep_config_index;

extern void init_mep_all_core_isas_mask (void);
extern void init_mep_all_cop_isas_mask  (void);
extern CGEN_ATTR_VALUE_BITSET_TYPE mep_cop_isa  (void);

#define MEP_CONFIG     (mep_config_map[mep_config_index].config_enum)
#define MEP_CPU        (mep_config_map[mep_config_index].cpu_flag)
#define MEP_OMASK      (mep_config_map[mep_config_index].option_mask)
#define MEP_VLIW       (mep_config_map[mep_config_index].vliw_bits > 0)
#define MEP_VLIW32     (mep_config_map[mep_config_index].vliw_bits == 32)
#define MEP_VLIW64     (mep_config_map[mep_config_index].vliw_bits == 64)
#define MEP_COP16_ISA  (mep_config_map[mep_config_index].cop16_isa)
#define MEP_COP32_ISA  (mep_config_map[mep_config_index].cop32_isa)
#define MEP_COP48_ISA  (mep_config_map[mep_config_index].cop48_isa)
#define MEP_COP64_ISA  (mep_config_map[mep_config_index].cop64_isa)
#define MEP_COP_ISA    (mep_config_map[mep_config_index].cop_isa)
#define MEP_CORE_ISA   (mep_config_map[mep_config_index].core_isa)

extern int mep_insn_supported_by_isa (const CGEN_INSN *, CGEN_ATTR_VALUE_BITSET_TYPE *);

/* A mask for all ISAs executed by the core.  */
#define MEP_ALL_CORE_ISAS_MASK mep_all_core_isas_mask
extern CGEN_ATTR_VALUE_BITSET_TYPE mep_all_core_isas_mask;

#define MEP_INSN_CORE_P(insn) ( \
  init_mep_all_core_isas_mask (), \
  mep_insn_supported_by_isa (insn, & MEP_ALL_CORE_ISAS_MASK) \
)

/* A mask for all ISAs executed by a VLIW coprocessor.  */
#define MEP_ALL_COP_ISAS_MASK mep_all_cop_isas_mask 
extern CGEN_ATTR_VALUE_BITSET_TYPE mep_all_cop_isas_mask;

#define MEP_INSN_COP_P(insn) ( \
  init_mep_all_cop_isas_mask (), \
  mep_insn_supported_by_isa (insn, & MEP_ALL_COP_ISAS_MASK) \
)

extern int mep_cgen_insn_supported (CGEN_CPU_DESC, const CGEN_INSN *);

/* -- asm.c */
/* Enum declaration for mep instruction types.  */
typedef enum cgen_insn_type {
  MEP_INSN_INVALID, MEP_INSN_SB, MEP_INSN_SH, MEP_INSN_SW
 , MEP_INSN_LB, MEP_INSN_LH, MEP_INSN_LW, MEP_INSN_LBU
 , MEP_INSN_LHU, MEP_INSN_SW_SP, MEP_INSN_LW_SP, MEP_INSN_SB_TP
 , MEP_INSN_SH_TP, MEP_INSN_SW_TP, MEP_INSN_LB_TP, MEP_INSN_LH_TP
 , MEP_INSN_LW_TP, MEP_INSN_LBU_TP, MEP_INSN_LHU_TP, MEP_INSN_SB16
 , MEP_INSN_SH16, MEP_INSN_SW16, MEP_INSN_LB16, MEP_INSN_LH16
 , MEP_INSN_LW16, MEP_INSN_LBU16, MEP_INSN_LHU16, MEP_INSN_SW24
 , MEP_INSN_LW24, MEP_INSN_EXTB, MEP_INSN_EXTH, MEP_INSN_EXTUB
 , MEP_INSN_EXTUH, MEP_INSN_SSARB, MEP_INSN_MOV, MEP_INSN_MOVI8
 , MEP_INSN_MOVI16, MEP_INSN_MOVU24, MEP_INSN_MOVU16, MEP_INSN_MOVH
 , MEP_INSN_ADD3, MEP_INSN_ADD, MEP_INSN_ADD3I, MEP_INSN_ADVCK3
 , MEP_INSN_SUB, MEP_INSN_SBVCK3, MEP_INSN_NEG, MEP_INSN_SLT3
 , MEP_INSN_SLTU3, MEP_INSN_SLT3I, MEP_INSN_SLTU3I, MEP_INSN_SL1AD3
 , MEP_INSN_SL2AD3, MEP_INSN_ADD3X, MEP_INSN_SLT3X, MEP_INSN_SLTU3X
 , MEP_INSN_OR, MEP_INSN_AND, MEP_INSN_XOR, MEP_INSN_NOR
 , MEP_INSN_OR3, MEP_INSN_AND3, MEP_INSN_XOR3, MEP_INSN_SRA
 , MEP_INSN_SRL, MEP_INSN_SLL, MEP_INSN_SRAI, MEP_INSN_SRLI
 , MEP_INSN_SLLI, MEP_INSN_SLL3, MEP_INSN_FSFT, MEP_INSN_BRA
 , MEP_INSN_BEQZ, MEP_INSN_BNEZ, MEP_INSN_BEQI, MEP_INSN_BNEI
 , MEP_INSN_BLTI, MEP_INSN_BGEI, MEP_INSN_BEQ, MEP_INSN_BNE
 , MEP_INSN_BSR12, MEP_INSN_BSR24, MEP_INSN_JMP, MEP_INSN_JMP24
 , MEP_INSN_JSR, MEP_INSN_RET, MEP_INSN_REPEAT, MEP_INSN_EREPEAT
 , MEP_INSN_STC_LP, MEP_INSN_STC_HI, MEP_INSN_STC_LO, MEP_INSN_STC
 , MEP_INSN_LDC_LP, MEP_INSN_LDC_HI, MEP_INSN_LDC_LO, MEP_INSN_LDC
 , MEP_INSN_DI, MEP_INSN_EI, MEP_INSN_RETI, MEP_INSN_HALT
 , MEP_INSN_SLEEP, MEP_INSN_SWI, MEP_INSN_BREAK, MEP_INSN_SYNCM
 , MEP_INSN_STCB, MEP_INSN_LDCB, MEP_INSN_BSETM, MEP_INSN_BCLRM
 , MEP_INSN_BNOTM, MEP_INSN_BTSTM, MEP_INSN_TAS, MEP_INSN_CACHE
 , MEP_INSN_MUL, MEP_INSN_MULU, MEP_INSN_MULR, MEP_INSN_MULRU
 , MEP_INSN_MADD, MEP_INSN_MADDU, MEP_INSN_MADDR, MEP_INSN_MADDRU
 , MEP_INSN_DIV, MEP_INSN_DIVU, MEP_INSN_DRET, MEP_INSN_DBREAK
 , MEP_INSN_LDZ, MEP_INSN_ABS, MEP_INSN_AVE, MEP_INSN_MIN
 , MEP_INSN_MAX, MEP_INSN_MINU, MEP_INSN_MAXU, MEP_INSN_CLIP
 , MEP_INSN_CLIPU, MEP_INSN_SADD, MEP_INSN_SSUB, MEP_INSN_SADDU
 , MEP_INSN_SSUBU, MEP_INSN_SWCP, MEP_INSN_LWCP, MEP_INSN_SMCP
 , MEP_INSN_LMCP, MEP_INSN_SWCPI, MEP_INSN_LWCPI, MEP_INSN_SMCPI
 , MEP_INSN_LMCPI, MEP_INSN_SWCP16, MEP_INSN_LWCP16, MEP_INSN_SMCP16
 , MEP_INSN_LMCP16, MEP_INSN_SBCPA, MEP_INSN_LBCPA, MEP_INSN_SHCPA
 , MEP_INSN_LHCPA, MEP_INSN_SWCPA, MEP_INSN_LWCPA, MEP_INSN_SMCPA
 , MEP_INSN_LMCPA, MEP_INSN_SBCPM0, MEP_INSN_LBCPM0, MEP_INSN_SHCPM0
 , MEP_INSN_LHCPM0, MEP_INSN_SWCPM0, MEP_INSN_LWCPM0, MEP_INSN_SMCPM0
 , MEP_INSN_LMCPM0, MEP_INSN_SBCPM1, MEP_INSN_LBCPM1, MEP_INSN_SHCPM1
 , MEP_INSN_LHCPM1, MEP_INSN_SWCPM1, MEP_INSN_LWCPM1, MEP_INSN_SMCPM1
 , MEP_INSN_LMCPM1, MEP_INSN_BCPEQ, MEP_INSN_BCPNE, MEP_INSN_BCPAT
 , MEP_INSN_BCPAF, MEP_INSN_SYNCCP, MEP_INSN_JSRV, MEP_INSN_BSRV
 , MEP_INSN_SIM_SYSCALL, MEP_INSN_RI_0, MEP_INSN_RI_1, MEP_INSN_RI_2
 , MEP_INSN_RI_3, MEP_INSN_RI_4, MEP_INSN_RI_5, MEP_INSN_RI_6
 , MEP_INSN_RI_7, MEP_INSN_RI_8, MEP_INSN_RI_9, MEP_INSN_RI_10
 , MEP_INSN_RI_11, MEP_INSN_RI_12, MEP_INSN_RI_13, MEP_INSN_RI_14
 , MEP_INSN_RI_15, MEP_INSN_RI_17, MEP_INSN_RI_20, MEP_INSN_RI_21
 , MEP_INSN_RI_22, MEP_INSN_RI_23, MEP_INSN_RI_24, MEP_INSN_RI_25
 , MEP_INSN_RI_26, MEP_INSN_RI_16, MEP_INSN_RI_18, MEP_INSN_RI_19
 , MEP_INSN_FADDS, MEP_INSN_FSUBS, MEP_INSN_FMULS, MEP_INSN_FDIVS
 , MEP_INSN_FSQRTS, MEP_INSN_FABSS, MEP_INSN_FNEGS, MEP_INSN_FMOVS
 , MEP_INSN_FROUNDWS, MEP_INSN_FTRUNCWS, MEP_INSN_FCEILWS, MEP_INSN_FFLOORWS
 , MEP_INSN_FCVTWS, MEP_INSN_FCVTSW, MEP_INSN_FCMPFS, MEP_INSN_FCMPUS
 , MEP_INSN_FCMPES, MEP_INSN_FCMPUES, MEP_INSN_FCMPLS, MEP_INSN_FCMPULS
 , MEP_INSN_FCMPLES, MEP_INSN_FCMPULES, MEP_INSN_FCMPFIS, MEP_INSN_FCMPUIS
 , MEP_INSN_FCMPEIS, MEP_INSN_FCMPUEIS, MEP_INSN_FCMPLIS, MEP_INSN_FCMPULIS
 , MEP_INSN_FCMPLEIS, MEP_INSN_FCMPULEIS, MEP_INSN_CMOV_FRN_RM, MEP_INSN_CMOV_RM_FRN
 , MEP_INSN_CMOVC_CCRN_RM, MEP_INSN_CMOVC_RM_CCRN
} CGEN_INSN_TYPE;

/* Index of `invalid' insn place holder.  */
#define CGEN_INSN_INVALID MEP_INSN_INVALID

/* Total number of insns in table.  */
#define MAX_INSNS ((int) MEP_INSN_CMOVC_RM_CCRN + 1)

/* This struct records data prior to insertion or after extraction.  */
struct cgen_fields
{
  int length;
  long f_nil;
  long f_anyof;
  long f_major;
  long f_rn;
  long f_rn3;
  long f_rm;
  long f_rl;
  long f_sub2;
  long f_sub3;
  long f_sub4;
  long f_ext;
  long f_crn;
  long f_csrn_hi;
  long f_csrn_lo;
  long f_csrn;
  long f_crnx_hi;
  long f_crnx_lo;
  long f_crnx;
  long f_0;
  long f_1;
  long f_2;
  long f_3;
  long f_4;
  long f_5;
  long f_6;
  long f_7;
  long f_8;
  long f_9;
  long f_10;
  long f_11;
  long f_12;
  long f_13;
  long f_14;
  long f_15;
  long f_16;
  long f_17;
  long f_18;
  long f_19;
  long f_20;
  long f_21;
  long f_22;
  long f_23;
  long f_24;
  long f_25;
  long f_26;
  long f_27;
  long f_28;
  long f_29;
  long f_30;
  long f_31;
  long f_8s8a2;
  long f_12s4a2;
  long f_17s16a2;
  long f_24s5a2n_hi;
  long f_24s5a2n_lo;
  long f_24s5a2n;
  long f_24u5a2n_hi;
  long f_24u5a2n_lo;
  long f_24u5a2n;
  long f_2u6;
  long f_7u9;
  long f_7u9a2;
  long f_7u9a4;
  long f_16s16;
  long f_2u10;
  long f_3u5;
  long f_4u8;
  long f_5u8;
  long f_5u24;
  long f_6s8;
  long f_8s8;
  long f_16u16;
  long f_12u16;
  long f_3u29;
  long f_8s24;
  long f_8s24a2;
  long f_8s24a4;
  long f_8s24a8;
  long f_24u8a4n_hi;
  long f_24u8a4n_lo;
  long f_24u8a4n;
  long f_24u8n_hi;
  long f_24u8n_lo;
  long f_24u8n;
  long f_24u4n_hi;
  long f_24u4n_lo;
  long f_24u4n;
  long f_callnum;
  long f_ccrn_hi;
  long f_ccrn_lo;
  long f_ccrn;
  long f_fmax_0_4;
  long f_fmax_4_4;
  long f_fmax_8_4;
  long f_fmax_12_4;
  long f_fmax_16_4;
  long f_fmax_20_4;
  long f_fmax_24_4;
  long f_fmax_28_1;
  long f_fmax_29_1;
  long f_fmax_30_1;
  long f_fmax_31_1;
  long f_fmax_frd;
  long f_fmax_frn;
  long f_fmax_frm;
  long f_fmax_rm;
};

#define CGEN_INIT_PARSE(od) \
{\
}
#define CGEN_INIT_INSERT(od) \
{\
}
#define CGEN_INIT_EXTRACT(od) \
{\
}
#define CGEN_INIT_PRINT(od) \
{\
}


#endif /* MEP_OPC_H */

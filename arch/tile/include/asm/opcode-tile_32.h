/* tile.h -- Header file for TILE opcode table
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Tilera Corp. */

#ifndef opcode_tile_h
#define opcode_tile_h

typedef unsigned long long tile_bundle_bits;


enum
{
  TILE_MAX_OPERANDS = 5 /* mm */
};

typedef enum
{
  TILE_OPC_BPT,
  TILE_OPC_INFO,
  TILE_OPC_INFOL,
  TILE_OPC_J,
  TILE_OPC_JAL,
  TILE_OPC_MOVE,
  TILE_OPC_MOVE_SN,
  TILE_OPC_MOVEI,
  TILE_OPC_MOVEI_SN,
  TILE_OPC_MOVELI,
  TILE_OPC_MOVELI_SN,
  TILE_OPC_MOVELIS,
  TILE_OPC_PREFETCH,
  TILE_OPC_RAISE,
  TILE_OPC_ADD,
  TILE_OPC_ADD_SN,
  TILE_OPC_ADDB,
  TILE_OPC_ADDB_SN,
  TILE_OPC_ADDBS_U,
  TILE_OPC_ADDBS_U_SN,
  TILE_OPC_ADDH,
  TILE_OPC_ADDH_SN,
  TILE_OPC_ADDHS,
  TILE_OPC_ADDHS_SN,
  TILE_OPC_ADDI,
  TILE_OPC_ADDI_SN,
  TILE_OPC_ADDIB,
  TILE_OPC_ADDIB_SN,
  TILE_OPC_ADDIH,
  TILE_OPC_ADDIH_SN,
  TILE_OPC_ADDLI,
  TILE_OPC_ADDLI_SN,
  TILE_OPC_ADDLIS,
  TILE_OPC_ADDS,
  TILE_OPC_ADDS_SN,
  TILE_OPC_ADIFFB_U,
  TILE_OPC_ADIFFB_U_SN,
  TILE_OPC_ADIFFH,
  TILE_OPC_ADIFFH_SN,
  TILE_OPC_AND,
  TILE_OPC_AND_SN,
  TILE_OPC_ANDI,
  TILE_OPC_ANDI_SN,
  TILE_OPC_AULI,
  TILE_OPC_AVGB_U,
  TILE_OPC_AVGB_U_SN,
  TILE_OPC_AVGH,
  TILE_OPC_AVGH_SN,
  TILE_OPC_BBNS,
  TILE_OPC_BBNS_SN,
  TILE_OPC_BBNST,
  TILE_OPC_BBNST_SN,
  TILE_OPC_BBS,
  TILE_OPC_BBS_SN,
  TILE_OPC_BBST,
  TILE_OPC_BBST_SN,
  TILE_OPC_BGEZ,
  TILE_OPC_BGEZ_SN,
  TILE_OPC_BGEZT,
  TILE_OPC_BGEZT_SN,
  TILE_OPC_BGZ,
  TILE_OPC_BGZ_SN,
  TILE_OPC_BGZT,
  TILE_OPC_BGZT_SN,
  TILE_OPC_BITX,
  TILE_OPC_BITX_SN,
  TILE_OPC_BLEZ,
  TILE_OPC_BLEZ_SN,
  TILE_OPC_BLEZT,
  TILE_OPC_BLEZT_SN,
  TILE_OPC_BLZ,
  TILE_OPC_BLZ_SN,
  TILE_OPC_BLZT,
  TILE_OPC_BLZT_SN,
  TILE_OPC_BNZ,
  TILE_OPC_BNZ_SN,
  TILE_OPC_BNZT,
  TILE_OPC_BNZT_SN,
  TILE_OPC_BYTEX,
  TILE_OPC_BYTEX_SN,
  TILE_OPC_BZ,
  TILE_OPC_BZ_SN,
  TILE_OPC_BZT,
  TILE_OPC_BZT_SN,
  TILE_OPC_CLZ,
  TILE_OPC_CLZ_SN,
  TILE_OPC_CRC32_32,
  TILE_OPC_CRC32_32_SN,
  TILE_OPC_CRC32_8,
  TILE_OPC_CRC32_8_SN,
  TILE_OPC_CTZ,
  TILE_OPC_CTZ_SN,
  TILE_OPC_DRAIN,
  TILE_OPC_DTLBPR,
  TILE_OPC_DWORD_ALIGN,
  TILE_OPC_DWORD_ALIGN_SN,
  TILE_OPC_FINV,
  TILE_OPC_FLUSH,
  TILE_OPC_FNOP,
  TILE_OPC_ICOH,
  TILE_OPC_ILL,
  TILE_OPC_INTHB,
  TILE_OPC_INTHB_SN,
  TILE_OPC_INTHH,
  TILE_OPC_INTHH_SN,
  TILE_OPC_INTLB,
  TILE_OPC_INTLB_SN,
  TILE_OPC_INTLH,
  TILE_OPC_INTLH_SN,
  TILE_OPC_INV,
  TILE_OPC_IRET,
  TILE_OPC_JALB,
  TILE_OPC_JALF,
  TILE_OPC_JALR,
  TILE_OPC_JALRP,
  TILE_OPC_JB,
  TILE_OPC_JF,
  TILE_OPC_JR,
  TILE_OPC_JRP,
  TILE_OPC_LB,
  TILE_OPC_LB_SN,
  TILE_OPC_LB_U,
  TILE_OPC_LB_U_SN,
  TILE_OPC_LBADD,
  TILE_OPC_LBADD_SN,
  TILE_OPC_LBADD_U,
  TILE_OPC_LBADD_U_SN,
  TILE_OPC_LH,
  TILE_OPC_LH_SN,
  TILE_OPC_LH_U,
  TILE_OPC_LH_U_SN,
  TILE_OPC_LHADD,
  TILE_OPC_LHADD_SN,
  TILE_OPC_LHADD_U,
  TILE_OPC_LHADD_U_SN,
  TILE_OPC_LNK,
  TILE_OPC_LNK_SN,
  TILE_OPC_LW,
  TILE_OPC_LW_SN,
  TILE_OPC_LW_NA,
  TILE_OPC_LW_NA_SN,
  TILE_OPC_LWADD,
  TILE_OPC_LWADD_SN,
  TILE_OPC_LWADD_NA,
  TILE_OPC_LWADD_NA_SN,
  TILE_OPC_MAXB_U,
  TILE_OPC_MAXB_U_SN,
  TILE_OPC_MAXH,
  TILE_OPC_MAXH_SN,
  TILE_OPC_MAXIB_U,
  TILE_OPC_MAXIB_U_SN,
  TILE_OPC_MAXIH,
  TILE_OPC_MAXIH_SN,
  TILE_OPC_MF,
  TILE_OPC_MFSPR,
  TILE_OPC_MINB_U,
  TILE_OPC_MINB_U_SN,
  TILE_OPC_MINH,
  TILE_OPC_MINH_SN,
  TILE_OPC_MINIB_U,
  TILE_OPC_MINIB_U_SN,
  TILE_OPC_MINIH,
  TILE_OPC_MINIH_SN,
  TILE_OPC_MM,
  TILE_OPC_MNZ,
  TILE_OPC_MNZ_SN,
  TILE_OPC_MNZB,
  TILE_OPC_MNZB_SN,
  TILE_OPC_MNZH,
  TILE_OPC_MNZH_SN,
  TILE_OPC_MTSPR,
  TILE_OPC_MULHH_SS,
  TILE_OPC_MULHH_SS_SN,
  TILE_OPC_MULHH_SU,
  TILE_OPC_MULHH_SU_SN,
  TILE_OPC_MULHH_UU,
  TILE_OPC_MULHH_UU_SN,
  TILE_OPC_MULHHA_SS,
  TILE_OPC_MULHHA_SS_SN,
  TILE_OPC_MULHHA_SU,
  TILE_OPC_MULHHA_SU_SN,
  TILE_OPC_MULHHA_UU,
  TILE_OPC_MULHHA_UU_SN,
  TILE_OPC_MULHHSA_UU,
  TILE_OPC_MULHHSA_UU_SN,
  TILE_OPC_MULHL_SS,
  TILE_OPC_MULHL_SS_SN,
  TILE_OPC_MULHL_SU,
  TILE_OPC_MULHL_SU_SN,
  TILE_OPC_MULHL_US,
  TILE_OPC_MULHL_US_SN,
  TILE_OPC_MULHL_UU,
  TILE_OPC_MULHL_UU_SN,
  TILE_OPC_MULHLA_SS,
  TILE_OPC_MULHLA_SS_SN,
  TILE_OPC_MULHLA_SU,
  TILE_OPC_MULHLA_SU_SN,
  TILE_OPC_MULHLA_US,
  TILE_OPC_MULHLA_US_SN,
  TILE_OPC_MULHLA_UU,
  TILE_OPC_MULHLA_UU_SN,
  TILE_OPC_MULHLSA_UU,
  TILE_OPC_MULHLSA_UU_SN,
  TILE_OPC_MULLL_SS,
  TILE_OPC_MULLL_SS_SN,
  TILE_OPC_MULLL_SU,
  TILE_OPC_MULLL_SU_SN,
  TILE_OPC_MULLL_UU,
  TILE_OPC_MULLL_UU_SN,
  TILE_OPC_MULLLA_SS,
  TILE_OPC_MULLLA_SS_SN,
  TILE_OPC_MULLLA_SU,
  TILE_OPC_MULLLA_SU_SN,
  TILE_OPC_MULLLA_UU,
  TILE_OPC_MULLLA_UU_SN,
  TILE_OPC_MULLLSA_UU,
  TILE_OPC_MULLLSA_UU_SN,
  TILE_OPC_MVNZ,
  TILE_OPC_MVNZ_SN,
  TILE_OPC_MVZ,
  TILE_OPC_MVZ_SN,
  TILE_OPC_MZ,
  TILE_OPC_MZ_SN,
  TILE_OPC_MZB,
  TILE_OPC_MZB_SN,
  TILE_OPC_MZH,
  TILE_OPC_MZH_SN,
  TILE_OPC_NAP,
  TILE_OPC_NOP,
  TILE_OPC_NOR,
  TILE_OPC_NOR_SN,
  TILE_OPC_OR,
  TILE_OPC_OR_SN,
  TILE_OPC_ORI,
  TILE_OPC_ORI_SN,
  TILE_OPC_PACKBS_U,
  TILE_OPC_PACKBS_U_SN,
  TILE_OPC_PACKHB,
  TILE_OPC_PACKHB_SN,
  TILE_OPC_PACKHS,
  TILE_OPC_PACKHS_SN,
  TILE_OPC_PACKLB,
  TILE_OPC_PACKLB_SN,
  TILE_OPC_PCNT,
  TILE_OPC_PCNT_SN,
  TILE_OPC_RL,
  TILE_OPC_RL_SN,
  TILE_OPC_RLI,
  TILE_OPC_RLI_SN,
  TILE_OPC_S1A,
  TILE_OPC_S1A_SN,
  TILE_OPC_S2A,
  TILE_OPC_S2A_SN,
  TILE_OPC_S3A,
  TILE_OPC_S3A_SN,
  TILE_OPC_SADAB_U,
  TILE_OPC_SADAB_U_SN,
  TILE_OPC_SADAH,
  TILE_OPC_SADAH_SN,
  TILE_OPC_SADAH_U,
  TILE_OPC_SADAH_U_SN,
  TILE_OPC_SADB_U,
  TILE_OPC_SADB_U_SN,
  TILE_OPC_SADH,
  TILE_OPC_SADH_SN,
  TILE_OPC_SADH_U,
  TILE_OPC_SADH_U_SN,
  TILE_OPC_SB,
  TILE_OPC_SBADD,
  TILE_OPC_SEQ,
  TILE_OPC_SEQ_SN,
  TILE_OPC_SEQB,
  TILE_OPC_SEQB_SN,
  TILE_OPC_SEQH,
  TILE_OPC_SEQH_SN,
  TILE_OPC_SEQI,
  TILE_OPC_SEQI_SN,
  TILE_OPC_SEQIB,
  TILE_OPC_SEQIB_SN,
  TILE_OPC_SEQIH,
  TILE_OPC_SEQIH_SN,
  TILE_OPC_SH,
  TILE_OPC_SHADD,
  TILE_OPC_SHL,
  TILE_OPC_SHL_SN,
  TILE_OPC_SHLB,
  TILE_OPC_SHLB_SN,
  TILE_OPC_SHLH,
  TILE_OPC_SHLH_SN,
  TILE_OPC_SHLI,
  TILE_OPC_SHLI_SN,
  TILE_OPC_SHLIB,
  TILE_OPC_SHLIB_SN,
  TILE_OPC_SHLIH,
  TILE_OPC_SHLIH_SN,
  TILE_OPC_SHR,
  TILE_OPC_SHR_SN,
  TILE_OPC_SHRB,
  TILE_OPC_SHRB_SN,
  TILE_OPC_SHRH,
  TILE_OPC_SHRH_SN,
  TILE_OPC_SHRI,
  TILE_OPC_SHRI_SN,
  TILE_OPC_SHRIB,
  TILE_OPC_SHRIB_SN,
  TILE_OPC_SHRIH,
  TILE_OPC_SHRIH_SN,
  TILE_OPC_SLT,
  TILE_OPC_SLT_SN,
  TILE_OPC_SLT_U,
  TILE_OPC_SLT_U_SN,
  TILE_OPC_SLTB,
  TILE_OPC_SLTB_SN,
  TILE_OPC_SLTB_U,
  TILE_OPC_SLTB_U_SN,
  TILE_OPC_SLTE,
  TILE_OPC_SLTE_SN,
  TILE_OPC_SLTE_U,
  TILE_OPC_SLTE_U_SN,
  TILE_OPC_SLTEB,
  TILE_OPC_SLTEB_SN,
  TILE_OPC_SLTEB_U,
  TILE_OPC_SLTEB_U_SN,
  TILE_OPC_SLTEH,
  TILE_OPC_SLTEH_SN,
  TILE_OPC_SLTEH_U,
  TILE_OPC_SLTEH_U_SN,
  TILE_OPC_SLTH,
  TILE_OPC_SLTH_SN,
  TILE_OPC_SLTH_U,
  TILE_OPC_SLTH_U_SN,
  TILE_OPC_SLTI,
  TILE_OPC_SLTI_SN,
  TILE_OPC_SLTI_U,
  TILE_OPC_SLTI_U_SN,
  TILE_OPC_SLTIB,
  TILE_OPC_SLTIB_SN,
  TILE_OPC_SLTIB_U,
  TILE_OPC_SLTIB_U_SN,
  TILE_OPC_SLTIH,
  TILE_OPC_SLTIH_SN,
  TILE_OPC_SLTIH_U,
  TILE_OPC_SLTIH_U_SN,
  TILE_OPC_SNE,
  TILE_OPC_SNE_SN,
  TILE_OPC_SNEB,
  TILE_OPC_SNEB_SN,
  TILE_OPC_SNEH,
  TILE_OPC_SNEH_SN,
  TILE_OPC_SRA,
  TILE_OPC_SRA_SN,
  TILE_OPC_SRAB,
  TILE_OPC_SRAB_SN,
  TILE_OPC_SRAH,
  TILE_OPC_SRAH_SN,
  TILE_OPC_SRAI,
  TILE_OPC_SRAI_SN,
  TILE_OPC_SRAIB,
  TILE_OPC_SRAIB_SN,
  TILE_OPC_SRAIH,
  TILE_OPC_SRAIH_SN,
  TILE_OPC_SUB,
  TILE_OPC_SUB_SN,
  TILE_OPC_SUBB,
  TILE_OPC_SUBB_SN,
  TILE_OPC_SUBBS_U,
  TILE_OPC_SUBBS_U_SN,
  TILE_OPC_SUBH,
  TILE_OPC_SUBH_SN,
  TILE_OPC_SUBHS,
  TILE_OPC_SUBHS_SN,
  TILE_OPC_SUBS,
  TILE_OPC_SUBS_SN,
  TILE_OPC_SW,
  TILE_OPC_SWADD,
  TILE_OPC_SWINT0,
  TILE_OPC_SWINT1,
  TILE_OPC_SWINT2,
  TILE_OPC_SWINT3,
  TILE_OPC_TBLIDXB0,
  TILE_OPC_TBLIDXB0_SN,
  TILE_OPC_TBLIDXB1,
  TILE_OPC_TBLIDXB1_SN,
  TILE_OPC_TBLIDXB2,
  TILE_OPC_TBLIDXB2_SN,
  TILE_OPC_TBLIDXB3,
  TILE_OPC_TBLIDXB3_SN,
  TILE_OPC_TNS,
  TILE_OPC_TNS_SN,
  TILE_OPC_WH64,
  TILE_OPC_XOR,
  TILE_OPC_XOR_SN,
  TILE_OPC_XORI,
  TILE_OPC_XORI_SN,
  TILE_OPC_NONE
} tile_mnemonic;

/* 64-bit pattern for a { bpt ; nop } bundle. */
#define TILE_BPT_BUNDLE 0x400b3cae70166000ULL


#define TILE_ELF_MACHINE_CODE EM_TILEPRO

#define TILE_ELF_NAME "elf32-tilepro"


static __inline unsigned int
get_BrOff_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3ff);
}

static __inline unsigned int
get_BrOff_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000);
}

static __inline unsigned int
get_BrType_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0xf);
}

static __inline unsigned int
get_Dest_Imm8_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 43)) & 0x000000c0);
}

static __inline unsigned int
get_Dest_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 2)) & 0x3);
}

static __inline unsigned int
get_Dest_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Imm16_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xffff);
}

static __inline unsigned int
get_Imm16_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xffff);
}

static __inline unsigned int
get_Imm8_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0xff);
}

static __inline unsigned int
get_Imm8_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_ImmOpcodeExtension_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0x7f);
}

static __inline unsigned int
get_ImmOpcodeExtension_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 51)) & 0x7f);
}

static __inline unsigned int
get_ImmRROpcodeExtension_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 8)) & 0x3);
}

static __inline unsigned int
get_JOffLong_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000) |
         (((unsigned int)(n >> 14)) & 0x001e0000) |
         (((unsigned int)(n >> 16)) & 0x07e00000) |
         (((unsigned int)(n >> 31)) & 0x18000000);
}

static __inline unsigned int
get_JOff_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000) |
         (((unsigned int)(n >> 14)) & 0x001e0000) |
         (((unsigned int)(n >> 16)) & 0x07e00000) |
         (((unsigned int)(n >> 31)) & 0x08000000);
}

static __inline unsigned int
get_MF_Imm15_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x00003fff) |
         (((unsigned int)(n >> 44)) & 0x00004000);
}

static __inline unsigned int
get_MMEnd_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x1f);
}

static __inline unsigned int
get_MMEnd_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x1f);
}

static __inline unsigned int
get_MMStart_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 23)) & 0x1f);
}

static __inline unsigned int
get_MMStart_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 54)) & 0x1f);
}

static __inline unsigned int
get_MT_Imm15_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 37)) & 0x00003fc0) |
         (((unsigned int)(n >> 44)) & 0x00004000);
}

static __inline unsigned int
get_Mode(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 63)) & 0x1);
}

static __inline unsigned int
get_NoRegOpcodeExtension_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0xf);
}

static __inline unsigned int
get_Opcode_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 10)) & 0x3f);
}

static __inline unsigned int
get_Opcode_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 28)) & 0x7);
}

static __inline unsigned int
get_Opcode_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 59)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 27)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 59)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y2(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 56)) & 0x7);
}

static __inline unsigned int
get_RROpcodeExtension_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 4)) & 0xf);
}

static __inline unsigned int
get_RRROpcodeExtension_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x1ff);
}

static __inline unsigned int
get_RRROpcodeExtension_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x1ff);
}

static __inline unsigned int
get_RRROpcodeExtension_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3);
}

static __inline unsigned int
get_RRROpcodeExtension_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3);
}

static __inline unsigned int
get_RouteOpcodeExtension_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3ff);
}

static __inline unsigned int
get_S_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 27)) & 0x1);
}

static __inline unsigned int
get_S_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 58)) & 0x1);
}

static __inline unsigned int
get_ShAmt_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_SrcA_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y2(tile_bundle_bits n)
{
  return (((n >> 26)) & 0x00000001) |
         (((unsigned int)(n >> 50)) & 0x0000003e);
}

static __inline unsigned int
get_SrcBDest_Y2(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_Src_SN(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3);
}

static __inline unsigned int
get_UnOpcodeExtension_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_UnShOpcodeExtension_X0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 17)) & 0x3ff);
}

static __inline unsigned int
get_UnShOpcodeExtension_X1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 48)) & 0x3ff);
}

static __inline unsigned int
get_UnShOpcodeExtension_Y0(tile_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 17)) & 0x7);
}

static __inline unsigned int
get_UnShOpcodeExtension_Y1(tile_bundle_bits n)
{
  return (((unsigned int)(n >> 48)) & 0x7);
}


static __inline int
sign_extend(int n, int num_bits)
{
  int shift = (int)(sizeof(int) * 8 - num_bits);
  return (n << shift) >> shift;
}



static __inline tile_bundle_bits
create_BrOff_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 0);
}

static __inline tile_bundle_bits
create_BrOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tile_bundle_bits)(n & 0x00018000)) << 20);
}

static __inline tile_bundle_bits
create_BrType_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xf)) << 31);
}

static __inline tile_bundle_bits
create_Dest_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tile_bundle_bits)(n & 0x000000c0)) << 43);
}

static __inline tile_bundle_bits
create_Dest_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 2);
}

static __inline tile_bundle_bits
create_Dest_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tile_bundle_bits
create_Dest_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tile_bundle_bits
create_Dest_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tile_bundle_bits
create_Dest_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tile_bundle_bits
create_Imm16_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xffff) << 12);
}

static __inline tile_bundle_bits
create_Imm16_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xffff)) << 43);
}

static __inline tile_bundle_bits
create_Imm8_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 0);
}

static __inline tile_bundle_bits
create_Imm8_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tile_bundle_bits
create_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xff)) << 43);
}

static __inline tile_bundle_bits
create_Imm8_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tile_bundle_bits
create_Imm8_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xff)) << 43);
}

static __inline tile_bundle_bits
create_ImmOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7f) << 20);
}

static __inline tile_bundle_bits
create_ImmOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x7f)) << 51);
}

static __inline tile_bundle_bits
create_ImmRROpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 8);
}

static __inline tile_bundle_bits
create_JOffLong_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tile_bundle_bits)(n & 0x00018000)) << 20) |
         (((tile_bundle_bits)(n & 0x001e0000)) << 14) |
         (((tile_bundle_bits)(n & 0x07e00000)) << 16) |
         (((tile_bundle_bits)(n & 0x18000000)) << 31);
}

static __inline tile_bundle_bits
create_JOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tile_bundle_bits)(n & 0x00018000)) << 20) |
         (((tile_bundle_bits)(n & 0x001e0000)) << 14) |
         (((tile_bundle_bits)(n & 0x07e00000)) << 16) |
         (((tile_bundle_bits)(n & 0x08000000)) << 31);
}

static __inline tile_bundle_bits
create_MF_Imm15_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x00003fff)) << 37) |
         (((tile_bundle_bits)(n & 0x00004000)) << 44);
}

static __inline tile_bundle_bits
create_MMEnd_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 18);
}

static __inline tile_bundle_bits
create_MMEnd_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 49);
}

static __inline tile_bundle_bits
create_MMStart_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 23);
}

static __inline tile_bundle_bits
create_MMStart_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 54);
}

static __inline tile_bundle_bits
create_MT_Imm15_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tile_bundle_bits)(n & 0x00003fc0)) << 37) |
         (((tile_bundle_bits)(n & 0x00004000)) << 44);
}

static __inline tile_bundle_bits
create_Mode(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1)) << 63);
}

static __inline tile_bundle_bits
create_NoRegOpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 0);
}

static __inline tile_bundle_bits
create_Opcode_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 10);
}

static __inline tile_bundle_bits
create_Opcode_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7) << 28);
}

static __inline tile_bundle_bits
create_Opcode_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xf)) << 59);
}

static __inline tile_bundle_bits
create_Opcode_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 27);
}

static __inline tile_bundle_bits
create_Opcode_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0xf)) << 59);
}

static __inline tile_bundle_bits
create_Opcode_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x7)) << 56);
}

static __inline tile_bundle_bits
create_RROpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 4);
}

static __inline tile_bundle_bits
create_RRROpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1ff) << 18);
}

static __inline tile_bundle_bits
create_RRROpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1ff)) << 49);
}

static __inline tile_bundle_bits
create_RRROpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 18);
}

static __inline tile_bundle_bits
create_RRROpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3)) << 49);
}

static __inline tile_bundle_bits
create_RouteOpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 0);
}

static __inline tile_bundle_bits
create_S_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1) << 27);
}

static __inline tile_bundle_bits
create_S_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1)) << 58);
}

static __inline tile_bundle_bits
create_ShAmt_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tile_bundle_bits
create_ShAmt_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tile_bundle_bits
create_ShAmt_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tile_bundle_bits
create_ShAmt_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tile_bundle_bits
create_SrcA_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tile_bundle_bits
create_SrcA_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tile_bundle_bits
create_SrcA_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tile_bundle_bits
create_SrcA_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tile_bundle_bits
create_SrcA_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x00000001) << 26) |
         (((tile_bundle_bits)(n & 0x0000003e)) << 50);
}

static __inline tile_bundle_bits
create_SrcBDest_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 20);
}

static __inline tile_bundle_bits
create_SrcB_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tile_bundle_bits
create_SrcB_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tile_bundle_bits
create_SrcB_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tile_bundle_bits
create_SrcB_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tile_bundle_bits
create_Src_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 0);
}

static __inline tile_bundle_bits
create_UnOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tile_bundle_bits
create_UnOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tile_bundle_bits
create_UnOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tile_bundle_bits
create_UnOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tile_bundle_bits
create_UnShOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 17);
}

static __inline tile_bundle_bits
create_UnShOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x3ff)) << 48);
}

static __inline tile_bundle_bits
create_UnShOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7) << 17);
}

static __inline tile_bundle_bits
create_UnShOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tile_bundle_bits)(n & 0x7)) << 48);
}



typedef enum
{
  TILE_PIPELINE_X0,
  TILE_PIPELINE_X1,
  TILE_PIPELINE_Y0,
  TILE_PIPELINE_Y1,
  TILE_PIPELINE_Y2,
} tile_pipeline;

#define tile_is_x_pipeline(p) ((int)(p) <= (int)TILE_PIPELINE_X1)

typedef enum
{
  TILE_OP_TYPE_REGISTER,
  TILE_OP_TYPE_IMMEDIATE,
  TILE_OP_TYPE_ADDRESS,
  TILE_OP_TYPE_SPR
} tile_operand_type;

/* This is the bit that determines if a bundle is in the Y encoding. */
#define TILE_BUNDLE_Y_ENCODING_MASK ((tile_bundle_bits)1 << 63)

enum
{
  /* Maximum number of instructions in a bundle (2 for X, 3 for Y). */
  TILE_MAX_INSTRUCTIONS_PER_BUNDLE = 3,

  /* How many different pipeline encodings are there? X0, X1, Y0, Y1, Y2. */
  TILE_NUM_PIPELINE_ENCODINGS = 5,

  /* Log base 2 of TILE_BUNDLE_SIZE_IN_BYTES. */
  TILE_LOG2_BUNDLE_SIZE_IN_BYTES = 3,

  /* Instructions take this many bytes. */
  TILE_BUNDLE_SIZE_IN_BYTES = 1 << TILE_LOG2_BUNDLE_SIZE_IN_BYTES,

  /* Log base 2 of TILE_BUNDLE_ALIGNMENT_IN_BYTES. */
  TILE_LOG2_BUNDLE_ALIGNMENT_IN_BYTES = 3,

  /* Bundles should be aligned modulo this number of bytes. */
  TILE_BUNDLE_ALIGNMENT_IN_BYTES =
    (1 << TILE_LOG2_BUNDLE_ALIGNMENT_IN_BYTES),

  /* Log base 2 of TILE_SN_INSTRUCTION_SIZE_IN_BYTES. */
  TILE_LOG2_SN_INSTRUCTION_SIZE_IN_BYTES = 1,

  /* Static network instructions take this many bytes. */
  TILE_SN_INSTRUCTION_SIZE_IN_BYTES =
    (1 << TILE_LOG2_SN_INSTRUCTION_SIZE_IN_BYTES),

  /* Number of registers (some are magic, such as network I/O). */
  TILE_NUM_REGISTERS = 64,

  /* Number of static network registers. */
  TILE_NUM_SN_REGISTERS = 4
};


struct tile_operand
{
  /* Is this operand a register, immediate or address? */
  tile_operand_type type;

  /* The default relocation type for this operand.  */
  signed int default_reloc : 16;

  /* How many bits is this value? (used for range checking) */
  unsigned int num_bits : 5;

  /* Is the value signed? (used for range checking) */
  unsigned int is_signed : 1;

  /* Is this operand a source register? */
  unsigned int is_src_reg : 1;

  /* Is this operand written? (i.e. is it a destination register) */
  unsigned int is_dest_reg : 1;

  /* Is this operand PC-relative? */
  unsigned int is_pc_relative : 1;

  /* By how many bits do we right shift the value before inserting? */
  unsigned int rightshift : 2;

  /* Return the bits for this operand to be ORed into an existing bundle. */
  tile_bundle_bits (*insert) (int op);

  /* Extract this operand and return it. */
  unsigned int (*extract) (tile_bundle_bits bundle);
};


extern const struct tile_operand tile_operands[];

/* One finite-state machine per pipe for rapid instruction decoding. */
extern const unsigned short * const
tile_bundle_decoder_fsms[TILE_NUM_PIPELINE_ENCODINGS];


struct tile_opcode
{
  /* The opcode mnemonic, e.g. "add" */
  const char *name;

  /* The enum value for this mnemonic. */
  tile_mnemonic mnemonic;

  /* A bit mask of which of the five pipes this instruction
     is compatible with:
     X0  0x01
     X1  0x02
     Y0  0x04
     Y1  0x08
     Y2  0x10 */
  unsigned char pipes;

  /* How many operands are there? */
  unsigned char num_operands;

  /* Which register does this write implicitly, or TREG_ZERO if none? */
  unsigned char implicitly_written_register;

  /* Can this be bundled with other instructions (almost always true). */
  unsigned char can_bundle;

  /* The description of the operands. Each of these is an
   * index into the tile_operands[] table. */
  unsigned char operands[TILE_NUM_PIPELINE_ENCODINGS][TILE_MAX_OPERANDS];

};

extern const struct tile_opcode tile_opcodes[];


/* Used for non-textual disassembly into structs. */
struct tile_decoded_instruction
{
  const struct tile_opcode *opcode;
  const struct tile_operand *operands[TILE_MAX_OPERANDS];
  int operand_values[TILE_MAX_OPERANDS];
};


/* Disassemble a bundle into a struct for machine processing. */
extern int parse_insn_tile(tile_bundle_bits bits,
                           unsigned int pc,
                           struct tile_decoded_instruction
                           decoded[TILE_MAX_INSTRUCTIONS_PER_BUNDLE]);


/* Given a set of bundle bits and a specific pipe, returns which
 * instruction the bundle contains in that pipe.
 */
extern const struct tile_opcode *
find_opcode(tile_bundle_bits bits, tile_pipeline pipe);



#endif /* opcode_tile_h */

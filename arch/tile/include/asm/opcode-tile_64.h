/* tile.h -- Header file for TILE opcode table
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Tilera Corp. */

#ifndef opcode_tile_h
#define opcode_tile_h

typedef unsigned long long tilegx_bundle_bits;


enum
{
  TILEGX_MAX_OPERANDS = 4 /* bfexts */
};

typedef enum
{
  TILEGX_OPC_BPT,
  TILEGX_OPC_INFO,
  TILEGX_OPC_INFOL,
  TILEGX_OPC_MOVE,
  TILEGX_OPC_MOVEI,
  TILEGX_OPC_MOVELI,
  TILEGX_OPC_PREFETCH,
  TILEGX_OPC_PREFETCH_ADD_L1,
  TILEGX_OPC_PREFETCH_ADD_L1_FAULT,
  TILEGX_OPC_PREFETCH_ADD_L2,
  TILEGX_OPC_PREFETCH_ADD_L2_FAULT,
  TILEGX_OPC_PREFETCH_ADD_L3,
  TILEGX_OPC_PREFETCH_ADD_L3_FAULT,
  TILEGX_OPC_PREFETCH_L1,
  TILEGX_OPC_PREFETCH_L1_FAULT,
  TILEGX_OPC_PREFETCH_L2,
  TILEGX_OPC_PREFETCH_L2_FAULT,
  TILEGX_OPC_PREFETCH_L3,
  TILEGX_OPC_PREFETCH_L3_FAULT,
  TILEGX_OPC_RAISE,
  TILEGX_OPC_ADD,
  TILEGX_OPC_ADDI,
  TILEGX_OPC_ADDLI,
  TILEGX_OPC_ADDX,
  TILEGX_OPC_ADDXI,
  TILEGX_OPC_ADDXLI,
  TILEGX_OPC_ADDXSC,
  TILEGX_OPC_AND,
  TILEGX_OPC_ANDI,
  TILEGX_OPC_BEQZ,
  TILEGX_OPC_BEQZT,
  TILEGX_OPC_BFEXTS,
  TILEGX_OPC_BFEXTU,
  TILEGX_OPC_BFINS,
  TILEGX_OPC_BGEZ,
  TILEGX_OPC_BGEZT,
  TILEGX_OPC_BGTZ,
  TILEGX_OPC_BGTZT,
  TILEGX_OPC_BLBC,
  TILEGX_OPC_BLBCT,
  TILEGX_OPC_BLBS,
  TILEGX_OPC_BLBST,
  TILEGX_OPC_BLEZ,
  TILEGX_OPC_BLEZT,
  TILEGX_OPC_BLTZ,
  TILEGX_OPC_BLTZT,
  TILEGX_OPC_BNEZ,
  TILEGX_OPC_BNEZT,
  TILEGX_OPC_CLZ,
  TILEGX_OPC_CMOVEQZ,
  TILEGX_OPC_CMOVNEZ,
  TILEGX_OPC_CMPEQ,
  TILEGX_OPC_CMPEQI,
  TILEGX_OPC_CMPEXCH,
  TILEGX_OPC_CMPEXCH4,
  TILEGX_OPC_CMPLES,
  TILEGX_OPC_CMPLEU,
  TILEGX_OPC_CMPLTS,
  TILEGX_OPC_CMPLTSI,
  TILEGX_OPC_CMPLTU,
  TILEGX_OPC_CMPLTUI,
  TILEGX_OPC_CMPNE,
  TILEGX_OPC_CMUL,
  TILEGX_OPC_CMULA,
  TILEGX_OPC_CMULAF,
  TILEGX_OPC_CMULF,
  TILEGX_OPC_CMULFR,
  TILEGX_OPC_CMULH,
  TILEGX_OPC_CMULHR,
  TILEGX_OPC_CRC32_32,
  TILEGX_OPC_CRC32_8,
  TILEGX_OPC_CTZ,
  TILEGX_OPC_DBLALIGN,
  TILEGX_OPC_DBLALIGN2,
  TILEGX_OPC_DBLALIGN4,
  TILEGX_OPC_DBLALIGN6,
  TILEGX_OPC_DRAIN,
  TILEGX_OPC_DTLBPR,
  TILEGX_OPC_EXCH,
  TILEGX_OPC_EXCH4,
  TILEGX_OPC_FDOUBLE_ADD_FLAGS,
  TILEGX_OPC_FDOUBLE_ADDSUB,
  TILEGX_OPC_FDOUBLE_MUL_FLAGS,
  TILEGX_OPC_FDOUBLE_PACK1,
  TILEGX_OPC_FDOUBLE_PACK2,
  TILEGX_OPC_FDOUBLE_SUB_FLAGS,
  TILEGX_OPC_FDOUBLE_UNPACK_MAX,
  TILEGX_OPC_FDOUBLE_UNPACK_MIN,
  TILEGX_OPC_FETCHADD,
  TILEGX_OPC_FETCHADD4,
  TILEGX_OPC_FETCHADDGEZ,
  TILEGX_OPC_FETCHADDGEZ4,
  TILEGX_OPC_FETCHAND,
  TILEGX_OPC_FETCHAND4,
  TILEGX_OPC_FETCHOR,
  TILEGX_OPC_FETCHOR4,
  TILEGX_OPC_FINV,
  TILEGX_OPC_FLUSH,
  TILEGX_OPC_FLUSHWB,
  TILEGX_OPC_FNOP,
  TILEGX_OPC_FSINGLE_ADD1,
  TILEGX_OPC_FSINGLE_ADDSUB2,
  TILEGX_OPC_FSINGLE_MUL1,
  TILEGX_OPC_FSINGLE_MUL2,
  TILEGX_OPC_FSINGLE_PACK1,
  TILEGX_OPC_FSINGLE_PACK2,
  TILEGX_OPC_FSINGLE_SUB1,
  TILEGX_OPC_ICOH,
  TILEGX_OPC_ILL,
  TILEGX_OPC_INV,
  TILEGX_OPC_IRET,
  TILEGX_OPC_J,
  TILEGX_OPC_JAL,
  TILEGX_OPC_JALR,
  TILEGX_OPC_JALRP,
  TILEGX_OPC_JR,
  TILEGX_OPC_JRP,
  TILEGX_OPC_LD,
  TILEGX_OPC_LD1S,
  TILEGX_OPC_LD1S_ADD,
  TILEGX_OPC_LD1U,
  TILEGX_OPC_LD1U_ADD,
  TILEGX_OPC_LD2S,
  TILEGX_OPC_LD2S_ADD,
  TILEGX_OPC_LD2U,
  TILEGX_OPC_LD2U_ADD,
  TILEGX_OPC_LD4S,
  TILEGX_OPC_LD4S_ADD,
  TILEGX_OPC_LD4U,
  TILEGX_OPC_LD4U_ADD,
  TILEGX_OPC_LD_ADD,
  TILEGX_OPC_LDNA,
  TILEGX_OPC_LDNA_ADD,
  TILEGX_OPC_LDNT,
  TILEGX_OPC_LDNT1S,
  TILEGX_OPC_LDNT1S_ADD,
  TILEGX_OPC_LDNT1U,
  TILEGX_OPC_LDNT1U_ADD,
  TILEGX_OPC_LDNT2S,
  TILEGX_OPC_LDNT2S_ADD,
  TILEGX_OPC_LDNT2U,
  TILEGX_OPC_LDNT2U_ADD,
  TILEGX_OPC_LDNT4S,
  TILEGX_OPC_LDNT4S_ADD,
  TILEGX_OPC_LDNT4U,
  TILEGX_OPC_LDNT4U_ADD,
  TILEGX_OPC_LDNT_ADD,
  TILEGX_OPC_LNK,
  TILEGX_OPC_MF,
  TILEGX_OPC_MFSPR,
  TILEGX_OPC_MM,
  TILEGX_OPC_MNZ,
  TILEGX_OPC_MTSPR,
  TILEGX_OPC_MUL_HS_HS,
  TILEGX_OPC_MUL_HS_HU,
  TILEGX_OPC_MUL_HS_LS,
  TILEGX_OPC_MUL_HS_LU,
  TILEGX_OPC_MUL_HU_HU,
  TILEGX_OPC_MUL_HU_LS,
  TILEGX_OPC_MUL_HU_LU,
  TILEGX_OPC_MUL_LS_LS,
  TILEGX_OPC_MUL_LS_LU,
  TILEGX_OPC_MUL_LU_LU,
  TILEGX_OPC_MULA_HS_HS,
  TILEGX_OPC_MULA_HS_HU,
  TILEGX_OPC_MULA_HS_LS,
  TILEGX_OPC_MULA_HS_LU,
  TILEGX_OPC_MULA_HU_HU,
  TILEGX_OPC_MULA_HU_LS,
  TILEGX_OPC_MULA_HU_LU,
  TILEGX_OPC_MULA_LS_LS,
  TILEGX_OPC_MULA_LS_LU,
  TILEGX_OPC_MULA_LU_LU,
  TILEGX_OPC_MULAX,
  TILEGX_OPC_MULX,
  TILEGX_OPC_MZ,
  TILEGX_OPC_NAP,
  TILEGX_OPC_NOP,
  TILEGX_OPC_NOR,
  TILEGX_OPC_OR,
  TILEGX_OPC_ORI,
  TILEGX_OPC_PCNT,
  TILEGX_OPC_REVBITS,
  TILEGX_OPC_REVBYTES,
  TILEGX_OPC_ROTL,
  TILEGX_OPC_ROTLI,
  TILEGX_OPC_SHL,
  TILEGX_OPC_SHL16INSLI,
  TILEGX_OPC_SHL1ADD,
  TILEGX_OPC_SHL1ADDX,
  TILEGX_OPC_SHL2ADD,
  TILEGX_OPC_SHL2ADDX,
  TILEGX_OPC_SHL3ADD,
  TILEGX_OPC_SHL3ADDX,
  TILEGX_OPC_SHLI,
  TILEGX_OPC_SHLX,
  TILEGX_OPC_SHLXI,
  TILEGX_OPC_SHRS,
  TILEGX_OPC_SHRSI,
  TILEGX_OPC_SHRU,
  TILEGX_OPC_SHRUI,
  TILEGX_OPC_SHRUX,
  TILEGX_OPC_SHRUXI,
  TILEGX_OPC_SHUFFLEBYTES,
  TILEGX_OPC_ST,
  TILEGX_OPC_ST1,
  TILEGX_OPC_ST1_ADD,
  TILEGX_OPC_ST2,
  TILEGX_OPC_ST2_ADD,
  TILEGX_OPC_ST4,
  TILEGX_OPC_ST4_ADD,
  TILEGX_OPC_ST_ADD,
  TILEGX_OPC_STNT,
  TILEGX_OPC_STNT1,
  TILEGX_OPC_STNT1_ADD,
  TILEGX_OPC_STNT2,
  TILEGX_OPC_STNT2_ADD,
  TILEGX_OPC_STNT4,
  TILEGX_OPC_STNT4_ADD,
  TILEGX_OPC_STNT_ADD,
  TILEGX_OPC_SUB,
  TILEGX_OPC_SUBX,
  TILEGX_OPC_SUBXSC,
  TILEGX_OPC_SWINT0,
  TILEGX_OPC_SWINT1,
  TILEGX_OPC_SWINT2,
  TILEGX_OPC_SWINT3,
  TILEGX_OPC_TBLIDXB0,
  TILEGX_OPC_TBLIDXB1,
  TILEGX_OPC_TBLIDXB2,
  TILEGX_OPC_TBLIDXB3,
  TILEGX_OPC_V1ADD,
  TILEGX_OPC_V1ADDI,
  TILEGX_OPC_V1ADDUC,
  TILEGX_OPC_V1ADIFFU,
  TILEGX_OPC_V1AVGU,
  TILEGX_OPC_V1CMPEQ,
  TILEGX_OPC_V1CMPEQI,
  TILEGX_OPC_V1CMPLES,
  TILEGX_OPC_V1CMPLEU,
  TILEGX_OPC_V1CMPLTS,
  TILEGX_OPC_V1CMPLTSI,
  TILEGX_OPC_V1CMPLTU,
  TILEGX_OPC_V1CMPLTUI,
  TILEGX_OPC_V1CMPNE,
  TILEGX_OPC_V1DDOTPU,
  TILEGX_OPC_V1DDOTPUA,
  TILEGX_OPC_V1DDOTPUS,
  TILEGX_OPC_V1DDOTPUSA,
  TILEGX_OPC_V1DOTP,
  TILEGX_OPC_V1DOTPA,
  TILEGX_OPC_V1DOTPU,
  TILEGX_OPC_V1DOTPUA,
  TILEGX_OPC_V1DOTPUS,
  TILEGX_OPC_V1DOTPUSA,
  TILEGX_OPC_V1INT_H,
  TILEGX_OPC_V1INT_L,
  TILEGX_OPC_V1MAXU,
  TILEGX_OPC_V1MAXUI,
  TILEGX_OPC_V1MINU,
  TILEGX_OPC_V1MINUI,
  TILEGX_OPC_V1MNZ,
  TILEGX_OPC_V1MULTU,
  TILEGX_OPC_V1MULU,
  TILEGX_OPC_V1MULUS,
  TILEGX_OPC_V1MZ,
  TILEGX_OPC_V1SADAU,
  TILEGX_OPC_V1SADU,
  TILEGX_OPC_V1SHL,
  TILEGX_OPC_V1SHLI,
  TILEGX_OPC_V1SHRS,
  TILEGX_OPC_V1SHRSI,
  TILEGX_OPC_V1SHRU,
  TILEGX_OPC_V1SHRUI,
  TILEGX_OPC_V1SUB,
  TILEGX_OPC_V1SUBUC,
  TILEGX_OPC_V2ADD,
  TILEGX_OPC_V2ADDI,
  TILEGX_OPC_V2ADDSC,
  TILEGX_OPC_V2ADIFFS,
  TILEGX_OPC_V2AVGS,
  TILEGX_OPC_V2CMPEQ,
  TILEGX_OPC_V2CMPEQI,
  TILEGX_OPC_V2CMPLES,
  TILEGX_OPC_V2CMPLEU,
  TILEGX_OPC_V2CMPLTS,
  TILEGX_OPC_V2CMPLTSI,
  TILEGX_OPC_V2CMPLTU,
  TILEGX_OPC_V2CMPLTUI,
  TILEGX_OPC_V2CMPNE,
  TILEGX_OPC_V2DOTP,
  TILEGX_OPC_V2DOTPA,
  TILEGX_OPC_V2INT_H,
  TILEGX_OPC_V2INT_L,
  TILEGX_OPC_V2MAXS,
  TILEGX_OPC_V2MAXSI,
  TILEGX_OPC_V2MINS,
  TILEGX_OPC_V2MINSI,
  TILEGX_OPC_V2MNZ,
  TILEGX_OPC_V2MULFSC,
  TILEGX_OPC_V2MULS,
  TILEGX_OPC_V2MULTS,
  TILEGX_OPC_V2MZ,
  TILEGX_OPC_V2PACKH,
  TILEGX_OPC_V2PACKL,
  TILEGX_OPC_V2PACKUC,
  TILEGX_OPC_V2SADAS,
  TILEGX_OPC_V2SADAU,
  TILEGX_OPC_V2SADS,
  TILEGX_OPC_V2SADU,
  TILEGX_OPC_V2SHL,
  TILEGX_OPC_V2SHLI,
  TILEGX_OPC_V2SHLSC,
  TILEGX_OPC_V2SHRS,
  TILEGX_OPC_V2SHRSI,
  TILEGX_OPC_V2SHRU,
  TILEGX_OPC_V2SHRUI,
  TILEGX_OPC_V2SUB,
  TILEGX_OPC_V2SUBSC,
  TILEGX_OPC_V4ADD,
  TILEGX_OPC_V4ADDSC,
  TILEGX_OPC_V4INT_H,
  TILEGX_OPC_V4INT_L,
  TILEGX_OPC_V4PACKSC,
  TILEGX_OPC_V4SHL,
  TILEGX_OPC_V4SHLSC,
  TILEGX_OPC_V4SHRS,
  TILEGX_OPC_V4SHRU,
  TILEGX_OPC_V4SUB,
  TILEGX_OPC_V4SUBSC,
  TILEGX_OPC_WH64,
  TILEGX_OPC_XOR,
  TILEGX_OPC_XORI,
  TILEGX_OPC_NONE
} tilegx_mnemonic;

/* 64-bit pattern for a { bpt ; nop } bundle. */
#define TILEGX_BPT_BUNDLE 0x286a44ae51485000ULL


#define TILE_ELF_MACHINE_CODE EM_TILE64

#define TILE_ELF_NAME "elf32-tile64"


static __inline unsigned int
get_BFEnd_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_BFOpcodeExtension_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 24)) & 0xf);
}

static __inline unsigned int
get_BFStart_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3f);
}

static __inline unsigned int
get_BrOff_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 37)) & 0x0001ffc0);
}

static __inline unsigned int
get_BrType_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 54)) & 0x1f);
}

static __inline unsigned int
get_Dest_Imm8_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 43)) & 0x000000c0);
}

static __inline unsigned int
get_Dest_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Imm16_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xffff);
}

static __inline unsigned int
get_Imm16_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xffff);
}

static __inline unsigned int
get_Imm8OpcodeExtension_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0xff);
}

static __inline unsigned int
get_Imm8OpcodeExtension_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 51)) & 0xff);
}

static __inline unsigned int
get_Imm8_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_JumpOff_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x7ffffff);
}

static __inline unsigned int
get_JumpOpcodeExtension_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 58)) & 0x1);
}

static __inline unsigned int
get_MF_Imm14_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3fff);
}

static __inline unsigned int
get_MT_Imm14_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 37)) & 0x00003fc0);
}

static __inline unsigned int
get_Mode(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 62)) & 0x3);
}

static __inline unsigned int
get_Opcode_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 28)) & 0x7);
}

static __inline unsigned int
get_Opcode_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 59)) & 0x7);
}

static __inline unsigned int
get_Opcode_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 27)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 58)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y2(tilegx_bundle_bits n)
{
  return (((n >> 26)) & 0x00000001) |
         (((unsigned int)(n >> 56)) & 0x00000002);
}

static __inline unsigned int
get_RRROpcodeExtension_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3ff);
}

static __inline unsigned int
get_RRROpcodeExtension_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3ff);
}

static __inline unsigned int
get_RRROpcodeExtension_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3);
}

static __inline unsigned int
get_RRROpcodeExtension_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3);
}

static __inline unsigned int
get_ShAmt_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_ShAmt_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_ShAmt_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_ShAmt_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_ShiftOpcodeExtension_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3ff);
}

static __inline unsigned int
get_ShiftOpcodeExtension_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3ff);
}

static __inline unsigned int
get_ShiftOpcodeExtension_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3);
}

static __inline unsigned int
get_ShiftOpcodeExtension_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3);
}

static __inline unsigned int
get_SrcA_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y2(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0x3f);
}

static __inline unsigned int
get_SrcBDest_Y2(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 51)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_UnaryOpcodeExtension_X0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_UnaryOpcodeExtension_X1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_UnaryOpcodeExtension_Y0(tilegx_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_UnaryOpcodeExtension_Y1(tilegx_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}


static __inline int
sign_extend(int n, int num_bits)
{
  int shift = (int)(sizeof(int) * 8 - num_bits);
  return (n << shift) >> shift;
}



static __inline tilegx_bundle_bits
create_BFEnd_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_BFOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 24);
}

static __inline tilegx_bundle_bits
create_BFStart_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 18);
}

static __inline tilegx_bundle_bits
create_BrOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tilegx_bundle_bits)(n & 0x0001ffc0)) << 37);
}

static __inline tilegx_bundle_bits
create_BrType_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x1f)) << 54);
}

static __inline tilegx_bundle_bits
create_Dest_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tilegx_bundle_bits)(n & 0x000000c0)) << 43);
}

static __inline tilegx_bundle_bits
create_Dest_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tilegx_bundle_bits
create_Dest_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tilegx_bundle_bits
create_Dest_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tilegx_bundle_bits
create_Dest_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tilegx_bundle_bits
create_Imm16_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xffff) << 12);
}

static __inline tilegx_bundle_bits
create_Imm16_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0xffff)) << 43);
}

static __inline tilegx_bundle_bits
create_Imm8OpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 20);
}

static __inline tilegx_bundle_bits
create_Imm8OpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0xff)) << 51);
}

static __inline tilegx_bundle_bits
create_Imm8_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tilegx_bundle_bits
create_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0xff)) << 43);
}

static __inline tilegx_bundle_bits
create_Imm8_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tilegx_bundle_bits
create_Imm8_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0xff)) << 43);
}

static __inline tilegx_bundle_bits
create_JumpOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x7ffffff)) << 31);
}

static __inline tilegx_bundle_bits
create_JumpOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x1)) << 58);
}

static __inline tilegx_bundle_bits
create_MF_Imm14_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3fff)) << 37);
}

static __inline tilegx_bundle_bits
create_MT_Imm14_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tilegx_bundle_bits)(n & 0x00003fc0)) << 37);
}

static __inline tilegx_bundle_bits
create_Mode(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3)) << 62);
}

static __inline tilegx_bundle_bits
create_Opcode_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7) << 28);
}

static __inline tilegx_bundle_bits
create_Opcode_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x7)) << 59);
}

static __inline tilegx_bundle_bits
create_Opcode_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 27);
}

static __inline tilegx_bundle_bits
create_Opcode_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0xf)) << 58);
}

static __inline tilegx_bundle_bits
create_Opcode_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x00000001) << 26) |
         (((tilegx_bundle_bits)(n & 0x00000002)) << 56);
}

static __inline tilegx_bundle_bits
create_RRROpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 18);
}

static __inline tilegx_bundle_bits
create_RRROpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3ff)) << 49);
}

static __inline tilegx_bundle_bits
create_RRROpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 18);
}

static __inline tilegx_bundle_bits
create_RRROpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3)) << 49);
}

static __inline tilegx_bundle_bits
create_ShAmt_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_ShAmt_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilegx_bundle_bits
create_ShAmt_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_ShAmt_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilegx_bundle_bits
create_ShiftOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 18);
}

static __inline tilegx_bundle_bits
create_ShiftOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3ff)) << 49);
}

static __inline tilegx_bundle_bits
create_ShiftOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 18);
}

static __inline tilegx_bundle_bits
create_ShiftOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3)) << 49);
}

static __inline tilegx_bundle_bits
create_SrcA_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tilegx_bundle_bits
create_SrcA_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tilegx_bundle_bits
create_SrcA_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tilegx_bundle_bits
create_SrcA_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tilegx_bundle_bits
create_SrcA_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 20);
}

static __inline tilegx_bundle_bits
create_SrcBDest_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 51);
}

static __inline tilegx_bundle_bits
create_SrcB_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_SrcB_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilegx_bundle_bits
create_SrcB_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_SrcB_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilegx_bundle_bits
create_UnaryOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_UnaryOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilegx_bundle_bits
create_UnaryOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilegx_bundle_bits
create_UnaryOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilegx_bundle_bits)(n & 0x3f)) << 43);
}


typedef enum
{
  TILEGX_PIPELINE_X0,
  TILEGX_PIPELINE_X1,
  TILEGX_PIPELINE_Y0,
  TILEGX_PIPELINE_Y1,
  TILEGX_PIPELINE_Y2,
} tilegx_pipeline;

#define tilegx_is_x_pipeline(p) ((int)(p) <= (int)TILEGX_PIPELINE_X1)

typedef enum
{
  TILEGX_OP_TYPE_REGISTER,
  TILEGX_OP_TYPE_IMMEDIATE,
  TILEGX_OP_TYPE_ADDRESS,
  TILEGX_OP_TYPE_SPR
} tilegx_operand_type;

/* These are the bits that determine if a bundle is in the X encoding. */
#define TILEGX_BUNDLE_MODE_MASK ((tilegx_bundle_bits)3 << 62)

enum
{
  /* Maximum number of instructions in a bundle (2 for X, 3 for Y). */
  TILEGX_MAX_INSTRUCTIONS_PER_BUNDLE = 3,

  /* How many different pipeline encodings are there? X0, X1, Y0, Y1, Y2. */
  TILEGX_NUM_PIPELINE_ENCODINGS = 5,

  /* Log base 2 of TILEGX_BUNDLE_SIZE_IN_BYTES. */
  TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES = 3,

  /* Instructions take this many bytes. */
  TILEGX_BUNDLE_SIZE_IN_BYTES = 1 << TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES,

  /* Log base 2 of TILEGX_BUNDLE_ALIGNMENT_IN_BYTES. */
  TILEGX_LOG2_BUNDLE_ALIGNMENT_IN_BYTES = 3,

  /* Bundles should be aligned modulo this number of bytes. */
  TILEGX_BUNDLE_ALIGNMENT_IN_BYTES =
    (1 << TILEGX_LOG2_BUNDLE_ALIGNMENT_IN_BYTES),

  /* Number of registers (some are magic, such as network I/O). */
  TILEGX_NUM_REGISTERS = 64,
};


struct tilegx_operand
{
  /* Is this operand a register, immediate or address? */
  tilegx_operand_type type;

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
  tilegx_bundle_bits (*insert) (int op);

  /* Extract this operand and return it. */
  unsigned int (*extract) (tilegx_bundle_bits bundle);
};


extern const struct tilegx_operand tilegx_operands[];

/* One finite-state machine per pipe for rapid instruction decoding. */
extern const unsigned short * const
tilegx_bundle_decoder_fsms[TILEGX_NUM_PIPELINE_ENCODINGS];


struct tilegx_opcode
{
  /* The opcode mnemonic, e.g. "add" */
  const char *name;

  /* The enum value for this mnemonic. */
  tilegx_mnemonic mnemonic;

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
   * index into the tilegx_operands[] table. */
  unsigned char operands[TILEGX_NUM_PIPELINE_ENCODINGS][TILEGX_MAX_OPERANDS];

};

extern const struct tilegx_opcode tilegx_opcodes[];

/* Used for non-textual disassembly into structs. */
struct tilegx_decoded_instruction
{
  const struct tilegx_opcode *opcode;
  const struct tilegx_operand *operands[TILEGX_MAX_OPERANDS];
  long long operand_values[TILEGX_MAX_OPERANDS];
};


/* Disassemble a bundle into a struct for machine processing. */
extern int parse_insn_tilegx(tilegx_bundle_bits bits,
                             unsigned long long pc,
                             struct tilegx_decoded_instruction
                             decoded[TILEGX_MAX_INSTRUCTIONS_PER_BUNDLE]);



#endif /* opcode_tilegx_h */

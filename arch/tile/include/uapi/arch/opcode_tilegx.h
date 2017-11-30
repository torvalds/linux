/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* TILE-Gx opcode information.
 *
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 *
 *
 *
 *
 */

#ifndef __ARCH_OPCODE_H__
#define __ARCH_OPCODE_H__

#ifndef __ASSEMBLER__

typedef unsigned long long tilegx_bundle_bits;

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

/* Make a few "tile_" variables to simplify common code between
   architectures.  */

typedef tilegx_bundle_bits tile_bundle_bits;
#define TILE_BUNDLE_SIZE_IN_BYTES TILEGX_BUNDLE_SIZE_IN_BYTES
#define TILE_BUNDLE_ALIGNMENT_IN_BYTES TILEGX_BUNDLE_ALIGNMENT_IN_BYTES
#define TILE_LOG2_BUNDLE_ALIGNMENT_IN_BYTES \
  TILEGX_LOG2_BUNDLE_ALIGNMENT_IN_BYTES
#define TILE_BPT_BUNDLE TILEGX_BPT_BUNDLE

/* 64-bit pattern for a { bpt ; nop } bundle. */
#define TILEGX_BPT_BUNDLE 0x286a44ae51485000ULL

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


enum
{
  ADDI_IMM8_OPCODE_X0 = 1,
  ADDI_IMM8_OPCODE_X1 = 1,
  ADDI_OPCODE_Y0 = 0,
  ADDI_OPCODE_Y1 = 1,
  ADDLI_OPCODE_X0 = 1,
  ADDLI_OPCODE_X1 = 0,
  ADDXI_IMM8_OPCODE_X0 = 2,
  ADDXI_IMM8_OPCODE_X1 = 2,
  ADDXI_OPCODE_Y0 = 1,
  ADDXI_OPCODE_Y1 = 2,
  ADDXLI_OPCODE_X0 = 2,
  ADDXLI_OPCODE_X1 = 1,
  ADDXSC_RRR_0_OPCODE_X0 = 1,
  ADDXSC_RRR_0_OPCODE_X1 = 1,
  ADDX_RRR_0_OPCODE_X0 = 2,
  ADDX_RRR_0_OPCODE_X1 = 2,
  ADDX_RRR_0_OPCODE_Y0 = 0,
  ADDX_RRR_0_OPCODE_Y1 = 0,
  ADD_RRR_0_OPCODE_X0 = 3,
  ADD_RRR_0_OPCODE_X1 = 3,
  ADD_RRR_0_OPCODE_Y0 = 1,
  ADD_RRR_0_OPCODE_Y1 = 1,
  ANDI_IMM8_OPCODE_X0 = 3,
  ANDI_IMM8_OPCODE_X1 = 3,
  ANDI_OPCODE_Y0 = 2,
  ANDI_OPCODE_Y1 = 3,
  AND_RRR_0_OPCODE_X0 = 4,
  AND_RRR_0_OPCODE_X1 = 4,
  AND_RRR_5_OPCODE_Y0 = 0,
  AND_RRR_5_OPCODE_Y1 = 0,
  BEQZT_BRANCH_OPCODE_X1 = 16,
  BEQZ_BRANCH_OPCODE_X1 = 17,
  BFEXTS_BF_OPCODE_X0 = 4,
  BFEXTU_BF_OPCODE_X0 = 5,
  BFINS_BF_OPCODE_X0 = 6,
  BF_OPCODE_X0 = 3,
  BGEZT_BRANCH_OPCODE_X1 = 18,
  BGEZ_BRANCH_OPCODE_X1 = 19,
  BGTZT_BRANCH_OPCODE_X1 = 20,
  BGTZ_BRANCH_OPCODE_X1 = 21,
  BLBCT_BRANCH_OPCODE_X1 = 22,
  BLBC_BRANCH_OPCODE_X1 = 23,
  BLBST_BRANCH_OPCODE_X1 = 24,
  BLBS_BRANCH_OPCODE_X1 = 25,
  BLEZT_BRANCH_OPCODE_X1 = 26,
  BLEZ_BRANCH_OPCODE_X1 = 27,
  BLTZT_BRANCH_OPCODE_X1 = 28,
  BLTZ_BRANCH_OPCODE_X1 = 29,
  BNEZT_BRANCH_OPCODE_X1 = 30,
  BNEZ_BRANCH_OPCODE_X1 = 31,
  BRANCH_OPCODE_X1 = 2,
  CMOVEQZ_RRR_0_OPCODE_X0 = 5,
  CMOVEQZ_RRR_4_OPCODE_Y0 = 0,
  CMOVNEZ_RRR_0_OPCODE_X0 = 6,
  CMOVNEZ_RRR_4_OPCODE_Y0 = 1,
  CMPEQI_IMM8_OPCODE_X0 = 4,
  CMPEQI_IMM8_OPCODE_X1 = 4,
  CMPEQI_OPCODE_Y0 = 3,
  CMPEQI_OPCODE_Y1 = 4,
  CMPEQ_RRR_0_OPCODE_X0 = 7,
  CMPEQ_RRR_0_OPCODE_X1 = 5,
  CMPEQ_RRR_3_OPCODE_Y0 = 0,
  CMPEQ_RRR_3_OPCODE_Y1 = 2,
  CMPEXCH4_RRR_0_OPCODE_X1 = 6,
  CMPEXCH_RRR_0_OPCODE_X1 = 7,
  CMPLES_RRR_0_OPCODE_X0 = 8,
  CMPLES_RRR_0_OPCODE_X1 = 8,
  CMPLES_RRR_2_OPCODE_Y0 = 0,
  CMPLES_RRR_2_OPCODE_Y1 = 0,
  CMPLEU_RRR_0_OPCODE_X0 = 9,
  CMPLEU_RRR_0_OPCODE_X1 = 9,
  CMPLEU_RRR_2_OPCODE_Y0 = 1,
  CMPLEU_RRR_2_OPCODE_Y1 = 1,
  CMPLTSI_IMM8_OPCODE_X0 = 5,
  CMPLTSI_IMM8_OPCODE_X1 = 5,
  CMPLTSI_OPCODE_Y0 = 4,
  CMPLTSI_OPCODE_Y1 = 5,
  CMPLTS_RRR_0_OPCODE_X0 = 10,
  CMPLTS_RRR_0_OPCODE_X1 = 10,
  CMPLTS_RRR_2_OPCODE_Y0 = 2,
  CMPLTS_RRR_2_OPCODE_Y1 = 2,
  CMPLTUI_IMM8_OPCODE_X0 = 6,
  CMPLTUI_IMM8_OPCODE_X1 = 6,
  CMPLTU_RRR_0_OPCODE_X0 = 11,
  CMPLTU_RRR_0_OPCODE_X1 = 11,
  CMPLTU_RRR_2_OPCODE_Y0 = 3,
  CMPLTU_RRR_2_OPCODE_Y1 = 3,
  CMPNE_RRR_0_OPCODE_X0 = 12,
  CMPNE_RRR_0_OPCODE_X1 = 12,
  CMPNE_RRR_3_OPCODE_Y0 = 1,
  CMPNE_RRR_3_OPCODE_Y1 = 3,
  CMULAF_RRR_0_OPCODE_X0 = 13,
  CMULA_RRR_0_OPCODE_X0 = 14,
  CMULFR_RRR_0_OPCODE_X0 = 15,
  CMULF_RRR_0_OPCODE_X0 = 16,
  CMULHR_RRR_0_OPCODE_X0 = 17,
  CMULH_RRR_0_OPCODE_X0 = 18,
  CMUL_RRR_0_OPCODE_X0 = 19,
  CNTLZ_UNARY_OPCODE_X0 = 1,
  CNTLZ_UNARY_OPCODE_Y0 = 1,
  CNTTZ_UNARY_OPCODE_X0 = 2,
  CNTTZ_UNARY_OPCODE_Y0 = 2,
  CRC32_32_RRR_0_OPCODE_X0 = 20,
  CRC32_8_RRR_0_OPCODE_X0 = 21,
  DBLALIGN2_RRR_0_OPCODE_X0 = 22,
  DBLALIGN2_RRR_0_OPCODE_X1 = 13,
  DBLALIGN4_RRR_0_OPCODE_X0 = 23,
  DBLALIGN4_RRR_0_OPCODE_X1 = 14,
  DBLALIGN6_RRR_0_OPCODE_X0 = 24,
  DBLALIGN6_RRR_0_OPCODE_X1 = 15,
  DBLALIGN_RRR_0_OPCODE_X0 = 25,
  DRAIN_UNARY_OPCODE_X1 = 1,
  DTLBPR_UNARY_OPCODE_X1 = 2,
  EXCH4_RRR_0_OPCODE_X1 = 16,
  EXCH_RRR_0_OPCODE_X1 = 17,
  FDOUBLE_ADDSUB_RRR_0_OPCODE_X0 = 26,
  FDOUBLE_ADD_FLAGS_RRR_0_OPCODE_X0 = 27,
  FDOUBLE_MUL_FLAGS_RRR_0_OPCODE_X0 = 28,
  FDOUBLE_PACK1_RRR_0_OPCODE_X0 = 29,
  FDOUBLE_PACK2_RRR_0_OPCODE_X0 = 30,
  FDOUBLE_SUB_FLAGS_RRR_0_OPCODE_X0 = 31,
  FDOUBLE_UNPACK_MAX_RRR_0_OPCODE_X0 = 32,
  FDOUBLE_UNPACK_MIN_RRR_0_OPCODE_X0 = 33,
  FETCHADD4_RRR_0_OPCODE_X1 = 18,
  FETCHADDGEZ4_RRR_0_OPCODE_X1 = 19,
  FETCHADDGEZ_RRR_0_OPCODE_X1 = 20,
  FETCHADD_RRR_0_OPCODE_X1 = 21,
  FETCHAND4_RRR_0_OPCODE_X1 = 22,
  FETCHAND_RRR_0_OPCODE_X1 = 23,
  FETCHOR4_RRR_0_OPCODE_X1 = 24,
  FETCHOR_RRR_0_OPCODE_X1 = 25,
  FINV_UNARY_OPCODE_X1 = 3,
  FLUSHWB_UNARY_OPCODE_X1 = 4,
  FLUSH_UNARY_OPCODE_X1 = 5,
  FNOP_UNARY_OPCODE_X0 = 3,
  FNOP_UNARY_OPCODE_X1 = 6,
  FNOP_UNARY_OPCODE_Y0 = 3,
  FNOP_UNARY_OPCODE_Y1 = 8,
  FSINGLE_ADD1_RRR_0_OPCODE_X0 = 34,
  FSINGLE_ADDSUB2_RRR_0_OPCODE_X0 = 35,
  FSINGLE_MUL1_RRR_0_OPCODE_X0 = 36,
  FSINGLE_MUL2_RRR_0_OPCODE_X0 = 37,
  FSINGLE_PACK1_UNARY_OPCODE_X0 = 4,
  FSINGLE_PACK1_UNARY_OPCODE_Y0 = 4,
  FSINGLE_PACK2_RRR_0_OPCODE_X0 = 38,
  FSINGLE_SUB1_RRR_0_OPCODE_X0 = 39,
  ICOH_UNARY_OPCODE_X1 = 7,
  ILL_UNARY_OPCODE_X1 = 8,
  ILL_UNARY_OPCODE_Y1 = 9,
  IMM8_OPCODE_X0 = 4,
  IMM8_OPCODE_X1 = 3,
  INV_UNARY_OPCODE_X1 = 9,
  IRET_UNARY_OPCODE_X1 = 10,
  JALRP_UNARY_OPCODE_X1 = 11,
  JALRP_UNARY_OPCODE_Y1 = 10,
  JALR_UNARY_OPCODE_X1 = 12,
  JALR_UNARY_OPCODE_Y1 = 11,
  JAL_JUMP_OPCODE_X1 = 0,
  JRP_UNARY_OPCODE_X1 = 13,
  JRP_UNARY_OPCODE_Y1 = 12,
  JR_UNARY_OPCODE_X1 = 14,
  JR_UNARY_OPCODE_Y1 = 13,
  JUMP_OPCODE_X1 = 4,
  J_JUMP_OPCODE_X1 = 1,
  LD1S_ADD_IMM8_OPCODE_X1 = 7,
  LD1S_OPCODE_Y2 = 0,
  LD1S_UNARY_OPCODE_X1 = 15,
  LD1U_ADD_IMM8_OPCODE_X1 = 8,
  LD1U_OPCODE_Y2 = 1,
  LD1U_UNARY_OPCODE_X1 = 16,
  LD2S_ADD_IMM8_OPCODE_X1 = 9,
  LD2S_OPCODE_Y2 = 2,
  LD2S_UNARY_OPCODE_X1 = 17,
  LD2U_ADD_IMM8_OPCODE_X1 = 10,
  LD2U_OPCODE_Y2 = 3,
  LD2U_UNARY_OPCODE_X1 = 18,
  LD4S_ADD_IMM8_OPCODE_X1 = 11,
  LD4S_OPCODE_Y2 = 1,
  LD4S_UNARY_OPCODE_X1 = 19,
  LD4U_ADD_IMM8_OPCODE_X1 = 12,
  LD4U_OPCODE_Y2 = 2,
  LD4U_UNARY_OPCODE_X1 = 20,
  LDNA_ADD_IMM8_OPCODE_X1 = 21,
  LDNA_UNARY_OPCODE_X1 = 21,
  LDNT1S_ADD_IMM8_OPCODE_X1 = 13,
  LDNT1S_UNARY_OPCODE_X1 = 22,
  LDNT1U_ADD_IMM8_OPCODE_X1 = 14,
  LDNT1U_UNARY_OPCODE_X1 = 23,
  LDNT2S_ADD_IMM8_OPCODE_X1 = 15,
  LDNT2S_UNARY_OPCODE_X1 = 24,
  LDNT2U_ADD_IMM8_OPCODE_X1 = 16,
  LDNT2U_UNARY_OPCODE_X1 = 25,
  LDNT4S_ADD_IMM8_OPCODE_X1 = 17,
  LDNT4S_UNARY_OPCODE_X1 = 26,
  LDNT4U_ADD_IMM8_OPCODE_X1 = 18,
  LDNT4U_UNARY_OPCODE_X1 = 27,
  LDNT_ADD_IMM8_OPCODE_X1 = 19,
  LDNT_UNARY_OPCODE_X1 = 28,
  LD_ADD_IMM8_OPCODE_X1 = 20,
  LD_OPCODE_Y2 = 3,
  LD_UNARY_OPCODE_X1 = 29,
  LNK_UNARY_OPCODE_X1 = 30,
  LNK_UNARY_OPCODE_Y1 = 14,
  MFSPR_IMM8_OPCODE_X1 = 22,
  MF_UNARY_OPCODE_X1 = 31,
  MM_BF_OPCODE_X0 = 7,
  MNZ_RRR_0_OPCODE_X0 = 40,
  MNZ_RRR_0_OPCODE_X1 = 26,
  MNZ_RRR_4_OPCODE_Y0 = 2,
  MNZ_RRR_4_OPCODE_Y1 = 2,
  MODE_OPCODE_YA2 = 1,
  MODE_OPCODE_YB2 = 2,
  MODE_OPCODE_YC2 = 3,
  MTSPR_IMM8_OPCODE_X1 = 23,
  MULAX_RRR_0_OPCODE_X0 = 41,
  MULAX_RRR_3_OPCODE_Y0 = 2,
  MULA_HS_HS_RRR_0_OPCODE_X0 = 42,
  MULA_HS_HS_RRR_9_OPCODE_Y0 = 0,
  MULA_HS_HU_RRR_0_OPCODE_X0 = 43,
  MULA_HS_LS_RRR_0_OPCODE_X0 = 44,
  MULA_HS_LU_RRR_0_OPCODE_X0 = 45,
  MULA_HU_HU_RRR_0_OPCODE_X0 = 46,
  MULA_HU_HU_RRR_9_OPCODE_Y0 = 1,
  MULA_HU_LS_RRR_0_OPCODE_X0 = 47,
  MULA_HU_LU_RRR_0_OPCODE_X0 = 48,
  MULA_LS_LS_RRR_0_OPCODE_X0 = 49,
  MULA_LS_LS_RRR_9_OPCODE_Y0 = 2,
  MULA_LS_LU_RRR_0_OPCODE_X0 = 50,
  MULA_LU_LU_RRR_0_OPCODE_X0 = 51,
  MULA_LU_LU_RRR_9_OPCODE_Y0 = 3,
  MULX_RRR_0_OPCODE_X0 = 52,
  MULX_RRR_3_OPCODE_Y0 = 3,
  MUL_HS_HS_RRR_0_OPCODE_X0 = 53,
  MUL_HS_HS_RRR_8_OPCODE_Y0 = 0,
  MUL_HS_HU_RRR_0_OPCODE_X0 = 54,
  MUL_HS_LS_RRR_0_OPCODE_X0 = 55,
  MUL_HS_LU_RRR_0_OPCODE_X0 = 56,
  MUL_HU_HU_RRR_0_OPCODE_X0 = 57,
  MUL_HU_HU_RRR_8_OPCODE_Y0 = 1,
  MUL_HU_LS_RRR_0_OPCODE_X0 = 58,
  MUL_HU_LU_RRR_0_OPCODE_X0 = 59,
  MUL_LS_LS_RRR_0_OPCODE_X0 = 60,
  MUL_LS_LS_RRR_8_OPCODE_Y0 = 2,
  MUL_LS_LU_RRR_0_OPCODE_X0 = 61,
  MUL_LU_LU_RRR_0_OPCODE_X0 = 62,
  MUL_LU_LU_RRR_8_OPCODE_Y0 = 3,
  MZ_RRR_0_OPCODE_X0 = 63,
  MZ_RRR_0_OPCODE_X1 = 27,
  MZ_RRR_4_OPCODE_Y0 = 3,
  MZ_RRR_4_OPCODE_Y1 = 3,
  NAP_UNARY_OPCODE_X1 = 32,
  NOP_UNARY_OPCODE_X0 = 5,
  NOP_UNARY_OPCODE_X1 = 33,
  NOP_UNARY_OPCODE_Y0 = 5,
  NOP_UNARY_OPCODE_Y1 = 15,
  NOR_RRR_0_OPCODE_X0 = 64,
  NOR_RRR_0_OPCODE_X1 = 28,
  NOR_RRR_5_OPCODE_Y0 = 1,
  NOR_RRR_5_OPCODE_Y1 = 1,
  ORI_IMM8_OPCODE_X0 = 7,
  ORI_IMM8_OPCODE_X1 = 24,
  OR_RRR_0_OPCODE_X0 = 65,
  OR_RRR_0_OPCODE_X1 = 29,
  OR_RRR_5_OPCODE_Y0 = 2,
  OR_RRR_5_OPCODE_Y1 = 2,
  PCNT_UNARY_OPCODE_X0 = 6,
  PCNT_UNARY_OPCODE_Y0 = 6,
  REVBITS_UNARY_OPCODE_X0 = 7,
  REVBITS_UNARY_OPCODE_Y0 = 7,
  REVBYTES_UNARY_OPCODE_X0 = 8,
  REVBYTES_UNARY_OPCODE_Y0 = 8,
  ROTLI_SHIFT_OPCODE_X0 = 1,
  ROTLI_SHIFT_OPCODE_X1 = 1,
  ROTLI_SHIFT_OPCODE_Y0 = 0,
  ROTLI_SHIFT_OPCODE_Y1 = 0,
  ROTL_RRR_0_OPCODE_X0 = 66,
  ROTL_RRR_0_OPCODE_X1 = 30,
  ROTL_RRR_6_OPCODE_Y0 = 0,
  ROTL_RRR_6_OPCODE_Y1 = 0,
  RRR_0_OPCODE_X0 = 5,
  RRR_0_OPCODE_X1 = 5,
  RRR_0_OPCODE_Y0 = 5,
  RRR_0_OPCODE_Y1 = 6,
  RRR_1_OPCODE_Y0 = 6,
  RRR_1_OPCODE_Y1 = 7,
  RRR_2_OPCODE_Y0 = 7,
  RRR_2_OPCODE_Y1 = 8,
  RRR_3_OPCODE_Y0 = 8,
  RRR_3_OPCODE_Y1 = 9,
  RRR_4_OPCODE_Y0 = 9,
  RRR_4_OPCODE_Y1 = 10,
  RRR_5_OPCODE_Y0 = 10,
  RRR_5_OPCODE_Y1 = 11,
  RRR_6_OPCODE_Y0 = 11,
  RRR_6_OPCODE_Y1 = 12,
  RRR_7_OPCODE_Y0 = 12,
  RRR_7_OPCODE_Y1 = 13,
  RRR_8_OPCODE_Y0 = 13,
  RRR_9_OPCODE_Y0 = 14,
  SHIFT_OPCODE_X0 = 6,
  SHIFT_OPCODE_X1 = 6,
  SHIFT_OPCODE_Y0 = 15,
  SHIFT_OPCODE_Y1 = 14,
  SHL16INSLI_OPCODE_X0 = 7,
  SHL16INSLI_OPCODE_X1 = 7,
  SHL1ADDX_RRR_0_OPCODE_X0 = 67,
  SHL1ADDX_RRR_0_OPCODE_X1 = 31,
  SHL1ADDX_RRR_7_OPCODE_Y0 = 1,
  SHL1ADDX_RRR_7_OPCODE_Y1 = 1,
  SHL1ADD_RRR_0_OPCODE_X0 = 68,
  SHL1ADD_RRR_0_OPCODE_X1 = 32,
  SHL1ADD_RRR_1_OPCODE_Y0 = 0,
  SHL1ADD_RRR_1_OPCODE_Y1 = 0,
  SHL2ADDX_RRR_0_OPCODE_X0 = 69,
  SHL2ADDX_RRR_0_OPCODE_X1 = 33,
  SHL2ADDX_RRR_7_OPCODE_Y0 = 2,
  SHL2ADDX_RRR_7_OPCODE_Y1 = 2,
  SHL2ADD_RRR_0_OPCODE_X0 = 70,
  SHL2ADD_RRR_0_OPCODE_X1 = 34,
  SHL2ADD_RRR_1_OPCODE_Y0 = 1,
  SHL2ADD_RRR_1_OPCODE_Y1 = 1,
  SHL3ADDX_RRR_0_OPCODE_X0 = 71,
  SHL3ADDX_RRR_0_OPCODE_X1 = 35,
  SHL3ADDX_RRR_7_OPCODE_Y0 = 3,
  SHL3ADDX_RRR_7_OPCODE_Y1 = 3,
  SHL3ADD_RRR_0_OPCODE_X0 = 72,
  SHL3ADD_RRR_0_OPCODE_X1 = 36,
  SHL3ADD_RRR_1_OPCODE_Y0 = 2,
  SHL3ADD_RRR_1_OPCODE_Y1 = 2,
  SHLI_SHIFT_OPCODE_X0 = 2,
  SHLI_SHIFT_OPCODE_X1 = 2,
  SHLI_SHIFT_OPCODE_Y0 = 1,
  SHLI_SHIFT_OPCODE_Y1 = 1,
  SHLXI_SHIFT_OPCODE_X0 = 3,
  SHLXI_SHIFT_OPCODE_X1 = 3,
  SHLX_RRR_0_OPCODE_X0 = 73,
  SHLX_RRR_0_OPCODE_X1 = 37,
  SHL_RRR_0_OPCODE_X0 = 74,
  SHL_RRR_0_OPCODE_X1 = 38,
  SHL_RRR_6_OPCODE_Y0 = 1,
  SHL_RRR_6_OPCODE_Y1 = 1,
  SHRSI_SHIFT_OPCODE_X0 = 4,
  SHRSI_SHIFT_OPCODE_X1 = 4,
  SHRSI_SHIFT_OPCODE_Y0 = 2,
  SHRSI_SHIFT_OPCODE_Y1 = 2,
  SHRS_RRR_0_OPCODE_X0 = 75,
  SHRS_RRR_0_OPCODE_X1 = 39,
  SHRS_RRR_6_OPCODE_Y0 = 2,
  SHRS_RRR_6_OPCODE_Y1 = 2,
  SHRUI_SHIFT_OPCODE_X0 = 5,
  SHRUI_SHIFT_OPCODE_X1 = 5,
  SHRUI_SHIFT_OPCODE_Y0 = 3,
  SHRUI_SHIFT_OPCODE_Y1 = 3,
  SHRUXI_SHIFT_OPCODE_X0 = 6,
  SHRUXI_SHIFT_OPCODE_X1 = 6,
  SHRUX_RRR_0_OPCODE_X0 = 76,
  SHRUX_RRR_0_OPCODE_X1 = 40,
  SHRU_RRR_0_OPCODE_X0 = 77,
  SHRU_RRR_0_OPCODE_X1 = 41,
  SHRU_RRR_6_OPCODE_Y0 = 3,
  SHRU_RRR_6_OPCODE_Y1 = 3,
  SHUFFLEBYTES_RRR_0_OPCODE_X0 = 78,
  ST1_ADD_IMM8_OPCODE_X1 = 25,
  ST1_OPCODE_Y2 = 0,
  ST1_RRR_0_OPCODE_X1 = 42,
  ST2_ADD_IMM8_OPCODE_X1 = 26,
  ST2_OPCODE_Y2 = 1,
  ST2_RRR_0_OPCODE_X1 = 43,
  ST4_ADD_IMM8_OPCODE_X1 = 27,
  ST4_OPCODE_Y2 = 2,
  ST4_RRR_0_OPCODE_X1 = 44,
  STNT1_ADD_IMM8_OPCODE_X1 = 28,
  STNT1_RRR_0_OPCODE_X1 = 45,
  STNT2_ADD_IMM8_OPCODE_X1 = 29,
  STNT2_RRR_0_OPCODE_X1 = 46,
  STNT4_ADD_IMM8_OPCODE_X1 = 30,
  STNT4_RRR_0_OPCODE_X1 = 47,
  STNT_ADD_IMM8_OPCODE_X1 = 31,
  STNT_RRR_0_OPCODE_X1 = 48,
  ST_ADD_IMM8_OPCODE_X1 = 32,
  ST_OPCODE_Y2 = 3,
  ST_RRR_0_OPCODE_X1 = 49,
  SUBXSC_RRR_0_OPCODE_X0 = 79,
  SUBXSC_RRR_0_OPCODE_X1 = 50,
  SUBX_RRR_0_OPCODE_X0 = 80,
  SUBX_RRR_0_OPCODE_X1 = 51,
  SUBX_RRR_0_OPCODE_Y0 = 2,
  SUBX_RRR_0_OPCODE_Y1 = 2,
  SUB_RRR_0_OPCODE_X0 = 81,
  SUB_RRR_0_OPCODE_X1 = 52,
  SUB_RRR_0_OPCODE_Y0 = 3,
  SUB_RRR_0_OPCODE_Y1 = 3,
  SWINT0_UNARY_OPCODE_X1 = 34,
  SWINT1_UNARY_OPCODE_X1 = 35,
  SWINT2_UNARY_OPCODE_X1 = 36,
  SWINT3_UNARY_OPCODE_X1 = 37,
  TBLIDXB0_UNARY_OPCODE_X0 = 9,
  TBLIDXB0_UNARY_OPCODE_Y0 = 9,
  TBLIDXB1_UNARY_OPCODE_X0 = 10,
  TBLIDXB1_UNARY_OPCODE_Y0 = 10,
  TBLIDXB2_UNARY_OPCODE_X0 = 11,
  TBLIDXB2_UNARY_OPCODE_Y0 = 11,
  TBLIDXB3_UNARY_OPCODE_X0 = 12,
  TBLIDXB3_UNARY_OPCODE_Y0 = 12,
  UNARY_RRR_0_OPCODE_X0 = 82,
  UNARY_RRR_0_OPCODE_X1 = 53,
  UNARY_RRR_1_OPCODE_Y0 = 3,
  UNARY_RRR_1_OPCODE_Y1 = 3,
  V1ADDI_IMM8_OPCODE_X0 = 8,
  V1ADDI_IMM8_OPCODE_X1 = 33,
  V1ADDUC_RRR_0_OPCODE_X0 = 83,
  V1ADDUC_RRR_0_OPCODE_X1 = 54,
  V1ADD_RRR_0_OPCODE_X0 = 84,
  V1ADD_RRR_0_OPCODE_X1 = 55,
  V1ADIFFU_RRR_0_OPCODE_X0 = 85,
  V1AVGU_RRR_0_OPCODE_X0 = 86,
  V1CMPEQI_IMM8_OPCODE_X0 = 9,
  V1CMPEQI_IMM8_OPCODE_X1 = 34,
  V1CMPEQ_RRR_0_OPCODE_X0 = 87,
  V1CMPEQ_RRR_0_OPCODE_X1 = 56,
  V1CMPLES_RRR_0_OPCODE_X0 = 88,
  V1CMPLES_RRR_0_OPCODE_X1 = 57,
  V1CMPLEU_RRR_0_OPCODE_X0 = 89,
  V1CMPLEU_RRR_0_OPCODE_X1 = 58,
  V1CMPLTSI_IMM8_OPCODE_X0 = 10,
  V1CMPLTSI_IMM8_OPCODE_X1 = 35,
  V1CMPLTS_RRR_0_OPCODE_X0 = 90,
  V1CMPLTS_RRR_0_OPCODE_X1 = 59,
  V1CMPLTUI_IMM8_OPCODE_X0 = 11,
  V1CMPLTUI_IMM8_OPCODE_X1 = 36,
  V1CMPLTU_RRR_0_OPCODE_X0 = 91,
  V1CMPLTU_RRR_0_OPCODE_X1 = 60,
  V1CMPNE_RRR_0_OPCODE_X0 = 92,
  V1CMPNE_RRR_0_OPCODE_X1 = 61,
  V1DDOTPUA_RRR_0_OPCODE_X0 = 161,
  V1DDOTPUSA_RRR_0_OPCODE_X0 = 93,
  V1DDOTPUS_RRR_0_OPCODE_X0 = 94,
  V1DDOTPU_RRR_0_OPCODE_X0 = 162,
  V1DOTPA_RRR_0_OPCODE_X0 = 95,
  V1DOTPUA_RRR_0_OPCODE_X0 = 163,
  V1DOTPUSA_RRR_0_OPCODE_X0 = 96,
  V1DOTPUS_RRR_0_OPCODE_X0 = 97,
  V1DOTPU_RRR_0_OPCODE_X0 = 164,
  V1DOTP_RRR_0_OPCODE_X0 = 98,
  V1INT_H_RRR_0_OPCODE_X0 = 99,
  V1INT_H_RRR_0_OPCODE_X1 = 62,
  V1INT_L_RRR_0_OPCODE_X0 = 100,
  V1INT_L_RRR_0_OPCODE_X1 = 63,
  V1MAXUI_IMM8_OPCODE_X0 = 12,
  V1MAXUI_IMM8_OPCODE_X1 = 37,
  V1MAXU_RRR_0_OPCODE_X0 = 101,
  V1MAXU_RRR_0_OPCODE_X1 = 64,
  V1MINUI_IMM8_OPCODE_X0 = 13,
  V1MINUI_IMM8_OPCODE_X1 = 38,
  V1MINU_RRR_0_OPCODE_X0 = 102,
  V1MINU_RRR_0_OPCODE_X1 = 65,
  V1MNZ_RRR_0_OPCODE_X0 = 103,
  V1MNZ_RRR_0_OPCODE_X1 = 66,
  V1MULTU_RRR_0_OPCODE_X0 = 104,
  V1MULUS_RRR_0_OPCODE_X0 = 105,
  V1MULU_RRR_0_OPCODE_X0 = 106,
  V1MZ_RRR_0_OPCODE_X0 = 107,
  V1MZ_RRR_0_OPCODE_X1 = 67,
  V1SADAU_RRR_0_OPCODE_X0 = 108,
  V1SADU_RRR_0_OPCODE_X0 = 109,
  V1SHLI_SHIFT_OPCODE_X0 = 7,
  V1SHLI_SHIFT_OPCODE_X1 = 7,
  V1SHL_RRR_0_OPCODE_X0 = 110,
  V1SHL_RRR_0_OPCODE_X1 = 68,
  V1SHRSI_SHIFT_OPCODE_X0 = 8,
  V1SHRSI_SHIFT_OPCODE_X1 = 8,
  V1SHRS_RRR_0_OPCODE_X0 = 111,
  V1SHRS_RRR_0_OPCODE_X1 = 69,
  V1SHRUI_SHIFT_OPCODE_X0 = 9,
  V1SHRUI_SHIFT_OPCODE_X1 = 9,
  V1SHRU_RRR_0_OPCODE_X0 = 112,
  V1SHRU_RRR_0_OPCODE_X1 = 70,
  V1SUBUC_RRR_0_OPCODE_X0 = 113,
  V1SUBUC_RRR_0_OPCODE_X1 = 71,
  V1SUB_RRR_0_OPCODE_X0 = 114,
  V1SUB_RRR_0_OPCODE_X1 = 72,
  V2ADDI_IMM8_OPCODE_X0 = 14,
  V2ADDI_IMM8_OPCODE_X1 = 39,
  V2ADDSC_RRR_0_OPCODE_X0 = 115,
  V2ADDSC_RRR_0_OPCODE_X1 = 73,
  V2ADD_RRR_0_OPCODE_X0 = 116,
  V2ADD_RRR_0_OPCODE_X1 = 74,
  V2ADIFFS_RRR_0_OPCODE_X0 = 117,
  V2AVGS_RRR_0_OPCODE_X0 = 118,
  V2CMPEQI_IMM8_OPCODE_X0 = 15,
  V2CMPEQI_IMM8_OPCODE_X1 = 40,
  V2CMPEQ_RRR_0_OPCODE_X0 = 119,
  V2CMPEQ_RRR_0_OPCODE_X1 = 75,
  V2CMPLES_RRR_0_OPCODE_X0 = 120,
  V2CMPLES_RRR_0_OPCODE_X1 = 76,
  V2CMPLEU_RRR_0_OPCODE_X0 = 121,
  V2CMPLEU_RRR_0_OPCODE_X1 = 77,
  V2CMPLTSI_IMM8_OPCODE_X0 = 16,
  V2CMPLTSI_IMM8_OPCODE_X1 = 41,
  V2CMPLTS_RRR_0_OPCODE_X0 = 122,
  V2CMPLTS_RRR_0_OPCODE_X1 = 78,
  V2CMPLTUI_IMM8_OPCODE_X0 = 17,
  V2CMPLTUI_IMM8_OPCODE_X1 = 42,
  V2CMPLTU_RRR_0_OPCODE_X0 = 123,
  V2CMPLTU_RRR_0_OPCODE_X1 = 79,
  V2CMPNE_RRR_0_OPCODE_X0 = 124,
  V2CMPNE_RRR_0_OPCODE_X1 = 80,
  V2DOTPA_RRR_0_OPCODE_X0 = 125,
  V2DOTP_RRR_0_OPCODE_X0 = 126,
  V2INT_H_RRR_0_OPCODE_X0 = 127,
  V2INT_H_RRR_0_OPCODE_X1 = 81,
  V2INT_L_RRR_0_OPCODE_X0 = 128,
  V2INT_L_RRR_0_OPCODE_X1 = 82,
  V2MAXSI_IMM8_OPCODE_X0 = 18,
  V2MAXSI_IMM8_OPCODE_X1 = 43,
  V2MAXS_RRR_0_OPCODE_X0 = 129,
  V2MAXS_RRR_0_OPCODE_X1 = 83,
  V2MINSI_IMM8_OPCODE_X0 = 19,
  V2MINSI_IMM8_OPCODE_X1 = 44,
  V2MINS_RRR_0_OPCODE_X0 = 130,
  V2MINS_RRR_0_OPCODE_X1 = 84,
  V2MNZ_RRR_0_OPCODE_X0 = 131,
  V2MNZ_RRR_0_OPCODE_X1 = 85,
  V2MULFSC_RRR_0_OPCODE_X0 = 132,
  V2MULS_RRR_0_OPCODE_X0 = 133,
  V2MULTS_RRR_0_OPCODE_X0 = 134,
  V2MZ_RRR_0_OPCODE_X0 = 135,
  V2MZ_RRR_0_OPCODE_X1 = 86,
  V2PACKH_RRR_0_OPCODE_X0 = 136,
  V2PACKH_RRR_0_OPCODE_X1 = 87,
  V2PACKL_RRR_0_OPCODE_X0 = 137,
  V2PACKL_RRR_0_OPCODE_X1 = 88,
  V2PACKUC_RRR_0_OPCODE_X0 = 138,
  V2PACKUC_RRR_0_OPCODE_X1 = 89,
  V2SADAS_RRR_0_OPCODE_X0 = 139,
  V2SADAU_RRR_0_OPCODE_X0 = 140,
  V2SADS_RRR_0_OPCODE_X0 = 141,
  V2SADU_RRR_0_OPCODE_X0 = 142,
  V2SHLI_SHIFT_OPCODE_X0 = 10,
  V2SHLI_SHIFT_OPCODE_X1 = 10,
  V2SHLSC_RRR_0_OPCODE_X0 = 143,
  V2SHLSC_RRR_0_OPCODE_X1 = 90,
  V2SHL_RRR_0_OPCODE_X0 = 144,
  V2SHL_RRR_0_OPCODE_X1 = 91,
  V2SHRSI_SHIFT_OPCODE_X0 = 11,
  V2SHRSI_SHIFT_OPCODE_X1 = 11,
  V2SHRS_RRR_0_OPCODE_X0 = 145,
  V2SHRS_RRR_0_OPCODE_X1 = 92,
  V2SHRUI_SHIFT_OPCODE_X0 = 12,
  V2SHRUI_SHIFT_OPCODE_X1 = 12,
  V2SHRU_RRR_0_OPCODE_X0 = 146,
  V2SHRU_RRR_0_OPCODE_X1 = 93,
  V2SUBSC_RRR_0_OPCODE_X0 = 147,
  V2SUBSC_RRR_0_OPCODE_X1 = 94,
  V2SUB_RRR_0_OPCODE_X0 = 148,
  V2SUB_RRR_0_OPCODE_X1 = 95,
  V4ADDSC_RRR_0_OPCODE_X0 = 149,
  V4ADDSC_RRR_0_OPCODE_X1 = 96,
  V4ADD_RRR_0_OPCODE_X0 = 150,
  V4ADD_RRR_0_OPCODE_X1 = 97,
  V4INT_H_RRR_0_OPCODE_X0 = 151,
  V4INT_H_RRR_0_OPCODE_X1 = 98,
  V4INT_L_RRR_0_OPCODE_X0 = 152,
  V4INT_L_RRR_0_OPCODE_X1 = 99,
  V4PACKSC_RRR_0_OPCODE_X0 = 153,
  V4PACKSC_RRR_0_OPCODE_X1 = 100,
  V4SHLSC_RRR_0_OPCODE_X0 = 154,
  V4SHLSC_RRR_0_OPCODE_X1 = 101,
  V4SHL_RRR_0_OPCODE_X0 = 155,
  V4SHL_RRR_0_OPCODE_X1 = 102,
  V4SHRS_RRR_0_OPCODE_X0 = 156,
  V4SHRS_RRR_0_OPCODE_X1 = 103,
  V4SHRU_RRR_0_OPCODE_X0 = 157,
  V4SHRU_RRR_0_OPCODE_X1 = 104,
  V4SUBSC_RRR_0_OPCODE_X0 = 158,
  V4SUBSC_RRR_0_OPCODE_X1 = 105,
  V4SUB_RRR_0_OPCODE_X0 = 159,
  V4SUB_RRR_0_OPCODE_X1 = 106,
  WH64_UNARY_OPCODE_X1 = 38,
  XORI_IMM8_OPCODE_X0 = 20,
  XORI_IMM8_OPCODE_X1 = 45,
  XOR_RRR_0_OPCODE_X0 = 160,
  XOR_RRR_0_OPCODE_X1 = 107,
  XOR_RRR_5_OPCODE_Y0 = 3,
  XOR_RRR_5_OPCODE_Y1 = 3
};


#endif /* __ASSEMBLER__ */

#endif /* __ARCH_OPCODE_H__ */

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* TILEPro opcode information.
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

typedef unsigned long long tilepro_bundle_bits;

/* This is the bit that determines if a bundle is in the Y encoding. */
#define TILEPRO_BUNDLE_Y_ENCODING_MASK ((tilepro_bundle_bits)1 << 63)

enum
{
  /* Maximum number of instructions in a bundle (2 for X, 3 for Y). */
  TILEPRO_MAX_INSTRUCTIONS_PER_BUNDLE = 3,

  /* How many different pipeline encodings are there? X0, X1, Y0, Y1, Y2. */
  TILEPRO_NUM_PIPELINE_ENCODINGS = 5,

  /* Log base 2 of TILEPRO_BUNDLE_SIZE_IN_BYTES. */
  TILEPRO_LOG2_BUNDLE_SIZE_IN_BYTES = 3,

  /* Instructions take this many bytes. */
  TILEPRO_BUNDLE_SIZE_IN_BYTES = 1 << TILEPRO_LOG2_BUNDLE_SIZE_IN_BYTES,

  /* Log base 2 of TILEPRO_BUNDLE_ALIGNMENT_IN_BYTES. */
  TILEPRO_LOG2_BUNDLE_ALIGNMENT_IN_BYTES = 3,

  /* Bundles should be aligned modulo this number of bytes. */
  TILEPRO_BUNDLE_ALIGNMENT_IN_BYTES =
    (1 << TILEPRO_LOG2_BUNDLE_ALIGNMENT_IN_BYTES),

  /* Log base 2 of TILEPRO_SN_INSTRUCTION_SIZE_IN_BYTES. */
  TILEPRO_LOG2_SN_INSTRUCTION_SIZE_IN_BYTES = 1,

  /* Static network instructions take this many bytes. */
  TILEPRO_SN_INSTRUCTION_SIZE_IN_BYTES =
    (1 << TILEPRO_LOG2_SN_INSTRUCTION_SIZE_IN_BYTES),

  /* Number of registers (some are magic, such as network I/O). */
  TILEPRO_NUM_REGISTERS = 64,

  /* Number of static network registers. */
  TILEPRO_NUM_SN_REGISTERS = 4
};

/* Make a few "tile_" variables to simplify common code between
   architectures.  */

typedef tilepro_bundle_bits tile_bundle_bits;
#define TILE_BUNDLE_SIZE_IN_BYTES TILEPRO_BUNDLE_SIZE_IN_BYTES
#define TILE_BUNDLE_ALIGNMENT_IN_BYTES TILEPRO_BUNDLE_ALIGNMENT_IN_BYTES
#define TILE_LOG2_BUNDLE_ALIGNMENT_IN_BYTES \
  TILEPRO_LOG2_BUNDLE_ALIGNMENT_IN_BYTES
#define TILE_BPT_BUNDLE TILEPRO_BPT_BUNDLE

/* 64-bit pattern for a { bpt ; nop } bundle. */
#define TILEPRO_BPT_BUNDLE 0x400b3cae70166000ULL

static __inline unsigned int
get_BrOff_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3ff);
}

static __inline unsigned int
get_BrOff_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000);
}

static __inline unsigned int
get_BrType_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0xf);
}

static __inline unsigned int
get_Dest_Imm8_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 43)) & 0x000000c0);
}

static __inline unsigned int
get_Dest_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 2)) & 0x3);
}

static __inline unsigned int
get_Dest_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3f);
}

static __inline unsigned int
get_Dest_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x3f);
}

static __inline unsigned int
get_Imm16_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xffff);
}

static __inline unsigned int
get_Imm16_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xffff);
}

static __inline unsigned int
get_Imm8_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0xff);
}

static __inline unsigned int
get_Imm8_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0xff);
}

static __inline unsigned int
get_Imm8_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0xff);
}

static __inline unsigned int
get_ImmOpcodeExtension_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0x7f);
}

static __inline unsigned int
get_ImmOpcodeExtension_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 51)) & 0x7f);
}

static __inline unsigned int
get_ImmRROpcodeExtension_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 8)) & 0x3);
}

static __inline unsigned int
get_JOffLong_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000) |
         (((unsigned int)(n >> 14)) & 0x001e0000) |
         (((unsigned int)(n >> 16)) & 0x07e00000) |
         (((unsigned int)(n >> 31)) & 0x18000000);
}

static __inline unsigned int
get_JOff_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x00007fff) |
         (((unsigned int)(n >> 20)) & 0x00018000) |
         (((unsigned int)(n >> 14)) & 0x001e0000) |
         (((unsigned int)(n >> 16)) & 0x07e00000) |
         (((unsigned int)(n >> 31)) & 0x08000000);
}

static __inline unsigned int
get_MF_Imm15_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x00003fff) |
         (((unsigned int)(n >> 44)) & 0x00004000);
}

static __inline unsigned int
get_MMEnd_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x1f);
}

static __inline unsigned int
get_MMEnd_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x1f);
}

static __inline unsigned int
get_MMStart_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 23)) & 0x1f);
}

static __inline unsigned int
get_MMStart_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 54)) & 0x1f);
}

static __inline unsigned int
get_MT_Imm15_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 31)) & 0x0000003f) |
         (((unsigned int)(n >> 37)) & 0x00003fc0) |
         (((unsigned int)(n >> 44)) & 0x00004000);
}

static __inline unsigned int
get_Mode(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 63)) & 0x1);
}

static __inline unsigned int
get_NoRegOpcodeExtension_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0xf);
}

static __inline unsigned int
get_Opcode_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 10)) & 0x3f);
}

static __inline unsigned int
get_Opcode_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 28)) & 0x7);
}

static __inline unsigned int
get_Opcode_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 59)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 27)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 59)) & 0xf);
}

static __inline unsigned int
get_Opcode_Y2(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 56)) & 0x7);
}

static __inline unsigned int
get_RROpcodeExtension_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 4)) & 0xf);
}

static __inline unsigned int
get_RRROpcodeExtension_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x1ff);
}

static __inline unsigned int
get_RRROpcodeExtension_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x1ff);
}

static __inline unsigned int
get_RRROpcodeExtension_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 18)) & 0x3);
}

static __inline unsigned int
get_RRROpcodeExtension_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 49)) & 0x3);
}

static __inline unsigned int
get_RouteOpcodeExtension_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3ff);
}

static __inline unsigned int
get_S_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 27)) & 0x1);
}

static __inline unsigned int
get_S_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 58)) & 0x1);
}

static __inline unsigned int
get_ShAmt_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_ShAmt_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_SrcA_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 6)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 37)) & 0x3f);
}

static __inline unsigned int
get_SrcA_Y2(tilepro_bundle_bits n)
{
  return (((n >> 26)) & 0x00000001) |
         (((unsigned int)(n >> 50)) & 0x0000003e);
}

static __inline unsigned int
get_SrcBDest_Y2(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 20)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x3f);
}

static __inline unsigned int
get_SrcB_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x3f);
}

static __inline unsigned int
get_Src_SN(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 0)) & 0x3);
}

static __inline unsigned int
get_UnOpcodeExtension_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 12)) & 0x1f);
}

static __inline unsigned int
get_UnOpcodeExtension_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 43)) & 0x1f);
}

static __inline unsigned int
get_UnShOpcodeExtension_X0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 17)) & 0x3ff);
}

static __inline unsigned int
get_UnShOpcodeExtension_X1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 48)) & 0x3ff);
}

static __inline unsigned int
get_UnShOpcodeExtension_Y0(tilepro_bundle_bits num)
{
  const unsigned int n = (unsigned int)num;
  return (((n >> 17)) & 0x7);
}

static __inline unsigned int
get_UnShOpcodeExtension_Y1(tilepro_bundle_bits n)
{
  return (((unsigned int)(n >> 48)) & 0x7);
}


static __inline int
sign_extend(int n, int num_bits)
{
  int shift = (int)(sizeof(int) * 8 - num_bits);
  return (n << shift) >> shift;
}



static __inline tilepro_bundle_bits
create_BrOff_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 0);
}

static __inline tilepro_bundle_bits
create_BrOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tilepro_bundle_bits)(n & 0x00018000)) << 20);
}

static __inline tilepro_bundle_bits
create_BrType_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xf)) << 31);
}

static __inline tilepro_bundle_bits
create_Dest_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tilepro_bundle_bits)(n & 0x000000c0)) << 43);
}

static __inline tilepro_bundle_bits
create_Dest_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 2);
}

static __inline tilepro_bundle_bits
create_Dest_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tilepro_bundle_bits
create_Dest_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tilepro_bundle_bits
create_Dest_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 0);
}

static __inline tilepro_bundle_bits
create_Dest_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 31);
}

static __inline tilepro_bundle_bits
create_Imm16_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xffff) << 12);
}

static __inline tilepro_bundle_bits
create_Imm16_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xffff)) << 43);
}

static __inline tilepro_bundle_bits
create_Imm8_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 0);
}

static __inline tilepro_bundle_bits
create_Imm8_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tilepro_bundle_bits
create_Imm8_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xff)) << 43);
}

static __inline tilepro_bundle_bits
create_Imm8_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xff) << 12);
}

static __inline tilepro_bundle_bits
create_Imm8_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xff)) << 43);
}

static __inline tilepro_bundle_bits
create_ImmOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7f) << 20);
}

static __inline tilepro_bundle_bits
create_ImmOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x7f)) << 51);
}

static __inline tilepro_bundle_bits
create_ImmRROpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 8);
}

static __inline tilepro_bundle_bits
create_JOffLong_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tilepro_bundle_bits)(n & 0x00018000)) << 20) |
         (((tilepro_bundle_bits)(n & 0x001e0000)) << 14) |
         (((tilepro_bundle_bits)(n & 0x07e00000)) << 16) |
         (((tilepro_bundle_bits)(n & 0x18000000)) << 31);
}

static __inline tilepro_bundle_bits
create_JOff_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x00007fff)) << 43) |
         (((tilepro_bundle_bits)(n & 0x00018000)) << 20) |
         (((tilepro_bundle_bits)(n & 0x001e0000)) << 14) |
         (((tilepro_bundle_bits)(n & 0x07e00000)) << 16) |
         (((tilepro_bundle_bits)(n & 0x08000000)) << 31);
}

static __inline tilepro_bundle_bits
create_MF_Imm15_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x00003fff)) << 37) |
         (((tilepro_bundle_bits)(n & 0x00004000)) << 44);
}

static __inline tilepro_bundle_bits
create_MMEnd_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 18);
}

static __inline tilepro_bundle_bits
create_MMEnd_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 49);
}

static __inline tilepro_bundle_bits
create_MMStart_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 23);
}

static __inline tilepro_bundle_bits
create_MMStart_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 54);
}

static __inline tilepro_bundle_bits
create_MT_Imm15_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x0000003f)) << 31) |
         (((tilepro_bundle_bits)(n & 0x00003fc0)) << 37) |
         (((tilepro_bundle_bits)(n & 0x00004000)) << 44);
}

static __inline tilepro_bundle_bits
create_Mode(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1)) << 63);
}

static __inline tilepro_bundle_bits
create_NoRegOpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 0);
}

static __inline tilepro_bundle_bits
create_Opcode_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 10);
}

static __inline tilepro_bundle_bits
create_Opcode_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7) << 28);
}

static __inline tilepro_bundle_bits
create_Opcode_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xf)) << 59);
}

static __inline tilepro_bundle_bits
create_Opcode_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 27);
}

static __inline tilepro_bundle_bits
create_Opcode_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0xf)) << 59);
}

static __inline tilepro_bundle_bits
create_Opcode_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x7)) << 56);
}

static __inline tilepro_bundle_bits
create_RROpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0xf) << 4);
}

static __inline tilepro_bundle_bits
create_RRROpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1ff) << 18);
}

static __inline tilepro_bundle_bits
create_RRROpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1ff)) << 49);
}

static __inline tilepro_bundle_bits
create_RRROpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 18);
}

static __inline tilepro_bundle_bits
create_RRROpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3)) << 49);
}

static __inline tilepro_bundle_bits
create_RouteOpcodeExtension_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 0);
}

static __inline tilepro_bundle_bits
create_S_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1) << 27);
}

static __inline tilepro_bundle_bits
create_S_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1)) << 58);
}

static __inline tilepro_bundle_bits
create_ShAmt_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tilepro_bundle_bits
create_ShAmt_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tilepro_bundle_bits
create_ShAmt_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tilepro_bundle_bits
create_ShAmt_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tilepro_bundle_bits
create_SrcA_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tilepro_bundle_bits
create_SrcA_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tilepro_bundle_bits
create_SrcA_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 6);
}

static __inline tilepro_bundle_bits
create_SrcA_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 37);
}

static __inline tilepro_bundle_bits
create_SrcA_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x00000001) << 26) |
         (((tilepro_bundle_bits)(n & 0x0000003e)) << 50);
}

static __inline tilepro_bundle_bits
create_SrcBDest_Y2(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 20);
}

static __inline tilepro_bundle_bits
create_SrcB_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilepro_bundle_bits
create_SrcB_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilepro_bundle_bits
create_SrcB_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3f) << 12);
}

static __inline tilepro_bundle_bits
create_SrcB_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3f)) << 43);
}

static __inline tilepro_bundle_bits
create_Src_SN(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3) << 0);
}

static __inline tilepro_bundle_bits
create_UnOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tilepro_bundle_bits
create_UnOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tilepro_bundle_bits
create_UnOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x1f) << 12);
}

static __inline tilepro_bundle_bits
create_UnOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x1f)) << 43);
}

static __inline tilepro_bundle_bits
create_UnShOpcodeExtension_X0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x3ff) << 17);
}

static __inline tilepro_bundle_bits
create_UnShOpcodeExtension_X1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x3ff)) << 48);
}

static __inline tilepro_bundle_bits
create_UnShOpcodeExtension_Y0(int num)
{
  const unsigned int n = (unsigned int)num;
  return ((n & 0x7) << 17);
}

static __inline tilepro_bundle_bits
create_UnShOpcodeExtension_Y1(int num)
{
  const unsigned int n = (unsigned int)num;
  return (((tilepro_bundle_bits)(n & 0x7)) << 48);
}


enum
{
  ADDBS_U_SPECIAL_0_OPCODE_X0 = 98,
  ADDBS_U_SPECIAL_0_OPCODE_X1 = 68,
  ADDB_SPECIAL_0_OPCODE_X0 = 1,
  ADDB_SPECIAL_0_OPCODE_X1 = 1,
  ADDHS_SPECIAL_0_OPCODE_X0 = 99,
  ADDHS_SPECIAL_0_OPCODE_X1 = 69,
  ADDH_SPECIAL_0_OPCODE_X0 = 2,
  ADDH_SPECIAL_0_OPCODE_X1 = 2,
  ADDIB_IMM_0_OPCODE_X0 = 1,
  ADDIB_IMM_0_OPCODE_X1 = 1,
  ADDIH_IMM_0_OPCODE_X0 = 2,
  ADDIH_IMM_0_OPCODE_X1 = 2,
  ADDI_IMM_0_OPCODE_X0 = 3,
  ADDI_IMM_0_OPCODE_X1 = 3,
  ADDI_IMM_1_OPCODE_SN = 1,
  ADDI_OPCODE_Y0 = 9,
  ADDI_OPCODE_Y1 = 7,
  ADDLIS_OPCODE_X0 = 1,
  ADDLIS_OPCODE_X1 = 2,
  ADDLI_OPCODE_X0 = 2,
  ADDLI_OPCODE_X1 = 3,
  ADDS_SPECIAL_0_OPCODE_X0 = 96,
  ADDS_SPECIAL_0_OPCODE_X1 = 66,
  ADD_SPECIAL_0_OPCODE_X0 = 3,
  ADD_SPECIAL_0_OPCODE_X1 = 3,
  ADD_SPECIAL_0_OPCODE_Y0 = 0,
  ADD_SPECIAL_0_OPCODE_Y1 = 0,
  ADIFFB_U_SPECIAL_0_OPCODE_X0 = 4,
  ADIFFH_SPECIAL_0_OPCODE_X0 = 5,
  ANDI_IMM_0_OPCODE_X0 = 1,
  ANDI_IMM_0_OPCODE_X1 = 4,
  ANDI_OPCODE_Y0 = 10,
  ANDI_OPCODE_Y1 = 8,
  AND_SPECIAL_0_OPCODE_X0 = 6,
  AND_SPECIAL_0_OPCODE_X1 = 4,
  AND_SPECIAL_2_OPCODE_Y0 = 0,
  AND_SPECIAL_2_OPCODE_Y1 = 0,
  AULI_OPCODE_X0 = 3,
  AULI_OPCODE_X1 = 4,
  AVGB_U_SPECIAL_0_OPCODE_X0 = 7,
  AVGH_SPECIAL_0_OPCODE_X0 = 8,
  BBNST_BRANCH_OPCODE_X1 = 15,
  BBNS_BRANCH_OPCODE_X1 = 14,
  BBNS_OPCODE_SN = 63,
  BBST_BRANCH_OPCODE_X1 = 13,
  BBS_BRANCH_OPCODE_X1 = 12,
  BBS_OPCODE_SN = 62,
  BGEZT_BRANCH_OPCODE_X1 = 7,
  BGEZ_BRANCH_OPCODE_X1 = 6,
  BGEZ_OPCODE_SN = 61,
  BGZT_BRANCH_OPCODE_X1 = 5,
  BGZ_BRANCH_OPCODE_X1 = 4,
  BGZ_OPCODE_SN = 58,
  BITX_UN_0_SHUN_0_OPCODE_X0 = 1,
  BITX_UN_0_SHUN_0_OPCODE_Y0 = 1,
  BLEZT_BRANCH_OPCODE_X1 = 11,
  BLEZ_BRANCH_OPCODE_X1 = 10,
  BLEZ_OPCODE_SN = 59,
  BLZT_BRANCH_OPCODE_X1 = 9,
  BLZ_BRANCH_OPCODE_X1 = 8,
  BLZ_OPCODE_SN = 60,
  BNZT_BRANCH_OPCODE_X1 = 3,
  BNZ_BRANCH_OPCODE_X1 = 2,
  BNZ_OPCODE_SN = 57,
  BPT_NOREG_RR_IMM_0_OPCODE_SN = 1,
  BRANCH_OPCODE_X1 = 5,
  BYTEX_UN_0_SHUN_0_OPCODE_X0 = 2,
  BYTEX_UN_0_SHUN_0_OPCODE_Y0 = 2,
  BZT_BRANCH_OPCODE_X1 = 1,
  BZ_BRANCH_OPCODE_X1 = 0,
  BZ_OPCODE_SN = 56,
  CLZ_UN_0_SHUN_0_OPCODE_X0 = 3,
  CLZ_UN_0_SHUN_0_OPCODE_Y0 = 3,
  CRC32_32_SPECIAL_0_OPCODE_X0 = 9,
  CRC32_8_SPECIAL_0_OPCODE_X0 = 10,
  CTZ_UN_0_SHUN_0_OPCODE_X0 = 4,
  CTZ_UN_0_SHUN_0_OPCODE_Y0 = 4,
  DRAIN_UN_0_SHUN_0_OPCODE_X1 = 1,
  DTLBPR_UN_0_SHUN_0_OPCODE_X1 = 2,
  DWORD_ALIGN_SPECIAL_0_OPCODE_X0 = 95,
  FINV_UN_0_SHUN_0_OPCODE_X1 = 3,
  FLUSH_UN_0_SHUN_0_OPCODE_X1 = 4,
  FNOP_NOREG_RR_IMM_0_OPCODE_SN = 3,
  FNOP_UN_0_SHUN_0_OPCODE_X0 = 5,
  FNOP_UN_0_SHUN_0_OPCODE_X1 = 5,
  FNOP_UN_0_SHUN_0_OPCODE_Y0 = 5,
  FNOP_UN_0_SHUN_0_OPCODE_Y1 = 1,
  HALT_NOREG_RR_IMM_0_OPCODE_SN = 0,
  ICOH_UN_0_SHUN_0_OPCODE_X1 = 6,
  ILL_UN_0_SHUN_0_OPCODE_X1 = 7,
  ILL_UN_0_SHUN_0_OPCODE_Y1 = 2,
  IMM_0_OPCODE_SN = 0,
  IMM_0_OPCODE_X0 = 4,
  IMM_0_OPCODE_X1 = 6,
  IMM_1_OPCODE_SN = 1,
  IMM_OPCODE_0_X0 = 5,
  INTHB_SPECIAL_0_OPCODE_X0 = 11,
  INTHB_SPECIAL_0_OPCODE_X1 = 5,
  INTHH_SPECIAL_0_OPCODE_X0 = 12,
  INTHH_SPECIAL_0_OPCODE_X1 = 6,
  INTLB_SPECIAL_0_OPCODE_X0 = 13,
  INTLB_SPECIAL_0_OPCODE_X1 = 7,
  INTLH_SPECIAL_0_OPCODE_X0 = 14,
  INTLH_SPECIAL_0_OPCODE_X1 = 8,
  INV_UN_0_SHUN_0_OPCODE_X1 = 8,
  IRET_UN_0_SHUN_0_OPCODE_X1 = 9,
  JALB_OPCODE_X1 = 13,
  JALF_OPCODE_X1 = 12,
  JALRP_SPECIAL_0_OPCODE_X1 = 9,
  JALRR_IMM_1_OPCODE_SN = 3,
  JALR_RR_IMM_0_OPCODE_SN = 5,
  JALR_SPECIAL_0_OPCODE_X1 = 10,
  JB_OPCODE_X1 = 11,
  JF_OPCODE_X1 = 10,
  JRP_SPECIAL_0_OPCODE_X1 = 11,
  JRR_IMM_1_OPCODE_SN = 2,
  JR_RR_IMM_0_OPCODE_SN = 4,
  JR_SPECIAL_0_OPCODE_X1 = 12,
  LBADD_IMM_0_OPCODE_X1 = 22,
  LBADD_U_IMM_0_OPCODE_X1 = 23,
  LB_OPCODE_Y2 = 0,
  LB_UN_0_SHUN_0_OPCODE_X1 = 10,
  LB_U_OPCODE_Y2 = 1,
  LB_U_UN_0_SHUN_0_OPCODE_X1 = 11,
  LHADD_IMM_0_OPCODE_X1 = 24,
  LHADD_U_IMM_0_OPCODE_X1 = 25,
  LH_OPCODE_Y2 = 2,
  LH_UN_0_SHUN_0_OPCODE_X1 = 12,
  LH_U_OPCODE_Y2 = 3,
  LH_U_UN_0_SHUN_0_OPCODE_X1 = 13,
  LNK_SPECIAL_0_OPCODE_X1 = 13,
  LWADD_IMM_0_OPCODE_X1 = 26,
  LWADD_NA_IMM_0_OPCODE_X1 = 27,
  LW_NA_UN_0_SHUN_0_OPCODE_X1 = 24,
  LW_OPCODE_Y2 = 4,
  LW_UN_0_SHUN_0_OPCODE_X1 = 14,
  MAXB_U_SPECIAL_0_OPCODE_X0 = 15,
  MAXB_U_SPECIAL_0_OPCODE_X1 = 14,
  MAXH_SPECIAL_0_OPCODE_X0 = 16,
  MAXH_SPECIAL_0_OPCODE_X1 = 15,
  MAXIB_U_IMM_0_OPCODE_X0 = 4,
  MAXIB_U_IMM_0_OPCODE_X1 = 5,
  MAXIH_IMM_0_OPCODE_X0 = 5,
  MAXIH_IMM_0_OPCODE_X1 = 6,
  MFSPR_IMM_0_OPCODE_X1 = 7,
  MF_UN_0_SHUN_0_OPCODE_X1 = 15,
  MINB_U_SPECIAL_0_OPCODE_X0 = 17,
  MINB_U_SPECIAL_0_OPCODE_X1 = 16,
  MINH_SPECIAL_0_OPCODE_X0 = 18,
  MINH_SPECIAL_0_OPCODE_X1 = 17,
  MINIB_U_IMM_0_OPCODE_X0 = 6,
  MINIB_U_IMM_0_OPCODE_X1 = 8,
  MINIH_IMM_0_OPCODE_X0 = 7,
  MINIH_IMM_0_OPCODE_X1 = 9,
  MM_OPCODE_X0 = 6,
  MM_OPCODE_X1 = 7,
  MNZB_SPECIAL_0_OPCODE_X0 = 19,
  MNZB_SPECIAL_0_OPCODE_X1 = 18,
  MNZH_SPECIAL_0_OPCODE_X0 = 20,
  MNZH_SPECIAL_0_OPCODE_X1 = 19,
  MNZ_SPECIAL_0_OPCODE_X0 = 21,
  MNZ_SPECIAL_0_OPCODE_X1 = 20,
  MNZ_SPECIAL_1_OPCODE_Y0 = 0,
  MNZ_SPECIAL_1_OPCODE_Y1 = 1,
  MOVEI_IMM_1_OPCODE_SN = 0,
  MOVE_RR_IMM_0_OPCODE_SN = 8,
  MTSPR_IMM_0_OPCODE_X1 = 10,
  MULHHA_SS_SPECIAL_0_OPCODE_X0 = 22,
  MULHHA_SS_SPECIAL_7_OPCODE_Y0 = 0,
  MULHHA_SU_SPECIAL_0_OPCODE_X0 = 23,
  MULHHA_UU_SPECIAL_0_OPCODE_X0 = 24,
  MULHHA_UU_SPECIAL_7_OPCODE_Y0 = 1,
  MULHHSA_UU_SPECIAL_0_OPCODE_X0 = 25,
  MULHH_SS_SPECIAL_0_OPCODE_X0 = 26,
  MULHH_SS_SPECIAL_6_OPCODE_Y0 = 0,
  MULHH_SU_SPECIAL_0_OPCODE_X0 = 27,
  MULHH_UU_SPECIAL_0_OPCODE_X0 = 28,
  MULHH_UU_SPECIAL_6_OPCODE_Y0 = 1,
  MULHLA_SS_SPECIAL_0_OPCODE_X0 = 29,
  MULHLA_SU_SPECIAL_0_OPCODE_X0 = 30,
  MULHLA_US_SPECIAL_0_OPCODE_X0 = 31,
  MULHLA_UU_SPECIAL_0_OPCODE_X0 = 32,
  MULHLSA_UU_SPECIAL_0_OPCODE_X0 = 33,
  MULHLSA_UU_SPECIAL_5_OPCODE_Y0 = 0,
  MULHL_SS_SPECIAL_0_OPCODE_X0 = 34,
  MULHL_SU_SPECIAL_0_OPCODE_X0 = 35,
  MULHL_US_SPECIAL_0_OPCODE_X0 = 36,
  MULHL_UU_SPECIAL_0_OPCODE_X0 = 37,
  MULLLA_SS_SPECIAL_0_OPCODE_X0 = 38,
  MULLLA_SS_SPECIAL_7_OPCODE_Y0 = 2,
  MULLLA_SU_SPECIAL_0_OPCODE_X0 = 39,
  MULLLA_UU_SPECIAL_0_OPCODE_X0 = 40,
  MULLLA_UU_SPECIAL_7_OPCODE_Y0 = 3,
  MULLLSA_UU_SPECIAL_0_OPCODE_X0 = 41,
  MULLL_SS_SPECIAL_0_OPCODE_X0 = 42,
  MULLL_SS_SPECIAL_6_OPCODE_Y0 = 2,
  MULLL_SU_SPECIAL_0_OPCODE_X0 = 43,
  MULLL_UU_SPECIAL_0_OPCODE_X0 = 44,
  MULLL_UU_SPECIAL_6_OPCODE_Y0 = 3,
  MVNZ_SPECIAL_0_OPCODE_X0 = 45,
  MVNZ_SPECIAL_1_OPCODE_Y0 = 1,
  MVZ_SPECIAL_0_OPCODE_X0 = 46,
  MVZ_SPECIAL_1_OPCODE_Y0 = 2,
  MZB_SPECIAL_0_OPCODE_X0 = 47,
  MZB_SPECIAL_0_OPCODE_X1 = 21,
  MZH_SPECIAL_0_OPCODE_X0 = 48,
  MZH_SPECIAL_0_OPCODE_X1 = 22,
  MZ_SPECIAL_0_OPCODE_X0 = 49,
  MZ_SPECIAL_0_OPCODE_X1 = 23,
  MZ_SPECIAL_1_OPCODE_Y0 = 3,
  MZ_SPECIAL_1_OPCODE_Y1 = 2,
  NAP_UN_0_SHUN_0_OPCODE_X1 = 16,
  NOP_NOREG_RR_IMM_0_OPCODE_SN = 2,
  NOP_UN_0_SHUN_0_OPCODE_X0 = 6,
  NOP_UN_0_SHUN_0_OPCODE_X1 = 17,
  NOP_UN_0_SHUN_0_OPCODE_Y0 = 6,
  NOP_UN_0_SHUN_0_OPCODE_Y1 = 3,
  NOREG_RR_IMM_0_OPCODE_SN = 0,
  NOR_SPECIAL_0_OPCODE_X0 = 50,
  NOR_SPECIAL_0_OPCODE_X1 = 24,
  NOR_SPECIAL_2_OPCODE_Y0 = 1,
  NOR_SPECIAL_2_OPCODE_Y1 = 1,
  ORI_IMM_0_OPCODE_X0 = 8,
  ORI_IMM_0_OPCODE_X1 = 11,
  ORI_OPCODE_Y0 = 11,
  ORI_OPCODE_Y1 = 9,
  OR_SPECIAL_0_OPCODE_X0 = 51,
  OR_SPECIAL_0_OPCODE_X1 = 25,
  OR_SPECIAL_2_OPCODE_Y0 = 2,
  OR_SPECIAL_2_OPCODE_Y1 = 2,
  PACKBS_U_SPECIAL_0_OPCODE_X0 = 103,
  PACKBS_U_SPECIAL_0_OPCODE_X1 = 73,
  PACKHB_SPECIAL_0_OPCODE_X0 = 52,
  PACKHB_SPECIAL_0_OPCODE_X1 = 26,
  PACKHS_SPECIAL_0_OPCODE_X0 = 102,
  PACKHS_SPECIAL_0_OPCODE_X1 = 72,
  PACKLB_SPECIAL_0_OPCODE_X0 = 53,
  PACKLB_SPECIAL_0_OPCODE_X1 = 27,
  PCNT_UN_0_SHUN_0_OPCODE_X0 = 7,
  PCNT_UN_0_SHUN_0_OPCODE_Y0 = 7,
  RLI_SHUN_0_OPCODE_X0 = 1,
  RLI_SHUN_0_OPCODE_X1 = 1,
  RLI_SHUN_0_OPCODE_Y0 = 1,
  RLI_SHUN_0_OPCODE_Y1 = 1,
  RL_SPECIAL_0_OPCODE_X0 = 54,
  RL_SPECIAL_0_OPCODE_X1 = 28,
  RL_SPECIAL_3_OPCODE_Y0 = 0,
  RL_SPECIAL_3_OPCODE_Y1 = 0,
  RR_IMM_0_OPCODE_SN = 0,
  S1A_SPECIAL_0_OPCODE_X0 = 55,
  S1A_SPECIAL_0_OPCODE_X1 = 29,
  S1A_SPECIAL_0_OPCODE_Y0 = 1,
  S1A_SPECIAL_0_OPCODE_Y1 = 1,
  S2A_SPECIAL_0_OPCODE_X0 = 56,
  S2A_SPECIAL_0_OPCODE_X1 = 30,
  S2A_SPECIAL_0_OPCODE_Y0 = 2,
  S2A_SPECIAL_0_OPCODE_Y1 = 2,
  S3A_SPECIAL_0_OPCODE_X0 = 57,
  S3A_SPECIAL_0_OPCODE_X1 = 31,
  S3A_SPECIAL_5_OPCODE_Y0 = 1,
  S3A_SPECIAL_5_OPCODE_Y1 = 1,
  SADAB_U_SPECIAL_0_OPCODE_X0 = 58,
  SADAH_SPECIAL_0_OPCODE_X0 = 59,
  SADAH_U_SPECIAL_0_OPCODE_X0 = 60,
  SADB_U_SPECIAL_0_OPCODE_X0 = 61,
  SADH_SPECIAL_0_OPCODE_X0 = 62,
  SADH_U_SPECIAL_0_OPCODE_X0 = 63,
  SBADD_IMM_0_OPCODE_X1 = 28,
  SB_OPCODE_Y2 = 5,
  SB_SPECIAL_0_OPCODE_X1 = 32,
  SEQB_SPECIAL_0_OPCODE_X0 = 64,
  SEQB_SPECIAL_0_OPCODE_X1 = 33,
  SEQH_SPECIAL_0_OPCODE_X0 = 65,
  SEQH_SPECIAL_0_OPCODE_X1 = 34,
  SEQIB_IMM_0_OPCODE_X0 = 9,
  SEQIB_IMM_0_OPCODE_X1 = 12,
  SEQIH_IMM_0_OPCODE_X0 = 10,
  SEQIH_IMM_0_OPCODE_X1 = 13,
  SEQI_IMM_0_OPCODE_X0 = 11,
  SEQI_IMM_0_OPCODE_X1 = 14,
  SEQI_OPCODE_Y0 = 12,
  SEQI_OPCODE_Y1 = 10,
  SEQ_SPECIAL_0_OPCODE_X0 = 66,
  SEQ_SPECIAL_0_OPCODE_X1 = 35,
  SEQ_SPECIAL_5_OPCODE_Y0 = 2,
  SEQ_SPECIAL_5_OPCODE_Y1 = 2,
  SHADD_IMM_0_OPCODE_X1 = 29,
  SHL8II_IMM_0_OPCODE_SN = 3,
  SHLB_SPECIAL_0_OPCODE_X0 = 67,
  SHLB_SPECIAL_0_OPCODE_X1 = 36,
  SHLH_SPECIAL_0_OPCODE_X0 = 68,
  SHLH_SPECIAL_0_OPCODE_X1 = 37,
  SHLIB_SHUN_0_OPCODE_X0 = 2,
  SHLIB_SHUN_0_OPCODE_X1 = 2,
  SHLIH_SHUN_0_OPCODE_X0 = 3,
  SHLIH_SHUN_0_OPCODE_X1 = 3,
  SHLI_SHUN_0_OPCODE_X0 = 4,
  SHLI_SHUN_0_OPCODE_X1 = 4,
  SHLI_SHUN_0_OPCODE_Y0 = 2,
  SHLI_SHUN_0_OPCODE_Y1 = 2,
  SHL_SPECIAL_0_OPCODE_X0 = 69,
  SHL_SPECIAL_0_OPCODE_X1 = 38,
  SHL_SPECIAL_3_OPCODE_Y0 = 1,
  SHL_SPECIAL_3_OPCODE_Y1 = 1,
  SHR1_RR_IMM_0_OPCODE_SN = 9,
  SHRB_SPECIAL_0_OPCODE_X0 = 70,
  SHRB_SPECIAL_0_OPCODE_X1 = 39,
  SHRH_SPECIAL_0_OPCODE_X0 = 71,
  SHRH_SPECIAL_0_OPCODE_X1 = 40,
  SHRIB_SHUN_0_OPCODE_X0 = 5,
  SHRIB_SHUN_0_OPCODE_X1 = 5,
  SHRIH_SHUN_0_OPCODE_X0 = 6,
  SHRIH_SHUN_0_OPCODE_X1 = 6,
  SHRI_SHUN_0_OPCODE_X0 = 7,
  SHRI_SHUN_0_OPCODE_X1 = 7,
  SHRI_SHUN_0_OPCODE_Y0 = 3,
  SHRI_SHUN_0_OPCODE_Y1 = 3,
  SHR_SPECIAL_0_OPCODE_X0 = 72,
  SHR_SPECIAL_0_OPCODE_X1 = 41,
  SHR_SPECIAL_3_OPCODE_Y0 = 2,
  SHR_SPECIAL_3_OPCODE_Y1 = 2,
  SHUN_0_OPCODE_X0 = 7,
  SHUN_0_OPCODE_X1 = 8,
  SHUN_0_OPCODE_Y0 = 13,
  SHUN_0_OPCODE_Y1 = 11,
  SH_OPCODE_Y2 = 6,
  SH_SPECIAL_0_OPCODE_X1 = 42,
  SLTB_SPECIAL_0_OPCODE_X0 = 73,
  SLTB_SPECIAL_0_OPCODE_X1 = 43,
  SLTB_U_SPECIAL_0_OPCODE_X0 = 74,
  SLTB_U_SPECIAL_0_OPCODE_X1 = 44,
  SLTEB_SPECIAL_0_OPCODE_X0 = 75,
  SLTEB_SPECIAL_0_OPCODE_X1 = 45,
  SLTEB_U_SPECIAL_0_OPCODE_X0 = 76,
  SLTEB_U_SPECIAL_0_OPCODE_X1 = 46,
  SLTEH_SPECIAL_0_OPCODE_X0 = 77,
  SLTEH_SPECIAL_0_OPCODE_X1 = 47,
  SLTEH_U_SPECIAL_0_OPCODE_X0 = 78,
  SLTEH_U_SPECIAL_0_OPCODE_X1 = 48,
  SLTE_SPECIAL_0_OPCODE_X0 = 79,
  SLTE_SPECIAL_0_OPCODE_X1 = 49,
  SLTE_SPECIAL_4_OPCODE_Y0 = 0,
  SLTE_SPECIAL_4_OPCODE_Y1 = 0,
  SLTE_U_SPECIAL_0_OPCODE_X0 = 80,
  SLTE_U_SPECIAL_0_OPCODE_X1 = 50,
  SLTE_U_SPECIAL_4_OPCODE_Y0 = 1,
  SLTE_U_SPECIAL_4_OPCODE_Y1 = 1,
  SLTH_SPECIAL_0_OPCODE_X0 = 81,
  SLTH_SPECIAL_0_OPCODE_X1 = 51,
  SLTH_U_SPECIAL_0_OPCODE_X0 = 82,
  SLTH_U_SPECIAL_0_OPCODE_X1 = 52,
  SLTIB_IMM_0_OPCODE_X0 = 12,
  SLTIB_IMM_0_OPCODE_X1 = 15,
  SLTIB_U_IMM_0_OPCODE_X0 = 13,
  SLTIB_U_IMM_0_OPCODE_X1 = 16,
  SLTIH_IMM_0_OPCODE_X0 = 14,
  SLTIH_IMM_0_OPCODE_X1 = 17,
  SLTIH_U_IMM_0_OPCODE_X0 = 15,
  SLTIH_U_IMM_0_OPCODE_X1 = 18,
  SLTI_IMM_0_OPCODE_X0 = 16,
  SLTI_IMM_0_OPCODE_X1 = 19,
  SLTI_OPCODE_Y0 = 14,
  SLTI_OPCODE_Y1 = 12,
  SLTI_U_IMM_0_OPCODE_X0 = 17,
  SLTI_U_IMM_0_OPCODE_X1 = 20,
  SLTI_U_OPCODE_Y0 = 15,
  SLTI_U_OPCODE_Y1 = 13,
  SLT_SPECIAL_0_OPCODE_X0 = 83,
  SLT_SPECIAL_0_OPCODE_X1 = 53,
  SLT_SPECIAL_4_OPCODE_Y0 = 2,
  SLT_SPECIAL_4_OPCODE_Y1 = 2,
  SLT_U_SPECIAL_0_OPCODE_X0 = 84,
  SLT_U_SPECIAL_0_OPCODE_X1 = 54,
  SLT_U_SPECIAL_4_OPCODE_Y0 = 3,
  SLT_U_SPECIAL_4_OPCODE_Y1 = 3,
  SNEB_SPECIAL_0_OPCODE_X0 = 85,
  SNEB_SPECIAL_0_OPCODE_X1 = 55,
  SNEH_SPECIAL_0_OPCODE_X0 = 86,
  SNEH_SPECIAL_0_OPCODE_X1 = 56,
  SNE_SPECIAL_0_OPCODE_X0 = 87,
  SNE_SPECIAL_0_OPCODE_X1 = 57,
  SNE_SPECIAL_5_OPCODE_Y0 = 3,
  SNE_SPECIAL_5_OPCODE_Y1 = 3,
  SPECIAL_0_OPCODE_X0 = 0,
  SPECIAL_0_OPCODE_X1 = 1,
  SPECIAL_0_OPCODE_Y0 = 1,
  SPECIAL_0_OPCODE_Y1 = 1,
  SPECIAL_1_OPCODE_Y0 = 2,
  SPECIAL_1_OPCODE_Y1 = 2,
  SPECIAL_2_OPCODE_Y0 = 3,
  SPECIAL_2_OPCODE_Y1 = 3,
  SPECIAL_3_OPCODE_Y0 = 4,
  SPECIAL_3_OPCODE_Y1 = 4,
  SPECIAL_4_OPCODE_Y0 = 5,
  SPECIAL_4_OPCODE_Y1 = 5,
  SPECIAL_5_OPCODE_Y0 = 6,
  SPECIAL_5_OPCODE_Y1 = 6,
  SPECIAL_6_OPCODE_Y0 = 7,
  SPECIAL_7_OPCODE_Y0 = 8,
  SRAB_SPECIAL_0_OPCODE_X0 = 88,
  SRAB_SPECIAL_0_OPCODE_X1 = 58,
  SRAH_SPECIAL_0_OPCODE_X0 = 89,
  SRAH_SPECIAL_0_OPCODE_X1 = 59,
  SRAIB_SHUN_0_OPCODE_X0 = 8,
  SRAIB_SHUN_0_OPCODE_X1 = 8,
  SRAIH_SHUN_0_OPCODE_X0 = 9,
  SRAIH_SHUN_0_OPCODE_X1 = 9,
  SRAI_SHUN_0_OPCODE_X0 = 10,
  SRAI_SHUN_0_OPCODE_X1 = 10,
  SRAI_SHUN_0_OPCODE_Y0 = 4,
  SRAI_SHUN_0_OPCODE_Y1 = 4,
  SRA_SPECIAL_0_OPCODE_X0 = 90,
  SRA_SPECIAL_0_OPCODE_X1 = 60,
  SRA_SPECIAL_3_OPCODE_Y0 = 3,
  SRA_SPECIAL_3_OPCODE_Y1 = 3,
  SUBBS_U_SPECIAL_0_OPCODE_X0 = 100,
  SUBBS_U_SPECIAL_0_OPCODE_X1 = 70,
  SUBB_SPECIAL_0_OPCODE_X0 = 91,
  SUBB_SPECIAL_0_OPCODE_X1 = 61,
  SUBHS_SPECIAL_0_OPCODE_X0 = 101,
  SUBHS_SPECIAL_0_OPCODE_X1 = 71,
  SUBH_SPECIAL_0_OPCODE_X0 = 92,
  SUBH_SPECIAL_0_OPCODE_X1 = 62,
  SUBS_SPECIAL_0_OPCODE_X0 = 97,
  SUBS_SPECIAL_0_OPCODE_X1 = 67,
  SUB_SPECIAL_0_OPCODE_X0 = 93,
  SUB_SPECIAL_0_OPCODE_X1 = 63,
  SUB_SPECIAL_0_OPCODE_Y0 = 3,
  SUB_SPECIAL_0_OPCODE_Y1 = 3,
  SWADD_IMM_0_OPCODE_X1 = 30,
  SWINT0_UN_0_SHUN_0_OPCODE_X1 = 18,
  SWINT1_UN_0_SHUN_0_OPCODE_X1 = 19,
  SWINT2_UN_0_SHUN_0_OPCODE_X1 = 20,
  SWINT3_UN_0_SHUN_0_OPCODE_X1 = 21,
  SW_OPCODE_Y2 = 7,
  SW_SPECIAL_0_OPCODE_X1 = 64,
  TBLIDXB0_UN_0_SHUN_0_OPCODE_X0 = 8,
  TBLIDXB0_UN_0_SHUN_0_OPCODE_Y0 = 8,
  TBLIDXB1_UN_0_SHUN_0_OPCODE_X0 = 9,
  TBLIDXB1_UN_0_SHUN_0_OPCODE_Y0 = 9,
  TBLIDXB2_UN_0_SHUN_0_OPCODE_X0 = 10,
  TBLIDXB2_UN_0_SHUN_0_OPCODE_Y0 = 10,
  TBLIDXB3_UN_0_SHUN_0_OPCODE_X0 = 11,
  TBLIDXB3_UN_0_SHUN_0_OPCODE_Y0 = 11,
  TNS_UN_0_SHUN_0_OPCODE_X1 = 22,
  UN_0_SHUN_0_OPCODE_X0 = 11,
  UN_0_SHUN_0_OPCODE_X1 = 11,
  UN_0_SHUN_0_OPCODE_Y0 = 5,
  UN_0_SHUN_0_OPCODE_Y1 = 5,
  WH64_UN_0_SHUN_0_OPCODE_X1 = 23,
  XORI_IMM_0_OPCODE_X0 = 2,
  XORI_IMM_0_OPCODE_X1 = 21,
  XOR_SPECIAL_0_OPCODE_X0 = 94,
  XOR_SPECIAL_0_OPCODE_X1 = 65,
  XOR_SPECIAL_2_OPCODE_Y0 = 3,
  XOR_SPECIAL_2_OPCODE_Y1 = 3
};


#endif /* __ASSEMBLER__ */

#endif /* __ARCH_OPCODE_H__ */

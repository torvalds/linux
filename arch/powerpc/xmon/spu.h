/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPU ELF support for BFD.

   Copyright 2006 Free Software Foundation, Inc.

   This file is part of GDB, GAS, and the GNU binutils.

 */


/* These two enums are from rel_apu/common/spu_asm_format.h */
/* definition of instruction format */
typedef enum {
  RRR,
  RI18,
  RI16,
  RI10,
  RI8,
  RI7,
  RR,
  LBT,
  LBTI,
  IDATA,
  UNKNOWN_IFORMAT
} spu_iformat;

/* These values describe assembly instruction arguments.  They indicate
 * how to encode, range checking and which relocation to use. */
typedef enum {
  A_T,  /* register at pos 0 */
  A_A,  /* register at pos 7 */
  A_B,  /* register at pos 14 */
  A_C,  /* register at pos 21 */
  A_S,  /* special purpose register at pos 7 */
  A_H,  /* channel register at pos 7 */
  A_P,  /* parenthesis, this has to separate regs from immediates */
  A_S3,
  A_S6,
  A_S7N,
  A_S7,
  A_U7A,
  A_U7B,
  A_S10B,
  A_S10,
  A_S11,
  A_S11I,
  A_S14,
  A_S16,
  A_S18,
  A_R18,
  A_U3,
  A_U5,
  A_U6,
  A_U7,
  A_U14,
  A_X16,
  A_U18,
  A_MAX
} spu_aformat;

enum spu_insns {
#define APUOP(TAG,MACFORMAT,OPCODE,MNEMONIC,ASMFORMAT,DEP,PIPE) \
	TAG,
#define APUOPFB(TAG,MACFORMAT,OPCODE,FB,MNEMONIC,ASMFORMAT,DEP,PIPE) \
	TAG,
#include "spu-insns.h"
#undef APUOP
#undef APUOPFB
        M_SPU_MAX
};

struct spu_opcode
{
   spu_iformat insn_type;
   unsigned int opcode;
   char *mnemonic;
   int arg[5];
};

#define SIGNED_EXTRACT(insn,size,pos) (((int)((insn) << (32-size-pos))) >> (32-size))
#define UNSIGNED_EXTRACT(insn,size,pos) (((insn) >> pos) & ((1 << size)-1))

#define DECODE_INSN_RT(insn) (insn & 0x7f)
#define DECODE_INSN_RA(insn) ((insn >> 7) & 0x7f)
#define DECODE_INSN_RB(insn) ((insn >> 14) & 0x7f)
#define DECODE_INSN_RC(insn) ((insn >> 21) & 0x7f)

#define DECODE_INSN_I10(insn) SIGNED_EXTRACT(insn,10,14)
#define DECODE_INSN_U10(insn) UNSIGNED_EXTRACT(insn,10,14)

/* For branching, immediate loads, hbr and  lqa/stqa. */
#define DECODE_INSN_I16(insn) SIGNED_EXTRACT(insn,16,7)
#define DECODE_INSN_U16(insn) UNSIGNED_EXTRACT(insn,16,7)

/* for stop */
#define DECODE_INSN_U14(insn) UNSIGNED_EXTRACT(insn,14,0)

/* For ila */
#define DECODE_INSN_I18(insn) SIGNED_EXTRACT(insn,18,7)
#define DECODE_INSN_U18(insn) UNSIGNED_EXTRACT(insn,18,7)

/* For rotate and shift and generate control mask */
#define DECODE_INSN_I7(insn) SIGNED_EXTRACT(insn,7,14)
#define DECODE_INSN_U7(insn) UNSIGNED_EXTRACT(insn,7,14)

/* For float <-> int conversion */
#define DECODE_INSN_I8(insn)  SIGNED_EXTRACT(insn,8,14)
#define DECODE_INSN_U8(insn) UNSIGNED_EXTRACT(insn,8,14)

/* For hbr  */
#define DECODE_INSN_I9a(insn) ((SIGNED_EXTRACT(insn,2,23) << 7) | UNSIGNED_EXTRACT(insn,7,0))
#define DECODE_INSN_I9b(insn) ((SIGNED_EXTRACT(insn,2,14) << 7) | UNSIGNED_EXTRACT(insn,7,0))
#define DECODE_INSN_U9a(insn) ((UNSIGNED_EXTRACT(insn,2,23) << 7) | UNSIGNED_EXTRACT(insn,7,0))
#define DECODE_INSN_U9b(insn) ((UNSIGNED_EXTRACT(insn,2,14) << 7) | UNSIGNED_EXTRACT(insn,7,0))


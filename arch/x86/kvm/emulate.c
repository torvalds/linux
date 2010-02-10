/******************************************************************************
 * emulate.c
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * Linux coding style, mod r/m decoder, segment base fixes, real-mode
 * privileged instructions:
 *
 * Copyright (C) 2006 Qumranet
 *
 *   Avi Kivity <avi@qumranet.com>
 *   Yaniv Kamay <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */

#ifndef __KERNEL__
#include <stdio.h>
#include <stdint.h>
#include <public/xen.h>
#define DPRINTF(_f, _a ...) printf(_f , ## _a)
#else
#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"
#define DPRINTF(x...) do {} while (0)
#endif
#include <linux/module.h>
#include <asm/kvm_emulate.h>

#include "x86.h"

/*
 * Opcode effective-address decode tables.
 * Note that we only emulate instructions that have at least one memory
 * operand (excluding implicit stack references). We assume that stack
 * references and instruction fetches will never occur in special memory
 * areas that require emulation. So, for example, 'mov <imm>,<reg>' need
 * not be handled.
 */

/* Operand sizes: 8-bit operands or specified/overridden size. */
#define ByteOp      (1<<0)	/* 8-bit operands. */
/* Destination operand type. */
#define ImplicitOps (1<<1)	/* Implicit in opcode. No generic decode. */
#define DstReg      (2<<1)	/* Register operand. */
#define DstMem      (3<<1)	/* Memory operand. */
#define DstAcc      (4<<1)      /* Destination Accumulator */
#define DstMask     (7<<1)
/* Source operand type. */
#define SrcNone     (0<<4)	/* No source operand. */
#define SrcImplicit (0<<4)	/* Source operand is implicit in the opcode. */
#define SrcReg      (1<<4)	/* Register operand. */
#define SrcMem      (2<<4)	/* Memory operand. */
#define SrcMem16    (3<<4)	/* Memory operand (16-bit). */
#define SrcMem32    (4<<4)	/* Memory operand (32-bit). */
#define SrcImm      (5<<4)	/* Immediate operand. */
#define SrcImmByte  (6<<4)	/* 8-bit sign-extended immediate operand. */
#define SrcOne      (7<<4)	/* Implied '1' */
#define SrcImmUByte (8<<4)      /* 8-bit unsigned immediate operand. */
#define SrcImmU     (9<<4)      /* Immediate operand, unsigned */
#define SrcMask     (0xf<<4)
/* Generic ModRM decode. */
#define ModRM       (1<<8)
/* Destination is only written; never read. */
#define Mov         (1<<9)
#define BitOp       (1<<10)
#define MemAbs      (1<<11)      /* Memory operand is absolute displacement */
#define String      (1<<12)     /* String instruction (rep capable) */
#define Stack       (1<<13)     /* Stack instruction (push/pop) */
#define Group       (1<<14)     /* Bits 3:5 of modrm byte extend opcode */
#define GroupDual   (1<<15)     /* Alternate decoding of mod == 3 */
#define GroupMask   0xff        /* Group number stored in bits 0:7 */
/* Misc flags */
#define No64	    (1<<28)
/* Source 2 operand type */
#define Src2None    (0<<29)
#define Src2CL      (1<<29)
#define Src2ImmByte (2<<29)
#define Src2One     (3<<29)
#define Src2Imm16   (4<<29)
#define Src2Mask    (7<<29)

enum {
	Group1_80, Group1_81, Group1_82, Group1_83,
	Group1A, Group3_Byte, Group3, Group4, Group5, Group7,
	Group8, Group9,
};

static u32 opcode_table[256] = {
	/* 0x00 - 0x07 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x08 - 0x0F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, 0,
	/* 0x10 - 0x17 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x18 - 0x1F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x20 - 0x27 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	DstAcc | SrcImmByte, DstAcc | SrcImm, 0, 0,
	/* 0x28 - 0x2F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x30 - 0x37 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x38 - 0x3F */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	0, 0,
	/* 0x40 - 0x47 */
	DstReg, DstReg, DstReg, DstReg, DstReg, DstReg, DstReg, DstReg,
	/* 0x48 - 0x4F */
	DstReg, DstReg, DstReg, DstReg,	DstReg, DstReg, DstReg, DstReg,
	/* 0x50 - 0x57 */
	SrcReg | Stack, SrcReg | Stack, SrcReg | Stack, SrcReg | Stack,
	SrcReg | Stack, SrcReg | Stack, SrcReg | Stack, SrcReg | Stack,
	/* 0x58 - 0x5F */
	DstReg | Stack, DstReg | Stack, DstReg | Stack, DstReg | Stack,
	DstReg | Stack, DstReg | Stack, DstReg | Stack, DstReg | Stack,
	/* 0x60 - 0x67 */
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	0, DstReg | SrcMem32 | ModRM | Mov /* movsxd (x86/64) */ ,
	0, 0, 0, 0,
	/* 0x68 - 0x6F */
	SrcImm | Mov | Stack, 0, SrcImmByte | Mov | Stack, 0,
	SrcNone  | ByteOp  | ImplicitOps, SrcNone  | ImplicitOps, /* insb, insw/insd */
	SrcNone  | ByteOp  | ImplicitOps, SrcNone  | ImplicitOps, /* outsb, outsw/outsd */
	/* 0x70 - 0x77 */
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	/* 0x78 - 0x7F */
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	/* 0x80 - 0x87 */
	Group | Group1_80, Group | Group1_81,
	Group | Group1_82, Group | Group1_83,
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM,
	/* 0x88 - 0x8F */
	ByteOp | DstMem | SrcReg | ModRM | Mov, DstMem | SrcReg | ModRM | Mov,
	ByteOp | DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstMem | SrcReg | ModRM | Mov, ModRM | DstReg,
	DstReg | SrcMem | ModRM | Mov, Group | Group1A,
	/* 0x90 - 0x97 */
	DstReg, DstReg, DstReg, DstReg,	DstReg, DstReg, DstReg, DstReg,
	/* 0x98 - 0x9F */
	0, 0, SrcImm | Src2Imm16 | No64, 0,
	ImplicitOps | Stack, ImplicitOps | Stack, 0, 0,
	/* 0xA0 - 0xA7 */
	ByteOp | DstReg | SrcMem | Mov | MemAbs, DstReg | SrcMem | Mov | MemAbs,
	ByteOp | DstMem | SrcReg | Mov | MemAbs, DstMem | SrcReg | Mov | MemAbs,
	ByteOp | ImplicitOps | Mov | String, ImplicitOps | Mov | String,
	ByteOp | ImplicitOps | String, ImplicitOps | String,
	/* 0xA8 - 0xAF */
	0, 0, ByteOp | ImplicitOps | Mov | String, ImplicitOps | Mov | String,
	ByteOp | ImplicitOps | Mov | String, ImplicitOps | Mov | String,
	ByteOp | ImplicitOps | String, ImplicitOps | String,
	/* 0xB0 - 0xB7 */
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	/* 0xB8 - 0xBF */
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	/* 0xC0 - 0xC7 */
	ByteOp | DstMem | SrcImm | ModRM, DstMem | SrcImmByte | ModRM,
	0, ImplicitOps | Stack, 0, 0,
	ByteOp | DstMem | SrcImm | ModRM | Mov, DstMem | SrcImm | ModRM | Mov,
	/* 0xC8 - 0xCF */
	0, 0, 0, ImplicitOps | Stack,
	ImplicitOps, SrcImmByte, ImplicitOps | No64, ImplicitOps,
	/* 0xD0 - 0xD7 */
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	0, 0, 0, 0,
	/* 0xD8 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xE7 */
	0, 0, 0, 0,
	ByteOp | SrcImmUByte, SrcImmUByte,
	ByteOp | SrcImmUByte, SrcImmUByte,
	/* 0xE8 - 0xEF */
	SrcImm | Stack, SrcImm | ImplicitOps,
	SrcImmU | Src2Imm16 | No64, SrcImmByte | ImplicitOps,
	SrcNone | ByteOp | ImplicitOps, SrcNone | ImplicitOps,
	SrcNone | ByteOp | ImplicitOps, SrcNone | ImplicitOps,
	/* 0xF0 - 0xF7 */
	0, 0, 0, 0,
	ImplicitOps, ImplicitOps, Group | Group3_Byte, Group | Group3,
	/* 0xF8 - 0xFF */
	ImplicitOps, 0, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, Group | Group4, Group | Group5,
};

static u32 twobyte_table[256] = {
	/* 0x00 - 0x0F */
	0, Group | GroupDual | Group7, 0, 0, 0, ImplicitOps, ImplicitOps, 0,
	ImplicitOps, ImplicitOps, 0, 0, 0, ImplicitOps | ModRM, 0, 0,
	/* 0x10 - 0x1F */
	0, 0, 0, 0, 0, 0, 0, 0, ImplicitOps | ModRM, 0, 0, 0, 0, 0, 0, 0,
	/* 0x20 - 0x2F */
	ModRM | ImplicitOps, ModRM, ModRM | ImplicitOps, ModRM, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x30 - 0x3F */
	ImplicitOps, 0, ImplicitOps, 0,
	ImplicitOps, ImplicitOps, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x40 - 0x47 */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x48 - 0x4F */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x50 - 0x5F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x60 - 0x6F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x70 - 0x7F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x80 - 0x8F */
	SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm,
	SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm,
	/* 0x90 - 0x9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xA0 - 0xA7 */
	ImplicitOps | Stack, ImplicitOps | Stack,
	0, DstMem | SrcReg | ModRM | BitOp,
	DstMem | SrcReg | Src2ImmByte | ModRM,
	DstMem | SrcReg | Src2CL | ModRM, 0, 0,
	/* 0xA8 - 0xAF */
	ImplicitOps | Stack, ImplicitOps | Stack,
	0, DstMem | SrcReg | ModRM | BitOp,
	DstMem | SrcReg | Src2ImmByte | ModRM,
	DstMem | SrcReg | Src2CL | ModRM,
	ModRM, 0,
	/* 0xB0 - 0xB7 */
	ByteOp | DstMem | SrcReg | ModRM, DstMem | SrcReg | ModRM, 0,
	    DstMem | SrcReg | ModRM | BitOp,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xB8 - 0xBF */
	0, 0, Group | Group8, DstMem | SrcReg | ModRM | BitOp,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xC0 - 0xCF */
	0, 0, 0, DstMem | SrcReg | ModRM | Mov,
	0, 0, 0, Group | GroupDual | Group9,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xD0 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xEF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xF0 - 0xFF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u32 group_table[] = {
	[Group1_80*8] =
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	[Group1_81*8] =
	DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	[Group1_82*8] =
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	ByteOp | DstMem | SrcImm | ModRM, ByteOp | DstMem | SrcImm | ModRM,
	[Group1_83*8] =
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	[Group1A*8] =
	DstMem | SrcNone | ModRM | Mov | Stack, 0, 0, 0, 0, 0, 0, 0,
	[Group3_Byte*8] =
	ByteOp | SrcImm | DstMem | ModRM, 0,
	ByteOp | DstMem | SrcNone | ModRM, ByteOp | DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group3*8] =
	DstMem | SrcImm | ModRM, 0,
	DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group4*8] =
	ByteOp | DstMem | SrcNone | ModRM, ByteOp | DstMem | SrcNone | ModRM,
	0, 0, 0, 0, 0, 0,
	[Group5*8] =
	DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM,
	SrcMem | ModRM | Stack, 0,
	SrcMem | ModRM | Stack, 0, SrcMem | ModRM | Stack, 0,
	[Group7*8] =
	0, 0, ModRM | SrcMem, ModRM | SrcMem,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov, SrcMem | ModRM | ByteOp,
	[Group8*8] =
	0, 0, 0, 0,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM,
	[Group9*8] =
	0, ImplicitOps | ModRM, 0, 0, 0, 0, 0, 0,
};

static u32 group2_table[] = {
	[Group7*8] =
	SrcNone | ModRM, 0, 0, SrcNone | ModRM,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov, 0,
	[Group9*8] =
	0, 0, 0, 0, 0, 0, 0, 0,
};

/* EFLAGS bit definitions. */
#define EFLG_VM (1<<17)
#define EFLG_RF (1<<16)
#define EFLG_OF (1<<11)
#define EFLG_DF (1<<10)
#define EFLG_IF (1<<9)
#define EFLG_SF (1<<7)
#define EFLG_ZF (1<<6)
#define EFLG_AF (1<<4)
#define EFLG_PF (1<<2)
#define EFLG_CF (1<<0)

/*
 * Instruction emulation:
 * Most instructions are emulated directly via a fragment of inline assembly
 * code. This allows us to save/restore EFLAGS and thus very easily pick up
 * any modified flags.
 */

#if defined(CONFIG_X86_64)
#define _LO32 "k"		/* force 32-bit operand */
#define _STK  "%%rsp"		/* stack pointer */
#elif defined(__i386__)
#define _LO32 ""		/* force 32-bit operand */
#define _STK  "%%esp"		/* stack pointer */
#endif

/*
 * These EFLAGS bits are restored from saved value during emulation, and
 * any changes are written back to the saved value after emulation.
 */
#define EFLAGS_MASK (EFLG_OF|EFLG_SF|EFLG_ZF|EFLG_AF|EFLG_PF|EFLG_CF)

/* Before executing instruction: restore necessary bits in EFLAGS. */
#define _PRE_EFLAGS(_sav, _msk, _tmp)					\
	/* EFLAGS = (_sav & _msk) | (EFLAGS & ~_msk); _sav &= ~_msk; */ \
	"movl %"_sav",%"_LO32 _tmp"; "                                  \
	"push %"_tmp"; "                                                \
	"push %"_tmp"; "                                                \
	"movl %"_msk",%"_LO32 _tmp"; "                                  \
	"andl %"_LO32 _tmp",("_STK"); "                                 \
	"pushf; "                                                       \
	"notl %"_LO32 _tmp"; "                                          \
	"andl %"_LO32 _tmp",("_STK"); "                                 \
	"andl %"_LO32 _tmp","__stringify(BITS_PER_LONG/4)"("_STK"); "	\
	"pop  %"_tmp"; "                                                \
	"orl  %"_LO32 _tmp",("_STK"); "                                 \
	"popf; "                                                        \
	"pop  %"_sav"; "

/* After executing instruction: write-back necessary bits in EFLAGS. */
#define _POST_EFLAGS(_sav, _msk, _tmp) \
	/* _sav |= EFLAGS & _msk; */		\
	"pushf; "				\
	"pop  %"_tmp"; "			\
	"andl %"_msk",%"_LO32 _tmp"; "		\
	"orl  %"_LO32 _tmp",%"_sav"; "

#ifdef CONFIG_X86_64
#define ON64(x) x
#else
#define ON64(x)
#endif

#define ____emulate_2op(_op, _src, _dst, _eflags, _x, _y, _suffix)	\
	do {								\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "4", "2")			\
			_op _suffix " %"_x"3,%1; "			\
			_POST_EFLAGS("0", "4", "2")			\
			: "=m" (_eflags), "=m" ((_dst).val),		\
			  "=&r" (_tmp)					\
			: _y ((_src).val), "i" (EFLAGS_MASK));		\
	} while (0)


/* Raw emulation: instruction has two explicit operands. */
#define __emulate_2op_nobyte(_op,_src,_dst,_eflags,_wx,_wy,_lx,_ly,_qx,_qy) \
	do {								\
		unsigned long _tmp;					\
									\
		switch ((_dst).bytes) {					\
		case 2:							\
			____emulate_2op(_op,_src,_dst,_eflags,_wx,_wy,"w"); \
			break;						\
		case 4:							\
			____emulate_2op(_op,_src,_dst,_eflags,_lx,_ly,"l"); \
			break;						\
		case 8:							\
			ON64(____emulate_2op(_op,_src,_dst,_eflags,_qx,_qy,"q")); \
			break;						\
		}							\
	} while (0)

#define __emulate_2op(_op,_src,_dst,_eflags,_bx,_by,_wx,_wy,_lx,_ly,_qx,_qy) \
	do {								     \
		unsigned long _tmp;					     \
		switch ((_dst).bytes) {				             \
		case 1:							     \
			____emulate_2op(_op,_src,_dst,_eflags,_bx,_by,"b");  \
			break;						     \
		default:						     \
			__emulate_2op_nobyte(_op, _src, _dst, _eflags,	     \
					     _wx, _wy, _lx, _ly, _qx, _qy);  \
			break;						     \
		}							     \
	} while (0)

/* Source operand is byte-sized and may be restricted to just %cl. */
#define emulate_2op_SrcB(_op, _src, _dst, _eflags)                      \
	__emulate_2op(_op, _src, _dst, _eflags,				\
		      "b", "c", "b", "c", "b", "c", "b", "c")

/* Source operand is byte, word, long or quad sized. */
#define emulate_2op_SrcV(_op, _src, _dst, _eflags)                      \
	__emulate_2op(_op, _src, _dst, _eflags,				\
		      "b", "q", "w", "r", _LO32, "r", "", "r")

/* Source operand is word, long or quad sized. */
#define emulate_2op_SrcV_nobyte(_op, _src, _dst, _eflags)               \
	__emulate_2op_nobyte(_op, _src, _dst, _eflags,			\
			     "w", "r", _LO32, "r", "", "r")

/* Instruction has three operands and one operand is stored in ECX register */
#define __emulate_2op_cl(_op, _cl, _src, _dst, _eflags, _suffix, _type) 	\
	do {									\
		unsigned long _tmp;						\
		_type _clv  = (_cl).val;  					\
		_type _srcv = (_src).val;    					\
		_type _dstv = (_dst).val;					\
										\
		__asm__ __volatile__ (						\
			_PRE_EFLAGS("0", "5", "2")				\
			_op _suffix " %4,%1 \n"					\
			_POST_EFLAGS("0", "5", "2")				\
			: "=m" (_eflags), "+r" (_dstv), "=&r" (_tmp)		\
			: "c" (_clv) , "r" (_srcv), "i" (EFLAGS_MASK)		\
			); 							\
										\
		(_cl).val  = (unsigned long) _clv;				\
		(_src).val = (unsigned long) _srcv;				\
		(_dst).val = (unsigned long) _dstv;				\
	} while (0)

#define emulate_2op_cl(_op, _cl, _src, _dst, _eflags)				\
	do {									\
		switch ((_dst).bytes) {						\
		case 2:								\
			__emulate_2op_cl(_op, _cl, _src, _dst, _eflags,  	\
						"w", unsigned short);         	\
			break;							\
		case 4: 							\
			__emulate_2op_cl(_op, _cl, _src, _dst, _eflags,  	\
						"l", unsigned int);           	\
			break;							\
		case 8:								\
			ON64(__emulate_2op_cl(_op, _cl, _src, _dst, _eflags,	\
						"q", unsigned long));  		\
			break;							\
		}								\
	} while (0)

#define __emulate_1op(_op, _dst, _eflags, _suffix)			\
	do {								\
		unsigned long _tmp;					\
									\
		__asm__ __volatile__ (					\
			_PRE_EFLAGS("0", "3", "2")			\
			_op _suffix " %1; "				\
			_POST_EFLAGS("0", "3", "2")			\
			: "=m" (_eflags), "+m" ((_dst).val),		\
			  "=&r" (_tmp)					\
			: "i" (EFLAGS_MASK));				\
	} while (0)

/* Instruction has only one explicit operand (no source operand). */
#define emulate_1op(_op, _dst, _eflags)                                    \
	do {								\
		switch ((_dst).bytes) {				        \
		case 1:	__emulate_1op(_op, _dst, _eflags, "b"); break;	\
		case 2:	__emulate_1op(_op, _dst, _eflags, "w"); break;	\
		case 4:	__emulate_1op(_op, _dst, _eflags, "l"); break;	\
		case 8:	ON64(__emulate_1op(_op, _dst, _eflags, "q")); break; \
		}							\
	} while (0)

/* Fetch next part of the instruction being emulated. */
#define insn_fetch(_type, _size, _eip)                                  \
({	unsigned long _x;						\
	rc = do_insn_fetch(ctxt, ops, (_eip), &_x, (_size));		\
	if (rc != 0)							\
		goto done;						\
	(_eip) += (_size);						\
	(_type)_x;							\
})

static inline unsigned long ad_mask(struct decode_cache *c)
{
	return (1UL << (c->ad_bytes << 3)) - 1;
}

/* Access/update address held in a register, based on addressing mode. */
static inline unsigned long
address_mask(struct decode_cache *c, unsigned long reg)
{
	if (c->ad_bytes == sizeof(unsigned long))
		return reg;
	else
		return reg & ad_mask(c);
}

static inline unsigned long
register_address(struct decode_cache *c, unsigned long base, unsigned long reg)
{
	return base + address_mask(c, reg);
}

static inline void
register_address_increment(struct decode_cache *c, unsigned long *reg, int inc)
{
	if (c->ad_bytes == sizeof(unsigned long))
		*reg += inc;
	else
		*reg = (*reg & ~ad_mask(c)) | ((*reg + inc) & ad_mask(c));
}

static inline void jmp_rel(struct decode_cache *c, int rel)
{
	register_address_increment(c, &c->eip, rel);
}

static void set_seg_override(struct decode_cache *c, int seg)
{
	c->has_seg_override = true;
	c->seg_override = seg;
}

static unsigned long seg_base(struct x86_emulate_ctxt *ctxt, int seg)
{
	if (ctxt->mode == X86EMUL_MODE_PROT64 && seg < VCPU_SREG_FS)
		return 0;

	return kvm_x86_ops->get_segment_base(ctxt->vcpu, seg);
}

static unsigned long seg_override_base(struct x86_emulate_ctxt *ctxt,
				       struct decode_cache *c)
{
	if (!c->has_seg_override)
		return 0;

	return seg_base(ctxt, c->seg_override);
}

static unsigned long es_base(struct x86_emulate_ctxt *ctxt)
{
	return seg_base(ctxt, VCPU_SREG_ES);
}

static unsigned long ss_base(struct x86_emulate_ctxt *ctxt)
{
	return seg_base(ctxt, VCPU_SREG_SS);
}

static int do_fetch_insn_byte(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops,
			      unsigned long linear, u8 *dest)
{
	struct fetch_cache *fc = &ctxt->decode.fetch;
	int rc;
	int size;

	if (linear < fc->start || linear >= fc->end) {
		size = min(15UL, PAGE_SIZE - offset_in_page(linear));
		rc = ops->read_std(linear, fc->data, size, ctxt->vcpu);
		if (rc)
			return rc;
		fc->start = linear;
		fc->end = linear + size;
	}
	*dest = fc->data[linear - fc->start];
	return 0;
}

static int do_insn_fetch(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 unsigned long eip, void *dest, unsigned size)
{
	int rc = 0;

	/* x86 instructions are limited to 15 bytes. */
	if (eip + size - ctxt->decode.eip_orig > 15)
		return X86EMUL_UNHANDLEABLE;
	eip += ctxt->cs_base;
	while (size--) {
		rc = do_fetch_insn_byte(ctxt, ops, eip++, dest++);
		if (rc)
			return rc;
	}
	return 0;
}

/*
 * Given the 'reg' portion of a ModRM byte, and a register block, return a
 * pointer into the block that addresses the relevant register.
 * @highbyte_regs specifies whether to decode AH,CH,DH,BH.
 */
static void *decode_register(u8 modrm_reg, unsigned long *regs,
			     int highbyte_regs)
{
	void *p;

	p = &regs[modrm_reg];
	if (highbyte_regs && modrm_reg >= 4 && modrm_reg < 8)
		p = (unsigned char *)&regs[modrm_reg & 3] + 1;
	return p;
}

static int read_descriptor(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops,
			   void *ptr,
			   u16 *size, unsigned long *address, int op_bytes)
{
	int rc;

	if (op_bytes == 2)
		op_bytes = 3;
	*address = 0;
	rc = ops->read_std((unsigned long)ptr, (unsigned long *)size, 2,
			   ctxt->vcpu);
	if (rc)
		return rc;
	rc = ops->read_std((unsigned long)ptr + 2, address, op_bytes,
			   ctxt->vcpu);
	return rc;
}

static int test_cc(unsigned int condition, unsigned int flags)
{
	int rc = 0;

	switch ((condition & 15) >> 1) {
	case 0: /* o */
		rc |= (flags & EFLG_OF);
		break;
	case 1: /* b/c/nae */
		rc |= (flags & EFLG_CF);
		break;
	case 2: /* z/e */
		rc |= (flags & EFLG_ZF);
		break;
	case 3: /* be/na */
		rc |= (flags & (EFLG_CF|EFLG_ZF));
		break;
	case 4: /* s */
		rc |= (flags & EFLG_SF);
		break;
	case 5: /* p/pe */
		rc |= (flags & EFLG_PF);
		break;
	case 7: /* le/ng */
		rc |= (flags & EFLG_ZF);
		/* fall through */
	case 6: /* l/nge */
		rc |= (!(flags & EFLG_SF) != !(flags & EFLG_OF));
		break;
	}

	/* Odd condition identifiers (lsb == 1) have inverted sense. */
	return (!!rc ^ (condition & 1));
}

static void decode_register_operand(struct operand *op,
				    struct decode_cache *c,
				    int inhibit_bytereg)
{
	unsigned reg = c->modrm_reg;
	int highbyte_regs = c->rex_prefix == 0;

	if (!(c->d & ModRM))
		reg = (c->b & 7) | ((c->rex_prefix & 1) << 3);
	op->type = OP_REG;
	if ((c->d & ByteOp) && !inhibit_bytereg) {
		op->ptr = decode_register(reg, c->regs, highbyte_regs);
		op->val = *(u8 *)op->ptr;
		op->bytes = 1;
	} else {
		op->ptr = decode_register(reg, c->regs, 0);
		op->bytes = c->op_bytes;
		switch (op->bytes) {
		case 2:
			op->val = *(u16 *)op->ptr;
			break;
		case 4:
			op->val = *(u32 *)op->ptr;
			break;
		case 8:
			op->val = *(u64 *) op->ptr;
			break;
		}
	}
	op->orig_val = op->val;
}

static int decode_modrm(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	u8 sib;
	int index_reg = 0, base_reg = 0, scale;
	int rc = 0;

	if (c->rex_prefix) {
		c->modrm_reg = (c->rex_prefix & 4) << 1;	/* REX.R */
		index_reg = (c->rex_prefix & 2) << 2; /* REX.X */
		c->modrm_rm = base_reg = (c->rex_prefix & 1) << 3; /* REG.B */
	}

	c->modrm = insn_fetch(u8, 1, c->eip);
	c->modrm_mod |= (c->modrm & 0xc0) >> 6;
	c->modrm_reg |= (c->modrm & 0x38) >> 3;
	c->modrm_rm |= (c->modrm & 0x07);
	c->modrm_ea = 0;
	c->use_modrm_ea = 1;

	if (c->modrm_mod == 3) {
		c->modrm_ptr = decode_register(c->modrm_rm,
					       c->regs, c->d & ByteOp);
		c->modrm_val = *(unsigned long *)c->modrm_ptr;
		return rc;
	}

	if (c->ad_bytes == 2) {
		unsigned bx = c->regs[VCPU_REGS_RBX];
		unsigned bp = c->regs[VCPU_REGS_RBP];
		unsigned si = c->regs[VCPU_REGS_RSI];
		unsigned di = c->regs[VCPU_REGS_RDI];

		/* 16-bit ModR/M decode. */
		switch (c->modrm_mod) {
		case 0:
			if (c->modrm_rm == 6)
				c->modrm_ea += insn_fetch(u16, 2, c->eip);
			break;
		case 1:
			c->modrm_ea += insn_fetch(s8, 1, c->eip);
			break;
		case 2:
			c->modrm_ea += insn_fetch(u16, 2, c->eip);
			break;
		}
		switch (c->modrm_rm) {
		case 0:
			c->modrm_ea += bx + si;
			break;
		case 1:
			c->modrm_ea += bx + di;
			break;
		case 2:
			c->modrm_ea += bp + si;
			break;
		case 3:
			c->modrm_ea += bp + di;
			break;
		case 4:
			c->modrm_ea += si;
			break;
		case 5:
			c->modrm_ea += di;
			break;
		case 6:
			if (c->modrm_mod != 0)
				c->modrm_ea += bp;
			break;
		case 7:
			c->modrm_ea += bx;
			break;
		}
		if (c->modrm_rm == 2 || c->modrm_rm == 3 ||
		    (c->modrm_rm == 6 && c->modrm_mod != 0))
			if (!c->has_seg_override)
				set_seg_override(c, VCPU_SREG_SS);
		c->modrm_ea = (u16)c->modrm_ea;
	} else {
		/* 32/64-bit ModR/M decode. */
		if ((c->modrm_rm & 7) == 4) {
			sib = insn_fetch(u8, 1, c->eip);
			index_reg |= (sib >> 3) & 7;
			base_reg |= sib & 7;
			scale = sib >> 6;

			if ((base_reg & 7) == 5 && c->modrm_mod == 0)
				c->modrm_ea += insn_fetch(s32, 4, c->eip);
			else
				c->modrm_ea += c->regs[base_reg];
			if (index_reg != 4)
				c->modrm_ea += c->regs[index_reg] << scale;
		} else if ((c->modrm_rm & 7) == 5 && c->modrm_mod == 0) {
			if (ctxt->mode == X86EMUL_MODE_PROT64)
				c->rip_relative = 1;
		} else
			c->modrm_ea += c->regs[c->modrm_rm];
		switch (c->modrm_mod) {
		case 0:
			if (c->modrm_rm == 5)
				c->modrm_ea += insn_fetch(s32, 4, c->eip);
			break;
		case 1:
			c->modrm_ea += insn_fetch(s8, 1, c->eip);
			break;
		case 2:
			c->modrm_ea += insn_fetch(s32, 4, c->eip);
			break;
		}
	}
done:
	return rc;
}

static int decode_abs(struct x86_emulate_ctxt *ctxt,
		      struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = 0;

	switch (c->ad_bytes) {
	case 2:
		c->modrm_ea = insn_fetch(u16, 2, c->eip);
		break;
	case 4:
		c->modrm_ea = insn_fetch(u32, 4, c->eip);
		break;
	case 8:
		c->modrm_ea = insn_fetch(u64, 8, c->eip);
		break;
	}
done:
	return rc;
}

int
x86_decode_insn(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = 0;
	int mode = ctxt->mode;
	int def_op_bytes, def_ad_bytes, group;

	/* Shadow copy of register state. Committed on successful emulation. */

	memset(c, 0, sizeof(struct decode_cache));
	c->eip = c->eip_orig = kvm_rip_read(ctxt->vcpu);
	ctxt->cs_base = seg_base(ctxt, VCPU_SREG_CS);
	memcpy(c->regs, ctxt->vcpu->arch.regs, sizeof c->regs);

	switch (mode) {
	case X86EMUL_MODE_REAL:
	case X86EMUL_MODE_VM86:
	case X86EMUL_MODE_PROT16:
		def_op_bytes = def_ad_bytes = 2;
		break;
	case X86EMUL_MODE_PROT32:
		def_op_bytes = def_ad_bytes = 4;
		break;
#ifdef CONFIG_X86_64
	case X86EMUL_MODE_PROT64:
		def_op_bytes = 4;
		def_ad_bytes = 8;
		break;
#endif
	default:
		return -1;
	}

	c->op_bytes = def_op_bytes;
	c->ad_bytes = def_ad_bytes;

	/* Legacy prefixes. */
	for (;;) {
		switch (c->b = insn_fetch(u8, 1, c->eip)) {
		case 0x66:	/* operand-size override */
			/* switch between 2/4 bytes */
			c->op_bytes = def_op_bytes ^ 6;
			break;
		case 0x67:	/* address-size override */
			if (mode == X86EMUL_MODE_PROT64)
				/* switch between 4/8 bytes */
				c->ad_bytes = def_ad_bytes ^ 12;
			else
				/* switch between 2/4 bytes */
				c->ad_bytes = def_ad_bytes ^ 6;
			break;
		case 0x26:	/* ES override */
		case 0x2e:	/* CS override */
		case 0x36:	/* SS override */
		case 0x3e:	/* DS override */
			set_seg_override(c, (c->b >> 3) & 3);
			break;
		case 0x64:	/* FS override */
		case 0x65:	/* GS override */
			set_seg_override(c, c->b & 7);
			break;
		case 0x40 ... 0x4f: /* REX */
			if (mode != X86EMUL_MODE_PROT64)
				goto done_prefixes;
			c->rex_prefix = c->b;
			continue;
		case 0xf0:	/* LOCK */
			c->lock_prefix = 1;
			break;
		case 0xf2:	/* REPNE/REPNZ */
			c->rep_prefix = REPNE_PREFIX;
			break;
		case 0xf3:	/* REP/REPE/REPZ */
			c->rep_prefix = REPE_PREFIX;
			break;
		default:
			goto done_prefixes;
		}

		/* Any legacy prefix after a REX prefix nullifies its effect. */

		c->rex_prefix = 0;
	}

done_prefixes:

	/* REX prefix. */
	if (c->rex_prefix)
		if (c->rex_prefix & 8)
			c->op_bytes = 8;	/* REX.W */

	/* Opcode byte(s). */
	c->d = opcode_table[c->b];
	if (c->d == 0) {
		/* Two-byte opcode? */
		if (c->b == 0x0f) {
			c->twobyte = 1;
			c->b = insn_fetch(u8, 1, c->eip);
			c->d = twobyte_table[c->b];
		}
	}

	if (mode == X86EMUL_MODE_PROT64 && (c->d & No64)) {
		kvm_report_emulation_failure(ctxt->vcpu, "invalid x86/64 instruction");;
		return -1;
	}

	if (c->d & Group) {
		group = c->d & GroupMask;
		c->modrm = insn_fetch(u8, 1, c->eip);
		--c->eip;

		group = (group << 3) + ((c->modrm >> 3) & 7);
		if ((c->d & GroupDual) && (c->modrm >> 6) == 3)
			c->d = group2_table[group];
		else
			c->d = group_table[group];
	}

	/* Unrecognised? */
	if (c->d == 0) {
		DPRINTF("Cannot emulate %02x\n", c->b);
		return -1;
	}

	if (mode == X86EMUL_MODE_PROT64 && (c->d & Stack))
		c->op_bytes = 8;

	/* ModRM and SIB bytes. */
	if (c->d & ModRM)
		rc = decode_modrm(ctxt, ops);
	else if (c->d & MemAbs)
		rc = decode_abs(ctxt, ops);
	if (rc)
		goto done;

	if (!c->has_seg_override)
		set_seg_override(c, VCPU_SREG_DS);

	if (!(!c->twobyte && c->b == 0x8d))
		c->modrm_ea += seg_override_base(ctxt, c);

	if (c->ad_bytes != 8)
		c->modrm_ea = (u32)c->modrm_ea;
	/*
	 * Decode and fetch the source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & SrcMask) {
	case SrcNone:
		break;
	case SrcReg:
		decode_register_operand(&c->src, c, 0);
		break;
	case SrcMem16:
		c->src.bytes = 2;
		goto srcmem_common;
	case SrcMem32:
		c->src.bytes = 4;
		goto srcmem_common;
	case SrcMem:
		c->src.bytes = (c->d & ByteOp) ? 1 :
							   c->op_bytes;
		/* Don't fetch the address for invlpg: it could be unmapped. */
		if (c->twobyte && c->b == 0x01 && c->modrm_reg == 7)
			break;
	srcmem_common:
		/*
		 * For instructions with a ModR/M byte, switch to register
		 * access if Mod = 3.
		 */
		if ((c->d & ModRM) && c->modrm_mod == 3) {
			c->src.type = OP_REG;
			c->src.val = c->modrm_val;
			c->src.ptr = c->modrm_ptr;
			break;
		}
		c->src.type = OP_MEM;
		break;
	case SrcImm:
	case SrcImmU:
		c->src.type = OP_IMM;
		c->src.ptr = (unsigned long *)c->eip;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		if (c->src.bytes == 8)
			c->src.bytes = 4;
		/* NB. Immediates are sign-extended as necessary. */
		switch (c->src.bytes) {
		case 1:
			c->src.val = insn_fetch(s8, 1, c->eip);
			break;
		case 2:
			c->src.val = insn_fetch(s16, 2, c->eip);
			break;
		case 4:
			c->src.val = insn_fetch(s32, 4, c->eip);
			break;
		}
		if ((c->d & SrcMask) == SrcImmU) {
			switch (c->src.bytes) {
			case 1:
				c->src.val &= 0xff;
				break;
			case 2:
				c->src.val &= 0xffff;
				break;
			case 4:
				c->src.val &= 0xffffffff;
				break;
			}
		}
		break;
	case SrcImmByte:
	case SrcImmUByte:
		c->src.type = OP_IMM;
		c->src.ptr = (unsigned long *)c->eip;
		c->src.bytes = 1;
		if ((c->d & SrcMask) == SrcImmByte)
			c->src.val = insn_fetch(s8, 1, c->eip);
		else
			c->src.val = insn_fetch(u8, 1, c->eip);
		break;
	case SrcOne:
		c->src.bytes = 1;
		c->src.val = 1;
		break;
	}

	/*
	 * Decode and fetch the second source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & Src2Mask) {
	case Src2None:
		break;
	case Src2CL:
		c->src2.bytes = 1;
		c->src2.val = c->regs[VCPU_REGS_RCX] & 0x8;
		break;
	case Src2ImmByte:
		c->src2.type = OP_IMM;
		c->src2.ptr = (unsigned long *)c->eip;
		c->src2.bytes = 1;
		c->src2.val = insn_fetch(u8, 1, c->eip);
		break;
	case Src2Imm16:
		c->src2.type = OP_IMM;
		c->src2.ptr = (unsigned long *)c->eip;
		c->src2.bytes = 2;
		c->src2.val = insn_fetch(u16, 2, c->eip);
		break;
	case Src2One:
		c->src2.bytes = 1;
		c->src2.val = 1;
		break;
	}

	/* Decode and fetch the destination operand: register or memory. */
	switch (c->d & DstMask) {
	case ImplicitOps:
		/* Special instructions do their own operand decoding. */
		return 0;
	case DstReg:
		decode_register_operand(&c->dst, c,
			 c->twobyte && (c->b == 0xb6 || c->b == 0xb7));
		break;
	case DstMem:
		if ((c->d & ModRM) && c->modrm_mod == 3) {
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
			c->dst.type = OP_REG;
			c->dst.val = c->dst.orig_val = c->modrm_val;
			c->dst.ptr = c->modrm_ptr;
			break;
		}
		c->dst.type = OP_MEM;
		break;
	case DstAcc:
		c->dst.type = OP_REG;
		c->dst.bytes = c->op_bytes;
		c->dst.ptr = &c->regs[VCPU_REGS_RAX];
		switch (c->op_bytes) {
			case 1:
				c->dst.val = *(u8 *)c->dst.ptr;
				break;
			case 2:
				c->dst.val = *(u16 *)c->dst.ptr;
				break;
			case 4:
				c->dst.val = *(u32 *)c->dst.ptr;
				break;
		}
		c->dst.orig_val = c->dst.val;
		break;
	}

	if (c->rip_relative)
		c->modrm_ea += c->eip;

done:
	return (rc == X86EMUL_UNHANDLEABLE) ? -1 : 0;
}

static inline void emulate_push(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.type  = OP_MEM;
	c->dst.bytes = c->op_bytes;
	c->dst.val = c->src.val;
	register_address_increment(c, &c->regs[VCPU_REGS_RSP], -c->op_bytes);
	c->dst.ptr = (void *) register_address(c, ss_base(ctxt),
					       c->regs[VCPU_REGS_RSP]);
}

static int emulate_pop(struct x86_emulate_ctxt *ctxt,
		       struct x86_emulate_ops *ops,
		       void *dest, int len)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	rc = ops->read_emulated(register_address(c, ss_base(ctxt),
						 c->regs[VCPU_REGS_RSP]),
				dest, len, ctxt->vcpu);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	register_address_increment(c, &c->regs[VCPU_REGS_RSP], len);
	return rc;
}

static void emulate_push_sreg(struct x86_emulate_ctxt *ctxt, int seg)
{
	struct decode_cache *c = &ctxt->decode;
	struct kvm_segment segment;

	kvm_x86_ops->get_segment(ctxt->vcpu, &segment, seg);

	c->src.val = segment.selector;
	emulate_push(ctxt);
}

static int emulate_pop_sreg(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long selector;
	int rc;

	rc = emulate_pop(ctxt, ops, &selector, c->op_bytes);
	if (rc != 0)
		return rc;

	rc = kvm_load_segment_descriptor(ctxt->vcpu, (u16)selector, 1, seg);
	return rc;
}

static void emulate_pusha(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long old_esp = c->regs[VCPU_REGS_RSP];
	int reg = VCPU_REGS_RAX;

	while (reg <= VCPU_REGS_RDI) {
		(reg == VCPU_REGS_RSP) ?
		(c->src.val = old_esp) : (c->src.val = c->regs[reg]);

		emulate_push(ctxt);
		++reg;
	}
}

static int emulate_popa(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = 0;
	int reg = VCPU_REGS_RDI;

	while (reg >= VCPU_REGS_RAX) {
		if (reg == VCPU_REGS_RSP) {
			register_address_increment(c, &c->regs[VCPU_REGS_RSP],
							c->op_bytes);
			--reg;
		}

		rc = emulate_pop(ctxt, ops, &c->regs[reg], c->op_bytes);
		if (rc != 0)
			break;
		--reg;
	}
	return rc;
}

static inline int emulate_grp1a(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	rc = emulate_pop(ctxt, ops, &c->dst.val, c->dst.bytes);
	if (rc != 0)
		return rc;
	return 0;
}

static inline void emulate_grp2(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	switch (c->modrm_reg) {
	case 0:	/* rol */
		emulate_2op_SrcB("rol", c->src, c->dst, ctxt->eflags);
		break;
	case 1:	/* ror */
		emulate_2op_SrcB("ror", c->src, c->dst, ctxt->eflags);
		break;
	case 2:	/* rcl */
		emulate_2op_SrcB("rcl", c->src, c->dst, ctxt->eflags);
		break;
	case 3:	/* rcr */
		emulate_2op_SrcB("rcr", c->src, c->dst, ctxt->eflags);
		break;
	case 4:	/* sal/shl */
	case 6:	/* sal/shl */
		emulate_2op_SrcB("sal", c->src, c->dst, ctxt->eflags);
		break;
	case 5:	/* shr */
		emulate_2op_SrcB("shr", c->src, c->dst, ctxt->eflags);
		break;
	case 7:	/* sar */
		emulate_2op_SrcB("sar", c->src, c->dst, ctxt->eflags);
		break;
	}
}

static inline int emulate_grp3(struct x86_emulate_ctxt *ctxt,
			       struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = 0;

	switch (c->modrm_reg) {
	case 0 ... 1:	/* test */
		emulate_2op_SrcV("test", c->src, c->dst, ctxt->eflags);
		break;
	case 2:	/* not */
		c->dst.val = ~c->dst.val;
		break;
	case 3:	/* neg */
		emulate_1op("neg", c->dst, ctxt->eflags);
		break;
	default:
		DPRINTF("Cannot emulate %02x\n", c->b);
		rc = X86EMUL_UNHANDLEABLE;
		break;
	}
	return rc;
}

static inline int emulate_grp45(struct x86_emulate_ctxt *ctxt,
			       struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;

	switch (c->modrm_reg) {
	case 0:	/* inc */
		emulate_1op("inc", c->dst, ctxt->eflags);
		break;
	case 1:	/* dec */
		emulate_1op("dec", c->dst, ctxt->eflags);
		break;
	case 2: /* call near abs */ {
		long int old_eip;
		old_eip = c->eip;
		c->eip = c->src.val;
		c->src.val = old_eip;
		emulate_push(ctxt);
		break;
	}
	case 4: /* jmp abs */
		c->eip = c->src.val;
		break;
	case 6:	/* push */
		emulate_push(ctxt);
		break;
	}
	return 0;
}

static inline int emulate_grp9(struct x86_emulate_ctxt *ctxt,
			       struct x86_emulate_ops *ops,
			       unsigned long memop)
{
	struct decode_cache *c = &ctxt->decode;
	u64 old, new;
	int rc;

	rc = ops->read_emulated(memop, &old, 8, ctxt->vcpu);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	if (((u32) (old >> 0) != (u32) c->regs[VCPU_REGS_RAX]) ||
	    ((u32) (old >> 32) != (u32) c->regs[VCPU_REGS_RDX])) {

		c->regs[VCPU_REGS_RAX] = (u32) (old >> 0);
		c->regs[VCPU_REGS_RDX] = (u32) (old >> 32);
		ctxt->eflags &= ~EFLG_ZF;

	} else {
		new = ((u64)c->regs[VCPU_REGS_RCX] << 32) |
		       (u32) c->regs[VCPU_REGS_RBX];

		rc = ops->cmpxchg_emulated(memop, &old, &new, 8, ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		ctxt->eflags |= EFLG_ZF;
	}
	return 0;
}

static int emulate_ret_far(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	unsigned long cs;

	rc = emulate_pop(ctxt, ops, &c->eip, c->op_bytes);
	if (rc)
		return rc;
	if (c->op_bytes == 4)
		c->eip = (u32)c->eip;
	rc = emulate_pop(ctxt, ops, &cs, c->op_bytes);
	if (rc)
		return rc;
	rc = kvm_load_segment_descriptor(ctxt->vcpu, (u16)cs, 1, VCPU_SREG_CS);
	return rc;
}

static inline int writeback(struct x86_emulate_ctxt *ctxt,
			    struct x86_emulate_ops *ops)
{
	int rc;
	struct decode_cache *c = &ctxt->decode;

	switch (c->dst.type) {
	case OP_REG:
		/* The 4-byte case *is* correct:
		 * in 64-bit mode we zero-extend.
		 */
		switch (c->dst.bytes) {
		case 1:
			*(u8 *)c->dst.ptr = (u8)c->dst.val;
			break;
		case 2:
			*(u16 *)c->dst.ptr = (u16)c->dst.val;
			break;
		case 4:
			*c->dst.ptr = (u32)c->dst.val;
			break;	/* 64b: zero-ext */
		case 8:
			*c->dst.ptr = c->dst.val;
			break;
		}
		break;
	case OP_MEM:
		if (c->lock_prefix)
			rc = ops->cmpxchg_emulated(
					(unsigned long)c->dst.ptr,
					&c->dst.orig_val,
					&c->dst.val,
					c->dst.bytes,
					ctxt->vcpu);
		else
			rc = ops->write_emulated(
					(unsigned long)c->dst.ptr,
					&c->dst.val,
					c->dst.bytes,
					ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		break;
	case OP_NONE:
		/* no writeback */
		break;
	default:
		break;
	}
	return 0;
}

static void toggle_interruptibility(struct x86_emulate_ctxt *ctxt, u32 mask)
{
	u32 int_shadow = kvm_x86_ops->get_interrupt_shadow(ctxt->vcpu, mask);
	/*
	 * an sti; sti; sequence only disable interrupts for the first
	 * instruction. So, if the last instruction, be it emulated or
	 * not, left the system with the INT_STI flag enabled, it
	 * means that the last instruction is an sti. We should not
	 * leave the flag on in this case. The same goes for mov ss
	 */
	if (!(int_shadow & mask))
		ctxt->interruptibility = mask;
}

static inline void
setup_syscalls_segments(struct x86_emulate_ctxt *ctxt,
	struct kvm_segment *cs, struct kvm_segment *ss)
{
	memset(cs, 0, sizeof(struct kvm_segment));
	kvm_x86_ops->get_segment(ctxt->vcpu, cs, VCPU_SREG_CS);
	memset(ss, 0, sizeof(struct kvm_segment));

	cs->l = 0;		/* will be adjusted later */
	cs->base = 0;		/* flat segment */
	cs->g = 1;		/* 4kb granularity */
	cs->limit = 0xffffffff;	/* 4GB limit */
	cs->type = 0x0b;	/* Read, Execute, Accessed */
	cs->s = 1;
	cs->dpl = 0;		/* will be adjusted later */
	cs->present = 1;
	cs->db = 1;

	ss->unusable = 0;
	ss->base = 0;		/* flat segment */
	ss->limit = 0xffffffff;	/* 4GB limit */
	ss->g = 1;		/* 4kb granularity */
	ss->s = 1;
	ss->type = 0x03;	/* Read/Write, Accessed */
	ss->db = 1;		/* 32bit stack segment */
	ss->dpl = 0;
	ss->present = 1;
}

static int
emulate_syscall(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct kvm_segment cs, ss;
	u64 msr_data;

	/* syscall is not available in real mode */
	if (c->lock_prefix || ctxt->mode == X86EMUL_MODE_REAL
	    || ctxt->mode == X86EMUL_MODE_VM86)
		return -1;

	setup_syscalls_segments(ctxt, &cs, &ss);

	kvm_x86_ops->get_msr(ctxt->vcpu, MSR_STAR, &msr_data);
	msr_data >>= 32;
	cs.selector = (u16)(msr_data & 0xfffc);
	ss.selector = (u16)(msr_data + 8);

	if (is_long_mode(ctxt->vcpu)) {
		cs.db = 0;
		cs.l = 1;
	}
	kvm_x86_ops->set_segment(ctxt->vcpu, &cs, VCPU_SREG_CS);
	kvm_x86_ops->set_segment(ctxt->vcpu, &ss, VCPU_SREG_SS);

	c->regs[VCPU_REGS_RCX] = c->eip;
	if (is_long_mode(ctxt->vcpu)) {
#ifdef CONFIG_X86_64
		c->regs[VCPU_REGS_R11] = ctxt->eflags & ~EFLG_RF;

		kvm_x86_ops->get_msr(ctxt->vcpu,
			ctxt->mode == X86EMUL_MODE_PROT64 ?
			MSR_LSTAR : MSR_CSTAR, &msr_data);
		c->eip = msr_data;

		kvm_x86_ops->get_msr(ctxt->vcpu, MSR_SYSCALL_MASK, &msr_data);
		ctxt->eflags &= ~(msr_data | EFLG_RF);
#endif
	} else {
		/* legacy mode */
		kvm_x86_ops->get_msr(ctxt->vcpu, MSR_STAR, &msr_data);
		c->eip = (u32)msr_data;

		ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	}

	return 0;
}

static int
emulate_sysenter(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct kvm_segment cs, ss;
	u64 msr_data;

	/* inject #UD if LOCK prefix is used */
	if (c->lock_prefix)
		return -1;

	/* inject #GP if in real mode */
	if (ctxt->mode == X86EMUL_MODE_REAL) {
		kvm_inject_gp(ctxt->vcpu, 0);
		return -1;
	}

	/* XXX sysenter/sysexit have not been tested in 64bit mode.
	* Therefore, we inject an #UD.
	*/
	if (ctxt->mode == X86EMUL_MODE_PROT64)
		return -1;

	setup_syscalls_segments(ctxt, &cs, &ss);

	kvm_x86_ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (ctxt->mode) {
	case X86EMUL_MODE_PROT32:
		if ((msr_data & 0xfffc) == 0x0) {
			kvm_inject_gp(ctxt->vcpu, 0);
			return -1;
		}
		break;
	case X86EMUL_MODE_PROT64:
		if (msr_data == 0x0) {
			kvm_inject_gp(ctxt->vcpu, 0);
			return -1;
		}
		break;
	}

	ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	cs.selector = (u16)msr_data;
	cs.selector &= ~SELECTOR_RPL_MASK;
	ss.selector = cs.selector + 8;
	ss.selector &= ~SELECTOR_RPL_MASK;
	if (ctxt->mode == X86EMUL_MODE_PROT64
		|| is_long_mode(ctxt->vcpu)) {
		cs.db = 0;
		cs.l = 1;
	}

	kvm_x86_ops->set_segment(ctxt->vcpu, &cs, VCPU_SREG_CS);
	kvm_x86_ops->set_segment(ctxt->vcpu, &ss, VCPU_SREG_SS);

	kvm_x86_ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_EIP, &msr_data);
	c->eip = msr_data;

	kvm_x86_ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_ESP, &msr_data);
	c->regs[VCPU_REGS_RSP] = msr_data;

	return 0;
}

static int
emulate_sysexit(struct x86_emulate_ctxt *ctxt)
{
	struct decode_cache *c = &ctxt->decode;
	struct kvm_segment cs, ss;
	u64 msr_data;
	int usermode;

	/* inject #UD if LOCK prefix is used */
	if (c->lock_prefix)
		return -1;

	/* inject #GP if in real mode or Virtual 8086 mode */
	if (ctxt->mode == X86EMUL_MODE_REAL ||
	    ctxt->mode == X86EMUL_MODE_VM86) {
		kvm_inject_gp(ctxt->vcpu, 0);
		return -1;
	}

	/* sysexit must be called from CPL 0 */
	if (kvm_x86_ops->get_cpl(ctxt->vcpu) != 0) {
		kvm_inject_gp(ctxt->vcpu, 0);
		return -1;
	}

	setup_syscalls_segments(ctxt, &cs, &ss);

	if ((c->rex_prefix & 0x8) != 0x0)
		usermode = X86EMUL_MODE_PROT64;
	else
		usermode = X86EMUL_MODE_PROT32;

	cs.dpl = 3;
	ss.dpl = 3;
	kvm_x86_ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (usermode) {
	case X86EMUL_MODE_PROT32:
		cs.selector = (u16)(msr_data + 16);
		if ((msr_data & 0xfffc) == 0x0) {
			kvm_inject_gp(ctxt->vcpu, 0);
			return -1;
		}
		ss.selector = (u16)(msr_data + 24);
		break;
	case X86EMUL_MODE_PROT64:
		cs.selector = (u16)(msr_data + 32);
		if (msr_data == 0x0) {
			kvm_inject_gp(ctxt->vcpu, 0);
			return -1;
		}
		ss.selector = cs.selector + 8;
		cs.db = 0;
		cs.l = 1;
		break;
	}
	cs.selector |= SELECTOR_RPL_MASK;
	ss.selector |= SELECTOR_RPL_MASK;

	kvm_x86_ops->set_segment(ctxt->vcpu, &cs, VCPU_SREG_CS);
	kvm_x86_ops->set_segment(ctxt->vcpu, &ss, VCPU_SREG_SS);

	c->eip = ctxt->vcpu->arch.regs[VCPU_REGS_RDX];
	c->regs[VCPU_REGS_RSP] = ctxt->vcpu->arch.regs[VCPU_REGS_RCX];

	return 0;
}

int
x86_emulate_insn(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	unsigned long memop = 0;
	u64 msr_data;
	unsigned long saved_eip = 0;
	struct decode_cache *c = &ctxt->decode;
	unsigned int port;
	int io_dir_in;
	int rc = 0;

	ctxt->interruptibility = 0;

	/* Shadow copy of register state. Committed on successful emulation.
	 * NOTE: we can copy them from vcpu as x86_decode_insn() doesn't
	 * modify them.
	 */

	memcpy(c->regs, ctxt->vcpu->arch.regs, sizeof c->regs);
	saved_eip = c->eip;

	if (((c->d & ModRM) && (c->modrm_mod != 3)) || (c->d & MemAbs))
		memop = c->modrm_ea;

	if (c->rep_prefix && (c->d & String)) {
		/* All REP prefixes have the same first termination condition */
		if (c->regs[VCPU_REGS_RCX] == 0) {
			kvm_rip_write(ctxt->vcpu, c->eip);
			goto done;
		}
		/* The second termination condition only applies for REPE
		 * and REPNE. Test if the repeat string operation prefix is
		 * REPE/REPZ or REPNE/REPNZ and if it's the case it tests the
		 * corresponding termination condition according to:
		 * 	- if REPE/REPZ and ZF = 0 then done
		 * 	- if REPNE/REPNZ and ZF = 1 then done
		 */
		if ((c->b == 0xa6) || (c->b == 0xa7) ||
				(c->b == 0xae) || (c->b == 0xaf)) {
			if ((c->rep_prefix == REPE_PREFIX) &&
				((ctxt->eflags & EFLG_ZF) == 0)) {
					kvm_rip_write(ctxt->vcpu, c->eip);
					goto done;
			}
			if ((c->rep_prefix == REPNE_PREFIX) &&
				((ctxt->eflags & EFLG_ZF) == EFLG_ZF)) {
				kvm_rip_write(ctxt->vcpu, c->eip);
				goto done;
			}
		}
		c->regs[VCPU_REGS_RCX]--;
		c->eip = kvm_rip_read(ctxt->vcpu);
	}

	if (c->src.type == OP_MEM) {
		c->src.ptr = (unsigned long *)memop;
		c->src.val = 0;
		rc = ops->read_emulated((unsigned long)c->src.ptr,
					&c->src.val,
					c->src.bytes,
					ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		c->src.orig_val = c->src.val;
	}

	if ((c->d & DstMask) == ImplicitOps)
		goto special_insn;


	if (c->dst.type == OP_MEM) {
		c->dst.ptr = (unsigned long *)memop;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.val = 0;
		if (c->d & BitOp) {
			unsigned long mask = ~(c->dst.bytes * 8 - 1);

			c->dst.ptr = (void *)c->dst.ptr +
						   (c->src.val & mask) / 8;
		}
		if (!(c->d & Mov)) {
			/* optimisation - avoid slow emulated read */
			rc = ops->read_emulated((unsigned long)c->dst.ptr,
						&c->dst.val,
						c->dst.bytes,
						ctxt->vcpu);
			if (rc != X86EMUL_CONTINUE)
				goto done;
		}
	}
	c->dst.orig_val = c->dst.val;

special_insn:

	if (c->twobyte)
		goto twobyte_insn;

	switch (c->b) {
	case 0x00 ... 0x05:
	      add:		/* add */
		emulate_2op_SrcV("add", c->src, c->dst, ctxt->eflags);
		break;
	case 0x06:		/* push es */
		emulate_push_sreg(ctxt, VCPU_SREG_ES);
		break;
	case 0x07:		/* pop es */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_ES);
		if (rc != 0)
			goto done;
		break;
	case 0x08 ... 0x0d:
	      or:		/* or */
		emulate_2op_SrcV("or", c->src, c->dst, ctxt->eflags);
		break;
	case 0x0e:		/* push cs */
		emulate_push_sreg(ctxt, VCPU_SREG_CS);
		break;
	case 0x10 ... 0x15:
	      adc:		/* adc */
		emulate_2op_SrcV("adc", c->src, c->dst, ctxt->eflags);
		break;
	case 0x16:		/* push ss */
		emulate_push_sreg(ctxt, VCPU_SREG_SS);
		break;
	case 0x17:		/* pop ss */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_SS);
		if (rc != 0)
			goto done;
		break;
	case 0x18 ... 0x1d:
	      sbb:		/* sbb */
		emulate_2op_SrcV("sbb", c->src, c->dst, ctxt->eflags);
		break;
	case 0x1e:		/* push ds */
		emulate_push_sreg(ctxt, VCPU_SREG_DS);
		break;
	case 0x1f:		/* pop ds */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_DS);
		if (rc != 0)
			goto done;
		break;
	case 0x20 ... 0x25:
	      and:		/* and */
		emulate_2op_SrcV("and", c->src, c->dst, ctxt->eflags);
		break;
	case 0x28 ... 0x2d:
	      sub:		/* sub */
		emulate_2op_SrcV("sub", c->src, c->dst, ctxt->eflags);
		break;
	case 0x30 ... 0x35:
	      xor:		/* xor */
		emulate_2op_SrcV("xor", c->src, c->dst, ctxt->eflags);
		break;
	case 0x38 ... 0x3d:
	      cmp:		/* cmp */
		emulate_2op_SrcV("cmp", c->src, c->dst, ctxt->eflags);
		break;
	case 0x40 ... 0x47: /* inc r16/r32 */
		emulate_1op("inc", c->dst, ctxt->eflags);
		break;
	case 0x48 ... 0x4f: /* dec r16/r32 */
		emulate_1op("dec", c->dst, ctxt->eflags);
		break;
	case 0x50 ... 0x57:  /* push reg */
		emulate_push(ctxt);
		break;
	case 0x58 ... 0x5f: /* pop reg */
	pop_instruction:
		rc = emulate_pop(ctxt, ops, &c->dst.val, c->op_bytes);
		if (rc != 0)
			goto done;
		break;
	case 0x60:	/* pusha */
		emulate_pusha(ctxt);
		break;
	case 0x61:	/* popa */
		rc = emulate_popa(ctxt, ops);
		if (rc != 0)
			goto done;
		break;
	case 0x63:		/* movsxd */
		if (ctxt->mode != X86EMUL_MODE_PROT64)
			goto cannot_emulate;
		c->dst.val = (s32) c->src.val;
		break;
	case 0x68: /* push imm */
	case 0x6a: /* push imm8 */
		emulate_push(ctxt);
		break;
	case 0x6c:		/* insb */
	case 0x6d:		/* insw/insd */
		 if (kvm_emulate_pio_string(ctxt->vcpu,
				1,
				(c->d & ByteOp) ? 1 : c->op_bytes,
				c->rep_prefix ?
				address_mask(c, c->regs[VCPU_REGS_RCX]) : 1,
				(ctxt->eflags & EFLG_DF),
				register_address(c, es_base(ctxt),
						 c->regs[VCPU_REGS_RDI]),
				c->rep_prefix,
				c->regs[VCPU_REGS_RDX]) == 0) {
			c->eip = saved_eip;
			return -1;
		}
		return 0;
	case 0x6e:		/* outsb */
	case 0x6f:		/* outsw/outsd */
		if (kvm_emulate_pio_string(ctxt->vcpu,
				0,
				(c->d & ByteOp) ? 1 : c->op_bytes,
				c->rep_prefix ?
				address_mask(c, c->regs[VCPU_REGS_RCX]) : 1,
				(ctxt->eflags & EFLG_DF),
					 register_address(c,
					  seg_override_base(ctxt, c),
						 c->regs[VCPU_REGS_RSI]),
				c->rep_prefix,
				c->regs[VCPU_REGS_RDX]) == 0) {
			c->eip = saved_eip;
			return -1;
		}
		return 0;
	case 0x70 ... 0x7f: /* jcc (short) */
		if (test_cc(c->b, ctxt->eflags))
			jmp_rel(c, c->src.val);
		break;
	case 0x80 ... 0x83:	/* Grp1 */
		switch (c->modrm_reg) {
		case 0:
			goto add;
		case 1:
			goto or;
		case 2:
			goto adc;
		case 3:
			goto sbb;
		case 4:
			goto and;
		case 5:
			goto sub;
		case 6:
			goto xor;
		case 7:
			goto cmp;
		}
		break;
	case 0x84 ... 0x85:
		emulate_2op_SrcV("test", c->src, c->dst, ctxt->eflags);
		break;
	case 0x86 ... 0x87:	/* xchg */
	xchg:
		/* Write back the register source. */
		switch (c->dst.bytes) {
		case 1:
			*(u8 *) c->src.ptr = (u8) c->dst.val;
			break;
		case 2:
			*(u16 *) c->src.ptr = (u16) c->dst.val;
			break;
		case 4:
			*c->src.ptr = (u32) c->dst.val;
			break;	/* 64b reg: zero-extend */
		case 8:
			*c->src.ptr = c->dst.val;
			break;
		}
		/*
		 * Write back the memory destination with implicit LOCK
		 * prefix.
		 */
		c->dst.val = c->src.val;
		c->lock_prefix = 1;
		break;
	case 0x88 ... 0x8b:	/* mov */
		goto mov;
	case 0x8c: { /* mov r/m, sreg */
		struct kvm_segment segreg;

		if (c->modrm_reg <= 5)
			kvm_get_segment(ctxt->vcpu, &segreg, c->modrm_reg);
		else {
			printk(KERN_INFO "0x8c: Invalid segreg in modrm byte 0x%02x\n",
			       c->modrm);
			goto cannot_emulate;
		}
		c->dst.val = segreg.selector;
		break;
	}
	case 0x8d: /* lea r16/r32, m */
		c->dst.val = c->modrm_ea;
		break;
	case 0x8e: { /* mov seg, r/m16 */
		uint16_t sel;
		int type_bits;
		int err;

		sel = c->src.val;
		if (c->modrm_reg == VCPU_SREG_SS)
			toggle_interruptibility(ctxt, X86_SHADOW_INT_MOV_SS);

		if (c->modrm_reg <= 5) {
			type_bits = (c->modrm_reg == 1) ? 9 : 1;
			err = kvm_load_segment_descriptor(ctxt->vcpu, sel,
							  type_bits, c->modrm_reg);
		} else {
			printk(KERN_INFO "Invalid segreg in modrm byte 0x%02x\n",
					c->modrm);
			goto cannot_emulate;
		}

		if (err < 0)
			goto cannot_emulate;

		c->dst.type = OP_NONE;  /* Disable writeback. */
		break;
	}
	case 0x8f:		/* pop (sole member of Grp1a) */
		rc = emulate_grp1a(ctxt, ops);
		if (rc != 0)
			goto done;
		break;
	case 0x90: /* nop / xchg r8,rax */
		if (!(c->rex_prefix & 1)) { /* nop */
			c->dst.type = OP_NONE;
			break;
		}
	case 0x91 ... 0x97: /* xchg reg,rax */
		c->src.type = c->dst.type = OP_REG;
		c->src.bytes = c->dst.bytes = c->op_bytes;
		c->src.ptr = (unsigned long *) &c->regs[VCPU_REGS_RAX];
		c->src.val = *(c->src.ptr);
		goto xchg;
	case 0x9c: /* pushf */
		c->src.val =  (unsigned long) ctxt->eflags;
		emulate_push(ctxt);
		break;
	case 0x9d: /* popf */
		c->dst.type = OP_REG;
		c->dst.ptr = (unsigned long *) &ctxt->eflags;
		c->dst.bytes = c->op_bytes;
		goto pop_instruction;
	case 0xa0 ... 0xa1:	/* mov */
		c->dst.ptr = (unsigned long *)&c->regs[VCPU_REGS_RAX];
		c->dst.val = c->src.val;
		break;
	case 0xa2 ... 0xa3:	/* mov */
		c->dst.val = (unsigned long)c->regs[VCPU_REGS_RAX];
		break;
	case 0xa4 ... 0xa5:	/* movs */
		c->dst.type = OP_MEM;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = (unsigned long *)register_address(c,
						   es_base(ctxt),
						   c->regs[VCPU_REGS_RDI]);
		rc = ops->read_emulated(register_address(c,
						seg_override_base(ctxt, c),
						c->regs[VCPU_REGS_RSI]),
					&c->dst.val,
					c->dst.bytes, ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		register_address_increment(c, &c->regs[VCPU_REGS_RSI],
				       (ctxt->eflags & EFLG_DF) ? -c->dst.bytes
							   : c->dst.bytes);
		register_address_increment(c, &c->regs[VCPU_REGS_RDI],
				       (ctxt->eflags & EFLG_DF) ? -c->dst.bytes
							   : c->dst.bytes);
		break;
	case 0xa6 ... 0xa7:	/* cmps */
		c->src.type = OP_NONE; /* Disable writeback. */
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.ptr = (unsigned long *)register_address(c,
				       seg_override_base(ctxt, c),
						   c->regs[VCPU_REGS_RSI]);
		rc = ops->read_emulated((unsigned long)c->src.ptr,
					&c->src.val,
					c->src.bytes,
					ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			goto done;

		c->dst.type = OP_NONE; /* Disable writeback. */
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = (unsigned long *)register_address(c,
						   es_base(ctxt),
						   c->regs[VCPU_REGS_RDI]);
		rc = ops->read_emulated((unsigned long)c->dst.ptr,
					&c->dst.val,
					c->dst.bytes,
					ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			goto done;

		DPRINTF("cmps: mem1=0x%p mem2=0x%p\n", c->src.ptr, c->dst.ptr);

		emulate_2op_SrcV("cmp", c->src, c->dst, ctxt->eflags);

		register_address_increment(c, &c->regs[VCPU_REGS_RSI],
				       (ctxt->eflags & EFLG_DF) ? -c->src.bytes
								  : c->src.bytes);
		register_address_increment(c, &c->regs[VCPU_REGS_RDI],
				       (ctxt->eflags & EFLG_DF) ? -c->dst.bytes
								  : c->dst.bytes);

		break;
	case 0xaa ... 0xab:	/* stos */
		c->dst.type = OP_MEM;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = (unsigned long *)register_address(c,
						   es_base(ctxt),
						   c->regs[VCPU_REGS_RDI]);
		c->dst.val = c->regs[VCPU_REGS_RAX];
		register_address_increment(c, &c->regs[VCPU_REGS_RDI],
				       (ctxt->eflags & EFLG_DF) ? -c->dst.bytes
							   : c->dst.bytes);
		break;
	case 0xac ... 0xad:	/* lods */
		c->dst.type = OP_REG;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = (unsigned long *)&c->regs[VCPU_REGS_RAX];
		rc = ops->read_emulated(register_address(c,
						seg_override_base(ctxt, c),
						c->regs[VCPU_REGS_RSI]),
					&c->dst.val,
					c->dst.bytes,
					ctxt->vcpu);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		register_address_increment(c, &c->regs[VCPU_REGS_RSI],
				       (ctxt->eflags & EFLG_DF) ? -c->dst.bytes
							   : c->dst.bytes);
		break;
	case 0xae ... 0xaf:	/* scas */
		DPRINTF("Urk! I don't handle SCAS.\n");
		goto cannot_emulate;
	case 0xb0 ... 0xbf: /* mov r, imm */
		goto mov;
	case 0xc0 ... 0xc1:
		emulate_grp2(ctxt);
		break;
	case 0xc3: /* ret */
		c->dst.type = OP_REG;
		c->dst.ptr = &c->eip;
		c->dst.bytes = c->op_bytes;
		goto pop_instruction;
	case 0xc6 ... 0xc7:	/* mov (sole member of Grp11) */
	mov:
		c->dst.val = c->src.val;
		break;
	case 0xcb:		/* ret far */
		rc = emulate_ret_far(ctxt, ops);
		if (rc)
			goto done;
		break;
	case 0xd0 ... 0xd1:	/* Grp2 */
		c->src.val = 1;
		emulate_grp2(ctxt);
		break;
	case 0xd2 ... 0xd3:	/* Grp2 */
		c->src.val = c->regs[VCPU_REGS_RCX];
		emulate_grp2(ctxt);
		break;
	case 0xe4: 	/* inb */
	case 0xe5: 	/* in */
		port = c->src.val;
		io_dir_in = 1;
		goto do_io;
	case 0xe6: /* outb */
	case 0xe7: /* out */
		port = c->src.val;
		io_dir_in = 0;
		goto do_io;
	case 0xe8: /* call (near) */ {
		long int rel = c->src.val;
		c->src.val = (unsigned long) c->eip;
		jmp_rel(c, rel);
		emulate_push(ctxt);
		break;
	}
	case 0xe9: /* jmp rel */
		goto jmp;
	case 0xea: /* jmp far */
		if (kvm_load_segment_descriptor(ctxt->vcpu, c->src2.val, 9,
					VCPU_SREG_CS) < 0) {
			DPRINTF("jmp far: Failed to load CS descriptor\n");
			goto cannot_emulate;
		}

		c->eip = c->src.val;
		break;
	case 0xeb:
	      jmp:		/* jmp rel short */
		jmp_rel(c, c->src.val);
		c->dst.type = OP_NONE; /* Disable writeback. */
		break;
	case 0xec: /* in al,dx */
	case 0xed: /* in (e/r)ax,dx */
		port = c->regs[VCPU_REGS_RDX];
		io_dir_in = 1;
		goto do_io;
	case 0xee: /* out al,dx */
	case 0xef: /* out (e/r)ax,dx */
		port = c->regs[VCPU_REGS_RDX];
		io_dir_in = 0;
	do_io:	if (kvm_emulate_pio(ctxt->vcpu, io_dir_in,
				   (c->d & ByteOp) ? 1 : c->op_bytes,
				   port) != 0) {
			c->eip = saved_eip;
			goto cannot_emulate;
		}
		break;
	case 0xf4:              /* hlt */
		ctxt->vcpu->arch.halt_request = 1;
		break;
	case 0xf5:	/* cmc */
		/* complement carry flag from eflags reg */
		ctxt->eflags ^= EFLG_CF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xf6 ... 0xf7:	/* Grp3 */
		rc = emulate_grp3(ctxt, ops);
		if (rc != 0)
			goto done;
		break;
	case 0xf8: /* clc */
		ctxt->eflags &= ~EFLG_CF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfa: /* cli */
		ctxt->eflags &= ~X86_EFLAGS_IF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfb: /* sti */
		toggle_interruptibility(ctxt, X86_SHADOW_INT_STI);
		ctxt->eflags |= X86_EFLAGS_IF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfc: /* cld */
		ctxt->eflags &= ~EFLG_DF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfd: /* std */
		ctxt->eflags |= EFLG_DF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfe ... 0xff:	/* Grp4/Grp5 */
		rc = emulate_grp45(ctxt, ops);
		if (rc != 0)
			goto done;
		break;
	}

writeback:
	rc = writeback(ctxt, ops);
	if (rc != 0)
		goto done;

	/* Commit shadow register state. */
	memcpy(ctxt->vcpu->arch.regs, c->regs, sizeof c->regs);
	kvm_rip_write(ctxt->vcpu, c->eip);

done:
	if (rc == X86EMUL_UNHANDLEABLE) {
		c->eip = saved_eip;
		return -1;
	}
	return 0;

twobyte_insn:
	switch (c->b) {
	case 0x01: /* lgdt, lidt, lmsw */
		switch (c->modrm_reg) {
			u16 size;
			unsigned long address;

		case 0: /* vmcall */
			if (c->modrm_mod != 3 || c->modrm_rm != 1)
				goto cannot_emulate;

			rc = kvm_fix_hypercall(ctxt->vcpu);
			if (rc)
				goto done;

			/* Let the processor re-execute the fixed hypercall */
			c->eip = kvm_rip_read(ctxt->vcpu);
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		case 2: /* lgdt */
			rc = read_descriptor(ctxt, ops, c->src.ptr,
					     &size, &address, c->op_bytes);
			if (rc)
				goto done;
			realmode_lgdt(ctxt->vcpu, size, address);
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		case 3: /* lidt/vmmcall */
			if (c->modrm_mod == 3) {
				switch (c->modrm_rm) {
				case 1:
					rc = kvm_fix_hypercall(ctxt->vcpu);
					if (rc)
						goto done;
					break;
				default:
					goto cannot_emulate;
				}
			} else {
				rc = read_descriptor(ctxt, ops, c->src.ptr,
						     &size, &address,
						     c->op_bytes);
				if (rc)
					goto done;
				realmode_lidt(ctxt->vcpu, size, address);
			}
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		case 4: /* smsw */
			c->dst.bytes = 2;
			c->dst.val = realmode_get_cr(ctxt->vcpu, 0);
			break;
		case 6: /* lmsw */
			realmode_lmsw(ctxt->vcpu, (u16)c->src.val,
				      &ctxt->eflags);
			c->dst.type = OP_NONE;
			break;
		case 7: /* invlpg*/
			emulate_invlpg(ctxt->vcpu, memop);
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		default:
			goto cannot_emulate;
		}
		break;
	case 0x05: 		/* syscall */
		if (emulate_syscall(ctxt) == -1)
			goto cannot_emulate;
		else
			goto writeback;
		break;
	case 0x06:
		emulate_clts(ctxt->vcpu);
		c->dst.type = OP_NONE;
		break;
	case 0x08:		/* invd */
	case 0x09:		/* wbinvd */
	case 0x0d:		/* GrpP (prefetch) */
	case 0x18:		/* Grp16 (prefetch/nop) */
		c->dst.type = OP_NONE;
		break;
	case 0x20: /* mov cr, reg */
		if (c->modrm_mod != 3)
			goto cannot_emulate;
		c->regs[c->modrm_rm] =
				realmode_get_cr(ctxt->vcpu, c->modrm_reg);
		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x21: /* mov from dr to reg */
		if (c->modrm_mod != 3)
			goto cannot_emulate;
		rc = emulator_get_dr(ctxt, c->modrm_reg, &c->regs[c->modrm_rm]);
		if (rc)
			goto cannot_emulate;
		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x22: /* mov reg, cr */
		if (c->modrm_mod != 3)
			goto cannot_emulate;
		realmode_set_cr(ctxt->vcpu,
				c->modrm_reg, c->modrm_val, &ctxt->eflags);
		c->dst.type = OP_NONE;
		break;
	case 0x23: /* mov from reg to dr */
		if (c->modrm_mod != 3)
			goto cannot_emulate;
		rc = emulator_set_dr(ctxt, c->modrm_reg,
				     c->regs[c->modrm_rm]);
		if (rc)
			goto cannot_emulate;
		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x30:
		/* wrmsr */
		msr_data = (u32)c->regs[VCPU_REGS_RAX]
			| ((u64)c->regs[VCPU_REGS_RDX] << 32);
		rc = kvm_set_msr(ctxt->vcpu, c->regs[VCPU_REGS_RCX], msr_data);
		if (rc) {
			kvm_inject_gp(ctxt->vcpu, 0);
			c->eip = kvm_rip_read(ctxt->vcpu);
		}
		rc = X86EMUL_CONTINUE;
		c->dst.type = OP_NONE;
		break;
	case 0x32:
		/* rdmsr */
		rc = kvm_get_msr(ctxt->vcpu, c->regs[VCPU_REGS_RCX], &msr_data);
		if (rc) {
			kvm_inject_gp(ctxt->vcpu, 0);
			c->eip = kvm_rip_read(ctxt->vcpu);
		} else {
			c->regs[VCPU_REGS_RAX] = (u32)msr_data;
			c->regs[VCPU_REGS_RDX] = msr_data >> 32;
		}
		rc = X86EMUL_CONTINUE;
		c->dst.type = OP_NONE;
		break;
	case 0x34:		/* sysenter */
		if (emulate_sysenter(ctxt) == -1)
			goto cannot_emulate;
		else
			goto writeback;
		break;
	case 0x35:		/* sysexit */
		if (emulate_sysexit(ctxt) == -1)
			goto cannot_emulate;
		else
			goto writeback;
		break;
	case 0x40 ... 0x4f:	/* cmov */
		c->dst.val = c->dst.orig_val = c->src.val;
		if (!test_cc(c->b, ctxt->eflags))
			c->dst.type = OP_NONE; /* no writeback */
		break;
	case 0x80 ... 0x8f: /* jnz rel, etc*/
		if (test_cc(c->b, ctxt->eflags))
			jmp_rel(c, c->src.val);
		c->dst.type = OP_NONE;
		break;
	case 0xa0:	  /* push fs */
		emulate_push_sreg(ctxt, VCPU_SREG_FS);
		break;
	case 0xa1:	 /* pop fs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_FS);
		if (rc != 0)
			goto done;
		break;
	case 0xa3:
	      bt:		/* bt */
		c->dst.type = OP_NONE;
		/* only subword offset */
		c->src.val &= (c->dst.bytes << 3) - 1;
		emulate_2op_SrcV_nobyte("bt", c->src, c->dst, ctxt->eflags);
		break;
	case 0xa4: /* shld imm8, r, r/m */
	case 0xa5: /* shld cl, r, r/m */
		emulate_2op_cl("shld", c->src2, c->src, c->dst, ctxt->eflags);
		break;
	case 0xa8:	/* push gs */
		emulate_push_sreg(ctxt, VCPU_SREG_GS);
		break;
	case 0xa9:	/* pop gs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_GS);
		if (rc != 0)
			goto done;
		break;
	case 0xab:
	      bts:		/* bts */
		/* only subword offset */
		c->src.val &= (c->dst.bytes << 3) - 1;
		emulate_2op_SrcV_nobyte("bts", c->src, c->dst, ctxt->eflags);
		break;
	case 0xac: /* shrd imm8, r, r/m */
	case 0xad: /* shrd cl, r, r/m */
		emulate_2op_cl("shrd", c->src2, c->src, c->dst, ctxt->eflags);
		break;
	case 0xae:              /* clflush */
		break;
	case 0xb0 ... 0xb1:	/* cmpxchg */
		/*
		 * Save real source value, then compare EAX against
		 * destination.
		 */
		c->src.orig_val = c->src.val;
		c->src.val = c->regs[VCPU_REGS_RAX];
		emulate_2op_SrcV("cmp", c->src, c->dst, ctxt->eflags);
		if (ctxt->eflags & EFLG_ZF) {
			/* Success: write back to memory. */
			c->dst.val = c->src.orig_val;
		} else {
			/* Failure: write the value we saw to EAX. */
			c->dst.type = OP_REG;
			c->dst.ptr = (unsigned long *)&c->regs[VCPU_REGS_RAX];
		}
		break;
	case 0xb3:
	      btr:		/* btr */
		/* only subword offset */
		c->src.val &= (c->dst.bytes << 3) - 1;
		emulate_2op_SrcV_nobyte("btr", c->src, c->dst, ctxt->eflags);
		break;
	case 0xb6 ... 0xb7:	/* movzx */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->d & ByteOp) ? (u8) c->src.val
						       : (u16) c->src.val;
		break;
	case 0xba:		/* Grp8 */
		switch (c->modrm_reg & 3) {
		case 0:
			goto bt;
		case 1:
			goto bts;
		case 2:
			goto btr;
		case 3:
			goto btc;
		}
		break;
	case 0xbb:
	      btc:		/* btc */
		/* only subword offset */
		c->src.val &= (c->dst.bytes << 3) - 1;
		emulate_2op_SrcV_nobyte("btc", c->src, c->dst, ctxt->eflags);
		break;
	case 0xbe ... 0xbf:	/* movsx */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->d & ByteOp) ? (s8) c->src.val :
							(s16) c->src.val;
		break;
	case 0xc3:		/* movnti */
		c->dst.bytes = c->op_bytes;
		c->dst.val = (c->op_bytes == 4) ? (u32) c->src.val :
							(u64) c->src.val;
		break;
	case 0xc7:		/* Grp9 (cmpxchg8b) */
		rc = emulate_grp9(ctxt, ops, memop);
		if (rc != 0)
			goto done;
		c->dst.type = OP_NONE;
		break;
	}
	goto writeback;

cannot_emulate:
	DPRINTF("Cannot emulate %02x\n", c->b);
	c->eip = saved_eip;
	return -1;
}

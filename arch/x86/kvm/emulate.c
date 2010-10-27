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
 * Copyright 2010 Red Hat, Inc. and/or its affilates.
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
#include "tss.h"

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
#define DstDI       (5<<1)	/* Destination is in ES:(E)DI */
#define DstMem64    (6<<1)	/* 64bit memory operand */
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
#define SrcSI       (0xa<<4)	/* Source is in the DS:RSI */
#define SrcImmFAddr (0xb<<4)	/* Source is immediate far address */
#define SrcMemFAddr (0xc<<4)	/* Source is far address in memory */
#define SrcAcc      (0xd<<4)	/* Source Accumulator */
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
#define Lock        (1<<26) /* lock prefix is allowed for the instruction */
#define Priv        (1<<27) /* instruction generates #GP if current CPL != 0 */
#define No64	    (1<<28)
/* Source 2 operand type */
#define Src2None    (0<<29)
#define Src2CL      (1<<29)
#define Src2ImmByte (2<<29)
#define Src2One     (3<<29)
#define Src2Mask    (7<<29)

enum {
	Group1_80, Group1_81, Group1_82, Group1_83,
	Group1A, Group3_Byte, Group3, Group4, Group5, Group7,
	Group8, Group9,
};

static u32 opcode_table[256] = {
	/* 0x00 - 0x07 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x08 - 0x0F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, 0,
	/* 0x10 - 0x17 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x18 - 0x1F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	/* 0x20 - 0x27 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImmByte, DstAcc | SrcImm, 0, 0,
	/* 0x28 - 0x2F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImmByte, DstAcc | SrcImm, 0, 0,
	/* 0x30 - 0x37 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImmByte, DstAcc | SrcImm, 0, 0,
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
	DstDI | ByteOp | Mov | String, DstDI | Mov | String, /* insb, insw/insd */
	SrcSI | ByteOp | ImplicitOps | String, SrcSI | ImplicitOps | String, /* outsb, outsw/outsd */
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
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	/* 0x88 - 0x8F */
	ByteOp | DstMem | SrcReg | ModRM | Mov, DstMem | SrcReg | ModRM | Mov,
	ByteOp | DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstMem | SrcNone | ModRM | Mov, ModRM | DstReg,
	ImplicitOps | SrcMem16 | ModRM, Group | Group1A,
	/* 0x90 - 0x97 */
	DstReg, DstReg, DstReg, DstReg,	DstReg, DstReg, DstReg, DstReg,
	/* 0x98 - 0x9F */
	0, 0, SrcImmFAddr | No64, 0,
	ImplicitOps | Stack, ImplicitOps | Stack, 0, 0,
	/* 0xA0 - 0xA7 */
	ByteOp | DstAcc | SrcMem | Mov | MemAbs, DstAcc | SrcMem | Mov | MemAbs,
	ByteOp | DstMem | SrcAcc | Mov | MemAbs, DstMem | SrcAcc | Mov | MemAbs,
	ByteOp | SrcSI | DstDI | Mov | String, SrcSI | DstDI | Mov | String,
	ByteOp | SrcSI | DstDI | String, SrcSI | DstDI | String,
	/* 0xA8 - 0xAF */
	DstAcc | SrcImmByte | ByteOp, DstAcc | SrcImm, ByteOp | DstDI | Mov | String, DstDI | Mov | String,
	ByteOp | SrcSI | DstAcc | Mov | String, SrcSI | DstAcc | Mov | String,
	ByteOp | DstDI | String, DstDI | String,
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
	ByteOp | SrcImmUByte | DstAcc, SrcImmUByte | DstAcc,
	ByteOp | SrcImmUByte | DstAcc, SrcImmUByte | DstAcc,
	/* 0xE8 - 0xEF */
	SrcImm | Stack, SrcImm | ImplicitOps,
	SrcImmFAddr | No64, SrcImmByte | ImplicitOps,
	SrcNone | ByteOp | DstAcc, SrcNone | DstAcc,
	SrcNone | ByteOp | DstAcc, SrcNone | DstAcc,
	/* 0xF0 - 0xF7 */
	0, 0, 0, 0,
	ImplicitOps | Priv, ImplicitOps, Group | Group3_Byte, Group | Group3,
	/* 0xF8 - 0xFF */
	ImplicitOps, 0, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, Group | Group4, Group | Group5,
};

static u32 twobyte_table[256] = {
	/* 0x00 - 0x0F */
	0, Group | GroupDual | Group7, 0, 0,
	0, ImplicitOps, ImplicitOps | Priv, 0,
	ImplicitOps | Priv, ImplicitOps | Priv, 0, 0,
	0, ImplicitOps | ModRM, 0, 0,
	/* 0x10 - 0x1F */
	0, 0, 0, 0, 0, 0, 0, 0, ImplicitOps | ModRM, 0, 0, 0, 0, 0, 0, 0,
	/* 0x20 - 0x2F */
	ModRM | ImplicitOps | Priv, ModRM | Priv,
	ModRM | ImplicitOps | Priv, ModRM | Priv,
	0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x30 - 0x3F */
	ImplicitOps | Priv, 0, ImplicitOps | Priv, 0,
	ImplicitOps, ImplicitOps | Priv, 0, 0,
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
	0, DstMem | SrcReg | ModRM | BitOp | Lock,
	DstMem | SrcReg | Src2ImmByte | ModRM,
	DstMem | SrcReg | Src2CL | ModRM,
	ModRM, 0,
	/* 0xB0 - 0xB7 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	0, DstMem | SrcReg | ModRM | BitOp | Lock,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xB8 - 0xBF */
	0, 0,
	Group | Group8, DstMem | SrcReg | ModRM | BitOp | Lock,
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
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM,
	[Group1_81*8] =
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM,
	[Group1_82*8] =
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64,
	[Group1_83*8] =
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM,
	[Group1A*8] =
	DstMem | SrcNone | ModRM | Mov | Stack, 0, 0, 0, 0, 0, 0, 0,
	[Group3_Byte*8] =
	ByteOp | SrcImm | DstMem | ModRM, ByteOp | SrcImm | DstMem | ModRM,
	ByteOp | DstMem | SrcNone | ModRM, ByteOp | DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group3*8] =
	DstMem | SrcImm | ModRM, DstMem | SrcImm | ModRM,
	DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group4*8] =
	ByteOp | DstMem | SrcNone | ModRM | Lock, ByteOp | DstMem | SrcNone | ModRM | Lock,
	0, 0, 0, 0, 0, 0,
	[Group5*8] =
	DstMem | SrcNone | ModRM | Lock, DstMem | SrcNone | ModRM | Lock,
	SrcMem | ModRM | Stack, 0,
	SrcMem | ModRM | Stack, SrcMemFAddr | ModRM | ImplicitOps,
	SrcMem | ModRM | Stack, 0,
	[Group7*8] =
	0, 0, ModRM | SrcMem | Priv, ModRM | SrcMem | Priv,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov | Priv, SrcMem | ModRM | ByteOp | Priv,
	[Group8*8] =
	0, 0, 0, 0,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock, DstMem | SrcImmByte | ModRM | Lock,
	[Group9*8] =
	0, DstMem64 | ModRM | Lock, 0, 0, 0, 0, 0, 0,
};

static u32 group2_table[] = {
	[Group7*8] =
	SrcNone | ModRM | Priv, 0, 0, SrcNone | ModRM | Priv,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov | Priv, 0,
	[Group9*8] =
	0, 0, 0, 0, 0, 0, 0, 0,
};

/* EFLAGS bit definitions. */
#define EFLG_ID (1<<21)
#define EFLG_VIP (1<<20)
#define EFLG_VIF (1<<19)
#define EFLG_AC (1<<18)
#define EFLG_VM (1<<17)
#define EFLG_RF (1<<16)
#define EFLG_IOPL (3<<12)
#define EFLG_NT (1<<14)
#define EFLG_OF (1<<11)
#define EFLG_DF (1<<10)
#define EFLG_IF (1<<9)
#define EFLG_TF (1<<8)
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
	if (rc != X86EMUL_CONTINUE)					\
		goto done;						\
	(_eip) += (_size);						\
	(_type)_x;							\
})

#define insn_fetch_arr(_arr, _size, _eip)                                \
({	rc = do_insn_fetch(ctxt, ops, (_eip), _arr, (_size));		\
	if (rc != X86EMUL_CONTINUE)					\
		goto done;						\
	(_eip) += (_size);						\
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

static unsigned long seg_base(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops, int seg)
{
	if (ctxt->mode == X86EMUL_MODE_PROT64 && seg < VCPU_SREG_FS)
		return 0;

	return ops->get_cached_segment_base(seg, ctxt->vcpu);
}

static unsigned long seg_override_base(struct x86_emulate_ctxt *ctxt,
				       struct x86_emulate_ops *ops,
				       struct decode_cache *c)
{
	if (!c->has_seg_override)
		return 0;

	return seg_base(ctxt, ops, c->seg_override);
}

static unsigned long es_base(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops)
{
	return seg_base(ctxt, ops, VCPU_SREG_ES);
}

static unsigned long ss_base(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops)
{
	return seg_base(ctxt, ops, VCPU_SREG_SS);
}

static void emulate_exception(struct x86_emulate_ctxt *ctxt, int vec,
				      u32 error, bool valid)
{
	ctxt->exception = vec;
	ctxt->error_code = error;
	ctxt->error_code_valid = valid;
	ctxt->restart = false;
}

static void emulate_gp(struct x86_emulate_ctxt *ctxt, int err)
{
	emulate_exception(ctxt, GP_VECTOR, err, true);
}

static void emulate_pf(struct x86_emulate_ctxt *ctxt, unsigned long addr,
		       int err)
{
	ctxt->cr2 = addr;
	emulate_exception(ctxt, PF_VECTOR, err, true);
}

static void emulate_ud(struct x86_emulate_ctxt *ctxt)
{
	emulate_exception(ctxt, UD_VECTOR, 0, false);
}

static void emulate_ts(struct x86_emulate_ctxt *ctxt, int err)
{
	emulate_exception(ctxt, TS_VECTOR, err, true);
}

static int do_fetch_insn_byte(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops,
			      unsigned long eip, u8 *dest)
{
	struct fetch_cache *fc = &ctxt->decode.fetch;
	int rc;
	int size, cur_size;

	if (eip == fc->end) {
		cur_size = fc->end - fc->start;
		size = min(15UL - cur_size, PAGE_SIZE - offset_in_page(eip));
		rc = ops->fetch(ctxt->cs_base + eip, fc->data + cur_size,
				size, ctxt->vcpu, NULL);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		fc->end += size;
	}
	*dest = fc->data[eip - fc->start];
	return X86EMUL_CONTINUE;
}

static int do_insn_fetch(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 unsigned long eip, void *dest, unsigned size)
{
	int rc;

	/* x86 instructions are limited to 15 bytes. */
	if (eip + size - ctxt->eip > 15)
		return X86EMUL_UNHANDLEABLE;
	while (size--) {
		rc = do_fetch_insn_byte(ctxt, ops, eip++, dest++);
		if (rc != X86EMUL_CONTINUE)
			return rc;
	}
	return X86EMUL_CONTINUE;
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
			   ctxt->vcpu, NULL);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	rc = ops->read_std((unsigned long)ptr + 2, address, op_bytes,
			   ctxt->vcpu, NULL);
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
	int rc = X86EMUL_CONTINUE;

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
	int rc = X86EMUL_CONTINUE;

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
	int rc = X86EMUL_CONTINUE;
	int mode = ctxt->mode;
	int def_op_bytes, def_ad_bytes, group;


	/* we cannot decode insn before we complete previous rep insn */
	WARN_ON(ctxt->restart);

	c->eip = ctxt->eip;
	c->fetch.start = c->fetch.end = c->eip;
	ctxt->cs_base = seg_base(ctxt, ops, VCPU_SREG_CS);

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
	if (rc != X86EMUL_CONTINUE)
		goto done;

	if (!c->has_seg_override)
		set_seg_override(c, VCPU_SREG_DS);

	if (!(!c->twobyte && c->b == 0x8d))
		c->modrm_ea += seg_override_base(ctxt, ops, c);

	if (c->ad_bytes != 8)
		c->modrm_ea = (u32)c->modrm_ea;

	if (c->rip_relative)
		c->modrm_ea += c->eip;

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
		c->src.ptr = (unsigned long *)c->modrm_ea;
		c->src.val = 0;
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
	case SrcAcc:
		c->src.type = OP_REG;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.ptr = &c->regs[VCPU_REGS_RAX];
		switch (c->src.bytes) {
			case 1:
				c->src.val = *(u8 *)c->src.ptr;
				break;
			case 2:
				c->src.val = *(u16 *)c->src.ptr;
				break;
			case 4:
				c->src.val = *(u32 *)c->src.ptr;
				break;
			case 8:
				c->src.val = *(u64 *)c->src.ptr;
				break;
		}
		break;
	case SrcOne:
		c->src.bytes = 1;
		c->src.val = 1;
		break;
	case SrcSI:
		c->src.type = OP_MEM;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.ptr = (unsigned long *)
			register_address(c,  seg_override_base(ctxt, ops, c),
					 c->regs[VCPU_REGS_RSI]);
		c->src.val = 0;
		break;
	case SrcImmFAddr:
		c->src.type = OP_IMM;
		c->src.ptr = (unsigned long *)c->eip;
		c->src.bytes = c->op_bytes + 2;
		insn_fetch_arr(c->src.valptr, c->src.bytes, c->eip);
		break;
	case SrcMemFAddr:
		c->src.type = OP_MEM;
		c->src.ptr = (unsigned long *)c->modrm_ea;
		c->src.bytes = c->op_bytes + 2;
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
	case DstMem64:
		if ((c->d & ModRM) && c->modrm_mod == 3) {
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
			c->dst.type = OP_REG;
			c->dst.val = c->dst.orig_val = c->modrm_val;
			c->dst.ptr = c->modrm_ptr;
			break;
		}
		c->dst.type = OP_MEM;
		c->dst.ptr = (unsigned long *)c->modrm_ea;
		if ((c->d & DstMask) == DstMem64)
			c->dst.bytes = 8;
		else
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.val = 0;
		if (c->d & BitOp) {
			unsigned long mask = ~(c->dst.bytes * 8 - 1);

			c->dst.ptr = (void *)c->dst.ptr +
						   (c->src.val & mask) / 8;
		}
		break;
	case DstAcc:
		c->dst.type = OP_REG;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = &c->regs[VCPU_REGS_RAX];
		switch (c->dst.bytes) {
			case 1:
				c->dst.val = *(u8 *)c->dst.ptr;
				break;
			case 2:
				c->dst.val = *(u16 *)c->dst.ptr;
				break;
			case 4:
				c->dst.val = *(u32 *)c->dst.ptr;
				break;
			case 8:
				c->dst.val = *(u64 *)c->dst.ptr;
				break;
		}
		c->dst.orig_val = c->dst.val;
		break;
	case DstDI:
		c->dst.type = OP_MEM;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = (unsigned long *)
			register_address(c, es_base(ctxt, ops),
					 c->regs[VCPU_REGS_RDI]);
		c->dst.val = 0;
		break;
	}

done:
	return (rc == X86EMUL_UNHANDLEABLE) ? -1 : 0;
}

static int read_emulated(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 unsigned long addr, void *dest, unsigned size)
{
	int rc;
	struct read_cache *mc = &ctxt->decode.mem_read;
	u32 err;

	while (size) {
		int n = min(size, 8u);
		size -= n;
		if (mc->pos < mc->end)
			goto read_cached;

		rc = ops->read_emulated(addr, mc->data + mc->end, n, &err,
					ctxt->vcpu);
		if (rc == X86EMUL_PROPAGATE_FAULT)
			emulate_pf(ctxt, addr, err);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		mc->end += n;

	read_cached:
		memcpy(dest, mc->data + mc->pos, n);
		mc->pos += n;
		dest += n;
		addr += n;
	}
	return X86EMUL_CONTINUE;
}

static int pio_in_emulated(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops,
			   unsigned int size, unsigned short port,
			   void *dest)
{
	struct read_cache *rc = &ctxt->decode.io_read;

	if (rc->pos == rc->end) { /* refill pio read ahead */
		struct decode_cache *c = &ctxt->decode;
		unsigned int in_page, n;
		unsigned int count = c->rep_prefix ?
			address_mask(c, c->regs[VCPU_REGS_RCX]) : 1;
		in_page = (ctxt->eflags & EFLG_DF) ?
			offset_in_page(c->regs[VCPU_REGS_RDI]) :
			PAGE_SIZE - offset_in_page(c->regs[VCPU_REGS_RDI]);
		n = min(min(in_page, (unsigned int)sizeof(rc->data)) / size,
			count);
		if (n == 0)
			n = 1;
		rc->pos = rc->end = 0;
		if (!ops->pio_in_emulated(size, port, rc->data, n, ctxt->vcpu))
			return 0;
		rc->end = n * size;
	}

	memcpy(dest, rc->data + rc->pos, size);
	rc->pos += size;
	return 1;
}

static u32 desc_limit_scaled(struct desc_struct *desc)
{
	u32 limit = get_desc_limit(desc);

	return desc->g ? (limit << 12) | 0xfff : limit;
}

static void get_descriptor_table_ptr(struct x86_emulate_ctxt *ctxt,
				     struct x86_emulate_ops *ops,
				     u16 selector, struct desc_ptr *dt)
{
	if (selector & 1 << 2) {
		struct desc_struct desc;
		memset (dt, 0, sizeof *dt);
		if (!ops->get_cached_descriptor(&desc, VCPU_SREG_LDTR, ctxt->vcpu))
			return;

		dt->size = desc_limit_scaled(&desc); /* what if limit > 65535? */
		dt->address = get_desc_base(&desc);
	} else
		ops->get_gdt(dt, ctxt->vcpu);
}

/* allowed just for 8 bytes segments */
static int read_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 selector, struct desc_struct *desc)
{
	struct desc_ptr dt;
	u16 index = selector >> 3;
	int ret;
	u32 err;
	ulong addr;

	get_descriptor_table_ptr(ctxt, ops, selector, &dt);

	if (dt.size < index * 8 + 7) {
		emulate_gp(ctxt, selector & 0xfffc);
		return X86EMUL_PROPAGATE_FAULT;
	}
	addr = dt.address + index * 8;
	ret = ops->read_std(addr, desc, sizeof *desc, ctxt->vcpu,  &err);
	if (ret == X86EMUL_PROPAGATE_FAULT)
		emulate_pf(ctxt, addr, err);

       return ret;
}

/* allowed just for 8 bytes segments */
static int write_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				    struct x86_emulate_ops *ops,
				    u16 selector, struct desc_struct *desc)
{
	struct desc_ptr dt;
	u16 index = selector >> 3;
	u32 err;
	ulong addr;
	int ret;

	get_descriptor_table_ptr(ctxt, ops, selector, &dt);

	if (dt.size < index * 8 + 7) {
		emulate_gp(ctxt, selector & 0xfffc);
		return X86EMUL_PROPAGATE_FAULT;
	}

	addr = dt.address + index * 8;
	ret = ops->write_std(addr, desc, sizeof *desc, ctxt->vcpu, &err);
	if (ret == X86EMUL_PROPAGATE_FAULT)
		emulate_pf(ctxt, addr, err);

	return ret;
}

static int load_segment_descriptor(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 selector, int seg)
{
	struct desc_struct seg_desc;
	u8 dpl, rpl, cpl;
	unsigned err_vec = GP_VECTOR;
	u32 err_code = 0;
	bool null_selector = !(selector & ~0x3); /* 0000-0003 are null */
	int ret;

	memset(&seg_desc, 0, sizeof seg_desc);

	if ((seg <= VCPU_SREG_GS && ctxt->mode == X86EMUL_MODE_VM86)
	    || ctxt->mode == X86EMUL_MODE_REAL) {
		/* set real mode segment descriptor */
		set_desc_base(&seg_desc, selector << 4);
		set_desc_limit(&seg_desc, 0xffff);
		seg_desc.type = 3;
		seg_desc.p = 1;
		seg_desc.s = 1;
		goto load;
	}

	/* NULL selector is not valid for TR, CS and SS */
	if ((seg == VCPU_SREG_CS || seg == VCPU_SREG_SS || seg == VCPU_SREG_TR)
	    && null_selector)
		goto exception;

	/* TR should be in GDT only */
	if (seg == VCPU_SREG_TR && (selector & (1 << 2)))
		goto exception;

	if (null_selector) /* for NULL selector skip all following checks */
		goto load;

	ret = read_segment_descriptor(ctxt, ops, selector, &seg_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	err_code = selector & 0xfffc;
	err_vec = GP_VECTOR;

	/* can't load system descriptor into segment selecor */
	if (seg <= VCPU_SREG_GS && !seg_desc.s)
		goto exception;

	if (!seg_desc.p) {
		err_vec = (seg == VCPU_SREG_SS) ? SS_VECTOR : NP_VECTOR;
		goto exception;
	}

	rpl = selector & 3;
	dpl = seg_desc.dpl;
	cpl = ops->cpl(ctxt->vcpu);

	switch (seg) {
	case VCPU_SREG_SS:
		/*
		 * segment is not a writable data segment or segment
		 * selector's RPL != CPL or segment selector's RPL != CPL
		 */
		if (rpl != cpl || (seg_desc.type & 0xa) != 0x2 || dpl != cpl)
			goto exception;
		break;
	case VCPU_SREG_CS:
		if (!(seg_desc.type & 8))
			goto exception;

		if (seg_desc.type & 4) {
			/* conforming */
			if (dpl > cpl)
				goto exception;
		} else {
			/* nonconforming */
			if (rpl > cpl || dpl != cpl)
				goto exception;
		}
		/* CS(RPL) <- CPL */
		selector = (selector & 0xfffc) | cpl;
		break;
	case VCPU_SREG_TR:
		if (seg_desc.s || (seg_desc.type != 1 && seg_desc.type != 9))
			goto exception;
		break;
	case VCPU_SREG_LDTR:
		if (seg_desc.s || seg_desc.type != 2)
			goto exception;
		break;
	default: /*  DS, ES, FS, or GS */
		/*
		 * segment is not a data or readable code segment or
		 * ((segment is a data or nonconforming code segment)
		 * and (both RPL and CPL > DPL))
		 */
		if ((seg_desc.type & 0xa) == 0x8 ||
		    (((seg_desc.type & 0xc) != 0xc) &&
		     (rpl > dpl && cpl > dpl)))
			goto exception;
		break;
	}

	if (seg_desc.s) {
		/* mark segment as accessed */
		seg_desc.type |= 1;
		ret = write_segment_descriptor(ctxt, ops, selector, &seg_desc);
		if (ret != X86EMUL_CONTINUE)
			return ret;
	}
load:
	ops->set_segment_selector(selector, seg, ctxt->vcpu);
	ops->set_cached_descriptor(&seg_desc, seg, ctxt->vcpu);
	return X86EMUL_CONTINUE;
exception:
	emulate_exception(ctxt, err_vec, err_code, true);
	return X86EMUL_PROPAGATE_FAULT;
}

static inline int writeback(struct x86_emulate_ctxt *ctxt,
			    struct x86_emulate_ops *ops)
{
	int rc;
	struct decode_cache *c = &ctxt->decode;
	u32 err;

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
					&err,
					ctxt->vcpu);
		else
			rc = ops->write_emulated(
					(unsigned long)c->dst.ptr,
					&c->dst.val,
					c->dst.bytes,
					&err,
					ctxt->vcpu);
		if (rc == X86EMUL_PROPAGATE_FAULT)
			emulate_pf(ctxt,
					      (unsigned long)c->dst.ptr, err);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		break;
	case OP_NONE:
		/* no writeback */
		break;
	default:
		break;
	}
	return X86EMUL_CONTINUE;
}

static inline void emulate_push(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;

	c->dst.type  = OP_MEM;
	c->dst.bytes = c->op_bytes;
	c->dst.val = c->src.val;
	register_address_increment(c, &c->regs[VCPU_REGS_RSP], -c->op_bytes);
	c->dst.ptr = (void *) register_address(c, ss_base(ctxt, ops),
					       c->regs[VCPU_REGS_RSP]);
}

static int emulate_pop(struct x86_emulate_ctxt *ctxt,
		       struct x86_emulate_ops *ops,
		       void *dest, int len)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	rc = read_emulated(ctxt, ops, register_address(c, ss_base(ctxt, ops),
						       c->regs[VCPU_REGS_RSP]),
			   dest, len);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	register_address_increment(c, &c->regs[VCPU_REGS_RSP], len);
	return rc;
}

static int emulate_popf(struct x86_emulate_ctxt *ctxt,
		       struct x86_emulate_ops *ops,
		       void *dest, int len)
{
	int rc;
	unsigned long val, change_mask;
	int iopl = (ctxt->eflags & X86_EFLAGS_IOPL) >> IOPL_SHIFT;
	int cpl = ops->cpl(ctxt->vcpu);

	rc = emulate_pop(ctxt, ops, &val, len);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	change_mask = EFLG_CF | EFLG_PF | EFLG_AF | EFLG_ZF | EFLG_SF | EFLG_OF
		| EFLG_TF | EFLG_DF | EFLG_NT | EFLG_RF | EFLG_AC | EFLG_ID;

	switch(ctxt->mode) {
	case X86EMUL_MODE_PROT64:
	case X86EMUL_MODE_PROT32:
	case X86EMUL_MODE_PROT16:
		if (cpl == 0)
			change_mask |= EFLG_IOPL;
		if (cpl <= iopl)
			change_mask |= EFLG_IF;
		break;
	case X86EMUL_MODE_VM86:
		if (iopl < 3) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
		change_mask |= EFLG_IF;
		break;
	default: /* real mode */
		change_mask |= (EFLG_IOPL | EFLG_IF);
		break;
	}

	*(unsigned long *)dest =
		(ctxt->eflags & ~change_mask) | (val & change_mask);

	return rc;
}

static void emulate_push_sreg(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;

	c->src.val = ops->get_segment_selector(seg, ctxt->vcpu);

	emulate_push(ctxt, ops);
}

static int emulate_pop_sreg(struct x86_emulate_ctxt *ctxt,
			     struct x86_emulate_ops *ops, int seg)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long selector;
	int rc;

	rc = emulate_pop(ctxt, ops, &selector, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	rc = load_segment_descriptor(ctxt, ops, (u16)selector, seg);
	return rc;
}

static int emulate_pusha(struct x86_emulate_ctxt *ctxt,
			  struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	unsigned long old_esp = c->regs[VCPU_REGS_RSP];
	int rc = X86EMUL_CONTINUE;
	int reg = VCPU_REGS_RAX;

	while (reg <= VCPU_REGS_RDI) {
		(reg == VCPU_REGS_RSP) ?
		(c->src.val = old_esp) : (c->src.val = c->regs[reg]);

		emulate_push(ctxt, ops);

		rc = writeback(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			return rc;

		++reg;
	}

	/* Disable writeback. */
	c->dst.type = OP_NONE;

	return rc;
}

static int emulate_popa(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	int reg = VCPU_REGS_RDI;

	while (reg >= VCPU_REGS_RAX) {
		if (reg == VCPU_REGS_RSP) {
			register_address_increment(c, &c->regs[VCPU_REGS_RSP],
							c->op_bytes);
			--reg;
		}

		rc = emulate_pop(ctxt, ops, &c->regs[reg], c->op_bytes);
		if (rc != X86EMUL_CONTINUE)
			break;
		--reg;
	}
	return rc;
}

static inline int emulate_grp1a(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;

	return emulate_pop(ctxt, ops, &c->dst.val, c->dst.bytes);
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
		return 0;
	}
	return 1;
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
		emulate_push(ctxt, ops);
		break;
	}
	case 4: /* jmp abs */
		c->eip = c->src.val;
		break;
	case 6:	/* push */
		emulate_push(ctxt, ops);
		break;
	}
	return X86EMUL_CONTINUE;
}

static inline int emulate_grp9(struct x86_emulate_ctxt *ctxt,
			       struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	u64 old = c->dst.orig_val64;

	if (((u32) (old >> 0) != (u32) c->regs[VCPU_REGS_RAX]) ||
	    ((u32) (old >> 32) != (u32) c->regs[VCPU_REGS_RDX])) {
		c->regs[VCPU_REGS_RAX] = (u32) (old >> 0);
		c->regs[VCPU_REGS_RDX] = (u32) (old >> 32);
		ctxt->eflags &= ~EFLG_ZF;
	} else {
		c->dst.val64 = ((u64)c->regs[VCPU_REGS_RCX] << 32) |
			(u32) c->regs[VCPU_REGS_RBX];

		ctxt->eflags |= EFLG_ZF;
	}
	return X86EMUL_CONTINUE;
}

static int emulate_ret_far(struct x86_emulate_ctxt *ctxt,
			   struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;
	unsigned long cs;

	rc = emulate_pop(ctxt, ops, &c->eip, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	if (c->op_bytes == 4)
		c->eip = (u32)c->eip;
	rc = emulate_pop(ctxt, ops, &cs, c->op_bytes);
	if (rc != X86EMUL_CONTINUE)
		return rc;
	rc = load_segment_descriptor(ctxt, ops, (u16)cs, VCPU_SREG_CS);
	return rc;
}

static inline void
setup_syscalls_segments(struct x86_emulate_ctxt *ctxt,
			struct x86_emulate_ops *ops, struct desc_struct *cs,
			struct desc_struct *ss)
{
	memset(cs, 0, sizeof(struct desc_struct));
	ops->get_cached_descriptor(cs, VCPU_SREG_CS, ctxt->vcpu);
	memset(ss, 0, sizeof(struct desc_struct));

	cs->l = 0;		/* will be adjusted later */
	set_desc_base(cs, 0);	/* flat segment */
	cs->g = 1;		/* 4kb granularity */
	set_desc_limit(cs, 0xfffff);	/* 4GB limit */
	cs->type = 0x0b;	/* Read, Execute, Accessed */
	cs->s = 1;
	cs->dpl = 0;		/* will be adjusted later */
	cs->p = 1;
	cs->d = 1;

	set_desc_base(ss, 0);	/* flat segment */
	set_desc_limit(ss, 0xfffff);	/* 4GB limit */
	ss->g = 1;		/* 4kb granularity */
	ss->s = 1;
	ss->type = 0x03;	/* Read/Write, Accessed */
	ss->d = 1;		/* 32bit stack segment */
	ss->dpl = 0;
	ss->p = 1;
}

static int
emulate_syscall(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	u16 cs_sel, ss_sel;

	/* syscall is not available in real mode */
	if (ctxt->mode == X86EMUL_MODE_REAL ||
	    ctxt->mode == X86EMUL_MODE_VM86) {
		emulate_ud(ctxt);
		return X86EMUL_PROPAGATE_FAULT;
	}

	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	ops->get_msr(ctxt->vcpu, MSR_STAR, &msr_data);
	msr_data >>= 32;
	cs_sel = (u16)(msr_data & 0xfffc);
	ss_sel = (u16)(msr_data + 8);

	if (is_long_mode(ctxt->vcpu)) {
		cs.d = 0;
		cs.l = 1;
	}
	ops->set_cached_descriptor(&cs, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_segment_selector(cs_sel, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_cached_descriptor(&ss, VCPU_SREG_SS, ctxt->vcpu);
	ops->set_segment_selector(ss_sel, VCPU_SREG_SS, ctxt->vcpu);

	c->regs[VCPU_REGS_RCX] = c->eip;
	if (is_long_mode(ctxt->vcpu)) {
#ifdef CONFIG_X86_64
		c->regs[VCPU_REGS_R11] = ctxt->eflags & ~EFLG_RF;

		ops->get_msr(ctxt->vcpu,
			     ctxt->mode == X86EMUL_MODE_PROT64 ?
			     MSR_LSTAR : MSR_CSTAR, &msr_data);
		c->eip = msr_data;

		ops->get_msr(ctxt->vcpu, MSR_SYSCALL_MASK, &msr_data);
		ctxt->eflags &= ~(msr_data | EFLG_RF);
#endif
	} else {
		/* legacy mode */
		ops->get_msr(ctxt->vcpu, MSR_STAR, &msr_data);
		c->eip = (u32)msr_data;

		ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	}

	return X86EMUL_CONTINUE;
}

static int
emulate_sysenter(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	u16 cs_sel, ss_sel;

	/* inject #GP if in real mode */
	if (ctxt->mode == X86EMUL_MODE_REAL) {
		emulate_gp(ctxt, 0);
		return X86EMUL_PROPAGATE_FAULT;
	}

	/* XXX sysenter/sysexit have not been tested in 64bit mode.
	* Therefore, we inject an #UD.
	*/
	if (ctxt->mode == X86EMUL_MODE_PROT64) {
		emulate_ud(ctxt);
		return X86EMUL_PROPAGATE_FAULT;
	}

	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (ctxt->mode) {
	case X86EMUL_MODE_PROT32:
		if ((msr_data & 0xfffc) == 0x0) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
		break;
	case X86EMUL_MODE_PROT64:
		if (msr_data == 0x0) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
		break;
	}

	ctxt->eflags &= ~(EFLG_VM | EFLG_IF | EFLG_RF);
	cs_sel = (u16)msr_data;
	cs_sel &= ~SELECTOR_RPL_MASK;
	ss_sel = cs_sel + 8;
	ss_sel &= ~SELECTOR_RPL_MASK;
	if (ctxt->mode == X86EMUL_MODE_PROT64
		|| is_long_mode(ctxt->vcpu)) {
		cs.d = 0;
		cs.l = 1;
	}

	ops->set_cached_descriptor(&cs, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_segment_selector(cs_sel, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_cached_descriptor(&ss, VCPU_SREG_SS, ctxt->vcpu);
	ops->set_segment_selector(ss_sel, VCPU_SREG_SS, ctxt->vcpu);

	ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_EIP, &msr_data);
	c->eip = msr_data;

	ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_ESP, &msr_data);
	c->regs[VCPU_REGS_RSP] = msr_data;

	return X86EMUL_CONTINUE;
}

static int
emulate_sysexit(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	struct decode_cache *c = &ctxt->decode;
	struct desc_struct cs, ss;
	u64 msr_data;
	int usermode;
	u16 cs_sel, ss_sel;

	/* inject #GP if in real mode or Virtual 8086 mode */
	if (ctxt->mode == X86EMUL_MODE_REAL ||
	    ctxt->mode == X86EMUL_MODE_VM86) {
		emulate_gp(ctxt, 0);
		return X86EMUL_PROPAGATE_FAULT;
	}

	setup_syscalls_segments(ctxt, ops, &cs, &ss);

	if ((c->rex_prefix & 0x8) != 0x0)
		usermode = X86EMUL_MODE_PROT64;
	else
		usermode = X86EMUL_MODE_PROT32;

	cs.dpl = 3;
	ss.dpl = 3;
	ops->get_msr(ctxt->vcpu, MSR_IA32_SYSENTER_CS, &msr_data);
	switch (usermode) {
	case X86EMUL_MODE_PROT32:
		cs_sel = (u16)(msr_data + 16);
		if ((msr_data & 0xfffc) == 0x0) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
		ss_sel = (u16)(msr_data + 24);
		break;
	case X86EMUL_MODE_PROT64:
		cs_sel = (u16)(msr_data + 32);
		if (msr_data == 0x0) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
		ss_sel = cs_sel + 8;
		cs.d = 0;
		cs.l = 1;
		break;
	}
	cs_sel |= SELECTOR_RPL_MASK;
	ss_sel |= SELECTOR_RPL_MASK;

	ops->set_cached_descriptor(&cs, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_segment_selector(cs_sel, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_cached_descriptor(&ss, VCPU_SREG_SS, ctxt->vcpu);
	ops->set_segment_selector(ss_sel, VCPU_SREG_SS, ctxt->vcpu);

	c->eip = c->regs[VCPU_REGS_RDX];
	c->regs[VCPU_REGS_RSP] = c->regs[VCPU_REGS_RCX];

	return X86EMUL_CONTINUE;
}

static bool emulator_bad_iopl(struct x86_emulate_ctxt *ctxt,
			      struct x86_emulate_ops *ops)
{
	int iopl;
	if (ctxt->mode == X86EMUL_MODE_REAL)
		return false;
	if (ctxt->mode == X86EMUL_MODE_VM86)
		return true;
	iopl = (ctxt->eflags & X86_EFLAGS_IOPL) >> IOPL_SHIFT;
	return ops->cpl(ctxt->vcpu) > iopl;
}

static bool emulator_io_port_access_allowed(struct x86_emulate_ctxt *ctxt,
					    struct x86_emulate_ops *ops,
					    u16 port, u16 len)
{
	struct desc_struct tr_seg;
	int r;
	u16 io_bitmap_ptr;
	u8 perm, bit_idx = port & 0x7;
	unsigned mask = (1 << len) - 1;

	ops->get_cached_descriptor(&tr_seg, VCPU_SREG_TR, ctxt->vcpu);
	if (!tr_seg.p)
		return false;
	if (desc_limit_scaled(&tr_seg) < 103)
		return false;
	r = ops->read_std(get_desc_base(&tr_seg) + 102, &io_bitmap_ptr, 2,
			  ctxt->vcpu, NULL);
	if (r != X86EMUL_CONTINUE)
		return false;
	if (io_bitmap_ptr + port/8 > desc_limit_scaled(&tr_seg))
		return false;
	r = ops->read_std(get_desc_base(&tr_seg) + io_bitmap_ptr + port/8,
			  &perm, 1, ctxt->vcpu, NULL);
	if (r != X86EMUL_CONTINUE)
		return false;
	if ((perm >> bit_idx) & mask)
		return false;
	return true;
}

static bool emulator_io_permited(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 u16 port, u16 len)
{
	if (emulator_bad_iopl(ctxt, ops))
		if (!emulator_io_port_access_allowed(ctxt, ops, port, len))
			return false;
	return true;
}

static void save_state_to_tss16(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops,
				struct tss_segment_16 *tss)
{
	struct decode_cache *c = &ctxt->decode;

	tss->ip = c->eip;
	tss->flag = ctxt->eflags;
	tss->ax = c->regs[VCPU_REGS_RAX];
	tss->cx = c->regs[VCPU_REGS_RCX];
	tss->dx = c->regs[VCPU_REGS_RDX];
	tss->bx = c->regs[VCPU_REGS_RBX];
	tss->sp = c->regs[VCPU_REGS_RSP];
	tss->bp = c->regs[VCPU_REGS_RBP];
	tss->si = c->regs[VCPU_REGS_RSI];
	tss->di = c->regs[VCPU_REGS_RDI];

	tss->es = ops->get_segment_selector(VCPU_SREG_ES, ctxt->vcpu);
	tss->cs = ops->get_segment_selector(VCPU_SREG_CS, ctxt->vcpu);
	tss->ss = ops->get_segment_selector(VCPU_SREG_SS, ctxt->vcpu);
	tss->ds = ops->get_segment_selector(VCPU_SREG_DS, ctxt->vcpu);
	tss->ldt = ops->get_segment_selector(VCPU_SREG_LDTR, ctxt->vcpu);
}

static int load_state_from_tss16(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 struct tss_segment_16 *tss)
{
	struct decode_cache *c = &ctxt->decode;
	int ret;

	c->eip = tss->ip;
	ctxt->eflags = tss->flag | 2;
	c->regs[VCPU_REGS_RAX] = tss->ax;
	c->regs[VCPU_REGS_RCX] = tss->cx;
	c->regs[VCPU_REGS_RDX] = tss->dx;
	c->regs[VCPU_REGS_RBX] = tss->bx;
	c->regs[VCPU_REGS_RSP] = tss->sp;
	c->regs[VCPU_REGS_RBP] = tss->bp;
	c->regs[VCPU_REGS_RSI] = tss->si;
	c->regs[VCPU_REGS_RDI] = tss->di;

	/*
	 * SDM says that segment selectors are loaded before segment
	 * descriptors
	 */
	ops->set_segment_selector(tss->ldt, VCPU_SREG_LDTR, ctxt->vcpu);
	ops->set_segment_selector(tss->es, VCPU_SREG_ES, ctxt->vcpu);
	ops->set_segment_selector(tss->cs, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_segment_selector(tss->ss, VCPU_SREG_SS, ctxt->vcpu);
	ops->set_segment_selector(tss->ds, VCPU_SREG_DS, ctxt->vcpu);

	/*
	 * Now load segment descriptors. If fault happenes at this stage
	 * it is handled in a context of new task
	 */
	ret = load_segment_descriptor(ctxt, ops, tss->ldt, VCPU_SREG_LDTR);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->es, VCPU_SREG_ES);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->cs, VCPU_SREG_CS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ss, VCPU_SREG_SS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ds, VCPU_SREG_DS);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	return X86EMUL_CONTINUE;
}

static int task_switch_16(struct x86_emulate_ctxt *ctxt,
			  struct x86_emulate_ops *ops,
			  u16 tss_selector, u16 old_tss_sel,
			  ulong old_tss_base, struct desc_struct *new_desc)
{
	struct tss_segment_16 tss_seg;
	int ret;
	u32 err, new_tss_base = get_desc_base(new_desc);

	ret = ops->read_std(old_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			    &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, old_tss_base, err);
		return ret;
	}

	save_state_to_tss16(ctxt, ops, &tss_seg);

	ret = ops->write_std(old_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			     &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, old_tss_base, err);
		return ret;
	}

	ret = ops->read_std(new_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			    &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, new_tss_base, err);
		return ret;
	}

	if (old_tss_sel != 0xffff) {
		tss_seg.prev_task_link = old_tss_sel;

		ret = ops->write_std(new_tss_base,
				     &tss_seg.prev_task_link,
				     sizeof tss_seg.prev_task_link,
				     ctxt->vcpu, &err);
		if (ret == X86EMUL_PROPAGATE_FAULT) {
			/* FIXME: need to provide precise fault address */
			emulate_pf(ctxt, new_tss_base, err);
			return ret;
		}
	}

	return load_state_from_tss16(ctxt, ops, &tss_seg);
}

static void save_state_to_tss32(struct x86_emulate_ctxt *ctxt,
				struct x86_emulate_ops *ops,
				struct tss_segment_32 *tss)
{
	struct decode_cache *c = &ctxt->decode;

	tss->cr3 = ops->get_cr(3, ctxt->vcpu);
	tss->eip = c->eip;
	tss->eflags = ctxt->eflags;
	tss->eax = c->regs[VCPU_REGS_RAX];
	tss->ecx = c->regs[VCPU_REGS_RCX];
	tss->edx = c->regs[VCPU_REGS_RDX];
	tss->ebx = c->regs[VCPU_REGS_RBX];
	tss->esp = c->regs[VCPU_REGS_RSP];
	tss->ebp = c->regs[VCPU_REGS_RBP];
	tss->esi = c->regs[VCPU_REGS_RSI];
	tss->edi = c->regs[VCPU_REGS_RDI];

	tss->es = ops->get_segment_selector(VCPU_SREG_ES, ctxt->vcpu);
	tss->cs = ops->get_segment_selector(VCPU_SREG_CS, ctxt->vcpu);
	tss->ss = ops->get_segment_selector(VCPU_SREG_SS, ctxt->vcpu);
	tss->ds = ops->get_segment_selector(VCPU_SREG_DS, ctxt->vcpu);
	tss->fs = ops->get_segment_selector(VCPU_SREG_FS, ctxt->vcpu);
	tss->gs = ops->get_segment_selector(VCPU_SREG_GS, ctxt->vcpu);
	tss->ldt_selector = ops->get_segment_selector(VCPU_SREG_LDTR, ctxt->vcpu);
}

static int load_state_from_tss32(struct x86_emulate_ctxt *ctxt,
				 struct x86_emulate_ops *ops,
				 struct tss_segment_32 *tss)
{
	struct decode_cache *c = &ctxt->decode;
	int ret;

	if (ops->set_cr(3, tss->cr3, ctxt->vcpu)) {
		emulate_gp(ctxt, 0);
		return X86EMUL_PROPAGATE_FAULT;
	}
	c->eip = tss->eip;
	ctxt->eflags = tss->eflags | 2;
	c->regs[VCPU_REGS_RAX] = tss->eax;
	c->regs[VCPU_REGS_RCX] = tss->ecx;
	c->regs[VCPU_REGS_RDX] = tss->edx;
	c->regs[VCPU_REGS_RBX] = tss->ebx;
	c->regs[VCPU_REGS_RSP] = tss->esp;
	c->regs[VCPU_REGS_RBP] = tss->ebp;
	c->regs[VCPU_REGS_RSI] = tss->esi;
	c->regs[VCPU_REGS_RDI] = tss->edi;

	/*
	 * SDM says that segment selectors are loaded before segment
	 * descriptors
	 */
	ops->set_segment_selector(tss->ldt_selector, VCPU_SREG_LDTR, ctxt->vcpu);
	ops->set_segment_selector(tss->es, VCPU_SREG_ES, ctxt->vcpu);
	ops->set_segment_selector(tss->cs, VCPU_SREG_CS, ctxt->vcpu);
	ops->set_segment_selector(tss->ss, VCPU_SREG_SS, ctxt->vcpu);
	ops->set_segment_selector(tss->ds, VCPU_SREG_DS, ctxt->vcpu);
	ops->set_segment_selector(tss->fs, VCPU_SREG_FS, ctxt->vcpu);
	ops->set_segment_selector(tss->gs, VCPU_SREG_GS, ctxt->vcpu);

	/*
	 * Now load segment descriptors. If fault happenes at this stage
	 * it is handled in a context of new task
	 */
	ret = load_segment_descriptor(ctxt, ops, tss->ldt_selector, VCPU_SREG_LDTR);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->es, VCPU_SREG_ES);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->cs, VCPU_SREG_CS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ss, VCPU_SREG_SS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->ds, VCPU_SREG_DS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->fs, VCPU_SREG_FS);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = load_segment_descriptor(ctxt, ops, tss->gs, VCPU_SREG_GS);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	return X86EMUL_CONTINUE;
}

static int task_switch_32(struct x86_emulate_ctxt *ctxt,
			  struct x86_emulate_ops *ops,
			  u16 tss_selector, u16 old_tss_sel,
			  ulong old_tss_base, struct desc_struct *new_desc)
{
	struct tss_segment_32 tss_seg;
	int ret;
	u32 err, new_tss_base = get_desc_base(new_desc);

	ret = ops->read_std(old_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			    &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, old_tss_base, err);
		return ret;
	}

	save_state_to_tss32(ctxt, ops, &tss_seg);

	ret = ops->write_std(old_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			     &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, old_tss_base, err);
		return ret;
	}

	ret = ops->read_std(new_tss_base, &tss_seg, sizeof tss_seg, ctxt->vcpu,
			    &err);
	if (ret == X86EMUL_PROPAGATE_FAULT) {
		/* FIXME: need to provide precise fault address */
		emulate_pf(ctxt, new_tss_base, err);
		return ret;
	}

	if (old_tss_sel != 0xffff) {
		tss_seg.prev_task_link = old_tss_sel;

		ret = ops->write_std(new_tss_base,
				     &tss_seg.prev_task_link,
				     sizeof tss_seg.prev_task_link,
				     ctxt->vcpu, &err);
		if (ret == X86EMUL_PROPAGATE_FAULT) {
			/* FIXME: need to provide precise fault address */
			emulate_pf(ctxt, new_tss_base, err);
			return ret;
		}
	}

	return load_state_from_tss32(ctxt, ops, &tss_seg);
}

static int emulator_do_task_switch(struct x86_emulate_ctxt *ctxt,
				   struct x86_emulate_ops *ops,
				   u16 tss_selector, int reason,
				   bool has_error_code, u32 error_code)
{
	struct desc_struct curr_tss_desc, next_tss_desc;
	int ret;
	u16 old_tss_sel = ops->get_segment_selector(VCPU_SREG_TR, ctxt->vcpu);
	ulong old_tss_base =
		ops->get_cached_segment_base(VCPU_SREG_TR, ctxt->vcpu);
	u32 desc_limit;

	/* FIXME: old_tss_base == ~0 ? */

	ret = read_segment_descriptor(ctxt, ops, tss_selector, &next_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;
	ret = read_segment_descriptor(ctxt, ops, old_tss_sel, &curr_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	/* FIXME: check that next_tss_desc is tss */

	if (reason != TASK_SWITCH_IRET) {
		if ((tss_selector & 3) > next_tss_desc.dpl ||
		    ops->cpl(ctxt->vcpu) > next_tss_desc.dpl) {
			emulate_gp(ctxt, 0);
			return X86EMUL_PROPAGATE_FAULT;
		}
	}

	desc_limit = desc_limit_scaled(&next_tss_desc);
	if (!next_tss_desc.p ||
	    ((desc_limit < 0x67 && (next_tss_desc.type & 8)) ||
	     desc_limit < 0x2b)) {
		emulate_ts(ctxt, tss_selector & 0xfffc);
		return X86EMUL_PROPAGATE_FAULT;
	}

	if (reason == TASK_SWITCH_IRET || reason == TASK_SWITCH_JMP) {
		curr_tss_desc.type &= ~(1 << 1); /* clear busy flag */
		write_segment_descriptor(ctxt, ops, old_tss_sel,
					 &curr_tss_desc);
	}

	if (reason == TASK_SWITCH_IRET)
		ctxt->eflags = ctxt->eflags & ~X86_EFLAGS_NT;

	/* set back link to prev task only if NT bit is set in eflags
	   note that old_tss_sel is not used afetr this point */
	if (reason != TASK_SWITCH_CALL && reason != TASK_SWITCH_GATE)
		old_tss_sel = 0xffff;

	if (next_tss_desc.type & 8)
		ret = task_switch_32(ctxt, ops, tss_selector, old_tss_sel,
				     old_tss_base, &next_tss_desc);
	else
		ret = task_switch_16(ctxt, ops, tss_selector, old_tss_sel,
				     old_tss_base, &next_tss_desc);
	if (ret != X86EMUL_CONTINUE)
		return ret;

	if (reason == TASK_SWITCH_CALL || reason == TASK_SWITCH_GATE)
		ctxt->eflags = ctxt->eflags | X86_EFLAGS_NT;

	if (reason != TASK_SWITCH_IRET) {
		next_tss_desc.type |= (1 << 1); /* set busy flag */
		write_segment_descriptor(ctxt, ops, tss_selector,
					 &next_tss_desc);
	}

	ops->set_cr(0,  ops->get_cr(0, ctxt->vcpu) | X86_CR0_TS, ctxt->vcpu);
	ops->set_cached_descriptor(&next_tss_desc, VCPU_SREG_TR, ctxt->vcpu);
	ops->set_segment_selector(tss_selector, VCPU_SREG_TR, ctxt->vcpu);

	if (has_error_code) {
		struct decode_cache *c = &ctxt->decode;

		c->op_bytes = c->ad_bytes = (next_tss_desc.type & 8) ? 4 : 2;
		c->lock_prefix = 0;
		c->src.val = (unsigned long) error_code;
		emulate_push(ctxt, ops);
	}

	return ret;
}

int emulator_task_switch(struct x86_emulate_ctxt *ctxt,
			 struct x86_emulate_ops *ops,
			 u16 tss_selector, int reason,
			 bool has_error_code, u32 error_code)
{
	struct decode_cache *c = &ctxt->decode;
	int rc;

	c->eip = ctxt->eip;
	c->dst.type = OP_NONE;

	rc = emulator_do_task_switch(ctxt, ops, tss_selector, reason,
				     has_error_code, error_code);

	if (rc == X86EMUL_CONTINUE) {
		rc = writeback(ctxt, ops);
		if (rc == X86EMUL_CONTINUE)
			ctxt->eip = c->eip;
	}

	return (rc == X86EMUL_UNHANDLEABLE) ? -1 : 0;
}

static void string_addr_inc(struct x86_emulate_ctxt *ctxt, unsigned long base,
			    int reg, struct operand *op)
{
	struct decode_cache *c = &ctxt->decode;
	int df = (ctxt->eflags & EFLG_DF) ? -1 : 1;

	register_address_increment(c, &c->regs[reg], df * op->bytes);
	op->ptr = (unsigned long *)register_address(c,  base, c->regs[reg]);
}

int
x86_emulate_insn(struct x86_emulate_ctxt *ctxt, struct x86_emulate_ops *ops)
{
	u64 msr_data;
	struct decode_cache *c = &ctxt->decode;
	int rc = X86EMUL_CONTINUE;
	int saved_dst_type = c->dst.type;

	ctxt->decode.mem_read.pos = 0;

	if (ctxt->mode == X86EMUL_MODE_PROT64 && (c->d & No64)) {
		emulate_ud(ctxt);
		goto done;
	}

	/* LOCK prefix is allowed only with some instructions */
	if (c->lock_prefix && (!(c->d & Lock) || c->dst.type != OP_MEM)) {
		emulate_ud(ctxt);
		goto done;
	}

	/* Privileged instruction can be executed only in CPL=0 */
	if ((c->d & Priv) && ops->cpl(ctxt->vcpu)) {
		emulate_gp(ctxt, 0);
		goto done;
	}

	if (c->rep_prefix && (c->d & String)) {
		ctxt->restart = true;
		/* All REP prefixes have the same first termination condition */
		if (address_mask(c, c->regs[VCPU_REGS_RCX]) == 0) {
		string_done:
			ctxt->restart = false;
			ctxt->eip = c->eip;
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
			    ((ctxt->eflags & EFLG_ZF) == 0))
				goto string_done;
			if ((c->rep_prefix == REPNE_PREFIX) &&
			    ((ctxt->eflags & EFLG_ZF) == EFLG_ZF))
				goto string_done;
		}
		c->eip = ctxt->eip;
	}

	if (c->src.type == OP_MEM) {
		rc = read_emulated(ctxt, ops, (unsigned long)c->src.ptr,
					c->src.valptr, c->src.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		c->src.orig_val64 = c->src.val64;
	}

	if (c->src2.type == OP_MEM) {
		rc = read_emulated(ctxt, ops, (unsigned long)c->src2.ptr,
					&c->src2.val, c->src2.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
	}

	if ((c->d & DstMask) == ImplicitOps)
		goto special_insn;


	if ((c->dst.type == OP_MEM) && !(c->d & Mov)) {
		/* optimisation - avoid slow emulated read if Mov */
		rc = read_emulated(ctxt, ops, (unsigned long)c->dst.ptr,
				   &c->dst.val, c->dst.bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
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
		emulate_push_sreg(ctxt, ops, VCPU_SREG_ES);
		break;
	case 0x07:		/* pop es */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_ES);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x08 ... 0x0d:
	      or:		/* or */
		emulate_2op_SrcV("or", c->src, c->dst, ctxt->eflags);
		break;
	case 0x0e:		/* push cs */
		emulate_push_sreg(ctxt, ops, VCPU_SREG_CS);
		break;
	case 0x10 ... 0x15:
	      adc:		/* adc */
		emulate_2op_SrcV("adc", c->src, c->dst, ctxt->eflags);
		break;
	case 0x16:		/* push ss */
		emulate_push_sreg(ctxt, ops, VCPU_SREG_SS);
		break;
	case 0x17:		/* pop ss */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_SS);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x18 ... 0x1d:
	      sbb:		/* sbb */
		emulate_2op_SrcV("sbb", c->src, c->dst, ctxt->eflags);
		break;
	case 0x1e:		/* push ds */
		emulate_push_sreg(ctxt, ops, VCPU_SREG_DS);
		break;
	case 0x1f:		/* pop ds */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_DS);
		if (rc != X86EMUL_CONTINUE)
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
		emulate_push(ctxt, ops);
		break;
	case 0x58 ... 0x5f: /* pop reg */
	pop_instruction:
		rc = emulate_pop(ctxt, ops, &c->dst.val, c->op_bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x60:	/* pusha */
		rc = emulate_pusha(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x61:	/* popa */
		rc = emulate_popa(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x63:		/* movsxd */
		if (ctxt->mode != X86EMUL_MODE_PROT64)
			goto cannot_emulate;
		c->dst.val = (s32) c->src.val;
		break;
	case 0x68: /* push imm */
	case 0x6a: /* push imm8 */
		emulate_push(ctxt, ops);
		break;
	case 0x6c:		/* insb */
	case 0x6d:		/* insw/insd */
		c->dst.bytes = min(c->dst.bytes, 4u);
		if (!emulator_io_permited(ctxt, ops, c->regs[VCPU_REGS_RDX],
					  c->dst.bytes)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		if (!pio_in_emulated(ctxt, ops, c->dst.bytes,
				     c->regs[VCPU_REGS_RDX], &c->dst.val))
			goto done; /* IO is needed, skip writeback */
		break;
	case 0x6e:		/* outsb */
	case 0x6f:		/* outsw/outsd */
		c->src.bytes = min(c->src.bytes, 4u);
		if (!emulator_io_permited(ctxt, ops, c->regs[VCPU_REGS_RDX],
					  c->src.bytes)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		ops->pio_out_emulated(c->src.bytes, c->regs[VCPU_REGS_RDX],
				      &c->src.val, 1, ctxt->vcpu);

		c->dst.type = OP_NONE; /* nothing to writeback */
		break;
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
	test:
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
	case 0x8c:  /* mov r/m, sreg */
		if (c->modrm_reg > VCPU_SREG_GS) {
			emulate_ud(ctxt);
			goto done;
		}
		c->dst.val = ops->get_segment_selector(c->modrm_reg, ctxt->vcpu);
		break;
	case 0x8d: /* lea r16/r32, m */
		c->dst.val = c->modrm_ea;
		break;
	case 0x8e: { /* mov seg, r/m16 */
		uint16_t sel;

		sel = c->src.val;

		if (c->modrm_reg == VCPU_SREG_CS ||
		    c->modrm_reg > VCPU_SREG_GS) {
			emulate_ud(ctxt);
			goto done;
		}

		if (c->modrm_reg == VCPU_SREG_SS)
			ctxt->interruptibility = KVM_X86_SHADOW_INT_MOV_SS;

		rc = load_segment_descriptor(ctxt, ops, sel, c->modrm_reg);

		c->dst.type = OP_NONE;  /* Disable writeback. */
		break;
	}
	case 0x8f:		/* pop (sole member of Grp1a) */
		rc = emulate_grp1a(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0x90: /* nop / xchg r8,rax */
		if (c->dst.ptr == (unsigned long *)&c->regs[VCPU_REGS_RAX]) {
			c->dst.type = OP_NONE;  /* nop */
			break;
		}
	case 0x91 ... 0x97: /* xchg reg,rax */
		c->src.type = OP_REG;
		c->src.bytes = c->op_bytes;
		c->src.ptr = (unsigned long *) &c->regs[VCPU_REGS_RAX];
		c->src.val = *(c->src.ptr);
		goto xchg;
	case 0x9c: /* pushf */
		c->src.val =  (unsigned long) ctxt->eflags;
		emulate_push(ctxt, ops);
		break;
	case 0x9d: /* popf */
		c->dst.type = OP_REG;
		c->dst.ptr = (unsigned long *) &ctxt->eflags;
		c->dst.bytes = c->op_bytes;
		rc = emulate_popf(ctxt, ops, &c->dst.val, c->op_bytes);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0xa0 ... 0xa3:	/* mov */
	case 0xa4 ... 0xa5:	/* movs */
		goto mov;
	case 0xa6 ... 0xa7:	/* cmps */
		c->dst.type = OP_NONE; /* Disable writeback. */
		DPRINTF("cmps: mem1=0x%p mem2=0x%p\n", c->src.ptr, c->dst.ptr);
		goto cmp;
	case 0xa8 ... 0xa9:	/* test ax, imm */
		goto test;
	case 0xaa ... 0xab:	/* stos */
		c->dst.val = c->regs[VCPU_REGS_RAX];
		break;
	case 0xac ... 0xad:	/* lods */
		goto mov;
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
		if (rc != X86EMUL_CONTINUE)
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
		goto do_io_in;
	case 0xe6: /* outb */
	case 0xe7: /* out */
		goto do_io_out;
	case 0xe8: /* call (near) */ {
		long int rel = c->src.val;
		c->src.val = (unsigned long) c->eip;
		jmp_rel(c, rel);
		emulate_push(ctxt, ops);
		break;
	}
	case 0xe9: /* jmp rel */
		goto jmp;
	case 0xea: { /* jmp far */
		unsigned short sel;
	jump_far:
		memcpy(&sel, c->src.valptr + c->op_bytes, 2);

		if (load_segment_descriptor(ctxt, ops, sel, VCPU_SREG_CS))
			goto done;

		c->eip = 0;
		memcpy(&c->eip, c->src.valptr, c->op_bytes);
		break;
	}
	case 0xeb:
	      jmp:		/* jmp rel short */
		jmp_rel(c, c->src.val);
		c->dst.type = OP_NONE; /* Disable writeback. */
		break;
	case 0xec: /* in al,dx */
	case 0xed: /* in (e/r)ax,dx */
		c->src.val = c->regs[VCPU_REGS_RDX];
	do_io_in:
		c->dst.bytes = min(c->dst.bytes, 4u);
		if (!emulator_io_permited(ctxt, ops, c->src.val, c->dst.bytes)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		if (!pio_in_emulated(ctxt, ops, c->dst.bytes, c->src.val,
				     &c->dst.val))
			goto done; /* IO is needed */
		break;
	case 0xee: /* out dx,al */
	case 0xef: /* out dx,(e/r)ax */
		c->src.val = c->regs[VCPU_REGS_RDX];
	do_io_out:
		c->dst.bytes = min(c->dst.bytes, 4u);
		if (!emulator_io_permited(ctxt, ops, c->src.val, c->dst.bytes)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		ops->pio_out_emulated(c->dst.bytes, c->src.val, &c->dst.val, 1,
				      ctxt->vcpu);
		c->dst.type = OP_NONE;	/* Disable writeback. */
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
		if (!emulate_grp3(ctxt, ops))
			goto cannot_emulate;
		break;
	case 0xf8: /* clc */
		ctxt->eflags &= ~EFLG_CF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfa: /* cli */
		if (emulator_bad_iopl(ctxt, ops)) {
			emulate_gp(ctxt, 0);
			goto done;
		} else {
			ctxt->eflags &= ~X86_EFLAGS_IF;
			c->dst.type = OP_NONE;	/* Disable writeback. */
		}
		break;
	case 0xfb: /* sti */
		if (emulator_bad_iopl(ctxt, ops)) {
			emulate_gp(ctxt, 0);
			goto done;
		} else {
			ctxt->interruptibility = KVM_X86_SHADOW_INT_STI;
			ctxt->eflags |= X86_EFLAGS_IF;
			c->dst.type = OP_NONE;	/* Disable writeback. */
		}
		break;
	case 0xfc: /* cld */
		ctxt->eflags &= ~EFLG_DF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfd: /* std */
		ctxt->eflags |= EFLG_DF;
		c->dst.type = OP_NONE;	/* Disable writeback. */
		break;
	case 0xfe: /* Grp4 */
	grp45:
		rc = emulate_grp45(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	case 0xff: /* Grp5 */
		if (c->modrm_reg == 5)
			goto jump_far;
		goto grp45;
	}

writeback:
	rc = writeback(ctxt, ops);
	if (rc != X86EMUL_CONTINUE)
		goto done;

	/*
	 * restore dst type in case the decoding will be reused
	 * (happens for string instruction )
	 */
	c->dst.type = saved_dst_type;

	if ((c->d & SrcMask) == SrcSI)
		string_addr_inc(ctxt, seg_override_base(ctxt, ops, c),
				VCPU_REGS_RSI, &c->src);

	if ((c->d & DstMask) == DstDI)
		string_addr_inc(ctxt, es_base(ctxt, ops), VCPU_REGS_RDI,
				&c->dst);

	if (c->rep_prefix && (c->d & String)) {
		struct read_cache *rc = &ctxt->decode.io_read;
		register_address_increment(c, &c->regs[VCPU_REGS_RCX], -1);
		/*
		 * Re-enter guest when pio read ahead buffer is empty or,
		 * if it is not used, after each 1024 iteration.
		 */
		if ((rc->end == 0 && !(c->regs[VCPU_REGS_RCX] & 0x3ff)) ||
		    (rc->end != 0 && rc->end == rc->pos))
			ctxt->restart = false;
	}
	/*
	 * reset read cache here in case string instruction is restared
	 * without decoding
	 */
	ctxt->decode.mem_read.end = 0;
	ctxt->eip = c->eip;

done:
	return (rc == X86EMUL_UNHANDLEABLE) ? -1 : 0;

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
			if (rc != X86EMUL_CONTINUE)
				goto done;

			/* Let the processor re-execute the fixed hypercall */
			c->eip = ctxt->eip;
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		case 2: /* lgdt */
			rc = read_descriptor(ctxt, ops, c->src.ptr,
					     &size, &address, c->op_bytes);
			if (rc != X86EMUL_CONTINUE)
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
					if (rc != X86EMUL_CONTINUE)
						goto done;
					break;
				default:
					goto cannot_emulate;
				}
			} else {
				rc = read_descriptor(ctxt, ops, c->src.ptr,
						     &size, &address,
						     c->op_bytes);
				if (rc != X86EMUL_CONTINUE)
					goto done;
				realmode_lidt(ctxt->vcpu, size, address);
			}
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		case 4: /* smsw */
			c->dst.bytes = 2;
			c->dst.val = ops->get_cr(0, ctxt->vcpu);
			break;
		case 6: /* lmsw */
			ops->set_cr(0, (ops->get_cr(0, ctxt->vcpu) & ~0x0ful) |
				    (c->src.val & 0x0f), ctxt->vcpu);
			c->dst.type = OP_NONE;
			break;
		case 5: /* not defined */
			emulate_ud(ctxt);
			goto done;
		case 7: /* invlpg*/
			emulate_invlpg(ctxt->vcpu, c->modrm_ea);
			/* Disable writeback. */
			c->dst.type = OP_NONE;
			break;
		default:
			goto cannot_emulate;
		}
		break;
	case 0x05: 		/* syscall */
		rc = emulate_syscall(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		else
			goto writeback;
		break;
	case 0x06:
		emulate_clts(ctxt->vcpu);
		c->dst.type = OP_NONE;
		break;
	case 0x09:		/* wbinvd */
		kvm_emulate_wbinvd(ctxt->vcpu);
		c->dst.type = OP_NONE;
		break;
	case 0x08:		/* invd */
	case 0x0d:		/* GrpP (prefetch) */
	case 0x18:		/* Grp16 (prefetch/nop) */
		c->dst.type = OP_NONE;
		break;
	case 0x20: /* mov cr, reg */
		switch (c->modrm_reg) {
		case 1:
		case 5 ... 7:
		case 9 ... 15:
			emulate_ud(ctxt);
			goto done;
		}
		c->regs[c->modrm_rm] = ops->get_cr(c->modrm_reg, ctxt->vcpu);
		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x21: /* mov from dr to reg */
		if ((ops->get_cr(4, ctxt->vcpu) & X86_CR4_DE) &&
		    (c->modrm_reg == 4 || c->modrm_reg == 5)) {
			emulate_ud(ctxt);
			goto done;
		}
		ops->get_dr(c->modrm_reg, &c->regs[c->modrm_rm], ctxt->vcpu);
		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x22: /* mov reg, cr */
		if (ops->set_cr(c->modrm_reg, c->modrm_val, ctxt->vcpu)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		c->dst.type = OP_NONE;
		break;
	case 0x23: /* mov from reg to dr */
		if ((ops->get_cr(4, ctxt->vcpu) & X86_CR4_DE) &&
		    (c->modrm_reg == 4 || c->modrm_reg == 5)) {
			emulate_ud(ctxt);
			goto done;
		}

		if (ops->set_dr(c->modrm_reg, c->regs[c->modrm_rm] &
				((ctxt->mode == X86EMUL_MODE_PROT64) ?
				 ~0ULL : ~0U), ctxt->vcpu) < 0) {
			/* #UD condition is already handled by the code above */
			emulate_gp(ctxt, 0);
			goto done;
		}

		c->dst.type = OP_NONE;	/* no writeback */
		break;
	case 0x30:
		/* wrmsr */
		msr_data = (u32)c->regs[VCPU_REGS_RAX]
			| ((u64)c->regs[VCPU_REGS_RDX] << 32);
		if (ops->set_msr(ctxt->vcpu, c->regs[VCPU_REGS_RCX], msr_data)) {
			emulate_gp(ctxt, 0);
			goto done;
		}
		rc = X86EMUL_CONTINUE;
		c->dst.type = OP_NONE;
		break;
	case 0x32:
		/* rdmsr */
		if (ops->get_msr(ctxt->vcpu, c->regs[VCPU_REGS_RCX], &msr_data)) {
			emulate_gp(ctxt, 0);
			goto done;
		} else {
			c->regs[VCPU_REGS_RAX] = (u32)msr_data;
			c->regs[VCPU_REGS_RDX] = msr_data >> 32;
		}
		rc = X86EMUL_CONTINUE;
		c->dst.type = OP_NONE;
		break;
	case 0x34:		/* sysenter */
		rc = emulate_sysenter(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		else
			goto writeback;
		break;
	case 0x35:		/* sysexit */
		rc = emulate_sysexit(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
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
		emulate_push_sreg(ctxt, ops, VCPU_SREG_FS);
		break;
	case 0xa1:	 /* pop fs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_FS);
		if (rc != X86EMUL_CONTINUE)
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
		emulate_push_sreg(ctxt, ops, VCPU_SREG_GS);
		break;
	case 0xa9:	/* pop gs */
		rc = emulate_pop_sreg(ctxt, ops, VCPU_SREG_GS);
		if (rc != X86EMUL_CONTINUE)
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
		rc = emulate_grp9(ctxt, ops);
		if (rc != X86EMUL_CONTINUE)
			goto done;
		break;
	}
	goto writeback;

cannot_emulate:
	DPRINTF("Cannot emulate %02x\n", c->b);
	return -1;
}

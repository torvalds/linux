/*
 * Copyright 2009 Freescale Semicondutor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_PPC_OPCODE_H
#define _ASM_POWERPC_PPC_OPCODE_H

#include <linux/stringify.h>
#include <asm/asm-compat.h>

/* sorted alphabetically */
#define PPC_INST_DCBA			0x7c0005ec
#define PPC_INST_DCBA_MASK		0xfc0007fe
#define PPC_INST_DCBAL			0x7c2005ec
#define PPC_INST_DCBZL			0x7c2007ec
#define PPC_INST_ISEL			0x7c00001e
#define PPC_INST_ISEL_MASK		0xfc00003e
#define PPC_INST_LSWI			0x7c0004aa
#define PPC_INST_LSWX			0x7c00042a
#define PPC_INST_LWSYNC			0x7c2004ac
#define PPC_INST_MCRXR			0x7c000400
#define PPC_INST_MCRXR_MASK		0xfc0007fe
#define PPC_INST_MFSPR_PVR		0x7c1f42a6
#define PPC_INST_MFSPR_PVR_MASK		0xfc1fffff
#define PPC_INST_MSGSND			0x7c00019c
#define PPC_INST_NOP			0x60000000
#define PPC_INST_POPCNTB		0x7c0000f4
#define PPC_INST_POPCNTB_MASK		0xfc0007fe
#define PPC_INST_RFCI			0x4c000066
#define PPC_INST_RFDI			0x4c00004e
#define PPC_INST_RFMCI			0x4c00004c

#define PPC_INST_STRING			0x7c00042a
#define PPC_INST_STRING_MASK		0xfc0007fe
#define PPC_INST_STRING_GEN_MASK	0xfc00067e

#define PPC_INST_STSWI			0x7c0005aa
#define PPC_INST_STSWX			0x7c00052a
#define PPC_INST_TLBILX			0x7c000626
#define PPC_INST_WAIT			0x7c00007c

/* macros to insert fields into opcodes */
#define __PPC_RA(a)	((a & 0x1f) << 16)
#define __PPC_RB(b)	((b & 0x1f) << 11)
#define __PPC_T_TLB(t)	((t & 0x3) << 21)
#define __PPC_WC(w)	((w & 0x3) << 21)

/* Deal with instructions that older assemblers aren't aware of */
#define	PPC_DCBAL(a, b)		stringify_in_c(.long PPC_INST_DCBAL | \
					__PPC_RA(a) | __PPC_RB(b))
#define	PPC_DCBZL(a, b)		stringify_in_c(.long PPC_INST_DCBZL | \
					__PPC_RA(a) | __PPC_RB(b))
#define PPC_MSGSND(b)		stringify_in_c(.long PPC_INST_MSGSND | \
					__PPC_RB(b))
#define PPC_RFCI		stringify_in_c(.long PPC_INST_RFCI)
#define PPC_RFDI		stringify_in_c(.long PPC_INST_RFDI)
#define PPC_RFMCI		stringify_in_c(.long PPC_INST_RFMCI)
#define PPC_TLBILX(t, a, b)	stringify_in_c(.long PPC_INST_TLBILX | \
					__PPC_T_TLB(t) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_TLBILX_ALL(a, b)	PPC_TLBILX(0, a, b)
#define PPC_TLBILX_PID(a, b)	PPC_TLBILX(1, a, b)
#define PPC_TLBILX_VA(a, b)	PPC_TLBILX(3, a, b)
#define PPC_WAIT(w)		stringify_in_c(.long PPC_INST_WAIT | \
					__PPC_WC(w))

#endif /* _ASM_POWERPC_PPC_OPCODE_H */

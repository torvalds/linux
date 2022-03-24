/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_INSN_DEF_H
#define __ASM_INSN_DEF_H

#include <asm/brk-imm.h>

/* A64 instructions are always 32 bits. */
#define	AARCH64_INSN_SIZE		4

/*
 * BRK instruction encoding
 * The #imm16 value should be placed at bits[20:5] within BRK ins
 */
#define AARCH64_BREAK_MON	0xd4200000

/*
 * BRK instruction for provoking a fault on purpose
 * Unlike kgdb, #imm16 value with unallocated handler is used for faulting.
 */
#define AARCH64_BREAK_FAULT	(AARCH64_BREAK_MON | (FAULT_BRK_IMM << 5))

#endif /* __ASM_INSN_DEF_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/hexagon/include/asm/kgdb.h - Hexagon KGDB Support
 *
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 */

#ifndef __HEXAGON_KGDB_H__
#define __HEXAGON_KGDB_H__

#define BREAK_INSTR_SIZE 4
#define CACHE_FLUSH_IS_SAFE   1
#define BUFMAX       ((NUMREGBYTES * 2) + 512)

static inline void arch_kgdb_breakpoint(void)
{
	asm("trap0(#0xDB)");
}

/* Registers:
 * 32 gpr + sa0/1 + lc0/1 + m0/1 + gp + ugp + pred + pc = 42 total.
 * vm regs = psp+elr+est+badva = 4
 * syscall+restart = 2 more
 * also add cs0/1 = 2
 * so 48 = 42 + 4 + 2 + 2
 */
#define DBG_USER_REGS 42
#define DBG_MAX_REG_NUM (DBG_USER_REGS + 8)
#define NUMREGBYTES  (DBG_MAX_REG_NUM*4)

#endif /* __HEXAGON_KGDB_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KGDB_H
#define _ASM_X86_KGDB_H

/*
 * Copyright (C) 2001-2004 Amit S. Kale
 * Copyright (C) 2008 Wind River Systems, Inc.
 */

#include <asm/ptrace.h>

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound
 * buffers at least NUMREGBYTES*2 are needed for register packets
 * Longer buffer is needed to list all threads
 */
#define BUFMAX			1024

/*
 *  Note that this register image is in a different order than
 *  the register image that Linux produces at interrupt time.
 *
 *  Linux's register image is defined by struct pt_regs in ptrace.h.
 *  Just why GDB uses a different order is a historical mystery.
 */
#ifdef CONFIG_X86_32
enum regnames {
	GDB_AX,			/* 0 */
	GDB_CX,			/* 1 */
	GDB_DX,			/* 2 */
	GDB_BX,			/* 3 */
	GDB_SP,			/* 4 */
	GDB_BP,			/* 5 */
	GDB_SI,			/* 6 */
	GDB_DI,			/* 7 */
	GDB_PC,			/* 8 also known as eip */
	GDB_PS,			/* 9 also known as eflags */
	GDB_CS,			/* 10 */
	GDB_SS,			/* 11 */
	GDB_DS,			/* 12 */
	GDB_ES,			/* 13 */
	GDB_FS,			/* 14 */
	GDB_GS,			/* 15 */
};
#define GDB_ORIG_AX		41
#define DBG_MAX_REG_NUM		16
#define NUMREGBYTES		((GDB_GS+1)*4)
#else /* ! CONFIG_X86_32 */
enum regnames {
	GDB_AX,			/* 0 */
	GDB_BX,			/* 1 */
	GDB_CX,			/* 2 */
	GDB_DX,			/* 3 */
	GDB_SI,			/* 4 */
	GDB_DI,			/* 5 */
	GDB_BP,			/* 6 */
	GDB_SP,			/* 7 */
	GDB_R8,			/* 8 */
	GDB_R9,			/* 9 */
	GDB_R10,		/* 10 */
	GDB_R11,		/* 11 */
	GDB_R12,		/* 12 */
	GDB_R13,		/* 13 */
	GDB_R14,		/* 14 */
	GDB_R15,		/* 15 */
	GDB_PC,			/* 16 */
	GDB_PS,			/* 17 */
	GDB_CS,			/* 18 */
	GDB_SS,			/* 19 */
	GDB_DS,			/* 20 */
	GDB_ES,			/* 21 */
	GDB_FS,			/* 22 */
	GDB_GS,			/* 23 */
};
#define GDB_ORIG_AX		57
#define DBG_MAX_REG_NUM		24
/* 17 64 bit regs and 5 32 bit regs */
#define NUMREGBYTES		((17 * 8) + (5 * 4))
#endif /* ! CONFIG_X86_32 */

static inline void arch_kgdb_breakpoint(void)
{
	asm("   int $3");
}
#define BREAK_INSTR_SIZE	1
#define CACHE_FLUSH_IS_SAFE	1
#define GDB_ADJUSTS_BREAK_OFFSET

extern int kgdb_ll_trap(int cmd, const char *str,
			struct pt_regs *regs, long err, int trap, int sig);

#endif /* _ASM_X86_KGDB_H */

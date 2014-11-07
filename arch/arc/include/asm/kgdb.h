/*
 * kgdb support for ARC
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARC_KGDB_H__
#define __ARC_KGDB_H__

#ifdef CONFIG_KGDB

#include <asm/ptrace.h>

/* to ensure compatibility with Linux 2.6.35, we don't implement the get/set
 * register API yet */
#undef DBG_MAX_REG_NUM

#define GDB_MAX_REGS		87

#define BREAK_INSTR_SIZE	2
#define CACHE_FLUSH_IS_SAFE	1
#define NUMREGBYTES		(GDB_MAX_REGS * 4)
#define BUFMAX			2048

static inline void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ ("trap_s	0x4\n");
}

extern void kgdb_trap(struct pt_regs *regs);

/* This is the numbering of registers according to the GDB. See GDB's
 * arc-tdep.h for details.
 *
 * Registers are ordered for GDB 7.5. It is incompatible with GDB 6.8. */
enum arc_linux_regnums {
	_R0		= 0,
	_R1, _R2, _R3, _R4, _R5, _R6, _R7, _R8, _R9, _R10, _R11, _R12, _R13,
	_R14, _R15, _R16, _R17, _R18, _R19, _R20, _R21, _R22, _R23, _R24,
	_R25, _R26,
	_FP		= 27,
	__SP		= 28,
	_R30		= 30,
	_BLINK		= 31,
	_LP_COUNT	= 60,
	_STOP_PC	= 64,
	_RET		= 64,
	_LP_START	= 65,
	_LP_END		= 66,
	_STATUS32	= 67,
	_ECR		= 76,
	_BTA		= 82,
};

#else
#define kgdb_trap(regs)
#endif

#endif	/* __ARC_KGDB_H__ */

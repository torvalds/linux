/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_KGDB_H
#define __ASM_SH_KGDB_H

#include <asm/cacheflush.h>
#include <asm/ptrace.h>

enum regnames {
	GDB_R0, GDB_R1, GDB_R2, GDB_R3, GDB_R4, GDB_R5, GDB_R6, GDB_R7,
	GDB_R8, GDB_R9, GDB_R10, GDB_R11, GDB_R12, GDB_R13, GDB_R14, GDB_R15,

	GDB_PC, GDB_PR, GDB_SR, GDB_GBR, GDB_MACH, GDB_MACL, GDB_VBR,
};

#define _GP_REGS	16
#define _EXTRA_REGS	7
#define GDB_SIZEOF_REG	sizeof(u32)

#define DBG_MAX_REG_NUM	(_GP_REGS + _EXTRA_REGS)
#define NUMREGBYTES	(DBG_MAX_REG_NUM * sizeof(GDB_SIZEOF_REG))

static inline void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ ("trapa #0x3c\n");
}

#define BREAK_INSTR_SIZE	2
#define BUFMAX			2048

#ifdef CONFIG_SMP
# define CACHE_FLUSH_IS_SAFE	0
#else
# define CACHE_FLUSH_IS_SAFE	1
#endif

#define GDB_ADJUSTS_BREAK_OFFSET

#endif /* __ASM_SH_KGDB_H */

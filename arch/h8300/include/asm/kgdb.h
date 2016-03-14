/*
 * Copyright (C) 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_H8300_KGDB_H
#define _ASM_H8300_KGDB_H

#define CACHE_FLUSH_IS_SAFE	1
#define BUFMAX			2048

enum regnames {
	GDB_ER0, GDB_ER1, GDB_ER2, GDB_ER3,
	GDB_ER4, GDB_ER5, GDB_ER6, GDB_SP,
	GDB_CCR, GDB_PC,
	GDB_CYCLLE,
#if defined(CONFIG_CPU_H8S)
	GDB_EXR,
#endif
	GDB_TICK, GDB_INST,
#if defined(CONFIG_CPU_H8S)
	GDB_MACH, GDB_MACL,
#endif
	/* do not change the last entry or anything below! */
	GDB_NUMREGBYTES,		/* number of registers */
};

#define GDB_SIZEOF_REG		sizeof(u32)
#if defined(CONFIG_CPU_H8300H)
#define DBG_MAX_REG_NUM		(13)
#elif defined(CONFIG_CPU_H8S)
#define DBG_MAX_REG_NUM		(14)
#endif
#define NUMREGBYTES		(DBG_MAX_REG_NUM * GDB_SIZEOF_REG)

#define BREAK_INSTR_SIZE	2
static inline void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__("trapa #2");
}

#endif /* _ASM_H8300_KGDB_H */

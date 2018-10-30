/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_BUG_H
#define _ASM_RISCV_BUG_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#include <asm/asm.h>

#ifdef CONFIG_GENERIC_BUG
#define __BUG_INSN	_AC(0x00100073, UL) /* ebreak */

#ifndef __ASSEMBLY__
typedef u32 bug_insn_t;

#ifdef CONFIG_GENERIC_BUG_RELATIVE_POINTERS
#define __BUG_ENTRY_ADDR	RISCV_INT " 1b - 2b"
#define __BUG_ENTRY_FILE	RISCV_INT " %0 - 2b"
#else
#define __BUG_ENTRY_ADDR	RISCV_PTR " 1b"
#define __BUG_ENTRY_FILE	RISCV_PTR " %0"
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY			\
	__BUG_ENTRY_ADDR "\n\t"		\
	__BUG_ENTRY_FILE "\n\t"		\
	RISCV_SHORT " %1"
#else
#define __BUG_ENTRY			\
	__BUG_ENTRY_ADDR
#endif

#define BUG()							\
do {								\
	__asm__ __volatile__ (					\
		"1:\n\t"					\
			"ebreak\n"				\
			".pushsection __bug_table,\"a\"\n\t"	\
		"2:\n\t"					\
			__BUG_ENTRY "\n\t"			\
			".org 2b + %2\n\t"			\
			".popsection"				\
		:						\
		: "i" (__FILE__), "i" (__LINE__),		\
		  "i" (sizeof(struct bug_entry)));		\
	unreachable();						\
} while (0)
#endif /* !__ASSEMBLY__ */
#else /* CONFIG_GENERIC_BUG */
#ifndef __ASSEMBLY__
#define BUG()							\
do {								\
	__asm__ __volatile__ ("ebreak\n");			\
	unreachable();						\
} while (0)
#endif /* !__ASSEMBLY__ */
#endif /* CONFIG_GENERIC_BUG */

#define HAVE_ARCH_BUG

#include <asm-generic/bug.h>

#ifndef __ASSEMBLY__

struct pt_regs;
struct task_struct;

extern void die(struct pt_regs *regs, const char *str);
extern void do_trap(struct pt_regs *regs, int signo, int code,
	unsigned long addr, struct task_struct *tsk);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_RISCV_BUG_H */

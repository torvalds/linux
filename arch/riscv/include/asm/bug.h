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

#ifndef _ASM_RISCV__H
#define _ASM_RISCV__H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#include <asm/asm.h>

#ifdef CONFIG_GENERIC_
#define ___INSN	_AC(0x00100073, UL) /* ebreak */

#ifndef __ASSEMBLY__
typedef u32 _insn_t;

#ifdef CONFIG_GENERIC__RELATIVE_POINTERS
#define ___ENTRY_ADDR	RISCV_INT " 1b - 2b"
#define ___ENTRY_FILE	RISCV_INT " %0 - 2b"
#else
#define ___ENTRY_ADDR	RISCV_PTR " 1b"
#define ___ENTRY_FILE	RISCV_PTR " %0"
#endif

#ifdef CONFIG_DE_VERBOSE
#define ___ENTRY			\
	___ENTRY_ADDR "\n\t"		\
	___ENTRY_FILE "\n\t"		\
	RISCV_SHORT " %1"
#else
#define ___ENTRY			\
	___ENTRY_ADDR
#endif

#define ()							\
do {								\
	__asm__ __volatile__ (					\
		"1:\n\t"					\
			"ebreak\n"				\
			".pushsection ___table,\"a\"\n\t"	\
		"2:\n\t"					\
			___ENTRY "\n\t"			\
			".org 2b + %2\n\t"			\
			".popsection"				\
		:						\
		: "i" (__FILE__), "i" (__LINE__),		\
		  "i" (sizeof(struct _entry)));		\
	unreachable();						\
} while (0)
#endif /* !__ASSEMBLY__ */
#else /* CONFIG_GENERIC_ */
#ifndef __ASSEMBLY__
#define ()							\
do {								\
	__asm__ __volatile__ ("ebreak\n");			\
	unreachable();						\
} while (0)
#endif /* !__ASSEMBLY__ */
#endif /* CONFIG_GENERIC_ */

#define HAVE_ARCH_

#include <asm-generic/.h>

#ifndef __ASSEMBLY__

struct pt_regs;
struct task_struct;

extern void die(struct pt_regs *regs, const char *str);
extern void do_trap(struct pt_regs *regs, int signo, int code,
	unsigned long addr, struct task_struct *tsk);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_RISCV__H */

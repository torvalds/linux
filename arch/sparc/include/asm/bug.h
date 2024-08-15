/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_BUG_H
#define _SPARC_BUG_H

#ifdef CONFIG_BUG
#include <linux/compiler.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
void do_BUG(const char *file, int line);
#define BUG() do {					\
	do_BUG(__FILE__, __LINE__);			\
	barrier_before_unreachable();			\
	__builtin_trap();				\
} while (0)
#else
#define BUG() do {					\
	barrier_before_unreachable();			\
	__builtin_trap();				\
} while (0)
#endif

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

struct pt_regs;
void __noreturn die_if_kernel(char *str, struct pt_regs *regs);

#endif

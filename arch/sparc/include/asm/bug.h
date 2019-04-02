/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC__H
#define _SPARC__H

#ifdef CONFIG_
#include <linux/compiler.h>

#ifdef CONFIG_DE_VERBOSE
void do_(const char *file, int line);
#define () do {					\
	do_(__FILE__, __LINE__);			\
	barrier_before_unreachable();			\
	__builtin_trap();				\
} while (0)
#else
#define () do {					\
	barrier_before_unreachable();			\
	__builtin_trap();				\
} while (0)
#endif

#define HAVE_ARCH_
#endif

#include <asm-generic/.h>

struct pt_regs;
void __noreturn die_if_kernel(char *str, struct pt_regs *regs);

#endif

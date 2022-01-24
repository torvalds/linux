/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Keith Packard <keithp@keithp.com>
 * Copyright (c) 2021 Google, LLC <ardb@kernel.org>
 */

#ifndef _ASM_ARM_CURRENT_H
#define _ASM_ARM_CURRENT_H

#ifndef __ASSEMBLY__
#include <asm/insn.h>

struct task_struct;

extern struct task_struct *__current;

static __always_inline __attribute_const__ struct task_struct *get_current(void)
{
	struct task_struct *cur;

#if __has_builtin(__builtin_thread_pointer) && \
    defined(CONFIG_CURRENT_POINTER_IN_TPIDRURO) && \
    !(defined(CONFIG_THUMB2_KERNEL) && \
      defined(CONFIG_CC_IS_CLANG) && CONFIG_CLANG_VERSION < 130001)
	/*
	 * Use the __builtin helper when available - this results in better
	 * code, especially when using GCC in combination with the per-task
	 * stack protector, as the compiler will recognize that it needs to
	 * load the TLS register only once in every function.
	 *
	 * Clang < 13.0.1 gets this wrong for Thumb2 builds:
	 * https://github.com/ClangBuiltLinux/linux/issues/1485
	 */
	cur = __builtin_thread_pointer();
#elif defined(CONFIG_CURRENT_POINTER_IN_TPIDRURO) || defined(CONFIG_SMP)
	asm("0:	mrc p15, 0, %0, c13, c0, 3			\n\t"
#ifdef CONFIG_CPU_V6
	    "1:							\n\t"
	    "	.subsection 1					\n\t"
#if defined(CONFIG_ARM_HAS_GROUP_RELOCS) && \
    !(defined(MODULE) && defined(CONFIG_ARM_MODULE_PLTS))
	    "2: " LOAD_SYM_ARMV6(%0, __current) "		\n\t"
	    "	b	1b					\n\t"
#else
	    "2:	ldr	%0, 3f					\n\t"
	    "	ldr	%0, [%0]				\n\t"
	    "	b	1b					\n\t"
	    "3:	.long	__current				\n\t"
#endif
	    "	.previous					\n\t"
	    "	.pushsection \".alt.smp.init\", \"a\"		\n\t"
	    "	.long	0b - .					\n\t"
	    "	b	. + (2b - 0b)				\n\t"
	    "	.popsection					\n\t"
#endif
	    : "=r"(cur));
#elif __LINUX_ARM_ARCH__>= 7 || \
      !defined(CONFIG_ARM_HAS_GROUP_RELOCS) || \
      (defined(MODULE) && defined(CONFIG_ARM_MODULE_PLTS))
	cur = __current;
#else
	asm(LOAD_SYM_ARMV6(%0, __current) : "=r"(cur));
#endif
	return cur;
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* _ASM_ARM_CURRENT_H */

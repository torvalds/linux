/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Keith Packard <keithp@keithp.com>
 * Copyright (c) 2021 Google, LLC <ardb@kernel.org>
 */

#ifndef _ASM_ARM_CURRENT_H
#define _ASM_ARM_CURRENT_H

#ifndef __ASSEMBLY__

struct task_struct;

static inline void set_current(struct task_struct *cur)
{
	if (!IS_ENABLED(CONFIG_CURRENT_POINTER_IN_TPIDRURO))
		return;

	/* Set TPIDRURO */
	asm("mcr p15, 0, %0, c13, c0, 3" :: "r"(cur) : "memory");
}

#ifdef CONFIG_CURRENT_POINTER_IN_TPIDRURO

static inline struct task_struct *get_current(void)
{
	struct task_struct *cur;

#if __has_builtin(__builtin_thread_pointer) && \
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
#else
	asm("mrc p15, 0, %0, c13, c0, 3" : "=r"(cur));
#endif
	return cur;
}

#define current get_current()
#else
#include <asm-generic/current.h>
#endif /* CONFIG_CURRENT_POINTER_IN_TPIDRURO */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_ARM_CURRENT_H */

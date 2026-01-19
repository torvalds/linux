/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#ifndef __ASM_SIMD_H
#define __ASM_SIMD_H

#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/types.h>

#include <asm/neon.h>

#ifdef CONFIG_KERNEL_MODE_NEON

/*
 * may_use_simd - whether it is allowable at this time to issue SIMD
 *                instructions or access the SIMD register file
 *
 * Callers must not assume that the result remains true beyond the next
 * preempt_enable() or return from softirq context.
 */
static __must_check inline bool may_use_simd(void)
{
	/*
	 * We must make sure that the SVE has been initialized properly
	 * before using the SIMD in kernel.
	 */
	return !WARN_ON(!system_capabilities_finalized()) &&
	       system_supports_fpsimd() &&
	       !in_hardirq() && !in_nmi();
}

#else /* ! CONFIG_KERNEL_MODE_NEON */

static __must_check inline bool may_use_simd(void) {
	return false;
}

#endif /* ! CONFIG_KERNEL_MODE_NEON */

DEFINE_LOCK_GUARD_1(ksimd,
		    struct user_fpsimd_state,
		    kernel_neon_begin(_T->lock),
		    kernel_neon_end(_T->lock))

#define __scoped_ksimd(_label)					\
	for (struct user_fpsimd_state __uninitialized __st;	\
	     true; ({ goto _label; }))				\
		if (0) {					\
_label:			break;					\
		} else scoped_guard(ksimd, &__st)

#define scoped_ksimd()	__scoped_ksimd(__UNIQUE_ID(label))

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SIMD_H
#define _ASM_SIMD_H

#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/preempt.h>
#include <linux/types.h>

#include <asm/neon.h>

static __must_check inline bool may_use_simd(void)
{
	return IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && !in_hardirq()
	       && !irqs_disabled();
}

DEFINE_LOCK_GUARD_0(ksimd, kernel_neon_begin(), kernel_neon_end())

#define scoped_ksimd()	scoped_guard(ksimd)

#endif	/* _ASM_SIMD_H */

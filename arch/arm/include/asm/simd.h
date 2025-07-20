/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SIMD_H
#define _ASM_SIMD_H

#include <linux/compiler_attributes.h>
#include <linux/preempt.h>
#include <linux/types.h>

static __must_check inline bool may_use_simd(void)
{
	return IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && !in_hardirq()
	       && !irqs_disabled();
}

#endif	/* _ASM_SIMD_H */

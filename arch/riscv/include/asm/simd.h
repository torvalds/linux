/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2023 SiFive
 */

#ifndef __ASM_SIMD_H
#define __ASM_SIMD_H

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/types.h>
#include <linux/thread_info.h>

#include <asm/vector.h>

#ifdef CONFIG_RISCV_ISA_V
/*
 * may_use_simd - whether it is allowable at this time to issue vector
 *                instructions or access the vector register file
 *
 * Callers must not assume that the result remains true beyond the next
 * preempt_enable() or return from softirq context.
 */
static __must_check inline bool may_use_simd(void)
{
	/*
	 * RISCV_KERNEL_MODE_V is only set while preemption is disabled,
	 * and is clear whenever preemption is enabled.
	 */
	if (in_hardirq() || in_nmi())
		return false;

	/*
	 * Nesting is achieved in preempt_v by spreading the control for
	 * preemptible and non-preemptible kernel-mode Vector into two fields.
	 * Only non-preempt_v can nest on top of preempt_v, if non-preempt_v is
	 * unavailable, then preempt_v is not allowed.
	 */
	return !(riscv_v_flags() & RISCV_KERNEL_MODE_V);
}

#else /* ! CONFIG_RISCV_ISA_V */

static __must_check inline bool may_use_simd(void)
{
	return false;
}

#endif /* ! CONFIG_RISCV_ISA_V */

#endif

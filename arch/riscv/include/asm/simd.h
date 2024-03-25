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
	 * Always try to match with preempt_v if kernel V-context exists. Then,
	 * fallback to check non preempt_v if nesting happens, or if the config
	 * is not set.
	 */
	if (IS_ENABLED(CONFIG_RISCV_ISA_V_PREEMPTIVE) && current->thread.kernel_vstate.datap) {
		if (!riscv_preempt_v_started(current))
			return true;
	}
	/*
	 * Non-preemptible kernel-mode Vector temporarily disables bh. So we
	 * must not return true on irq_disabled(). Otherwise we would fail the
	 * lockdep check calling local_bh_enable()
	 */
	return !irqs_disabled() && !(riscv_v_flags() & RISCV_KERNEL_MODE_V);
}

#else /* ! CONFIG_RISCV_ISA_V */

static __must_check inline bool may_use_simd(void)
{
	return false;
}

#endif /* ! CONFIG_RISCV_ISA_V */

#endif

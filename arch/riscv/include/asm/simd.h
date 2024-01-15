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
	return !in_hardirq() && !in_nmi() && !(riscv_v_flags() & RISCV_KERNEL_MODE_V);
}

#else /* ! CONFIG_RISCV_ISA_V */

static __must_check inline bool may_use_simd(void)
{
	return false;
}

#endif /* ! CONFIG_RISCV_ISA_V */

#endif

/*
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __ASM_SIMD_H
#define __ASM_SIMD_H

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/types.h>

#ifdef CONFIG_KERNEL_MODE_NEON

DECLARE_PER_CPU(bool, kernel_neon_busy);

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
	 * The raw_cpu_read() is racy if called with preemption enabled.
	 * This is not a bug: kernel_neon_busy is only set when
	 * preemption is disabled, so we cannot migrate to another CPU
	 * while it is set, nor can we migrate to a CPU where it is set.
	 * So, if we find it clear on some CPU then we're guaranteed to
	 * find it clear on any CPU we could migrate to.
	 *
	 * If we are in between kernel_neon_begin()...kernel_neon_end(),
	 * the flag will be set, but preemption is also disabled, so we
	 * can't migrate to another CPU and spuriously see it become
	 * false.
	 */
	return !in_irq() && !irqs_disabled() && !in_nmi() &&
		!raw_cpu_read(kernel_neon_busy);
}

#else /* ! CONFIG_KERNEL_MODE_NEON */

static __must_check inline bool may_use_simd(void) {
	return false;
}

#endif /* ! CONFIG_KERNEL_MODE_NEON */

#endif

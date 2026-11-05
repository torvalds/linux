// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/preempt.h>
#include <linux/bottom_half.h>
#include <linux/irqflags.h>

#include "igt_atomic.h"

static void __preempt_begin(void)
{
	preempt_disable();
}

static void __preempt_end(void)
{
	preempt_enable();
}

static void __softirq_begin(void)
{
	local_bh_disable();
}

static void __softirq_end(void)
{
	local_bh_enable();
}

static void __hardirq_begin(void)
{
	local_irq_disable();
}

static void __hardirq_end(void)
{
	local_irq_enable();
}

static void __maybe_unused __nop(void)
{}

const struct igt_atomic_section igt_atomic_phases[] = {
#if IS_ENABLED(CONFIG_PREEMPT_RT)
	{ "sleeping", __nop, __nop },
	{ },
#endif
	{ "preempt", __preempt_begin, __preempt_end },
	{ "softirq", __softirq_begin, __softirq_end },
	{ "hardirq", __hardirq_begin, __hardirq_end },
	{ }
};

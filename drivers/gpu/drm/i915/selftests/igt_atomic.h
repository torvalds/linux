/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2018 Intel Corporation
 */

#ifndef IGT_ATOMIC_H
#define IGT_ATOMIC_H

#include <linux/preempt.h>
#include <linux/bottom_half.h>
#include <linux/irqflags.h>

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

struct igt_atomic_section {
	const char *name;
	void (*critical_section_begin)(void);
	void (*critical_section_end)(void);
};

static const struct igt_atomic_section igt_atomic_phases[] = {
	{ "preempt", __preempt_begin, __preempt_end },
	{ "softirq", __softirq_begin, __softirq_end },
	{ "hardirq", __hardirq_begin, __hardirq_end },
	{ }
};

#endif /* IGT_ATOMIC_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * Code for supporting irq vector tracepoints.
 *
 * Copyright (C) 2013 Seiji Aguchi <seiji.aguchi@hds.com>
 *
 */
#include <linux/jump_label.h>
#include <linux/atomic.h>

#include <asm/hw_irq.h>
#include <asm/desc.h>

DEFINE_STATIC_KEY_FALSE(trace_pagefault_key);

int trace_pagefault_reg(void)
{
	static_branch_inc(&trace_pagefault_key);
	return 0;
}

void trace_pagefault_unreg(void)
{
	static_branch_dec(&trace_pagefault_key);
}

#ifdef CONFIG_SMP

DEFINE_STATIC_KEY_FALSE(trace_resched_ipi_key);

int trace_resched_ipi_reg(void)
{
	static_branch_inc(&trace_resched_ipi_key);
	return 0;
}

void trace_resched_ipi_unreg(void)
{
	static_branch_dec(&trace_resched_ipi_key);
}

#endif

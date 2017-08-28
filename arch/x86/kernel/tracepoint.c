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

DEFINE_STATIC_KEY_FALSE(trace_irqvectors_key);

int trace_irq_vector_regfunc(void)
{
	static_branch_inc(&trace_irqvectors_key);
	return 0;
}

void trace_irq_vector_unregfunc(void)
{
	static_branch_dec(&trace_irqvectors_key);
}

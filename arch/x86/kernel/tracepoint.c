/*
 * Code for supporting irq vector tracepoints.
 *
 * Copyright (C) 2013 Seiji Aguchi <seiji.aguchi@hds.com>
 *
 */
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <linux/atomic.h>

atomic_t trace_idt_ctr = ATOMIC_INIT(0);
struct desc_ptr trace_idt_descr = { NR_VECTORS * 16 - 1,
				(unsigned long) trace_idt_table };

#ifndef CONFIG_X86_64
gate_desc trace_idt_table[NR_VECTORS] __page_aligned_data
					= { { { { 0, 0 } } }, };
#endif

static int trace_irq_vector_refcount;
static DEFINE_MUTEX(irq_vector_mutex);

static void set_trace_idt_ctr(int val)
{
	atomic_set(&trace_idt_ctr, val);
	/* Ensure the trace_idt_ctr is set before sending IPI */
	wmb();
}

static void switch_idt(void *arg)
{
	unsigned long flags;

	local_irq_save(flags);
	load_current_idt();
	local_irq_restore(flags);
}

void trace_irq_vector_regfunc(void)
{
	mutex_lock(&irq_vector_mutex);
	if (!trace_irq_vector_refcount) {
		set_trace_idt_ctr(1);
		smp_call_function(switch_idt, NULL, 0);
		switch_idt(NULL);
	}
	trace_irq_vector_refcount++;
	mutex_unlock(&irq_vector_mutex);
}

void trace_irq_vector_unregfunc(void)
{
	mutex_lock(&irq_vector_mutex);
	trace_irq_vector_refcount--;
	if (!trace_irq_vector_refcount) {
		set_trace_idt_ctr(0);
		smp_call_function(switch_idt, NULL, 0);
		switch_idt(NULL);
	}
	mutex_unlock(&irq_vector_mutex);
}

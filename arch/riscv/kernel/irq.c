// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2018 Christoph Hellwig
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/scs.h>
#include <linux/seq_file.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/softirq_stack.h>
#include <asm/stacktrace.h>

static struct fwnode_handle *(*__get_intc_node)(void);

void riscv_set_intc_hwnode_fn(struct fwnode_handle *(*fn)(void))
{
	__get_intc_node = fn;
}

struct fwnode_handle *riscv_get_intc_hwnode(void)
{
	if (__get_intc_node)
		return __get_intc_node();

	return NULL;
}
EXPORT_SYMBOL_GPL(riscv_get_intc_hwnode);

#ifdef CONFIG_IRQ_STACKS
#include <asm/irq_stack.h>

DECLARE_PER_CPU(ulong *, irq_shadow_call_stack_ptr);

#ifdef CONFIG_SHADOW_CALL_STACK
DEFINE_PER_CPU(ulong *, irq_shadow_call_stack_ptr);
#endif

static void init_irq_scs(void)
{
	int cpu;

	if (!scs_is_enabled())
		return;

	for_each_possible_cpu(cpu)
		per_cpu(irq_shadow_call_stack_ptr, cpu) =
			scs_alloc(cpu_to_node(cpu));
}

DEFINE_PER_CPU(ulong *, irq_stack_ptr);

#ifdef CONFIG_VMAP_STACK
static void init_irq_stacks(void)
{
	int cpu;
	ulong *p;

	for_each_possible_cpu(cpu) {
		p = arch_alloc_vmap_stack(IRQ_STACK_SIZE, cpu_to_node(cpu));
		per_cpu(irq_stack_ptr, cpu) = p;
	}
}
#else
/* irq stack only needs to be 16 byte aligned - not IRQ_STACK_SIZE aligned. */
DEFINE_PER_CPU_ALIGNED(ulong [IRQ_STACK_SIZE/sizeof(ulong)], irq_stack);

static void init_irq_stacks(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(irq_stack_ptr, cpu) = per_cpu(irq_stack, cpu);
}
#endif /* CONFIG_VMAP_STACK */

#ifdef CONFIG_SOFTIRQ_ON_OWN_STACK
static void ___do_softirq(struct pt_regs *regs)
{
	__do_softirq();
}

void do_softirq_own_stack(void)
{
	if (on_thread_stack())
		call_on_irq_stack(NULL, ___do_softirq);
	else
		__do_softirq();
}
#endif /* CONFIG_SOFTIRQ_ON_OWN_STACK */

#else
static void init_irq_scs(void) {}
static void init_irq_stacks(void) {}
#endif /* CONFIG_IRQ_STACKS */

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_stats(p, prec);
	return 0;
}

void __init init_IRQ(void)
{
	init_irq_scs();
	init_irq_stacks();
	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");
	sbi_ipi_init();
}

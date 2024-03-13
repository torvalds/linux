// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/kernel/irq.c
 *
 * Copyright (C) 1992 Linus Torvalds
 * Modifications for ARM processor Copyright (C) 1995-2000 Russell King.
 * Support for Dynamic Tick Timer Copyright (C) 2004-2005 Nokia Corporation.
 * Dynamic Tick Timer written by Tony Lindgren <tony@atomide.com> and
 * Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/irq.h>
#include <linux/memory.h>
#include <linux/smp.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kprobes.h>
#include <linux/scs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <asm/daifflags.h>
#include <asm/exception.h>
#include <asm/vmap_stack.h>
#include <asm/softirq_stack.h>

/* Only access this in an NMI enter/exit */
DEFINE_PER_CPU(struct nmi_ctx, nmi_contexts);

DEFINE_PER_CPU(unsigned long *, irq_stack_ptr);


DECLARE_PER_CPU(unsigned long *, irq_shadow_call_stack_ptr);

#ifdef CONFIG_SHADOW_CALL_STACK
DEFINE_PER_CPU(unsigned long *, irq_shadow_call_stack_ptr);
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

#ifdef CONFIG_VMAP_STACK
static void init_irq_stacks(void)
{
	int cpu;
	unsigned long *p;

	for_each_possible_cpu(cpu) {
		p = arch_alloc_vmap_stack(IRQ_STACK_SIZE, cpu_to_node(cpu));
		per_cpu(irq_stack_ptr, cpu) = p;
	}
}
#else
/* irq stack only needs to be 16 byte aligned - not IRQ_STACK_SIZE aligned. */
DEFINE_PER_CPU_ALIGNED(unsigned long [IRQ_STACK_SIZE/sizeof(long)], irq_stack);

static void init_irq_stacks(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(irq_stack_ptr, cpu) = per_cpu(irq_stack, cpu);
}
#endif

#ifndef CONFIG_PREEMPT_RT
static void ____do_softirq(struct pt_regs *regs)
{
	__do_softirq();
}

void do_softirq_own_stack(void)
{
	call_on_irq_stack(NULL, ____do_softirq);
}
#endif

static void default_handle_irq(struct pt_regs *regs)
{
	panic("IRQ taken without a root IRQ handler\n");
}

static void default_handle_fiq(struct pt_regs *regs)
{
	panic("FIQ taken without a root FIQ handler\n");
}

void (*handle_arch_irq)(struct pt_regs *) __ro_after_init = default_handle_irq;
void (*handle_arch_fiq)(struct pt_regs *) __ro_after_init = default_handle_fiq;

int __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	if (handle_arch_irq != default_handle_irq)
		return -EBUSY;

	handle_arch_irq = handle_irq;
	pr_info("Root IRQ handler: %ps\n", handle_irq);
	return 0;
}

int __init set_handle_fiq(void (*handle_fiq)(struct pt_regs *))
{
	if (handle_arch_fiq != default_handle_fiq)
		return -EBUSY;

	handle_arch_fiq = handle_fiq;
	pr_info("Root FIQ handler: %ps\n", handle_fiq);
	return 0;
}

void __init init_IRQ(void)
{
	init_irq_stacks();
	init_irq_scs();
	irqchip_init();

	if (system_uses_irq_prio_masking()) {
		/*
		 * Now that we have a stack for our IRQ handler, set
		 * the PMR/PSR pair to a consistent state.
		 */
		WARN_ON(read_sysreg(daif) & PSR_A_BIT);
		local_daif_restore(DAIF_PROCCTX_NOIRQ);
	}
}

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
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <asm/daifflags.h>
#include <asm/vmap_stack.h>

/* Only access this in an NMI enter/exit */
DEFINE_PER_CPU(struct nmi_ctx, nmi_contexts);

DEFINE_PER_CPU(unsigned long *, irq_stack_ptr);

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

void __init init_IRQ(void)
{
	init_irq_stacks();
	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");

	if (system_uses_irq_prio_masking()) {
		/*
		 * Now that we have a stack for our IRQ handler, set
		 * the PMR/PSR pair to a consistent state.
		 */
		WARN_ON(read_sysreg(daif) & PSR_A_BIT);
		local_daif_restore(DAIF_PROCCTX_NOIRQ);
	}
}

/*
 * Stubs to make nmi_enter/exit() code callable from ASM
 */
asmlinkage void notrace asm_nmi_enter(void)
{
	nmi_enter();
}
NOKPROBE_SYMBOL(asm_nmi_enter);

asmlinkage void notrace asm_nmi_exit(void)
{
	nmi_exit();
}
NOKPROBE_SYMBOL(asm_nmi_exit);

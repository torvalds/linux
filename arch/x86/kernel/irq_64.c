// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86_64-specific interrupt
 * entry and irq statistics code. All the remaining irq logic is
 * done by the generic kernel/irq/ code and in the
 * x86_64-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/sched/task_stack.h>
#include <linux/vmalloc.h>

#include <asm/cpu_entry_area.h>
#include <asm/softirq_stack.h>
#include <asm/irq_stack.h>
#include <asm/io_apic.h>
#include <asm/apic.h>

DEFINE_PER_CPU_PAGE_ALIGNED(struct irq_stack, irq_stack_backing_store) __visible;
DECLARE_INIT_PER_CPU(irq_stack_backing_store);

#ifdef CONFIG_VMAP_STACK
/*
 * VMAP the backing store with guard pages
 */
static int map_irq_stack(unsigned int cpu)
{
	char *stack = (char *)per_cpu_ptr(&irq_stack_backing_store, cpu);
	struct page *pages[IRQ_STACK_SIZE / PAGE_SIZE];
	void *va;
	int i;

	for (i = 0; i < IRQ_STACK_SIZE / PAGE_SIZE; i++) {
		phys_addr_t pa = per_cpu_ptr_to_phys(stack + (i << PAGE_SHIFT));

		pages[i] = pfn_to_page(pa >> PAGE_SHIFT);
	}

	va = vmap(pages, IRQ_STACK_SIZE / PAGE_SIZE, VM_MAP, PAGE_KERNEL);
	if (!va)
		return -ENOMEM;

	/* Store actual TOS to avoid adjustment in the hotpath */
	per_cpu(pcpu_hot.hardirq_stack_ptr, cpu) = va + IRQ_STACK_SIZE - 8;
	return 0;
}
#else
/*
 * If VMAP stacks are disabled due to KASAN, just use the per cpu
 * backing store without guard pages.
 */
static int map_irq_stack(unsigned int cpu)
{
	void *va = per_cpu_ptr(&irq_stack_backing_store, cpu);

	/* Store actual TOS to avoid adjustment in the hotpath */
	per_cpu(pcpu_hot.hardirq_stack_ptr, cpu) = va + IRQ_STACK_SIZE - 8;
	return 0;
}
#endif

int irq_init_percpu_irqstack(unsigned int cpu)
{
	if (per_cpu(pcpu_hot.hardirq_stack_ptr, cpu))
		return 0;
	return map_irq_stack(cpu);
}

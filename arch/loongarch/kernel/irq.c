// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>

#include <asm/irq.h>
#include <asm/loongson.h>
#include <asm/setup.h>

DEFINE_PER_CPU(unsigned long, irq_stack);

struct irq_domain *cpu_domain;
struct irq_domain *liointc_domain;
struct irq_domain *pch_lpc_domain;
struct irq_domain *pch_msi_domain[MAX_IO_PICS];
struct irq_domain *pch_pic_domain[MAX_IO_PICS];

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	pr_warn("Unexpected IRQ # %d\n", irq);
}

atomic_t irq_err_count;

asmlinkage void spurious_interrupt(void)
{
	atomic_inc(&irq_err_count);
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
#ifdef CONFIG_SMP
	show_ipi_list(p, prec);
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
	return 0;
}

void __init init_IRQ(void)
{
	int i, r, ipi_irq;
	static int ipi_dummy_dev;
	unsigned int order = get_order(IRQ_STACK_SIZE);
	struct page *page;

	clear_csr_ecfg(ECFG0_IM);
	clear_csr_estat(ESTATF_IP);

	irqchip_init();
#ifdef CONFIG_SMP
	ipi_irq = EXCCODE_IPI - EXCCODE_INT_START;
	irq_set_percpu_devid(ipi_irq);
	r = request_percpu_irq(ipi_irq, loongson3_ipi_interrupt, "IPI", &ipi_dummy_dev);
	if (r < 0)
		panic("IPI IRQ request failed\n");
#endif

	for (i = 0; i < NR_IRQS; i++)
		irq_set_noprobe(i);

	for_each_possible_cpu(i) {
		page = alloc_pages_node(cpu_to_node(i), GFP_KERNEL, order);

		per_cpu(irq_stack, i) = (unsigned long)page_address(page);
		pr_debug("CPU%d IRQ stack at 0x%lx - 0x%lx\n", i,
			per_cpu(irq_stack, i), per_cpu(irq_stack, i) + IRQ_STACK_SIZE);
	}

	set_csr_ecfg(ECFGF_IP0 | ECFGF_IP1 | ECFGF_IP2 | ECFGF_IPI | ECFGF_PMC);
}

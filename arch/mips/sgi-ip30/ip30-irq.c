// SPDX-License-Identifier: GPL-2.0
/*
 * ip30-irq.c: Highlevel interrupt handling for IP30 architecture.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/tick.h>
#include <linux/types.h>

#include <asm/irq_cpu.h>
#include <asm/sgi/heart.h>

#include "ip30-common.h"

struct heart_irq_data {
	u64	*irq_mask;
	int	cpu;
};

static DECLARE_BITMAP(heart_irq_map, HEART_NUM_IRQS);

static DEFINE_PER_CPU(unsigned long, irq_enable_mask);

static inline int heart_alloc_int(void)
{
	int bit;

again:
	bit = find_first_zero_bit(heart_irq_map, HEART_NUM_IRQS);
	if (bit >= HEART_NUM_IRQS)
		return -ENOSPC;

	if (test_and_set_bit(bit, heart_irq_map))
		goto again;

	return bit;
}

static void ip30_error_irq(struct irq_desc *desc)
{
	u64 pending, mask, cause, error_irqs, err_reg;
	int cpu = smp_processor_id();
	int i;

	pending = heart_read(&heart_regs->isr);
	mask = heart_read(&heart_regs->imr[cpu]);
	cause = heart_read(&heart_regs->cause);
	error_irqs = (pending & HEART_L4_INT_MASK & mask);

	/* Bail if there's nothing to process (how did we get here, then?) */
	if (unlikely(!error_irqs))
		return;

	/* Prevent any of the error IRQs from firing again. */
	heart_write(mask & ~(pending), &heart_regs->imr[cpu]);

	/* Ack all error IRQs. */
	heart_write(HEART_L4_INT_MASK, &heart_regs->clear_isr);

	/*
	 * If we also have a cause value, then something happened, so loop
	 * through the error IRQs and report a "heart attack" for each one
	 * and print the value of the HEART cause register.  This is really
	 * primitive right now, but it should hopefully work until a more
	 * robust error handling routine can be put together.
	 *
	 * Refer to heart.h for the HC_* macros to work out the cause
	 * that got us here.
	 */
	if (cause) {
		pr_alert("IP30: CPU%d: HEART ATTACK! ISR = 0x%.16llx, IMR = 0x%.16llx, CAUSE = 0x%.16llx\n",
			 cpu, pending, mask, cause);

		if (cause & HC_COR_MEM_ERR) {
			err_reg = heart_read(&heart_regs->mem_err_addr);
			pr_alert("  HEART_MEMERR_ADDR = 0x%.16llx\n", err_reg);
		}

		/* i = 63; i >= 51; i-- */
		for (i = HEART_ERR_MASK_END; i >= HEART_ERR_MASK_START; i--)
			if ((pending >> i) & 1)
				pr_alert("  HEART Error IRQ #%d\n", i);

		/* XXX: Seems possible to loop forever here, so panic(). */
		panic("IP30: Fatal Error !\n");
	}

	/* Unmask the error IRQs. */
	heart_write(mask, &heart_regs->imr[cpu]);
}

static void ip30_normal_irq(struct irq_desc *desc)
{
	int cpu = smp_processor_id();
	struct irq_domain *domain;
	u64 pend, mask;
	int ret;

	pend = heart_read(&heart_regs->isr);
	mask = (heart_read(&heart_regs->imr[cpu]) &
		(HEART_L0_INT_MASK | HEART_L1_INT_MASK | HEART_L2_INT_MASK));

	pend &= mask;
	if (unlikely(!pend))
		return;

#ifdef CONFIG_SMP
	if (pend & BIT_ULL(HEART_L2_INT_RESCHED_CPU_0)) {
		heart_write(BIT_ULL(HEART_L2_INT_RESCHED_CPU_0),
			    &heart_regs->clear_isr);
		scheduler_ipi();
	} else if (pend & BIT_ULL(HEART_L2_INT_RESCHED_CPU_1)) {
		heart_write(BIT_ULL(HEART_L2_INT_RESCHED_CPU_1),
			    &heart_regs->clear_isr);
		scheduler_ipi();
	} else if (pend & BIT_ULL(HEART_L2_INT_CALL_CPU_0)) {
		heart_write(BIT_ULL(HEART_L2_INT_CALL_CPU_0),
			    &heart_regs->clear_isr);
		generic_smp_call_function_interrupt();
	} else if (pend & BIT_ULL(HEART_L2_INT_CALL_CPU_1)) {
		heart_write(BIT_ULL(HEART_L2_INT_CALL_CPU_1),
			    &heart_regs->clear_isr);
		generic_smp_call_function_interrupt();
	} else
#endif
	{
		domain = irq_desc_get_handler_data(desc);
		ret = generic_handle_domain_irq(domain, __ffs(pend));
		if (ret)
			spurious_interrupt();
	}
}

static void ip30_ack_heart_irq(struct irq_data *d)
{
	heart_write(BIT_ULL(d->hwirq), &heart_regs->clear_isr);
}

static void ip30_mask_heart_irq(struct irq_data *d)
{
	struct heart_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = &per_cpu(irq_enable_mask, hd->cpu);

	clear_bit(d->hwirq, mask);
	heart_write(*mask, &heart_regs->imr[hd->cpu]);
}

static void ip30_mask_and_ack_heart_irq(struct irq_data *d)
{
	struct heart_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = &per_cpu(irq_enable_mask, hd->cpu);

	clear_bit(d->hwirq, mask);
	heart_write(*mask, &heart_regs->imr[hd->cpu]);
	heart_write(BIT_ULL(d->hwirq), &heart_regs->clear_isr);
}

static void ip30_unmask_heart_irq(struct irq_data *d)
{
	struct heart_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = &per_cpu(irq_enable_mask, hd->cpu);

	set_bit(d->hwirq, mask);
	heart_write(*mask, &heart_regs->imr[hd->cpu]);
}

static int ip30_set_heart_irq_affinity(struct irq_data *d,
				       const struct cpumask *mask, bool force)
{
	struct heart_irq_data *hd = irq_data_get_irq_chip_data(d);

	if (!hd)
		return -EINVAL;

	if (irqd_is_started(d))
		ip30_mask_and_ack_heart_irq(d);

	hd->cpu = cpumask_first_and(mask, cpu_online_mask);

	if (irqd_is_started(d))
		ip30_unmask_heart_irq(d);

	irq_data_update_effective_affinity(d, cpumask_of(hd->cpu));

	return 0;
}

static struct irq_chip heart_irq_chip = {
	.name			= "HEART",
	.irq_ack		= ip30_ack_heart_irq,
	.irq_mask		= ip30_mask_heart_irq,
	.irq_mask_ack		= ip30_mask_and_ack_heart_irq,
	.irq_unmask		= ip30_unmask_heart_irq,
	.irq_set_affinity	= ip30_set_heart_irq_affinity,
};

static int heart_domain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	struct heart_irq_data *hd;
	int hwirq;

	if (nr_irqs > 1 || !info)
		return -EINVAL;

	hd = kzalloc(sizeof(*hd), GFP_KERNEL);
	if (!hd)
		return -ENOMEM;

	hwirq = heart_alloc_int();
	if (hwirq < 0) {
		kfree(hd);
		return -EAGAIN;
	}
	irq_domain_set_info(domain, virq, hwirq, &heart_irq_chip, hd,
			    handle_level_irq, NULL, NULL);

	return 0;
}

static void heart_domain_free(struct irq_domain *domain,
			      unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irqd;

	if (nr_irqs > 1)
		return;

	irqd = irq_domain_get_irq_data(domain, virq);
	if (irqd) {
		clear_bit(irqd->hwirq, heart_irq_map);
		kfree(irqd->chip_data);
	}
}

static const struct irq_domain_ops heart_domain_ops = {
	.alloc = heart_domain_alloc,
	.free  = heart_domain_free,
};

void __init ip30_install_ipi(void)
{
	int cpu = smp_processor_id();
	unsigned long *mask = &per_cpu(irq_enable_mask, cpu);

	set_bit(HEART_L2_INT_RESCHED_CPU_0 + cpu, mask);
	heart_write(BIT_ULL(HEART_L2_INT_RESCHED_CPU_0 + cpu),
		    &heart_regs->clear_isr);
	set_bit(HEART_L2_INT_CALL_CPU_0 + cpu, mask);
	heart_write(BIT_ULL(HEART_L2_INT_CALL_CPU_0 + cpu),
		    &heart_regs->clear_isr);

	heart_write(*mask, &heart_regs->imr[cpu]);
}

void __init arch_init_irq(void)
{
	struct irq_domain *domain;
	struct fwnode_handle *fn;
	unsigned long *mask;
	int i;

	mips_cpu_irq_init();

	/* Mask all IRQs. */
	heart_write(HEART_CLR_ALL_MASK, &heart_regs->imr[0]);
	heart_write(HEART_CLR_ALL_MASK, &heart_regs->imr[1]);
	heart_write(HEART_CLR_ALL_MASK, &heart_regs->imr[2]);
	heart_write(HEART_CLR_ALL_MASK, &heart_regs->imr[3]);

	/* Ack everything. */
	heart_write(HEART_ACK_ALL_MASK, &heart_regs->clear_isr);

	/* Enable specific HEART error IRQs for each CPU. */
	mask = &per_cpu(irq_enable_mask, 0);
	*mask |= HEART_CPU0_ERR_MASK;
	heart_write(*mask, &heart_regs->imr[0]);
	mask = &per_cpu(irq_enable_mask, 1);
	*mask |= HEART_CPU1_ERR_MASK;
	heart_write(*mask, &heart_regs->imr[1]);

	/*
	 * Some HEART bits are reserved by hardware or by software convention.
	 * Mark these as reserved right away so they won't be accidentally
	 * used later.
	 */
	set_bit(HEART_L0_INT_GENERIC, heart_irq_map);
	set_bit(HEART_L0_INT_FLOW_CTRL_HWTR_0, heart_irq_map);
	set_bit(HEART_L0_INT_FLOW_CTRL_HWTR_1, heart_irq_map);
	set_bit(HEART_L2_INT_RESCHED_CPU_0, heart_irq_map);
	set_bit(HEART_L2_INT_RESCHED_CPU_1, heart_irq_map);
	set_bit(HEART_L2_INT_CALL_CPU_0, heart_irq_map);
	set_bit(HEART_L2_INT_CALL_CPU_1, heart_irq_map);
	set_bit(HEART_L3_INT_TIMER, heart_irq_map);

	/* Reserve the error interrupts (#51 to #63). */
	for (i = HEART_L4_INT_XWID_ERR_9; i <= HEART_L4_INT_HEART_EXCP; i++)
		set_bit(i, heart_irq_map);

	fn = irq_domain_alloc_named_fwnode("HEART");
	WARN_ON(fn == NULL);
	if (!fn)
		return;
	domain = irq_domain_create_linear(fn, HEART_NUM_IRQS,
					  &heart_domain_ops, NULL);
	WARN_ON(domain == NULL);
	if (!domain)
		return;

	irq_set_default_domain(domain);

	irq_set_percpu_devid(IP30_HEART_L0_IRQ);
	irq_set_chained_handler_and_data(IP30_HEART_L0_IRQ, ip30_normal_irq,
					 domain);
	irq_set_percpu_devid(IP30_HEART_L1_IRQ);
	irq_set_chained_handler_and_data(IP30_HEART_L1_IRQ, ip30_normal_irq,
					 domain);
	irq_set_percpu_devid(IP30_HEART_L2_IRQ);
	irq_set_chained_handler_and_data(IP30_HEART_L2_IRQ, ip30_normal_irq,
					 domain);
	irq_set_percpu_devid(IP30_HEART_ERR_IRQ);
	irq_set_chained_handler_and_data(IP30_HEART_ERR_IRQ, ip30_error_irq,
					 domain);
}

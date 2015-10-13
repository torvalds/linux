/*
 * Root interrupt controller for the BCM2836 (Raspberry Pi 2).
 *
 * Copyright 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/cpu.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <asm/exception.h>

/*
 * The low 2 bits identify the CPU that the GPU IRQ goes to, and the
 * next 2 bits identify the CPU that the GPU FIQ goes to.
 */
#define LOCAL_GPU_ROUTING		0x00c
/* When setting bits 0-3, enables PMU interrupts on that CPU. */
#define LOCAL_PM_ROUTING_SET		0x010
/* When setting bits 0-3, disables PMU interrupts on that CPU. */
#define LOCAL_PM_ROUTING_CLR		0x014
/*
 * The low 4 bits of this are the CPU's timer IRQ enables, and the
 * next 4 bits are the CPU's timer FIQ enables (which override the IRQ
 * bits).
 */
#define LOCAL_TIMER_INT_CONTROL0	0x040
/*
 * The low 4 bits of this are the CPU's per-mailbox IRQ enables, and
 * the next 4 bits are the CPU's per-mailbox FIQ enables (which
 * override the IRQ bits).
 */
#define LOCAL_MAILBOX_INT_CONTROL0	0x050
/*
 * The CPU's interrupt status register.  Bits are defined by the the
 * LOCAL_IRQ_* bits below.
 */
#define LOCAL_IRQ_PENDING0		0x060
/* Same status bits as above, but for FIQ. */
#define LOCAL_FIQ_PENDING0		0x070
/*
 * Mailbox0 write-to-set bits.  There are 16 mailboxes, 4 per CPU, and
 * these bits are organized by mailbox number and then CPU number.  We
 * use mailbox 0 for IPIs.  The mailbox's interrupt is raised while
 * any bit is set.
 */
#define LOCAL_MAILBOX0_SET0		0x080
/* Mailbox0 write-to-clear bits. */
#define LOCAL_MAILBOX0_CLR0		0x0c0

#define LOCAL_IRQ_CNTPSIRQ	0
#define LOCAL_IRQ_CNTPNSIRQ	1
#define LOCAL_IRQ_CNTHPIRQ	2
#define LOCAL_IRQ_CNTVIRQ	3
#define LOCAL_IRQ_MAILBOX0	4
#define LOCAL_IRQ_MAILBOX1	5
#define LOCAL_IRQ_MAILBOX2	6
#define LOCAL_IRQ_MAILBOX3	7
#define LOCAL_IRQ_GPU_FAST	8
#define LOCAL_IRQ_PMU_FAST	9
#define LAST_IRQ		LOCAL_IRQ_PMU_FAST

struct bcm2836_arm_irqchip_intc {
	struct irq_domain *domain;
	void __iomem *base;
};

static struct bcm2836_arm_irqchip_intc intc  __read_mostly;

static void bcm2836_arm_irqchip_mask_per_cpu_irq(unsigned int reg_offset,
						 unsigned int bit,
						 int cpu)
{
	void __iomem *reg = intc.base + reg_offset + 4 * cpu;

	writel(readl(reg) & ~BIT(bit), reg);
}

static void bcm2836_arm_irqchip_unmask_per_cpu_irq(unsigned int reg_offset,
						   unsigned int bit,
						 int cpu)
{
	void __iomem *reg = intc.base + reg_offset + 4 * cpu;

	writel(readl(reg) | BIT(bit), reg);
}

static void bcm2836_arm_irqchip_mask_timer_irq(struct irq_data *d)
{
	bcm2836_arm_irqchip_mask_per_cpu_irq(LOCAL_TIMER_INT_CONTROL0,
					     d->hwirq - LOCAL_IRQ_CNTPSIRQ,
					     smp_processor_id());
}

static void bcm2836_arm_irqchip_unmask_timer_irq(struct irq_data *d)
{
	bcm2836_arm_irqchip_unmask_per_cpu_irq(LOCAL_TIMER_INT_CONTROL0,
					       d->hwirq - LOCAL_IRQ_CNTPSIRQ,
					       smp_processor_id());
}

static struct irq_chip bcm2836_arm_irqchip_timer = {
	.name		= "bcm2836-timer",
	.irq_mask	= bcm2836_arm_irqchip_mask_timer_irq,
	.irq_unmask	= bcm2836_arm_irqchip_unmask_timer_irq,
};

static void bcm2836_arm_irqchip_mask_pmu_irq(struct irq_data *d)
{
	writel(1 << smp_processor_id(), intc.base + LOCAL_PM_ROUTING_CLR);
}

static void bcm2836_arm_irqchip_unmask_pmu_irq(struct irq_data *d)
{
	writel(1 << smp_processor_id(), intc.base + LOCAL_PM_ROUTING_SET);
}

static struct irq_chip bcm2836_arm_irqchip_pmu = {
	.name		= "bcm2836-pmu",
	.irq_mask	= bcm2836_arm_irqchip_mask_pmu_irq,
	.irq_unmask	= bcm2836_arm_irqchip_unmask_pmu_irq,
};

static void bcm2836_arm_irqchip_mask_gpu_irq(struct irq_data *d)
{
}

static void bcm2836_arm_irqchip_unmask_gpu_irq(struct irq_data *d)
{
}

static struct irq_chip bcm2836_arm_irqchip_gpu = {
	.name		= "bcm2836-gpu",
	.irq_mask	= bcm2836_arm_irqchip_mask_gpu_irq,
	.irq_unmask	= bcm2836_arm_irqchip_unmask_gpu_irq,
};

static void bcm2836_arm_irqchip_register_irq(int hwirq, struct irq_chip *chip)
{
	int irq = irq_create_mapping(intc.domain, hwirq);

	irq_set_percpu_devid(irq);
	irq_set_chip_and_handler(irq, chip, handle_percpu_devid_irq);
	irq_set_status_flags(irq, IRQ_NOAUTOEN);
}

static void
__exception_irq_entry bcm2836_arm_irqchip_handle_irq(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	u32 stat;

	stat = readl_relaxed(intc.base + LOCAL_IRQ_PENDING0 + 4 * cpu);
	if (stat & 0x10) {
#ifdef CONFIG_SMP
		void __iomem *mailbox0 = (intc.base +
					  LOCAL_MAILBOX0_CLR0 + 16 * cpu);
		u32 mbox_val = readl(mailbox0);
		u32 ipi = ffs(mbox_val) - 1;

		writel(1 << ipi, mailbox0);
		handle_IPI(ipi, regs);
#endif
	} else {
		u32 hwirq = ffs(stat) - 1;

		handle_IRQ(irq_linear_revmap(intc.domain, hwirq), regs);
	}
}

#ifdef CONFIG_SMP
static void bcm2836_arm_irqchip_send_ipi(const struct cpumask *mask,
					 unsigned int ipi)
{
	int cpu;
	void __iomem *mailbox0_base = intc.base + LOCAL_MAILBOX0_SET0;

	/*
	 * Ensure that stores to normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	for_each_cpu(cpu, mask)	{
		writel(1 << ipi, mailbox0_base + 16 * cpu);
	}
}

/* Unmasks the IPI on the CPU when it's online. */
static int bcm2836_arm_irqchip_cpu_notify(struct notifier_block *nfb,
					  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	unsigned int int_reg = LOCAL_MAILBOX_INT_CONTROL0;
	unsigned int mailbox = 0;

	if (action == CPU_STARTING || action == CPU_STARTING_FROZEN)
		bcm2836_arm_irqchip_unmask_per_cpu_irq(int_reg, mailbox, cpu);
	else if (action == CPU_DYING)
		bcm2836_arm_irqchip_mask_per_cpu_irq(int_reg, mailbox, cpu);

	return NOTIFY_OK;
}

static struct notifier_block bcm2836_arm_irqchip_cpu_notifier = {
	.notifier_call = bcm2836_arm_irqchip_cpu_notify,
	.priority = 100,
};
#endif

static const struct irq_domain_ops bcm2836_arm_irqchip_intc_ops = {
	.xlate = irq_domain_xlate_onecell
};

static void
bcm2836_arm_irqchip_smp_init(void)
{
#ifdef CONFIG_SMP
	/* Unmask IPIs to the boot CPU. */
	bcm2836_arm_irqchip_cpu_notify(&bcm2836_arm_irqchip_cpu_notifier,
				       CPU_STARTING,
				       (void *)smp_processor_id());
	register_cpu_notifier(&bcm2836_arm_irqchip_cpu_notifier);

	set_smp_cross_call(bcm2836_arm_irqchip_send_ipi);
#endif
}

static int __init bcm2836_arm_irqchip_l1_intc_of_init(struct device_node *node,
						      struct device_node *parent)
{
	intc.base = of_iomap(node, 0);
	if (!intc.base) {
		panic("%s: unable to map local interrupt registers\n",
			node->full_name);
	}

	intc.domain = irq_domain_add_linear(node, LAST_IRQ + 1,
					    &bcm2836_arm_irqchip_intc_ops,
					    NULL);
	if (!intc.domain)
		panic("%s: unable to create IRQ domain\n", node->full_name);

	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_CNTPSIRQ,
					 &bcm2836_arm_irqchip_timer);
	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_CNTPNSIRQ,
					 &bcm2836_arm_irqchip_timer);
	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_CNTHPIRQ,
					 &bcm2836_arm_irqchip_timer);
	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_CNTVIRQ,
					 &bcm2836_arm_irqchip_timer);
	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_GPU_FAST,
					 &bcm2836_arm_irqchip_gpu);
	bcm2836_arm_irqchip_register_irq(LOCAL_IRQ_PMU_FAST,
					 &bcm2836_arm_irqchip_pmu);

	bcm2836_arm_irqchip_smp_init();

	set_handle_irq(bcm2836_arm_irqchip_handle_irq);
	return 0;
}

IRQCHIP_DECLARE(bcm2836_arm_irqchip_l1_intc, "brcm,bcm2836-l1-intc",
		bcm2836_arm_irqchip_l1_intc_of_init);

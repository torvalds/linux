// SPDX-License-Identifier: GPL-2.0+
/*
 * Root interrupt controller for the BCM2836 (Raspberry Pi 2).
 *
 * Copyright 2015 Broadcom
 */

#include <linux/cpu.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-bcm2836.h>

#include <asm/exception.h>

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
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
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
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
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
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
};

static void bcm2836_arm_irqchip_dummy_op(struct irq_data *d)
{
}

static struct irq_chip bcm2836_arm_irqchip_dummy = {
	.name		= "bcm2836-dummy",
	.irq_eoi	= bcm2836_arm_irqchip_dummy_op,
};

static int bcm2836_map(struct irq_domain *d, unsigned int irq,
		       irq_hw_number_t hw)
{
	struct irq_chip *chip;

	switch (hw) {
	case LOCAL_IRQ_MAILBOX0:
		chip = &bcm2836_arm_irqchip_dummy;
		break;
	case LOCAL_IRQ_CNTPSIRQ:
	case LOCAL_IRQ_CNTPNSIRQ:
	case LOCAL_IRQ_CNTHPIRQ:
	case LOCAL_IRQ_CNTVIRQ:
		chip = &bcm2836_arm_irqchip_timer;
		break;
	case LOCAL_IRQ_GPU_FAST:
		chip = &bcm2836_arm_irqchip_gpu;
		break;
	case LOCAL_IRQ_PMU_FAST:
		chip = &bcm2836_arm_irqchip_pmu;
		break;
	default:
		pr_warn_once("Unexpected hw irq: %lu\n", hw);
		return -EINVAL;
	}

	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hw, chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);
	irq_set_status_flags(irq, IRQ_NOAUTOEN);

	return 0;
}

static void
__exception_irq_entry bcm2836_arm_irqchip_handle_irq(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	u32 stat;

	stat = readl_relaxed(intc.base + LOCAL_IRQ_PENDING0 + 4 * cpu);
	if (stat) {
		u32 hwirq = ffs(stat) - 1;

		generic_handle_domain_irq(intc.domain, hwirq);
	}
}

#ifdef CONFIG_SMP
static struct irq_domain *ipi_domain;

static void bcm2836_arm_irqchip_handle_ipi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cpu = smp_processor_id();
	u32 mbox_val;

	chained_irq_enter(chip, desc);

	mbox_val = readl_relaxed(intc.base + LOCAL_MAILBOX0_CLR0 + 16 * cpu);
	if (mbox_val) {
		int hwirq = ffs(mbox_val) - 1;
		generic_handle_domain_irq(ipi_domain, hwirq);
	}

	chained_irq_exit(chip, desc);
}

static void bcm2836_arm_irqchip_ipi_ack(struct irq_data *d)
{
	int cpu = smp_processor_id();

	writel_relaxed(BIT(d->hwirq),
		       intc.base + LOCAL_MAILBOX0_CLR0 + 16 * cpu);
}

static void bcm2836_arm_irqchip_ipi_send_mask(struct irq_data *d,
					      const struct cpumask *mask)
{
	int cpu;
	void __iomem *mailbox0_base = intc.base + LOCAL_MAILBOX0_SET0;

	/*
	 * Ensure that stores to normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	smp_wmb();

	for_each_cpu(cpu, mask)
		writel_relaxed(BIT(d->hwirq), mailbox0_base + 16 * cpu);
}

static struct irq_chip bcm2836_arm_irqchip_ipi = {
	.name		= "IPI",
	.irq_mask	= bcm2836_arm_irqchip_dummy_op,
	.irq_unmask	= bcm2836_arm_irqchip_dummy_op,
	.irq_ack	= bcm2836_arm_irqchip_ipi_ack,
	.ipi_send_mask	= bcm2836_arm_irqchip_ipi_send_mask,
};

static int bcm2836_arm_irqchip_ipi_alloc(struct irq_domain *d,
					 unsigned int virq,
					 unsigned int nr_irqs, void *args)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_set_percpu_devid(virq + i);
		irq_domain_set_info(d, virq + i, i, &bcm2836_arm_irqchip_ipi,
				    d->host_data,
				    handle_percpu_devid_irq,
				    NULL, NULL);
	}

	return 0;
}

static void bcm2836_arm_irqchip_ipi_free(struct irq_domain *d,
					 unsigned int virq,
					 unsigned int nr_irqs)
{
	/* Not freeing IPIs */
}

static const struct irq_domain_ops ipi_domain_ops = {
	.alloc	= bcm2836_arm_irqchip_ipi_alloc,
	.free	= bcm2836_arm_irqchip_ipi_free,
};

static int bcm2836_cpu_starting(unsigned int cpu)
{
	bcm2836_arm_irqchip_unmask_per_cpu_irq(LOCAL_MAILBOX_INT_CONTROL0, 0,
					       cpu);
	return 0;
}

static int bcm2836_cpu_dying(unsigned int cpu)
{
	bcm2836_arm_irqchip_mask_per_cpu_irq(LOCAL_MAILBOX_INT_CONTROL0, 0,
					     cpu);
	return 0;
}

#define BITS_PER_MBOX	32

static void __init bcm2836_arm_irqchip_smp_init(void)
{
	struct irq_fwspec ipi_fwspec = {
		.fwnode		= intc.domain->fwnode,
		.param_count	= 1,
		.param		= {
			[0]	= LOCAL_IRQ_MAILBOX0,
		},
	};
	int base_ipi, mux_irq;

	mux_irq = irq_create_fwspec_mapping(&ipi_fwspec);
	if (WARN_ON(mux_irq <= 0))
		return;

	ipi_domain = irq_domain_create_linear(intc.domain->fwnode,
					      BITS_PER_MBOX, &ipi_domain_ops,
					      NULL);
	if (WARN_ON(!ipi_domain))
		return;

	ipi_domain->flags |= IRQ_DOMAIN_FLAG_IPI_SINGLE;
	irq_domain_update_bus_token(ipi_domain, DOMAIN_BUS_IPI);

	base_ipi = irq_domain_alloc_irqs(ipi_domain, BITS_PER_MBOX, NUMA_NO_NODE, NULL);
	if (WARN_ON(!base_ipi))
		return;

	set_smp_ipi_range(base_ipi, BITS_PER_MBOX);

	irq_set_chained_handler_and_data(mux_irq,
					 bcm2836_arm_irqchip_handle_ipi, NULL);

	/* Unmask IPIs to the boot CPU. */
	cpuhp_setup_state(CPUHP_AP_IRQ_BCM2836_STARTING,
			  "irqchip/bcm2836:starting", bcm2836_cpu_starting,
			  bcm2836_cpu_dying);
}
#else
#define bcm2836_arm_irqchip_smp_init()	do { } while(0)
#endif

static const struct irq_domain_ops bcm2836_arm_irqchip_intc_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = bcm2836_map,
};

/*
 * The LOCAL_IRQ_CNT* timer firings are based off of the external
 * oscillator with some scaling.  The firmware sets up CNTFRQ to
 * report 19.2Mhz, but doesn't set up the scaling registers.
 */
static void bcm2835_init_local_timer_frequency(void)
{
	/*
	 * Set the timer to source from the 19.2Mhz crystal clock (bit
	 * 8 unset), and only increment by 1 instead of 2 (bit 9
	 * unset).
	 */
	writel(0, intc.base + LOCAL_CONTROL);

	/*
	 * Set the timer prescaler to 1:1 (timer freq = input freq *
	 * 2**31 / prescaler)
	 */
	writel(0x80000000, intc.base + LOCAL_PRESCALER);
}

static int __init bcm2836_arm_irqchip_l1_intc_of_init(struct device_node *node,
						      struct device_node *parent)
{
	intc.base = of_iomap(node, 0);
	if (!intc.base) {
		panic("%pOF: unable to map local interrupt registers\n", node);
	}

	bcm2835_init_local_timer_frequency();

	intc.domain = irq_domain_create_linear(of_fwnode_handle(node), LAST_IRQ + 1,
					    &bcm2836_arm_irqchip_intc_ops,
					    NULL);
	if (!intc.domain)
		panic("%pOF: unable to create IRQ domain\n", node);

	irq_domain_update_bus_token(intc.domain, DOMAIN_BUS_WIRED);

	bcm2836_arm_irqchip_smp_init();

	set_handle_irq(bcm2836_arm_irqchip_handle_irq);
	return 0;
}

IRQCHIP_DECLARE(bcm2836_arm_irqchip_l1_intc, "brcm,bcm2836-l1-intc",
		bcm2836_arm_irqchip_l1_intc_of_init);

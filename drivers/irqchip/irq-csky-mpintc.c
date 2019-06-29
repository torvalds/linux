// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/reg_ops.h>

static struct irq_domain *root_domain;
static void __iomem *INTCG_base;
static void __iomem *INTCL_base;

#define IPI_IRQ		15
#define INTC_IRQS	256
#define COMM_IRQ_BASE	32

#define INTCG_SIZE	0x8000
#define INTCL_SIZE	0x1000

#define INTCG_ICTLR	0x0
#define INTCG_CICFGR	0x100
#define INTCG_CIDSTR	0x1000

#define INTCL_PICTLR	0x0
#define INTCL_SIGR	0x60
#define INTCL_HPPIR	0x68
#define INTCL_RDYIR	0x6c
#define INTCL_SENR	0xa0
#define INTCL_CENR	0xa4
#define INTCL_CACR	0xb4

static DEFINE_PER_CPU(void __iomem *, intcl_reg);

static void csky_mpintc_handler(struct pt_regs *regs)
{
	void __iomem *reg_base = this_cpu_read(intcl_reg);

	do {
		handle_domain_irq(root_domain,
				  readl_relaxed(reg_base + INTCL_RDYIR),
				  regs);
	} while (readl_relaxed(reg_base + INTCL_HPPIR) & BIT(31));
}

static void csky_mpintc_enable(struct irq_data *d)
{
	void __iomem *reg_base = this_cpu_read(intcl_reg);

	writel_relaxed(d->hwirq, reg_base + INTCL_SENR);
}

static void csky_mpintc_disable(struct irq_data *d)
{
	void __iomem *reg_base = this_cpu_read(intcl_reg);

	writel_relaxed(d->hwirq, reg_base + INTCL_CENR);
}

static void csky_mpintc_eoi(struct irq_data *d)
{
	void __iomem *reg_base = this_cpu_read(intcl_reg);

	writel_relaxed(d->hwirq, reg_base + INTCL_CACR);
}

#ifdef CONFIG_SMP
static int csky_irq_set_affinity(struct irq_data *d,
				 const struct cpumask *mask_val,
				 bool force)
{
	unsigned int cpu;
	unsigned int offset = 4 * (d->hwirq - COMM_IRQ_BASE);

	if (!force)
		cpu = cpumask_any_and(mask_val, cpu_online_mask);
	else
		cpu = cpumask_first(mask_val);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	/*
	 * The csky,mpintc could support auto irq deliver, but it only
	 * could deliver external irq to one cpu or all cpus. So it
	 * doesn't support deliver external irq to a group of cpus
	 * with cpu_mask.
	 * SO we only use auto deliver mode when affinity mask_val is
	 * equal to cpu_present_mask.
	 *
	 */
	if (cpumask_equal(mask_val, cpu_present_mask))
		cpu = 0;
	else
		cpu |= BIT(31);

	writel_relaxed(cpu, INTCG_base + INTCG_CIDSTR + offset);

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}
#endif

static struct irq_chip csky_irq_chip = {
	.name           = "C-SKY SMP Intc",
	.irq_eoi	= csky_mpintc_eoi,
	.irq_enable	= csky_mpintc_enable,
	.irq_disable	= csky_mpintc_disable,
#ifdef CONFIG_SMP
	.irq_set_affinity = csky_irq_set_affinity,
#endif
};

static int csky_irqdomain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	if (hwirq < COMM_IRQ_BASE) {
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &csky_irq_chip,
					 handle_percpu_irq);
	} else {
		irq_set_chip_and_handler(irq, &csky_irq_chip,
					 handle_fasteoi_irq);
	}

	return 0;
}

static const struct irq_domain_ops csky_irqdomain_ops = {
	.map	= csky_irqdomain_map,
	.xlate	= irq_domain_xlate_onecell,
};

#ifdef CONFIG_SMP
static void csky_mpintc_send_ipi(const struct cpumask *mask)
{
	void __iomem *reg_base = this_cpu_read(intcl_reg);

	/*
	 * INTCL_SIGR[3:0] INTID
	 * INTCL_SIGR[8:15] CPUMASK
	 */
	writel_relaxed((*cpumask_bits(mask)) << 8 | IPI_IRQ,
					reg_base + INTCL_SIGR);
}
#endif

/* C-SKY multi processor interrupt controller */
static int __init
csky_mpintc_init(struct device_node *node, struct device_node *parent)
{
	int ret;
	unsigned int cpu, nr_irq;
#ifdef CONFIG_SMP
	unsigned int ipi_irq;
#endif

	if (parent)
		return 0;

	ret = of_property_read_u32(node, "csky,num-irqs", &nr_irq);
	if (ret < 0)
		nr_irq = INTC_IRQS;

	if (INTCG_base == NULL) {
		INTCG_base = ioremap(mfcr("cr<31, 14>"),
				     INTCL_SIZE*nr_cpu_ids + INTCG_SIZE);
		if (INTCG_base == NULL)
			return -EIO;

		INTCL_base = INTCG_base + INTCG_SIZE;

		writel_relaxed(BIT(0), INTCG_base + INTCG_ICTLR);
	}

	root_domain = irq_domain_add_linear(node, nr_irq, &csky_irqdomain_ops,
					    NULL);
	if (!root_domain)
		return -ENXIO;

	/* for every cpu */
	for_each_present_cpu(cpu) {
		per_cpu(intcl_reg, cpu) = INTCL_base + (INTCL_SIZE * cpu);
		writel_relaxed(BIT(0), per_cpu(intcl_reg, cpu) + INTCL_PICTLR);
	}

	set_handle_irq(&csky_mpintc_handler);

#ifdef CONFIG_SMP
	ipi_irq = irq_create_mapping(root_domain, IPI_IRQ);
	if (!ipi_irq)
		return -EIO;

	set_send_ipi(&csky_mpintc_send_ipi, ipi_irq);
#endif

	return 0;
}
IRQCHIP_DECLARE(csky_mpintc, "csky,mpintc", csky_mpintc_init);

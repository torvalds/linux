// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver code for Tegra's Legacy Interrupt Controller
 *
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Heavily based on the original arch/arm/mach-tegra/irq.c code:
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * Copyright (C) 2010,2013, NVIDIA Corporation
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define ICTLR_CPU_IEP_VFIQ	0x08
#define ICTLR_CPU_IEP_FIR	0x14
#define ICTLR_CPU_IEP_FIR_SET	0x18
#define ICTLR_CPU_IEP_FIR_CLR	0x1c

#define ICTLR_CPU_IER		0x20
#define ICTLR_CPU_IER_SET	0x24
#define ICTLR_CPU_IER_CLR	0x28
#define ICTLR_CPU_IEP_CLASS	0x2C

#define ICTLR_COP_IER		0x30
#define ICTLR_COP_IER_SET	0x34
#define ICTLR_COP_IER_CLR	0x38
#define ICTLR_COP_IEP_CLASS	0x3c

#define TEGRA_MAX_NUM_ICTLRS	6

static unsigned int num_ictlrs;

struct tegra_ictlr_soc {
	unsigned int num_ictlrs;
};

static const struct tegra_ictlr_soc tegra20_ictlr_soc = {
	.num_ictlrs = 4,
};

static const struct tegra_ictlr_soc tegra30_ictlr_soc = {
	.num_ictlrs = 5,
};

static const struct tegra_ictlr_soc tegra210_ictlr_soc = {
	.num_ictlrs = 6,
};

static const struct of_device_id ictlr_matches[] = {
	{ .compatible = "nvidia,tegra210-ictlr", .data = &tegra210_ictlr_soc },
	{ .compatible = "nvidia,tegra30-ictlr", .data = &tegra30_ictlr_soc },
	{ .compatible = "nvidia,tegra20-ictlr", .data = &tegra20_ictlr_soc },
	{ }
};

struct tegra_ictlr_info {
	void __iomem *base[TEGRA_MAX_NUM_ICTLRS];
#ifdef CONFIG_PM_SLEEP
	u32 cop_ier[TEGRA_MAX_NUM_ICTLRS];
	u32 cop_iep[TEGRA_MAX_NUM_ICTLRS];
	u32 cpu_ier[TEGRA_MAX_NUM_ICTLRS];
	u32 cpu_iep[TEGRA_MAX_NUM_ICTLRS];

	u32 ictlr_wake_mask[TEGRA_MAX_NUM_ICTLRS];
#endif
};

static struct tegra_ictlr_info *lic;

static inline void tegra_ictlr_write_mask(struct irq_data *d, unsigned long reg)
{
	void __iomem *base = (void __iomem __force *)d->chip_data;
	u32 mask;

	mask = BIT(d->hwirq % 32);
	writel_relaxed(mask, base + reg);
}

static void tegra_mask(struct irq_data *d)
{
	tegra_ictlr_write_mask(d, ICTLR_CPU_IER_CLR);
	irq_chip_mask_parent(d);
}

static void tegra_unmask(struct irq_data *d)
{
	tegra_ictlr_write_mask(d, ICTLR_CPU_IER_SET);
	irq_chip_unmask_parent(d);
}

static void tegra_eoi(struct irq_data *d)
{
	tegra_ictlr_write_mask(d, ICTLR_CPU_IEP_FIR_CLR);
	irq_chip_eoi_parent(d);
}

static int tegra_retrigger(struct irq_data *d)
{
	tegra_ictlr_write_mask(d, ICTLR_CPU_IEP_FIR_SET);
	return irq_chip_retrigger_hierarchy(d);
}

#ifdef CONFIG_PM_SLEEP
static int tegra_set_wake(struct irq_data *d, unsigned int enable)
{
	u32 irq = d->hwirq;
	u32 index, mask;

	index = (irq / 32);
	mask = BIT(irq % 32);
	if (enable)
		lic->ictlr_wake_mask[index] |= mask;
	else
		lic->ictlr_wake_mask[index] &= ~mask;

	/*
	 * Do *not* call into the parent, as the GIC doesn't have any
	 * wake-up facility...
	 */
	return 0;
}

static int tegra_ictlr_suspend(void)
{
	unsigned long flags;
	unsigned int i;

	local_irq_save(flags);
	for (i = 0; i < num_ictlrs; i++) {
		void __iomem *ictlr = lic->base[i];

		/* Save interrupt state */
		lic->cpu_ier[i] = readl_relaxed(ictlr + ICTLR_CPU_IER);
		lic->cpu_iep[i] = readl_relaxed(ictlr + ICTLR_CPU_IEP_CLASS);
		lic->cop_ier[i] = readl_relaxed(ictlr + ICTLR_COP_IER);
		lic->cop_iep[i] = readl_relaxed(ictlr + ICTLR_COP_IEP_CLASS);

		/* Disable COP interrupts */
		writel_relaxed(~0ul, ictlr + ICTLR_COP_IER_CLR);

		/* Disable CPU interrupts */
		writel_relaxed(~0ul, ictlr + ICTLR_CPU_IER_CLR);

		/* Enable the wakeup sources of ictlr */
		writel_relaxed(lic->ictlr_wake_mask[i], ictlr + ICTLR_CPU_IER_SET);
	}
	local_irq_restore(flags);

	return 0;
}

static void tegra_ictlr_resume(void)
{
	unsigned long flags;
	unsigned int i;

	local_irq_save(flags);
	for (i = 0; i < num_ictlrs; i++) {
		void __iomem *ictlr = lic->base[i];

		writel_relaxed(lic->cpu_iep[i],
			       ictlr + ICTLR_CPU_IEP_CLASS);
		writel_relaxed(~0ul, ictlr + ICTLR_CPU_IER_CLR);
		writel_relaxed(lic->cpu_ier[i],
			       ictlr + ICTLR_CPU_IER_SET);
		writel_relaxed(lic->cop_iep[i],
			       ictlr + ICTLR_COP_IEP_CLASS);
		writel_relaxed(~0ul, ictlr + ICTLR_COP_IER_CLR);
		writel_relaxed(lic->cop_ier[i],
			       ictlr + ICTLR_COP_IER_SET);
	}
	local_irq_restore(flags);
}

static struct syscore_ops tegra_ictlr_syscore_ops = {
	.suspend	= tegra_ictlr_suspend,
	.resume		= tegra_ictlr_resume,
};

static void tegra_ictlr_syscore_init(void)
{
	register_syscore_ops(&tegra_ictlr_syscore_ops);
}
#else
#define tegra_set_wake	NULL
static inline void tegra_ictlr_syscore_init(void) {}
#endif

static struct irq_chip tegra_ictlr_chip = {
	.name			= "LIC",
	.irq_eoi		= tegra_eoi,
	.irq_mask		= tegra_mask,
	.irq_unmask		= tegra_unmask,
	.irq_retrigger		= tegra_retrigger,
	.irq_set_wake		= tegra_set_wake,
	.irq_set_type		= irq_chip_set_type_parent,
	.flags			= IRQCHIP_MASK_ON_SUSPEND,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int tegra_ictlr_domain_translate(struct irq_domain *d,
					struct irq_fwspec *fwspec,
					unsigned long *hwirq,
					unsigned int *type)
{
	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count != 3)
			return -EINVAL;

		/* No PPI should point to this domain */
		if (fwspec->param[0] != 0)
			return -EINVAL;

		*hwirq = fwspec->param[1];
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	return -EINVAL;
}

static int tegra_ictlr_domain_alloc(struct irq_domain *domain,
				    unsigned int virq,
				    unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct tegra_ictlr_info *info = domain->host_data;
	irq_hw_number_t hwirq;
	unsigned int i;

	if (fwspec->param_count != 3)
		return -EINVAL;	/* Not GIC compliant */
	if (fwspec->param[0] != GIC_SPI)
		return -EINVAL;	/* No PPI should point to this domain */

	hwirq = fwspec->param[1];
	if (hwirq >= (num_ictlrs * 32))
		return -EINVAL;

	for (i = 0; i < nr_irqs; i++) {
		int ictlr = (hwirq + i) / 32;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &tegra_ictlr_chip,
					      (void __force *)info->base[ictlr]);
	}

	parent_fwspec = *fwspec;
	parent_fwspec.fwnode = domain->parent->fwnode;
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops tegra_ictlr_domain_ops = {
	.translate	= tegra_ictlr_domain_translate,
	.alloc		= tegra_ictlr_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init tegra_ictlr_init(struct device_node *node,
				   struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	const struct of_device_id *match;
	const struct tegra_ictlr_soc *soc;
	unsigned int i;
	int err;

	if (!parent) {
		pr_err("%pOF: no parent, giving up\n", node);
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: unable to obtain parent domain\n", node);
		return -ENXIO;
	}

	match = of_match_node(ictlr_matches, node);
	if (!match)		/* Should never happen... */
		return -ENODEV;

	soc = match->data;

	lic = kzalloc(sizeof(*lic), GFP_KERNEL);
	if (!lic)
		return -ENOMEM;

	for (i = 0; i < TEGRA_MAX_NUM_ICTLRS; i++) {
		void __iomem *base;

		base = of_iomap(node, i);
		if (!base)
			break;

		lic->base[i] = base;

		/* Disable all interrupts */
		writel_relaxed(~0UL, base + ICTLR_CPU_IER_CLR);
		/* All interrupts target IRQ */
		writel_relaxed(0, base + ICTLR_CPU_IEP_CLASS);

		num_ictlrs++;
	}

	if (!num_ictlrs) {
		pr_err("%pOF: no valid regions, giving up\n", node);
		err = -ENOMEM;
		goto out_free;
	}

	WARN(num_ictlrs != soc->num_ictlrs,
	     "%pOF: Found %u interrupt controllers in DT; expected %u.\n",
	     node, num_ictlrs, soc->num_ictlrs);


	domain = irq_domain_add_hierarchy(parent_domain, 0, num_ictlrs * 32,
					  node, &tegra_ictlr_domain_ops,
					  lic);
	if (!domain) {
		pr_err("%pOF: failed to allocated domain\n", node);
		err = -ENOMEM;
		goto out_unmap;
	}

	tegra_ictlr_syscore_init();

	pr_info("%pOF: %d interrupts forwarded to %pOF\n",
		node, num_ictlrs * 32, parent);

	return 0;

out_unmap:
	for (i = 0; i < num_ictlrs; i++)
		iounmap(lic->base[i]);
out_free:
	kfree(lic);
	return err;
}

IRQCHIP_DECLARE(tegra20_ictlr, "nvidia,tegra20-ictlr", tegra_ictlr_init);
IRQCHIP_DECLARE(tegra30_ictlr, "nvidia,tegra30-ictlr", tegra_ictlr_init);
IRQCHIP_DECLARE(tegra210_ictlr, "nvidia,tegra210-ictlr", tegra_ictlr_init);

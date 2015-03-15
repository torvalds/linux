/*
 * Copyright (C) 2014-2015 Toradex AG
 * Author: Stefan Agner <stefan@agner.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * IRQ chip driver for MSCM interrupt router available on Vybrid SoC's.
 * The interrupt router is between the CPU's interrupt controller and the
 * peripheral. The router allows to route the peripheral interrupts to
 * one of the two available CPU's on Vybrid VF6xx SoC's (Cortex-A5 or
 * Cortex-M4). The router will be configured transparently on a IRQ
 * request.
 *
 * o All peripheral interrupts of the Vybrid SoC can be routed to
 *   CPU 0, CPU 1 or both. The routing is useful for dual-core
 *   variants of Vybrid SoC such as VF6xx. This driver routes the
 *   requested interrupt to the CPU currently running on.
 *
 * o It is required to setup the interrupt router even on single-core
 *   variants of Vybrid.
 */

#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "irqchip.h"

#define MSCM_CPxNUM		0x4

#define MSCM_IRSPRC(n)		(0x80 + 2 * (n))
#define MSCM_IRSPRC_CPEN_MASK	0x3

#define MSCM_IRSPRC_NUM		112

struct vf610_mscm_ir_chip_data {
	void __iomem *mscm_ir_base;
	u16 cpu_mask;
	u16 saved_irsprc[MSCM_IRSPRC_NUM];
};

static struct vf610_mscm_ir_chip_data *mscm_ir_data;

static inline void vf610_mscm_ir_save(struct vf610_mscm_ir_chip_data *data)
{
	int i;

	for (i = 0; i < MSCM_IRSPRC_NUM; i++)
		data->saved_irsprc[i] = readw_relaxed(data->mscm_ir_base + MSCM_IRSPRC(i));
}

static inline void vf610_mscm_ir_restore(struct vf610_mscm_ir_chip_data *data)
{
	int i;

	for (i = 0; i < MSCM_IRSPRC_NUM; i++)
		writew_relaxed(data->saved_irsprc[i], data->mscm_ir_base + MSCM_IRSPRC(i));
}

static int vf610_mscm_ir_notifier(struct notifier_block *self,
				  unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		vf610_mscm_ir_save(mscm_ir_data);
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		vf610_mscm_ir_restore(mscm_ir_data);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block mscm_ir_notifier_block = {
	.notifier_call = vf610_mscm_ir_notifier,
};

static void vf610_mscm_ir_enable(struct irq_data *data)
{
	irq_hw_number_t hwirq = data->hwirq;
	struct vf610_mscm_ir_chip_data *chip_data = data->chip_data;
	u16 irsprc;

	irsprc = readw_relaxed(chip_data->mscm_ir_base + MSCM_IRSPRC(hwirq));
	irsprc &= MSCM_IRSPRC_CPEN_MASK;

	WARN_ON(irsprc & ~chip_data->cpu_mask);

	writew_relaxed(chip_data->cpu_mask,
		       chip_data->mscm_ir_base + MSCM_IRSPRC(hwirq));

	irq_chip_unmask_parent(data);
}

static void vf610_mscm_ir_disable(struct irq_data *data)
{
	irq_hw_number_t hwirq = data->hwirq;
	struct vf610_mscm_ir_chip_data *chip_data = data->chip_data;

	writew_relaxed(0x0, chip_data->mscm_ir_base + MSCM_IRSPRC(hwirq));

	irq_chip_mask_parent(data);
}

static struct irq_chip vf610_mscm_ir_irq_chip = {
	.name			= "mscm-ir",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_enable		= vf610_mscm_ir_enable,
	.irq_disable		= vf610_mscm_ir_disable,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int vf610_mscm_ir_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	int i;
	irq_hw_number_t hwirq;
	struct of_phandle_args *irq_data = arg;
	struct of_phandle_args gic_data;

	if (irq_data->args_count != 2)
		return -EINVAL;

	hwirq = irq_data->args[0];
	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &vf610_mscm_ir_irq_chip,
					      domain->host_data);

	gic_data.np = domain->parent->of_node;
	gic_data.args_count = 3;
	gic_data.args[0] = GIC_SPI;
	gic_data.args[1] = irq_data->args[0];
	gic_data.args[2] = irq_data->args[1];
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &gic_data);
}

static const struct irq_domain_ops mscm_irq_domain_ops = {
	.xlate = irq_domain_xlate_twocell,
	.alloc = vf610_mscm_ir_domain_alloc,
	.free = irq_domain_free_irqs_common,
};

static int __init vf610_mscm_ir_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	struct regmap *mscm_cp_regmap;
	int ret, cpuid;

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("vf610_mscm_ir: interrupt-parent not found\n");
		return -EINVAL;
	}

	mscm_ir_data = kzalloc(sizeof(*mscm_ir_data), GFP_KERNEL);
	if (!mscm_ir_data)
		return -ENOMEM;

	mscm_ir_data->mscm_ir_base = of_io_request_and_map(node, 0, "mscm-ir");

	if (!mscm_ir_data->mscm_ir_base) {
		pr_err("vf610_mscm_ir: unable to map mscm register\n");
		ret = -ENOMEM;
		goto out_free;
	}

	mscm_cp_regmap = syscon_regmap_lookup_by_phandle(node, "fsl,cpucfg");
	if (IS_ERR(mscm_cp_regmap)) {
		ret = PTR_ERR(mscm_cp_regmap);
		pr_err("vf610_mscm_ir: regmap lookup for cpucfg failed\n");
		goto out_unmap;
	}

	regmap_read(mscm_cp_regmap, MSCM_CPxNUM, &cpuid);
	mscm_ir_data->cpu_mask = 0x1 << cpuid;

	domain = irq_domain_add_hierarchy(domain_parent, 0,
					  MSCM_IRSPRC_NUM, node,
					  &mscm_irq_domain_ops, mscm_ir_data);
	if (!domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	cpu_pm_register_notifier(&mscm_ir_notifier_block);

	return 0;

out_unmap:
	iounmap(mscm_ir_data->mscm_ir_base);
out_free:
	kfree(mscm_ir_data);
	return ret;
}
IRQCHIP_DECLARE(vf610_mscm_ir, "fsl,vf610-mscm-ir", vf610_mscm_ir_of_init);

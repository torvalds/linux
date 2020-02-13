// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "common.h"
#include "hardware.h"

#define GPC_CNTR		0x0
#define GPC_IMR1		0x008
#define GPC_PGC_CPU_PDN		0x2a0
#define GPC_PGC_CPU_PUPSCR	0x2a4
#define GPC_PGC_CPU_PDNSCR	0x2a8
#define GPC_PGC_SW2ISO_SHIFT	0x8
#define GPC_PGC_SW_SHIFT	0x0

#define GPC_CNTR_L2_PGE_SHIFT	22

#define IMR_NUM			4
#define GPC_MAX_IRQS		(IMR_NUM * 32)

static void __iomem *gpc_base;
static u32 gpc_wake_irqs[IMR_NUM];
static u32 gpc_saved_imrs[IMR_NUM];

void imx_gpc_set_arm_power_up_timing(u32 sw2iso, u32 sw)
{
	writel_relaxed((sw2iso << GPC_PGC_SW2ISO_SHIFT) |
		(sw << GPC_PGC_SW_SHIFT), gpc_base + GPC_PGC_CPU_PUPSCR);
}

void imx_gpc_set_arm_power_down_timing(u32 sw2iso, u32 sw)
{
	writel_relaxed((sw2iso << GPC_PGC_SW2ISO_SHIFT) |
		(sw << GPC_PGC_SW_SHIFT), gpc_base + GPC_PGC_CPU_PDNSCR);
}

void imx_gpc_set_arm_power_in_lpm(bool power_off)
{
	writel_relaxed(power_off, gpc_base + GPC_PGC_CPU_PDN);
}

void imx_gpc_set_l2_mem_power_in_lpm(bool power_off)
{
	u32 val;

	val = readl_relaxed(gpc_base + GPC_CNTR);
	val &= ~(1 << GPC_CNTR_L2_PGE_SHIFT);
	if (power_off)
		val |= 1 << GPC_CNTR_L2_PGE_SHIFT;
	writel_relaxed(val, gpc_base + GPC_CNTR);
}

void imx_gpc_pre_suspend(bool arm_power_off)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	/* Tell GPC to power off ARM core when suspend */
	if (arm_power_off)
		imx_gpc_set_arm_power_in_lpm(arm_power_off);

	for (i = 0; i < IMR_NUM; i++) {
		gpc_saved_imrs[i] = readl_relaxed(reg_imr1 + i * 4);
		writel_relaxed(~gpc_wake_irqs[i], reg_imr1 + i * 4);
	}
}

void imx_gpc_post_resume(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	/* Keep ARM core powered on for other low-power modes */
	imx_gpc_set_arm_power_in_lpm(false);

	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(gpc_saved_imrs[i], reg_imr1 + i * 4);
}

static int imx_gpc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned int idx = d->hwirq / 32;
	u32 mask;

	mask = 1 << d->hwirq % 32;
	gpc_wake_irqs[idx] = on ? gpc_wake_irqs[idx] | mask :
				  gpc_wake_irqs[idx] & ~mask;

	/*
	 * Do *not* call into the parent, as the GIC doesn't have any
	 * wake-up facility...
	 */
	return 0;
}

void imx_gpc_mask_all(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	for (i = 0; i < IMR_NUM; i++) {
		gpc_saved_imrs[i] = readl_relaxed(reg_imr1 + i * 4);
		writel_relaxed(~0, reg_imr1 + i * 4);
	}

}

void imx_gpc_restore_all(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(gpc_saved_imrs[i], reg_imr1 + i * 4);
}

void imx_gpc_hwirq_unmask(unsigned int hwirq)
{
	void __iomem *reg;
	u32 val;

	reg = gpc_base + GPC_IMR1 + hwirq / 32 * 4;
	val = readl_relaxed(reg);
	val &= ~(1 << hwirq % 32);
	writel_relaxed(val, reg);
}

void imx_gpc_hwirq_mask(unsigned int hwirq)
{
	void __iomem *reg;
	u32 val;

	reg = gpc_base + GPC_IMR1 + hwirq / 32 * 4;
	val = readl_relaxed(reg);
	val |= 1 << (hwirq % 32);
	writel_relaxed(val, reg);
}

static void imx_gpc_irq_unmask(struct irq_data *d)
{
	imx_gpc_hwirq_unmask(d->hwirq);
	irq_chip_unmask_parent(d);
}

static void imx_gpc_irq_mask(struct irq_data *d)
{
	imx_gpc_hwirq_mask(d->hwirq);
	irq_chip_mask_parent(d);
}

static struct irq_chip imx_gpc_chip = {
	.name			= "GPC",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= imx_gpc_irq_mask,
	.irq_unmask		= imx_gpc_irq_unmask,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_wake		= imx_gpc_irq_set_wake,
	.irq_set_type           = irq_chip_set_type_parent,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int imx_gpc_domain_translate(struct irq_domain *d,
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
		*type = fwspec->param[2];
		return 0;
	}

	return -EINVAL;
}

static int imx_gpc_domain_alloc(struct irq_domain *domain,
				  unsigned int irq,
				  unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	int i;

	if (fwspec->param_count != 3)
		return -EINVAL;	/* Not GIC compliant */
	if (fwspec->param[0] != 0)
		return -EINVAL;	/* No PPI should point to this domain */

	hwirq = fwspec->param[1];
	if (hwirq >= GPC_MAX_IRQS)
		return -EINVAL;	/* Can't deal with this */

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, irq + i, hwirq + i,
					      &imx_gpc_chip, NULL);

	parent_fwspec = *fwspec;
	parent_fwspec.fwnode = domain->parent->fwnode;
	return irq_domain_alloc_irqs_parent(domain, irq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops imx_gpc_domain_ops = {
	.translate	= imx_gpc_domain_translate,
	.alloc		= imx_gpc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init imx_gpc_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	int i;

	if (!parent) {
		pr_err("%pOF: no parent, giving up\n", node);
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: unable to obtain parent domain\n", node);
		return -ENXIO;
	}

	gpc_base = of_iomap(node, 0);
	if (WARN_ON(!gpc_base))
	        return -ENOMEM;

	domain = irq_domain_add_hierarchy(parent_domain, 0, GPC_MAX_IRQS,
					  node, &imx_gpc_domain_ops,
					  NULL);
	if (!domain) {
		iounmap(gpc_base);
		return -ENOMEM;
	}

	/* Initially mask all interrupts */
	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(~0, gpc_base + GPC_IMR1 + i * 4);

	/*
	 * Clear the OF_POPULATED flag set in of_irq_init so that
	 * later the GPC power domain driver will not be skipped.
	 */
	of_node_clear_flag(node, OF_POPULATED);

	return 0;
}
IRQCHIP_DECLARE(imx_gpc, "fsl,imx6q-gpc", imx_gpc_init);

void __init imx_gpc_check_dt(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-gpc");
	if (WARN_ON(!np))
		return;

	if (WARN_ON(!of_find_property(np, "interrupt-controller", NULL))) {
		pr_warn("Outdated DT detected, suspend/resume will NOT work\n");

		/* map GPC, so that at least CPUidle and WARs keep working */
		gpc_base = of_iomap(np, 0);
	}
	of_node_put(np);
}

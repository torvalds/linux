// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  arch/arm/mach-vt8500/irq.c
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 */

/*
 * This file is copied and modified from the original irq.c provided by
 * Alexey Charkov. Minor changes have been made for Device Tree Support.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/irq.h>
#include <asm/exception.h>
#include <asm/mach/irq.h>

#define VT8500_ICPC_IRQ		0x20
#define VT8500_ICPC_FIQ		0x24
#define VT8500_ICDC		0x40		/* Destination Control 64*u32 */
#define VT8500_ICIS		0x80		/* Interrupt status, 16*u32 */

/* ICPC */
#define ICPC_MASK		0x3F
#define ICPC_ROTATE		BIT(6)

/* IC_DCTR */
#define ICDC_IRQ		0x00
#define ICDC_FIQ		0x01
#define ICDC_DSS0		0x02
#define ICDC_DSS1		0x03
#define ICDC_DSS2		0x04
#define ICDC_DSS3		0x05
#define ICDC_DSS4		0x06
#define ICDC_DSS5		0x07

#define VT8500_INT_DISABLE	0
#define VT8500_INT_ENABLE	BIT(3)

#define VT8500_TRIGGER_HIGH	0
#define VT8500_TRIGGER_RISING	BIT(5)
#define VT8500_TRIGGER_FALLING	BIT(6)
#define VT8500_EDGE		( VT8500_TRIGGER_RISING \
				| VT8500_TRIGGER_FALLING)

/* vt8500 has 1 intc, wm8505 and wm8650 have 2 */
#define VT8500_INTC_MAX		2

struct vt8500_irq_data {
	void __iomem 		*base;		/* IO Memory base address */
	struct irq_domain	*domain;	/* Domain for this controller */
};

/* Global variable for accessing io-mem addresses */
static struct vt8500_irq_data intc[VT8500_INTC_MAX];
static u32 active_cnt = 0;

static void vt8500_irq_mask(struct irq_data *d)
{
	struct vt8500_irq_data *priv = d->domain->host_data;
	void __iomem *base = priv->base;
	void __iomem *stat_reg = base + VT8500_ICIS + (d->hwirq < 32 ? 0 : 4);
	u8 edge, dctr;
	u32 status;

	edge = readb(base + VT8500_ICDC + d->hwirq) & VT8500_EDGE;
	if (edge) {
		status = readl(stat_reg);

		status |= (1 << (d->hwirq & 0x1f));
		writel(status, stat_reg);
	} else {
		dctr = readb(base + VT8500_ICDC + d->hwirq);
		dctr &= ~VT8500_INT_ENABLE;
		writeb(dctr, base + VT8500_ICDC + d->hwirq);
	}
}

static void vt8500_irq_unmask(struct irq_data *d)
{
	struct vt8500_irq_data *priv = d->domain->host_data;
	void __iomem *base = priv->base;
	u8 dctr;

	dctr = readb(base + VT8500_ICDC + d->hwirq);
	dctr |= VT8500_INT_ENABLE;
	writeb(dctr, base + VT8500_ICDC + d->hwirq);
}

static int vt8500_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct vt8500_irq_data *priv = d->domain->host_data;
	void __iomem *base = priv->base;
	u8 dctr;

	dctr = readb(base + VT8500_ICDC + d->hwirq);
	dctr &= ~VT8500_EDGE;

	switch (flow_type) {
	case IRQF_TRIGGER_LOW:
		return -EINVAL;
	case IRQF_TRIGGER_HIGH:
		dctr |= VT8500_TRIGGER_HIGH;
		irq_set_handler_locked(d, handle_level_irq);
		break;
	case IRQF_TRIGGER_FALLING:
		dctr |= VT8500_TRIGGER_FALLING;
		irq_set_handler_locked(d, handle_edge_irq);
		break;
	case IRQF_TRIGGER_RISING:
		dctr |= VT8500_TRIGGER_RISING;
		irq_set_handler_locked(d, handle_edge_irq);
		break;
	}
	writeb(dctr, base + VT8500_ICDC + d->hwirq);

	return 0;
}

static struct irq_chip vt8500_irq_chip = {
	.name = "vt8500",
	.irq_ack = vt8500_irq_mask,
	.irq_mask = vt8500_irq_mask,
	.irq_unmask = vt8500_irq_unmask,
	.irq_set_type = vt8500_irq_set_type,
};

static void __init vt8500_init_irq_hw(void __iomem *base)
{
	u32 i;

	/* Enable rotating priority for IRQ */
	writel(ICPC_ROTATE, base + VT8500_ICPC_IRQ);
	writel(0x00, base + VT8500_ICPC_FIQ);

	/* Disable all interrupts and route them to IRQ */
	for (i = 0; i < 64; i++)
		writeb(VT8500_INT_DISABLE | ICDC_IRQ, base + VT8500_ICDC + i);
}

static int vt8500_irq_map(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &vt8500_irq_chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops vt8500_irq_domain_ops = {
	.map = vt8500_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static void __exception_irq_entry vt8500_handle_irq(struct pt_regs *regs)
{
	u32 stat, i;
	int irqnr;
	void __iomem *base;

	/* Loop through each active controller */
	for (i=0; i<active_cnt; i++) {
		base = intc[i].base;
		irqnr = readl_relaxed(base) & 0x3F;
		/*
		  Highest Priority register default = 63, so check that this
		  is a real interrupt by checking the status register
		*/
		if (irqnr == 63) {
			stat = readl_relaxed(base + VT8500_ICIS + 4);
			if (!(stat & BIT(31)))
				continue;
		}

		generic_handle_domain_irq(intc[i].domain, irqnr);
	}
}

static int __init vt8500_irq_init(struct device_node *node,
				  struct device_node *parent)
{
	int irq, i;
	struct device_node *np = node;

	if (active_cnt == VT8500_INTC_MAX) {
		pr_err("%s: Interrupt controllers > VT8500_INTC_MAX\n",
								__func__);
		goto out;
	}

	intc[active_cnt].base = of_iomap(np, 0);
	intc[active_cnt].domain = irq_domain_add_linear(node, 64,
			&vt8500_irq_domain_ops,	&intc[active_cnt]);

	if (!intc[active_cnt].base) {
		pr_err("%s: Unable to map IO memory\n", __func__);
		goto out;
	}

	if (!intc[active_cnt].domain) {
		pr_err("%s: Unable to add irq domain!\n", __func__);
		goto out;
	}

	set_handle_irq(vt8500_handle_irq);

	vt8500_init_irq_hw(intc[active_cnt].base);

	pr_info("vt8500-irq: Added interrupt controller\n");

	active_cnt++;

	/* check if this is a slaved controller */
	if (of_irq_count(np) != 0) {
		/* check that we have the correct number of interrupts */
		if (of_irq_count(np) != 8) {
			pr_err("%s: Incorrect IRQ map for slaved controller\n",
					__func__);
			return -EINVAL;
		}

		for (i = 0; i < 8; i++) {
			irq = irq_of_parse_and_map(np, i);
			enable_irq(irq);
		}

		pr_info("vt8500-irq: Enabled slave->parent interrupts\n");
	}
out:
	return 0;
}

IRQCHIP_DECLARE(vt8500_irq, "via,vt8500-intc", vt8500_irq_init);

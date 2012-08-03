/*
 *  arch/arm/mach-vt8500/irq.c
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This file is copied and modified from the original irq.c provided by
 * Alexey Charkov. Minor changes have been made for Device Tree Support.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/irq.h>


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

static int irq_cnt;

struct vt8500_irq_priv {
	void __iomem *base;
};

static void vt8500_irq_mask(struct irq_data *d)
{
	struct vt8500_irq_priv *priv =
			(struct vt8500_irq_priv *)(d->domain->host_data);
	void __iomem *base = priv->base;
	u8 edge;

	edge = readb(base + VT8500_ICDC + d->hwirq) & VT8500_EDGE;
	if (edge) {
		void __iomem *stat_reg = base + VT8500_ICIS
						+ (d->hwirq < 32 ? 0 : 4);
		unsigned status = readl(stat_reg);

		status |= (1 << (d->hwirq & 0x1f));
		writel(status, stat_reg);
	} else {
		u8 dctr = readb(base + VT8500_ICDC + d->hwirq);

		dctr &= ~VT8500_INT_ENABLE;
		writeb(dctr, base + VT8500_ICDC + d->hwirq);
	}
}

static void vt8500_irq_unmask(struct irq_data *d)
{
	struct vt8500_irq_priv *priv =
			(struct vt8500_irq_priv *)(d->domain->host_data);
	void __iomem *base = priv->base;
	u8 dctr;

	dctr = readb(base + VT8500_ICDC + d->hwirq);
	dctr |= VT8500_INT_ENABLE;
	writeb(dctr, base + VT8500_ICDC + d->hwirq);
}

static int vt8500_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct vt8500_irq_priv *priv =
			(struct vt8500_irq_priv *)(d->domain->host_data);
	void __iomem *base = priv->base;
	u8 dctr;

	dctr = readb(base + VT8500_ICDC + d->hwirq);
	dctr &= ~VT8500_EDGE;

	switch (flow_type) {
	case IRQF_TRIGGER_LOW:
		return -EINVAL;
	case IRQF_TRIGGER_HIGH:
		dctr |= VT8500_TRIGGER_HIGH;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		break;
	case IRQF_TRIGGER_FALLING:
		dctr |= VT8500_TRIGGER_FALLING;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;
	case IRQF_TRIGGER_RISING:
		dctr |= VT8500_TRIGGER_RISING;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
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
	unsigned int i;

	/* Enable rotating priority for IRQ */
	writel(ICPC_ROTATE, base + VT8500_ICPC_IRQ);
	writel(0x00, base + VT8500_ICPC_FIQ);

	for (i = 0; i < 64; i++) {
		/* Disable all interrupts and route them to IRQ */
		writeb(VT8500_INT_DISABLE | ICDC_IRQ,
						base + VT8500_ICDC + i);
	}
}

static int vt8500_irq_map(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &vt8500_irq_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);

	return 0;
}

static struct irq_domain_ops vt8500_irq_domain_ops = {
	.map = vt8500_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

int __init vt8500_irq_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *vt8500_irq_domain;
	struct vt8500_irq_priv *priv;
	int irq, i;
	struct device_node *np = node;

	priv = kzalloc(sizeof(struct vt8500_irq_priv), GFP_KERNEL);
	priv->base = of_iomap(np, 0);

	vt8500_irq_domain = irq_domain_add_legacy(node, 64, irq_cnt, 0,
				&vt8500_irq_domain_ops, priv);
	if (!vt8500_irq_domain)
		pr_err("%s: Unable to add wmt irq domain!\n", __func__);

	irq_set_default_host(vt8500_irq_domain);

	vt8500_init_irq_hw(priv->base);

	pr_info("Added IRQ Controller @ %x [virq_base = %d]\n",
						(u32)(priv->base), irq_cnt);

	/* check if this is a slaved controller */
	if (of_irq_count(np) != 0) {
		/* check that we have the correct number of interrupts */
		if (of_irq_count(np) != 8) {
			pr_err("%s: Incorrect IRQ map for slave controller\n",
					__func__);
			return -EINVAL;
		}

		for (i = 0; i < 8; i++) {
			irq = irq_of_parse_and_map(np, i);
			enable_irq(irq);
		}

		pr_info("vt8500-irq: Enabled slave->parent interrupts\n");
	}

	irq_cnt += 64;

	return 0;
}


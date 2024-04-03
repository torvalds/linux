// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>
 *
 * Description:
 * MPC5121ADS CPLD irq handling
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "mpc5121_ads.h"

static struct device_node *cpld_pic_node;
static struct irq_domain *cpld_pic_host;

/*
 * Bits to ignore in the misc_status register
 * 0x10 touch screen pendown is hard routed to irq1
 * 0x02 pci status is read from pci status register
 */
#define MISC_IGNORE 0x12

/*
 * Nothing to ignore in pci status register
 */
#define PCI_IGNORE 0x00

struct cpld_pic {
	u8 pci_mask;
	u8 pci_status;
	u8 route;
	u8 misc_mask;
	u8 misc_status;
	u8 misc_control;
};

static struct cpld_pic __iomem *cpld_regs;

static void __iomem *
irq_to_pic_mask(unsigned int irq)
{
	return irq <= 7 ? &cpld_regs->pci_mask : &cpld_regs->misc_mask;
}

static unsigned int
irq_to_pic_bit(unsigned int irq)
{
	return 1 << (irq & 0x7);
}

static void
cpld_mask_irq(struct irq_data *d)
{
	unsigned int cpld_irq = (unsigned int)irqd_to_hwirq(d);
	void __iomem *pic_mask = irq_to_pic_mask(cpld_irq);

	out_8(pic_mask,
	      in_8(pic_mask) | irq_to_pic_bit(cpld_irq));
}

static void
cpld_unmask_irq(struct irq_data *d)
{
	unsigned int cpld_irq = (unsigned int)irqd_to_hwirq(d);
	void __iomem *pic_mask = irq_to_pic_mask(cpld_irq);

	out_8(pic_mask,
	      in_8(pic_mask) & ~irq_to_pic_bit(cpld_irq));
}

static struct irq_chip cpld_pic = {
	.name = "CPLD PIC",
	.irq_mask = cpld_mask_irq,
	.irq_ack = cpld_mask_irq,
	.irq_unmask = cpld_unmask_irq,
};

static unsigned int
cpld_pic_get_irq(int offset, u8 ignore, u8 __iomem *statusp,
			    u8 __iomem *maskp)
{
	u8 status = in_8(statusp);
	u8 mask = in_8(maskp);

	/* ignore don't cares and masked irqs */
	status |= (ignore | mask);

	if (status == 0xff)
		return ~0;

	return ffz(status) + offset;
}

static void cpld_pic_cascade(struct irq_desc *desc)
{
	unsigned int hwirq;

	hwirq = cpld_pic_get_irq(0, PCI_IGNORE, &cpld_regs->pci_status,
		&cpld_regs->pci_mask);
	if (hwirq != ~0) {
		generic_handle_domain_irq(cpld_pic_host, hwirq);
		return;
	}

	hwirq = cpld_pic_get_irq(8, MISC_IGNORE, &cpld_regs->misc_status,
		&cpld_regs->misc_mask);
	if (hwirq != ~0) {
		generic_handle_domain_irq(cpld_pic_host, hwirq);
		return;
	}
}

static int
cpld_pic_host_match(struct irq_domain *h, struct device_node *node,
		    enum irq_domain_bus_token bus_token)
{
	return cpld_pic_node == node;
}

static int
cpld_pic_host_map(struct irq_domain *h, unsigned int virq,
			     irq_hw_number_t hw)
{
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &cpld_pic, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops cpld_pic_host_ops = {
	.match = cpld_pic_host_match,
	.map = cpld_pic_host_map,
};

void __init
mpc5121_ads_cpld_map(void)
{
	struct device_node *np = NULL;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121ads-cpld-pic");
	if (!np) {
		printk(KERN_ERR "CPLD PIC init: can not find cpld-pic node\n");
		return;
	}

	cpld_regs = of_iomap(np, 0);
	of_node_put(np);
}

void __init
mpc5121_ads_cpld_pic_init(void)
{
	unsigned int cascade_irq;
	struct device_node *np = NULL;

	pr_debug("cpld_ic_init\n");

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121ads-cpld-pic");
	if (!np) {
		printk(KERN_ERR "CPLD PIC init: can not find cpld-pic node\n");
		return;
	}

	if (!cpld_regs)
		goto end;

	cascade_irq = irq_of_parse_and_map(np, 0);
	if (!cascade_irq)
		goto end;

	/*
	 * statically route touch screen pendown through 1
	 * and ignore it here
	 * route all others through our cascade irq
	 */
	out_8(&cpld_regs->route, 0xfd);
	out_8(&cpld_regs->pci_mask, 0xff);
	/* unmask pci ints in misc mask */
	out_8(&cpld_regs->misc_mask, ~(MISC_IGNORE));

	cpld_pic_node = of_node_get(np);

	cpld_pic_host = irq_domain_add_linear(np, 16, &cpld_pic_host_ops, NULL);
	if (!cpld_pic_host) {
		printk(KERN_ERR "CPLD PIC: failed to allocate irq host!\n");
		goto end;
	}

	irq_set_chained_handler(cascade_irq, cpld_pic_cascade);
end:
	of_node_put(np);
}

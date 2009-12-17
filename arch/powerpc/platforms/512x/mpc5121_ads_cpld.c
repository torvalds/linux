/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>
 *
 * Description:
 * MPC5121ADS CPLD irq handling
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/prom.h>

static struct device_node *cpld_pic_node;
static struct irq_host *cpld_pic_host;

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
cpld_mask_irq(unsigned int irq)
{
	unsigned int cpld_irq = (unsigned int)irq_map[irq].hwirq;
	void __iomem *pic_mask = irq_to_pic_mask(cpld_irq);

	out_8(pic_mask,
	      in_8(pic_mask) | irq_to_pic_bit(cpld_irq));
}

static void
cpld_unmask_irq(unsigned int irq)
{
	unsigned int cpld_irq = (unsigned int)irq_map[irq].hwirq;
	void __iomem *pic_mask = irq_to_pic_mask(cpld_irq);

	out_8(pic_mask,
	      in_8(pic_mask) & ~irq_to_pic_bit(cpld_irq));
}

static struct irq_chip cpld_pic = {
	.name = " CPLD PIC ",
	.mask = cpld_mask_irq,
	.ack = cpld_mask_irq,
	.unmask = cpld_unmask_irq,
};

static int
cpld_pic_get_irq(int offset, u8 ignore, u8 __iomem *statusp,
			    u8 __iomem *maskp)
{
	int cpld_irq;
	u8 status = in_8(statusp);
	u8 mask = in_8(maskp);

	/* ignore don't cares and masked irqs */
	status |= (ignore | mask);

	if (status == 0xff)
		return NO_IRQ_IGNORE;

	cpld_irq = ffz(status) + offset;

	return irq_linear_revmap(cpld_pic_host, cpld_irq);
}

static void
cpld_pic_cascade(unsigned int irq, struct irq_desc *desc)
{
	irq = cpld_pic_get_irq(0, PCI_IGNORE, &cpld_regs->pci_status,
		&cpld_regs->pci_mask);
	if (irq != NO_IRQ && irq != NO_IRQ_IGNORE) {
		generic_handle_irq(irq);
		return;
	}

	irq = cpld_pic_get_irq(8, MISC_IGNORE, &cpld_regs->misc_status,
		&cpld_regs->misc_mask);
	if (irq != NO_IRQ && irq != NO_IRQ_IGNORE) {
		generic_handle_irq(irq);
		return;
	}
}

static int
cpld_pic_host_match(struct irq_host *h, struct device_node *node)
{
	return cpld_pic_node == node;
}

static int
cpld_pic_host_map(struct irq_host *h, unsigned int virq,
			     irq_hw_number_t hw)
{
	irq_to_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &cpld_pic, handle_level_irq);
	return 0;
}

static struct
irq_host_ops cpld_pic_host_ops = {
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
	if (cascade_irq == NO_IRQ)
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

	cpld_pic_host =
	    irq_alloc_host(np, IRQ_HOST_MAP_LINEAR, 16, &cpld_pic_host_ops, 16);
	if (!cpld_pic_host) {
		printk(KERN_ERR "CPLD PIC: failed to allocate irq host!\n");
		goto end;
	}

	set_irq_chained_handler(cascade_irq, cpld_pic_cascade);
end:
	of_node_put(np);
}

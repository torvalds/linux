/*
 * DSM-G600 board-level PCI initialization
 *
 * Copyright (C) 2006 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on ixdp425-pci.c:
 *	Copyright (C) 2002 Intel Corporation.
 *	Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Maintainer: http://www.nslu2-linux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

#define MAX_DEV		4
#define IRQ_LINES	3

/* PCI controller GPIO to IRQ pin mappings */
#define INTA		11
#define INTB		10
#define INTC		9
#define INTD		8
#define INTE		7
#define INTF		6

void __init dsmg600_pci_preinit(void)
{
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTA), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTB), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTC), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTD), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTE), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTF), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static int __init dsmg600_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[MAX_DEV][IRQ_LINES] = {
		{ IXP4XX_GPIO_IRQ(INTE), -1, -1 },
		{ IXP4XX_GPIO_IRQ(INTA), -1, -1 },
		{ IXP4XX_GPIO_IRQ(INTB), IXP4XX_GPIO_IRQ(INTC),
		  IXP4XX_GPIO_IRQ(INTD) },
		{ IXP4XX_GPIO_IRQ(INTF), -1, -1 },
	};

	if (slot >= 1 && slot <= MAX_DEV && pin >= 1 && pin <= IRQ_LINES)
		return pci_irq_table[slot - 1][pin - 1];

	return -1;
}

struct hw_pci __initdata dsmg600_pci = {
	.nr_controllers = 1,
	.preinit	= dsmg600_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= dsmg600_map_irq,
};

int __init dsmg600_pci_init(void)
{
	if (machine_is_dsmg600())
		pci_common_init(&dsmg600_pci);

	return 0;
}

subsys_initcall(dsmg600_pci_init);

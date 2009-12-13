/*
 * arch/arm/mach-ixp4xx/nas100d-pci.c
 *
 * NAS 100d board-level PCI initialization
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

#define MAX_DEV		3
#define IRQ_LINES	3

/* PCI controller GPIO to IRQ pin mappings */
#define INTA		11
#define INTB		10
#define INTC		9
#define INTD		8
#define INTE		7

void __init nas100d_pci_preinit(void)
{
	set_irq_type(IXP4XX_GPIO_IRQ(INTA), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(INTB), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(INTC), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(INTD), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(INTE), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static int __init nas100d_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[MAX_DEV][IRQ_LINES] = {
		{ IXP4XX_GPIO_IRQ(INTA), -1, -1 },
		{ IXP4XX_GPIO_IRQ(INTB), -1, -1 },
		{ IXP4XX_GPIO_IRQ(INTC), IXP4XX_GPIO_IRQ(INTD),
		  IXP4XX_GPIO_IRQ(INTE) },
	};

	if (slot >= 1 && slot <= MAX_DEV && pin >= 1 && pin <= IRQ_LINES)
		return pci_irq_table[slot - 1][pin - 1];

	return -1;
}

struct hw_pci __initdata nas100d_pci = {
	.nr_controllers = 1,
	.preinit	= nas100d_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= nas100d_map_irq,
};

int __init nas100d_pci_init(void)
{
	if (machine_is_nas100d())
		pci_common_init(&nas100d_pci);

	return 0;
}

subsys_initcall(nas100d_pci_init);

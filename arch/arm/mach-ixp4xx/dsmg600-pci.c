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

void __init dsmg600_pci_preinit(void)
{
	set_irq_type(IRQ_DSMG600_PCI_INTA, IRQT_LOW);
	set_irq_type(IRQ_DSMG600_PCI_INTB, IRQT_LOW);
	set_irq_type(IRQ_DSMG600_PCI_INTC, IRQT_LOW);
	set_irq_type(IRQ_DSMG600_PCI_INTD, IRQT_LOW);
	set_irq_type(IRQ_DSMG600_PCI_INTE, IRQT_LOW);
	set_irq_type(IRQ_DSMG600_PCI_INTF, IRQT_LOW);

	ixp4xx_pci_preinit();
}

static int __init dsmg600_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[DSMG600_PCI_MAX_DEV][DSMG600_PCI_IRQ_LINES] =
	{
		{ IRQ_DSMG600_PCI_INTE, -1, -1 },
		{ IRQ_DSMG600_PCI_INTA, -1, -1 },
		{ IRQ_DSMG600_PCI_INTB, IRQ_DSMG600_PCI_INTC, IRQ_DSMG600_PCI_INTD },
		{ IRQ_DSMG600_PCI_INTF, -1, -1 },
	};

	int irq = -1;

	if (slot >= 1 && slot <= DSMG600_PCI_MAX_DEV &&
		pin >= 1 && pin <= DSMG600_PCI_IRQ_LINES)
		irq = pci_irq_table[slot-1][pin-1];

	return irq;
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

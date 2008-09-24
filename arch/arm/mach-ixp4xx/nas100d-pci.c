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

void __init nas100d_pci_preinit(void)
{
	set_irq_type(IRQ_NAS100D_PCI_INTA, IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IRQ_NAS100D_PCI_INTB, IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IRQ_NAS100D_PCI_INTC, IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IRQ_NAS100D_PCI_INTD, IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IRQ_NAS100D_PCI_INTE, IRQ_TYPE_LEVEL_LOW);

	ixp4xx_pci_preinit();
}

static int __init nas100d_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[NAS100D_PCI_MAX_DEV][NAS100D_PCI_IRQ_LINES] =
	{
		{ IRQ_NAS100D_PCI_INTA, -1, -1 },
		{ IRQ_NAS100D_PCI_INTB, -1, -1 },
		{ IRQ_NAS100D_PCI_INTC, IRQ_NAS100D_PCI_INTD, IRQ_NAS100D_PCI_INTE },
	};

	int irq = -1;

	if (slot >= 1 && slot <= NAS100D_PCI_MAX_DEV &&
		pin >= 1 && pin <= NAS100D_PCI_IRQ_LINES)
		irq = pci_irq_table[slot-1][pin-1];

	return irq;
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

/*
 * arch/arm/mach-ixp4xx/ixdp425-pci.c 
 *
 * IXDP425 board-level PCI initialization
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/mach/pci.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

void __init ixdp425_pci_preinit(void)
{
	set_irq_type(IRQ_IXDP425_PCI_INTA, IRQT_LOW);
	set_irq_type(IRQ_IXDP425_PCI_INTB, IRQT_LOW);
	set_irq_type(IRQ_IXDP425_PCI_INTC, IRQT_LOW);
	set_irq_type(IRQ_IXDP425_PCI_INTD, IRQT_LOW);

	ixp4xx_pci_preinit();
}

static int __init ixdp425_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[IXDP425_PCI_IRQ_LINES] = {
		IRQ_IXDP425_PCI_INTA,
		IRQ_IXDP425_PCI_INTB,
		IRQ_IXDP425_PCI_INTC,
		IRQ_IXDP425_PCI_INTD
	};

	int irq = -1;

	if (slot >= 1 && slot <= IXDP425_PCI_MAX_DEV && 
		pin >= 1 && pin <= IXDP425_PCI_IRQ_LINES) {
		irq = pci_irq_table[(slot + pin - 2) % 4];
	}

	return irq;
}

struct hw_pci ixdp425_pci __initdata = {
	.nr_controllers = 1,
	.preinit	= ixdp425_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= ixdp425_map_irq,
};

int __init ixdp425_pci_init(void)
{
	if (machine_is_ixdp425() || machine_is_ixcdp1100() ||
			machine_is_avila() || machine_is_ixdp465())
		pci_common_init(&ixdp425_pci);
	return 0;
}

subsys_initcall(ixdp425_pci_init);


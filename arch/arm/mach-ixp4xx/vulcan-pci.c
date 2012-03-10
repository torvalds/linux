/*
 * arch/arch/mach-ixp4xx/vulcan-pci.c
 *
 * Vulcan board-level PCI initialization
 *
 * Copyright (C) 2010 Marc Zyngier <maz@misterjones.org>
 *
 * based on ixdp425-pci.c:
 *	Copyright (C) 2002 Intel Corporation.
 *	Copyright (C) 2003-2004 MontaVista Software, Inc.
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

/* PCI controller GPIO to IRQ pin mappings */
#define INTA	2
#define INTB	3

void __init vulcan_pci_preinit(void)
{
#ifndef CONFIG_IXP4XX_INDIRECT_PCI
	/*
	 * Cardbus bridge wants way more than the SoC can actually offer,
	 * and leaves the whole PCI bus in a mess. Artificially limit it
	 * to 8MB per region. Of course indirect mode doesn't have this
	 * limitation...
	 */
	pci_cardbus_mem_size = SZ_8M;
	pr_info("Vulcan PCI: limiting CardBus memory size to %dMB\n",
		(int)(pci_cardbus_mem_size >> 20));
#endif
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTA), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTB), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static int __init vulcan_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 1)
		return IXP4XX_GPIO_IRQ(INTA);

	if (slot == 2)
		return IXP4XX_GPIO_IRQ(INTB);

	return -1;
}

struct hw_pci vulcan_pci __initdata = {
	.nr_controllers	= 1,
	.preinit	= vulcan_pci_preinit,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= vulcan_map_irq,
};

int __init vulcan_pci_init(void)
{
	if (machine_is_arcom_vulcan())
		pci_common_init(&vulcan_pci);
	return 0;
}

subsys_initcall(vulcan_pci_init);

/*
 * arch/arm/mach-ixp4xx/avila-pci.c
 *
 * Gateworks Avila board-level PCI initialization
 *
 * Author: Michael-Luke Jones <mlj28@cam.ac.uk>
 *
 * Based on ixdp-pci.c
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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/mach/pci.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>

#define AVILA_MAX_DEV	4
#define LOFT_MAX_DEV	6
#define IRQ_LINES	4

/* PCI controller GPIO to IRQ pin mappings */
#define INTA		11
#define INTB		10
#define INTC		9
#define INTD		8

void __init avila_pci_preinit(void)
{
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTA), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTB), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTC), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(INTD), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static int __init avila_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[IRQ_LINES] = {
		IXP4XX_GPIO_IRQ(INTA),
		IXP4XX_GPIO_IRQ(INTB),
		IXP4XX_GPIO_IRQ(INTC),
		IXP4XX_GPIO_IRQ(INTD)
	};

	if (slot >= 1 &&
	    slot <= (machine_is_loft() ? LOFT_MAX_DEV : AVILA_MAX_DEV) &&
	    pin >= 1 && pin <= IRQ_LINES)
		return pci_irq_table[(slot + pin - 2) % 4];

	return -1;
}

struct hw_pci avila_pci __initdata = {
	.nr_controllers = 1,
	.preinit	= avila_pci_preinit,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= avila_map_irq,
};

int __init avila_pci_init(void)
{
	if (machine_is_avila() || machine_is_loft())
		pci_common_init(&avila_pci);
	return 0;
}

subsys_initcall(avila_pci_init);

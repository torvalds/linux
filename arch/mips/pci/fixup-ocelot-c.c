/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Based on work for the Linux port to the Ocelot board, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/momentum/ocelot_g/pci.c
 *     Board-specific PCI routines for mv64340 controller.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int bus = dev->bus->number;

	if (bus == 0 && slot == 1)
		return 2;       /* PCI-X A */
	if (bus == 1 && slot == 1)
		return 12;      /* PCI-X B */
	if (bus == 1 && slot == 2)
		return 4;       /* PCI B */

return 0;
	panic("Whooops in pcibios_map_irq");
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

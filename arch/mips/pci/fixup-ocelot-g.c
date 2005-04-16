/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2004 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int bus = dev->bus->number;

	if (bus == 0 && slot == 1)	/* Intel 82543 Gigabit MAC */
		return 2;		/* irq_nr is 2 for INT0 */

	if (bus == 0 && slot == 2)	/* Intel 82543 Gigabit MAC */
		return 3;		/* irq_nr is 3 for INT1 */

	if (bus == 1 && slot == 3)	/* Intel 21555 bridge */
		return 5;		/* irq_nr is 8 for INT6 */

	if (bus == 1 && slot == 4)	/* PMC Slot */
		return 9;		/* irq_nr is 9 for INT7 */

	return -1;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

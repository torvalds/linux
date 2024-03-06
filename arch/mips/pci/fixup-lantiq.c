// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#include <linux/of_pci.h>
#include <linux/pci.h>

int (*ltq_pci_plat_arch_init)(struct pci_dev *dev) = NULL;
int (*ltq_pci_plat_dev_init)(struct pci_dev *dev) = NULL;

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	if (ltq_pci_plat_arch_init)
		return ltq_pci_plat_arch_init(dev);

	if (ltq_pci_plat_dev_init)
		return ltq_pci_plat_dev_init(dev);

	return 0;
}

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return of_irq_parse_and_map_pci(dev, slot, pin);
}

/*
 *  Atheros AR71XX/AR724X specific PCI setup code
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/irq.h>
#include <asm/mach-ath79/pci.h>
#include "pci.h"

static int (*ath79_pci_plat_dev_init)(struct pci_dev *dev);
static const struct ath79_pci_irq *ath79_pci_irq_map __initdata;
static unsigned ath79_pci_nr_irqs __initdata;

static const struct ath79_pci_irq ar71xx_pci_irq_map[] __initconst = {
	{
		.slot	= 17,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(0),
	}, {
		.slot	= 18,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(1),
	}, {
		.slot	= 19,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(2),
	}
};

static const struct ath79_pci_irq ar724x_pci_irq_map[] __initconst = {
	{
		.slot	= 0,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(0),
	}
};

int __init pcibios_map_irq(const struct pci_dev *dev, uint8_t slot, uint8_t pin)
{
	int irq = -1;
	int i;

	if (ath79_pci_nr_irqs == 0 ||
	    ath79_pci_irq_map == NULL) {
		if (soc_is_ar71xx()) {
			ath79_pci_irq_map = ar71xx_pci_irq_map;
			ath79_pci_nr_irqs = ARRAY_SIZE(ar71xx_pci_irq_map);
		} else if (soc_is_ar724x()) {
			ath79_pci_irq_map = ar724x_pci_irq_map;
			ath79_pci_nr_irqs = ARRAY_SIZE(ar724x_pci_irq_map);
		} else {
			pr_crit("pci %s: invalid irq map\n",
				pci_name((struct pci_dev *) dev));
			return irq;
		}
	}

	for (i = 0; i < ath79_pci_nr_irqs; i++) {
		const struct ath79_pci_irq *entry;

		entry = &ath79_pci_irq_map[i];
		if (entry->slot == slot && entry->pin == pin) {
			irq = entry->irq;
			break;
		}
	}

	if (irq < 0)
		pr_crit("pci %s: no irq found for pin %u\n",
			pci_name((struct pci_dev *) dev), pin);
	else
		pr_info("pci %s: using irq %d for pin %u\n",
			pci_name((struct pci_dev *) dev), irq, pin);

	return irq;
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	if (ath79_pci_plat_dev_init)
		return ath79_pci_plat_dev_init(dev);

	return 0;
}

void __init ath79_pci_set_irq_map(unsigned nr_irqs,
				  const struct ath79_pci_irq *map)
{
	ath79_pci_nr_irqs = nr_irqs;
	ath79_pci_irq_map = map;
}

void __init ath79_pci_set_plat_dev_init(int (*func)(struct pci_dev *dev))
{
	ath79_pci_plat_dev_init = func;
}

int __init ath79_register_pci(void)
{
	if (soc_is_ar71xx())
		return ar71xx_pcibios_init();

	if (soc_is_ar724x())
		return ar724x_pcibios_init(ATH79_CPU_IRQ_IP2);

	return -ENODEV;
}

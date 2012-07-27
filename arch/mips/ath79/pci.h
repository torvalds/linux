/*
 *  Atheros AR71XX/AR724X PCI support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef _ATH79_PCI_H
#define _ATH79_PCI_H

struct ath79_pci_irq {
	u8	slot;
	u8	pin;
	int	irq;
};

#ifdef CONFIG_PCI
void ath79_pci_set_irq_map(unsigned nr_irqs, const struct ath79_pci_irq *map);
void ath79_pci_set_plat_dev_init(int (*func)(struct pci_dev *dev));
int ath79_register_pci(void);
#else
static inline void
ath79_pci_set_irq_map(unsigned nr_irqs, const struct ath79_pci_irq *map) {}
static inline void
ath79_pci_set_plat_dev_init(int (*func)(struct pci_dev *)) {}
static inline int ath79_register_pci(void) { return 0; }
#endif

#endif /* _ATH79_PCI_H */

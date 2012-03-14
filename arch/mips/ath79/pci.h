/*
 *  Atheros 724x PCI support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ATH79_PCI_ATH724X_H
#define __ASM_MACH_ATH79_PCI_ATH724X_H

struct ath724x_pci_data {
	int irq;
	void *pdata;
};

void ath724x_pci_add_data(struct ath724x_pci_data *data, int size);

#ifdef CONFIG_PCI
int ath79_register_pci(void);
#else
static inline int ath79_register_pci(void) { return 0; }
#endif

#endif /* __ASM_MACH_ATH79_PCI_ATH724X_H */

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fixup-mpc30x.c, The Victor MP-C303/304 specific PCI fixups.
 *
 *  Copyright (C) 2002,2004  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/mpc30x.h>

static const int internal_func_irqs[] = {
	VRC4173_CASCADE_IRQ,
	VRC4173_AC97_IRQ,
	VRC4173_USB_IRQ,
};

static const int irq_tab_mpc30x[] = {
 [12] = VRC4173_PCMCIA1_IRQ,
 [13] = VRC4173_PCMCIA2_IRQ,
 [29] = MQ200_IRQ,
};

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 30)
		return internal_func_irqs[PCI_FUNC(dev->devfn)];

	return irq_tab_mpc30x[slot];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

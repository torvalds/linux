// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fixup-cappcela.c, The ZAO Networks Capcella specific PCI fixups.
 *
 *  Copyright (C) 2002,2004  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/capcella.h>

/*
 * Shortcuts
 */
#define INT1	RTL8139_1_IRQ
#define INT2	RTL8139_2_IRQ
#define INTA	PC104PLUS_INTA_IRQ
#define INTB	PC104PLUS_INTB_IRQ
#define INTC	PC104PLUS_INTC_IRQ
#define INTD	PC104PLUS_INTD_IRQ

static char irq_tab_capcella[][5] = {
 [11] = { -1, INT1, INT1, INT1, INT1 },
 [12] = { -1, INT2, INT2, INT2, INT2 },
 [14] = { -1, INTA, INTB, INTC, INTD }
};

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab_capcella[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

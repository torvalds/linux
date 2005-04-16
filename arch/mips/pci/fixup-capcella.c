/*
 *  fixup-cappcela.c, The ZAO Networks Capcella specific PCI fixups.
 *
 *  Copyright (C) 2002,2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

static char irq_tab_capcella[][5] __initdata = {
 [11] = { -1, INT1, INT1, INT1, INT1 },
 [12] = { -1, INT2, INT2, INT2, INT2 },
 [14] = { -1, INTA, INTB, INTC, INTD }
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab_capcella[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

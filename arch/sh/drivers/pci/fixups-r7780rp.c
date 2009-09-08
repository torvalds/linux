/*
 * arch/sh/drivers/pci/fixups-r7780rp.c
 *
 * Highlander R7780RP-1 PCI fixups
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004 - 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include <linux/io.h>
#include "pci-sh4.h"

static char irq_tab[] __initdata = {
	65, 66, 67, 68,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	return irq_tab[slot];
}

int pci_fixup_pcic(struct pci_channel *chan)
{
	pci_write_reg(chan, 0x000043ff, SH4_PCIINTM);
	pci_write_reg(chan, 0x00000000, SH7780_PCIIBAR);
	pci_write_reg(chan, 0x08000000, SH7780_PCICSCR0);
	pci_write_reg(chan, 0x0000001b, SH7780_PCICSAR0);
	pci_write_reg(chan, 0xfd000000, SH7780_PCICSCR1);
	pci_write_reg(chan, 0x0000000f, SH7780_PCICSAR1);

	return 0;
}

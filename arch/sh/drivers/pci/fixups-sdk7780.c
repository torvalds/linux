/*
 * arch/sh/drivers/pci/fixups-sdk7780.c
 *
 * PCI fixups for the SDK7780SE03
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004 - 2006  Paul Mundt
 * Copyright (C) 2006  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include <linux/io.h>
#include "pci-sh4.h"

/* IDSEL [16][17][18][19][20][21][22][23][24][25][26][27][28][29][30][31] */
static char sdk7780_irq_tab[4][16] __initdata = {
	/* INTA */
	{ 65, 68, 67, 68, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTB */
	{ 66, 65, -1, 65, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTC */
	{ 67, 66, -1, 66, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTD */
	{ 68, 67, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
       return sdk7780_irq_tab[pin-1][slot];
}
int pci_fixup_pcic(struct pci_channel *chan)
{
	/* Enable all interrupts, so we know what to fix */
	pci_write_reg(chan, 0x0000C3FF, SH7780_PCIIMR);

	/* Set up standard PCI config registers */
	pci_write_reg(chan, 0x08000000, SH7780_PCIMBAR0);	/* PCI */
	pci_write_reg(chan, 0x08000000, SH4_PCILAR0);	/* SHwy */
	pci_write_reg(chan, 0x07F00001, SH4_PCILSR0);	/* size 128M w/ MBAR */

	pci_write_reg(chan, 0x00000000, SH7780_PCIMBAR1);
	pci_write_reg(chan, 0x00000000, SH4_PCILAR1);
	pci_write_reg(chan, 0x00000000, SH4_PCILSR1);

	pci_write_reg(chan, 0xAB000801, SH7780_PCIIBAR);
	pci_write_reg(chan, 0xA5000C01, SH4_PCICR);

	return 0;
}

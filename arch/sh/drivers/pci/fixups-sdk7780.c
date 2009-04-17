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
	pci_write_reg(chan, 0x0000380F, SH7780_PCIAINTM);

	/* Set up standard PCI config registers */
	pci_write_reg(chan, 0xFB00, SH7780_PCISTATUS);
	pci_write_reg(chan, 0x0047, SH7780_PCICMD);
	pci_write_reg(chan, 0x00, SH7780_PCIPIF);
	pci_write_reg(chan, 0x1912, SH7780_PCISVID);
	pci_write_reg(chan, 0x0001, SH7780_PCISID);

	pci_write_reg(chan, 0x08000000, SH7780_PCIMBAR0);	/* PCI */
	pci_write_reg(chan, 0x08000000, SH7780_PCILAR0);	/* SHwy */
	pci_write_reg(chan, 0x07F00001, SH7780_PCILSR);	/* size 128M w/ MBAR */

	pci_write_reg(chan, 0x00000000, SH7780_PCIMBAR1);
	pci_write_reg(chan, 0x00000000, SH7780_PCILAR1);
	pci_write_reg(chan, 0x00000000, SH7780_PCILSR1);

	pci_write_reg(chan, 0xAB000801, SH7780_PCIIBAR);

	/*
	 * Set the MBR so PCI address is one-to-one with window,
	 * meaning all calls go straight through... use ifdef to
	 * catch erroneous assumption.
	 */
	pci_write_reg(chan, 0xFD000000 , SH7780_PCIMBR0);
	pci_write_reg(chan, 0x00FC0000 , SH7780_PCIMBMR0);	/* 16M */

	/* Set IOBR for window containing area specified in pci.h */
	pci_write_reg(chan, chan->io_resource->start & ~(SH7780_PCI_IO_SIZE-1),
		      SH7780_PCIIOBR);
	pci_write_reg(chan, (SH7780_PCI_IO_SIZE-1) & (7 << 18),
		      SH7780_PCIIOBMR);

	pci_write_reg(chan, 0xA5000C01, SH7780_PCICR);

	return 0;
}

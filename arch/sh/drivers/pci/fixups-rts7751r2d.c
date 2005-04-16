/*
 * arch/sh/drivers/pci/fixups-rts7751r2d.c
 *
 * RTS7751R2D PCI fixups
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include "pci-sh7751.h"
#include <asm/io.h>

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

int pci_fixup_pcic(void)
{
	unsigned long bcr1, mcr;

	bcr1 = inl(SH7751_BCR1);
	bcr1 |= 0x40080000;	/* Enable Bit 19 BREQEN, set PCIC to slave */
	outl(bcr1, PCI_REG(SH7751_PCIBCR1));

	/* Enable all interrupts, so we known what to fix */
	outl(0x0000c3ff, PCI_REG(SH7751_PCIINTM));
	outl(0x0000380f, PCI_REG(SH7751_PCIAINTM));

	outl(0xfb900047, PCI_REG(SH7751_PCICONF1));
	outl(0xab000001, PCI_REG(SH7751_PCICONF4));

	mcr = inl(SH7751_MCR);
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	outl(mcr, PCI_REG(SH7751_PCIMCR));

	outl(0x0c000000, PCI_REG(SH7751_PCICONF5));
	outl(0xd0000000, PCI_REG(SH7751_PCICONF6));
	outl(0x0c000000, PCI_REG(SH7751_PCILAR0));
	outl(0x00000000, PCI_REG(SH7751_PCILAR1));
	return 0;
}

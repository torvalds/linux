/*
 * arch/sh/drivers/pci/fixups-lboxre2.c
 *
 * L-BOX RE2 PCI fixups
 *
 * Copyright (C) 2007 Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include "pci-sh4.h"

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

int pci_fixup_pcic(void)
{
	unsigned long bcr1, mcr;

	bcr1 = inl(SH7751_BCR1);
	bcr1 |= 0x40080000;	/* Enable Bit 19 BREQEN, set PCIC to slave */
	pci_write_reg(bcr1, SH4_PCIBCR1);

	/* Enable all interrupts, so we known what to fix */
	pci_write_reg(0x0000c3ff, SH4_PCIINTM);
	pci_write_reg(0x0000380f, SH4_PCIAINTM);
	pci_write_reg(0xfb900047, SH7751_PCICONF1);
	pci_write_reg(0xab000001, SH7751_PCICONF4);

	mcr = inl(SH7751_MCR);
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	pci_write_reg(mcr, SH4_PCIMCR);

	pci_write_reg(0x0c000000, SH7751_PCICONF5);
	pci_write_reg(0xd0000000, SH7751_PCICONF6);
	pci_write_reg(0x0c000000, SH4_PCILAR0);
	pci_write_reg(0x00000000, SH4_PCILAR1);

	return 0;
}

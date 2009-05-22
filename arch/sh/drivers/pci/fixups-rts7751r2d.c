/*
 * arch/sh/drivers/pci/fixups-rts7751r2d.c
 *
 * RTS7751R2D / LBOXRE2 PCI fixups
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004  Paul Mundt
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include <mach/lboxre2.h>
#include <mach/r2d.h>
#include "pci-sh4.h"
#include <asm/machtypes.h>

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

static u8 rts7751r2d_irq_tab[] __initdata = {
	IRQ_PCI_INTA,
	IRQ_PCI_INTB,
	IRQ_PCI_INTC,
	IRQ_PCI_INTD,
};

static char lboxre2_irq_tab[] __initdata = {
	IRQ_ETH0, IRQ_ETH1, IRQ_INTA, IRQ_INTD,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	if (mach_is_lboxre2())
		return lboxre2_irq_tab[slot];
	else
		return rts7751r2d_irq_tab[slot];
}

int pci_fixup_pcic(struct pci_channel *chan)
{
	unsigned long bcr1, mcr;

	bcr1 = ctrl_inl(SH7751_BCR1);
	bcr1 |= 0x40080000;	/* Enable Bit 19 BREQEN, set PCIC to slave */
	pci_write_reg(chan, bcr1, SH4_PCIBCR1);

	/* Enable all interrupts, so we known what to fix */
	pci_write_reg(chan, 0x0000c3ff, SH4_PCIINTM);
	pci_write_reg(chan, 0x0000380f, SH4_PCIAINTM);

	pci_write_reg(chan, 0xfb900047, SH7751_PCICONF1);
	pci_write_reg(chan, 0xab000001, SH7751_PCICONF4);

	mcr = ctrl_inl(SH7751_MCR);
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	pci_write_reg(chan, mcr, SH4_PCIMCR);

	pci_write_reg(chan, 0x0c000000, SH7751_PCICONF5);
	pci_write_reg(chan, 0xd0000000, SH7751_PCICONF6);
	pci_write_reg(chan, 0x0c000000, SH4_PCILAR0);
	pci_write_reg(chan, 0x00000000, SH4_PCILAR1);

	return 0;
}

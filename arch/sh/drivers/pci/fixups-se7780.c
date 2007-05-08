/*
 * arch/sh/drivers/pci/fixups-se7780.c
 *
 * HITACHI UL Solution Engine 7780  PCI fixups
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
#include "pci-sh4.h"
#include <asm/io.h>

int pci_fixup_pcic(void)
{
	ctrl_outl(0x00000001, SH7780_PCI_VCR2);

	/* Enable all interrupts, so we know what to fix */
	pci_write_reg(0x0000C3FF, SH7780_PCIIMR);
	pci_write_reg(0x0000380F, SH7780_PCIAINTM);

	/* Set up standard PCI config registers */
	ctrl_outw(0xFB00, PCI_REG(SH7780_PCISTATUS));
	ctrl_outw(0x0047, PCI_REG(SH7780_PCICMD));
	ctrl_outb(  0x00, PCI_REG(SH7780_PCIPIF));
	ctrl_outb(  0x00, PCI_REG(SH7780_PCISUB));
	ctrl_outb(  0x06, PCI_REG(SH7780_PCIBCC));
	ctrl_outw(0x1912, PCI_REG(SH7780_PCISVID));
	ctrl_outw(0x0001, PCI_REG(SH7780_PCISID));

	pci_write_reg(0x08000000, SH7780_PCIMBAR0);     /* PCI */
	pci_write_reg(0x08000000, SH7780_PCILAR0);     /* SHwy */
	pci_write_reg(0x07F00001, SH7780_PCILSR);      /* size 128M w/ MBAR */

	pci_write_reg(0x00000000, SH7780_PCIMBAR1);
	pci_write_reg(0x00000000, SH7780_PCILAR1);
	pci_write_reg(0x00000000, SH7780_PCILSR1);

	pci_write_reg(0xAB000801, SH7780_PCIIBAR);

	/*
	 * Set the MBR so PCI address is one-to-one with window,
	 * meaning all calls go straight through... use ifdef to
	 * catch erroneous assumption.
	 */
	pci_write_reg(0xFD000000 , SH7780_PCIMBR0);
	pci_write_reg(0x00FC0000 , SH7780_PCIMBMR0);    /* 16M */

	/* Set IOBR for window containing area specified in pci.h */
	pci_write_reg(PCIBIOS_MIN_IO & ~(SH7780_PCI_IO_SIZE-1), SH7780_PCIIOBR);
	pci_write_reg((SH7780_PCI_IO_SIZE-1) & (7 << 18), SH7780_PCIIOBMR);

	pci_write_reg(0xA5000C01, SH7780_PCICR);

	return 0;
}

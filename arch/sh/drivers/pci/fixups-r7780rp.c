/*
 * arch/sh/drivers/pci/fixups-r7780rp.c
 *
 * Highlander R7780RP-1 PCI fixups
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include "pci-sh7780.h"
#include <asm/io.h>

int pci_fixup_pcic(void)
{
	outl(0x000043ff, PCI_REG(SH7780_PCIIMR));
	outl(0x0000380f, PCI_REG(SH7780_PCIAINTM));

	outl(0xfbb00047, PCI_REG(SH7780_PCICMD));
	outl(0x00000000, PCI_REG(SH7780_PCIIBAR));

	outl(0x00011912, PCI_REG(SH7780_PCISVID));
	outl(0x08000000, PCI_REG(SH7780_PCICSCR0));
	outl(0x0000001b, PCI_REG(SH7780_PCICSAR0));
	outl(0xfd000000, PCI_REG(SH7780_PCICSCR1));
	outl(0x0000000f, PCI_REG(SH7780_PCICSAR1));

	outl(0xfd000000, PCI_REG(SH7780_PCIMBR0));
	outl(0x00fc0000, PCI_REG(SH7780_PCIMBMR0));

	/* Set IOBR for windows containing area specified in pci.h */
	outl((PCIBIOS_MIN_IO & ~(SH7780_PCI_IO_SIZE-1)), PCI_REG(SH7780_PCIIOBR));
	outl(((SH7780_PCI_IO_SIZE-1) & (7<<18)), PCI_REG(SH7780_PCIIOBMR));

	return 0;
}


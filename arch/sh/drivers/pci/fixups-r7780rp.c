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
#include "pci-sh4.h"
#include <asm/io.h>

int pci_fixup_pcic(struct pci_channel *chan)
{
	pci_write_reg(chan, 0x000043ff, SH4_PCIINTM);
	pci_write_reg(chan, 0x0000380f, SH4_PCIAINTM);

	pci_write_reg(chan, 0xfbb00047, SH7780_PCICMD);
	pci_write_reg(chan, 0x00000000, SH7780_PCIIBAR);

	pci_write_reg(chan, 0x00011912, SH7780_PCISVID);
	pci_write_reg(chan, 0x08000000, SH7780_PCICSCR0);
	pci_write_reg(chan, 0x0000001b, SH7780_PCICSAR0);
	pci_write_reg(chan, 0xfd000000, SH7780_PCICSCR1);
	pci_write_reg(chan, 0x0000000f, SH7780_PCICSAR1);

	pci_write_reg(chan, 0xfd000000, SH7780_PCIMBR0);
	pci_write_reg(chan, 0x00fc0000, SH7780_PCIMBMR0);

#ifdef CONFIG_32BIT
	pci_write_reg(chan, 0xc0000000, SH7780_PCIMBR2);
	pci_write_reg(chan, 0x20000000 - SH7780_PCI_IO_SIZE, SH7780_PCIMBMR2);
#endif

	/* Set IOBR for windows containing area specified in pci.h */
	pci_write_reg(chan, (PCIBIOS_MIN_IO & ~(SH7780_PCI_IO_SIZE - 1)),
		      SH7780_PCIIOBR);
	pci_write_reg(chan, ((SH7780_PCI_IO_SIZE-1) & (7<<18)),
		      SH7780_PCIIOBMR);

	return 0;
}

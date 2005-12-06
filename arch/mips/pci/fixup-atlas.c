/*
 * Copyright (C) 2003, 2004  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2005  MIPS Technologies, Inc.  All rights reserved.
 *	Author:	 Maciej W. Rozycki <macro@mips.com>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/mips-boards/atlasint.h>

#define PCIA		ATLASINT_PCIA
#define PCIB		ATLASINT_PCIB
#define PCIC		ATLASINT_PCIC
#define PCID		ATLASINT_PCID
#define INTA		ATLASINT_INTA
#define INTB		ATLASINT_INTB
#define ETH		ATLASINT_ETH
#define INTC		ATLASINT_INTC
#define SCSI		ATLASINT_SCSI
#define INTD		ATLASINT_INTD

static char irq_tab[][5] __initdata = {
	/*      INTA    INTB    INTC    INTD */
	{0,	0,	0,	0,	0 },	/*  0: Unused */
	{0,	0,	0,	0,	0 },	/*  1: Unused */
	{0,	0,	0,	0,	0 },	/*  2: Unused */
	{0,	0,	0,	0,	0 },	/*  3: Unused */
	{0,	0,	0,	0,	0 },	/*  4: Unused */
	{0,	0,	0,	0,	0 },	/*  5: Unused */
	{0,	0,	0,	0,	0 },	/*  6: Unused */
	{0,	0,	0,	0,	0 },	/*  7: Unused */
	{0,	0,	0,	0,	0 },	/*  8: Unused */
	{0,	0,	0,	0,	0 },	/*  9: Unused */
	{0,	0,	0,	0,	0 },	/* 10: Unused */
	{0,	0,	0,	0,	0 },	/* 11: Unused */
	{0,	0,	0,	0,	0 },	/* 12: Unused */
	{0,	0,	0,	0,	0 },	/* 13: Unused */
	{0,	0,	0,	0,	0 },	/* 14: Unused */
	{0,	PCIA,	PCIB,	PCIC,	PCID },	/* 15: cPCI (behind 21150) */
	{0,	SCSI,	0,	0,	0 },	/* 16: SYM53C810A SCSI */
	{0,	0,	0,	0,	0 },	/* 17: Core */
	{0,	INTA,	INTB,	INTC,	INTD },	/* 18: PCI Slot */
	{0,	ETH,	0,	0,	0 },	/* 19: SAA9730 Eth. et al. */
	{0,	0,	0,	0,	0 },	/* 20: Unused */
	{0,	0,	0,	0,	0 }	/* 21: Unused */
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

#ifdef CONFIG_KGDB
/*
 * The PCI scan may have moved the saa9730 I/O address, so reread
 * the address here.
 * This does mean that it's not possible to debug the PCI bus configuration
 * code, but it is better than nothing...
 */

static void atlas_saa9730_base_fixup (struct pci_dev *pdev)
{
	extern void *saa9730_base;
	if (pdev->bus == 0 && PCI_SLOT(pdev->devfn) == 19)
		(void) pci_read_config_dword (pdev, 0x14, (u32 *)&saa9730_base);
	printk ("saa9730_base = %x\n", saa9730_base);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA9730,
	 atlas_saa9730_base_fixup);

#endif

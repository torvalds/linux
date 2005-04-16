/*
 *	arch/mips/pci/fixup-sb1250.c
 *
 *	Copyright (C) 2004  MIPS Technologies, Inc.  All rights reserved.
 *	    Author:	Maciej W. Rozycki <macro@mips.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/pci.h>

/*
 * The BCM1250, etc. PCI/HT bridge reports as a host bridge.
 */
static void __init quirk_sb1250_ht(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIBYTE, PCI_DEVICE_ID_BCM1250_HT,
			quirk_sb1250_ht);

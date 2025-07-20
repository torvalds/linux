// SPDX-License-Identifier: GPL-2.0
/*
 * Exceptions for specific devices,
 *
 * Copyright IBM Corp. 2025
 *
 * Author(s):
 *   Niklas Schnelle <schnelle@linux.ibm.com>
 */
#include <linux/pci.h>

static void zpci_ism_bar_no_mmap(struct pci_dev *pdev)
{
	/*
	 * ISM's BAR is special. Drivers written for ISM know
	 * how to handle this but others need to be aware of their
	 * special nature e.g. to prevent attempts to mmap() it.
	 */
	pdev->non_mappable_bars = 1;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_IBM,
			PCI_DEVICE_ID_IBM_ISM,
			zpci_ism_bar_no_mmap);

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Copyright (C) 2004, 2006  MIPS Technologies, Inc.  All rights reserved.
 *	    Author:	Maciej W. Rozycki <macro@mips.com>
 *	Copyright (C) 2018  Maciej W. Rozycki
 */

#include <linux/dma-mapping.h>
#include <linux/pci.h>

/*
 * Set the BCM1250, etc. PCI host bridge's TRDY timeout
 * to the finite max.
 */
static void quirk_sb1250_pci(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0x40, 0xff);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIBYTE, PCI_DEVICE_ID_BCM1250_PCI,
			quirk_sb1250_pci);

/*
 * The BCM1250, etc. PCI host bridge does not support DAC on its 32-bit
 * bus, so we set the bus's DMA limit accordingly.  However the HT link
 * down the artificial PCI-HT bridge supports 40-bit addressing and the
 * SP1011 HT-PCI bridge downstream supports both DAC and a 64-bit bus
 * width, so we record the PCI-HT bridge's secondary and subordinate bus
 * numbers and do not set the limit for devices present in the inclusive
 * range of those.
 */
struct sb1250_bus_dma_limit_exclude {
	bool set;
	unsigned char start;
	unsigned char end;
};

static int sb1250_bus_dma_limit(struct pci_dev *dev, void *data)
{
	struct sb1250_bus_dma_limit_exclude *exclude = data;
	bool exclude_this;
	bool ht_bridge;

	exclude_this = exclude->set && (dev->bus->number >= exclude->start &&
					dev->bus->number <= exclude->end);
	ht_bridge = !exclude->set && (dev->vendor == PCI_VENDOR_ID_SIBYTE &&
				      dev->device == PCI_DEVICE_ID_BCM1250_HT);

	if (exclude_this) {
		dev_dbg(&dev->dev, "not disabling DAC for device");
	} else if (ht_bridge) {
		exclude->start = dev->subordinate->number;
		exclude->end = pci_bus_max_busnr(dev->subordinate);
		exclude->set = true;
		dev_dbg(&dev->dev, "not disabling DAC for [bus %02x-%02x]",
			exclude->start, exclude->end);
	} else {
		dev_dbg(&dev->dev, "disabling DAC for device");
		dev->dev.bus_dma_limit = DMA_BIT_MASK(32);
	}

	return 0;
}

static void quirk_sb1250_pci_dac(struct pci_dev *dev)
{
	struct sb1250_bus_dma_limit_exclude exclude = { .set = false };

	pci_walk_bus(dev->bus, sb1250_bus_dma_limit, &exclude);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SIBYTE, PCI_DEVICE_ID_BCM1250_PCI,
			quirk_sb1250_pci_dac);

/*
 * The BCM1250, etc. PCI/HT bridge reports as a host bridge.
 */
static void quirk_sb1250_ht(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI_NORMAL;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIBYTE, PCI_DEVICE_ID_BCM1250_HT,
			quirk_sb1250_ht);

/*
 * Set the SP1011 HT/PCI bridge's TRDY timeout to the finite max.
 */
static void quirk_sp1011(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0x64, 0xff);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIPACKETS, PCI_DEVICE_ID_SP1011,
			quirk_sp1011);

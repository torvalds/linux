/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2007 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "pci-common.h"

typeof(pcibios_map_irq) *octeon_pcibios_map_irq;
enum octeon_dma_bar_type octeon_dma_bar_type = OCTEON_DMA_BAR_TYPE_INVALID;

/**
 * Map a PCI device to the appropriate interrupt line
 *
 * @param dev    The Linux PCI device structure for the device to map
 * @param slot   The slot number for this device on __BUS 0__. Linux
 *               enumerates through all the bridges and figures out the
 *               slot on Bus 0 where this device eventually hooks to.
 * @param pin    The PCI interrupt pin read from the device, then swizzled
 *               as it goes through each bridge.
 * @return Interrupt number for the device
 */
int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (octeon_pcibios_map_irq)
		return octeon_pcibios_map_irq(dev, slot, pin);
	else
		panic("octeon_pcibios_map_irq doesn't point to a "
		      "pcibios_map_irq() function");
}


/**
 * Called to perform platform specific PCI setup
 *
 * @param dev
 * @return
 */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	uint16_t config;
	uint32_t dconfig;
	int pos;
	/*
	 * Force the Cache line setting to 64 bytes. The standard
	 * Linux bus scan doesn't seem to set it. Octeon really has
	 * 128 byte lines, but Intel bridges get really upset if you
	 * try and set values above 64 bytes. Value is specified in
	 * 32bit words.
	 */
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 64 / 4);
	/* Set latency timers for all devices */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 48);

	/* Enable reporting System errors and parity errors on all devices */
	/* Enable parity checking and error reporting */
	pci_read_config_word(dev, PCI_COMMAND, &config);
	config |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	pci_write_config_word(dev, PCI_COMMAND, config);

	if (dev->subordinate) {
		/* Set latency timers on sub bridges */
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER, 48);
		/* More bridge error detection */
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &config);
		config |= PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, config);
	}

	/* Enable the PCIe normal error reporting */
	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (pos) {
		/* Update Device Control */
		pci_read_config_word(dev, pos + PCI_EXP_DEVCTL, &config);
		/* Correctable Error Reporting */
		config |= PCI_EXP_DEVCTL_CERE;
		/* Non-Fatal Error Reporting */
		config |= PCI_EXP_DEVCTL_NFERE;
		/* Fatal Error Reporting */
		config |= PCI_EXP_DEVCTL_FERE;
		/* Unsupported Request */
		config |= PCI_EXP_DEVCTL_URRE;
		pci_write_config_word(dev, pos + PCI_EXP_DEVCTL, config);
	}

	/* Find the Advanced Error Reporting capability */
	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		/* Clear Uncorrectable Error Status */
		pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS,
				      &dconfig);
		pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS,
				       dconfig);
		/* Enable reporting of all uncorrectable errors */
		/* Uncorrectable Error Mask - turned on bits disable errors */
		pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, 0);
		/*
		 * Leave severity at HW default. This only controls if
		 * errors are reported as uncorrectable or
		 * correctable, not if the error is reported.
		 */
		/* PCI_ERR_UNCOR_SEVER - Uncorrectable Error Severity */
		/* Clear Correctable Error Status */
		pci_read_config_dword(dev, pos + PCI_ERR_COR_STATUS, &dconfig);
		pci_write_config_dword(dev, pos + PCI_ERR_COR_STATUS, dconfig);
		/* Enable reporting of all correctable errors */
		/* Correctable Error Mask - turned on bits disable errors */
		pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, 0);
		/* Advanced Error Capabilities */
		pci_read_config_dword(dev, pos + PCI_ERR_CAP, &dconfig);
		/* ECRC Generation Enable */
		if (config & PCI_ERR_CAP_ECRC_GENC)
			config |= PCI_ERR_CAP_ECRC_GENE;
		/* ECRC Check Enable */
		if (config & PCI_ERR_CAP_ECRC_CHKC)
			config |= PCI_ERR_CAP_ECRC_CHKE;
		pci_write_config_dword(dev, pos + PCI_ERR_CAP, dconfig);
		/* PCI_ERR_HEADER_LOG - Header Log Register (16 bytes) */
		/* Report all errors to the root complex */
		pci_write_config_dword(dev, pos + PCI_ERR_ROOT_COMMAND,
				       PCI_ERR_ROOT_CMD_COR_EN |
				       PCI_ERR_ROOT_CMD_NONFATAL_EN |
				       PCI_ERR_ROOT_CMD_FATAL_EN);
		/* Clear the Root status register */
		pci_read_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, &dconfig);
		pci_write_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, dconfig);
	}

	return 0;
}

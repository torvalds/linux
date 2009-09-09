/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2009 Cavium Networks
 */

#ifndef __PCI_OCTEON_H__
#define __PCI_OCTEON_H__

#include <linux/pci.h>

/* Some PCI cards require delays when accessing config space. */
#define PCI_CONFIG_SPACE_DELAY 10000

/*
 * pcibios_map_irq() is defined inside pci-octeon.c. All it does is
 * call the Octeon specific version pointed to by this variable. This
 * function needs to change for PCI or PCIe based hosts.
 */
extern int (*octeon_pcibios_map_irq)(const struct pci_dev *dev,
				     u8 slot, u8 pin);

/*
 * The following defines are used when octeon_dma_bar_type =
 * OCTEON_DMA_BAR_TYPE_BIG
 */
#define OCTEON_PCI_BAR1_HOLE_BITS 5
#define OCTEON_PCI_BAR1_HOLE_SIZE (1ul<<(OCTEON_PCI_BAR1_HOLE_BITS+3))

enum octeon_dma_bar_type {
	OCTEON_DMA_BAR_TYPE_INVALID,
	OCTEON_DMA_BAR_TYPE_SMALL,
	OCTEON_DMA_BAR_TYPE_BIG,
	OCTEON_DMA_BAR_TYPE_PCIE
};

/*
 * This tells the DMA mapping system in dma-octeon.c how to map PCI
 * DMA addresses.
 */
extern enum octeon_dma_bar_type octeon_dma_bar_type;

#endif

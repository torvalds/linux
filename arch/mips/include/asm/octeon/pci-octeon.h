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

/*
 * The physical memory base mapped by BAR1.  256MB at the end of the
 * first 4GB.
 */
#define CVMX_PCIE_BAR1_PHYS_BASE ((1ull << 32) - (1ull << 28))
#define CVMX_PCIE_BAR1_PHYS_SIZE (1ull << 28)

/*
 * The RC base of BAR1.	 gen1 has a 39-bit BAR2, gen2 has 41-bit BAR2,
 * place BAR1 so it is the same for both.
 */
#define CVMX_PCIE_BAR1_RC_BASE (1ull << 41)

/*
 * pcibios_map_irq() is defined inside pci-octeon.c. All it does is
 * call the Octeon specific version pointed to by this variable. This
 * function needs to change for PCI or PCIe based hosts.
 */
extern int (*octeon_pcibios_map_irq)(const struct pci_dev *dev,
				     u8 slot, u8 pin);

/*
 * For PCI (not PCIe) the BAR2 base address.
 */
#define OCTEON_BAR2_PCI_ADDRESS 0x8000000000ull

/*
 * For PCI (not PCIe) the base of the memory mapped by BAR1
 */
extern u64 octeon_bar1_pci_phys;

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
	OCTEON_DMA_BAR_TYPE_PCIE,
	OCTEON_DMA_BAR_TYPE_PCIE2
};

/*
 * This tells the DMA mapping system in dma-octeon.c how to map PCI
 * DMA addresses.
 */
extern enum octeon_dma_bar_type octeon_dma_bar_type;

void octeon_pci_dma_init(void);
extern char *octeon_swiotlb;

#endif

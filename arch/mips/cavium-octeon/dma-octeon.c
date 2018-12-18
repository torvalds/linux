/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001  Ralf Baechle <ralf@gnu.org>
 * Copyright (C) 2005 Ilya A. Volynets-Evenbakh <ilya@total-knowledge.com>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 * IP32 changes by Ilya.
 * Copyright (C) 2010 Cavium Networks, Inc.
 */
#include <linux/dma-direct.h>
#include <linux/bootmem.h>
#include <linux/swiotlb.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>

#include <asm/octeon/octeon.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#include <asm/octeon/pci-octeon.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-pci-defs.h>

struct octeon_dma_map_ops {
	dma_addr_t (*phys_to_dma)(struct device *dev, phys_addr_t paddr);
	phys_addr_t (*dma_to_phys)(struct device *dev, dma_addr_t daddr);
};

static dma_addr_t octeon_hole_phys_to_dma(phys_addr_t paddr)
{
	if (paddr >= CVMX_PCIE_BAR1_PHYS_BASE && paddr < (CVMX_PCIE_BAR1_PHYS_BASE + CVMX_PCIE_BAR1_PHYS_SIZE))
		return paddr - CVMX_PCIE_BAR1_PHYS_BASE + CVMX_PCIE_BAR1_RC_BASE;
	else
		return paddr;
}

static phys_addr_t octeon_hole_dma_to_phys(dma_addr_t daddr)
{
	if (daddr >= CVMX_PCIE_BAR1_RC_BASE)
		return daddr + CVMX_PCIE_BAR1_PHYS_BASE - CVMX_PCIE_BAR1_RC_BASE;
	else
		return daddr;
}

static dma_addr_t octeon_gen1_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	if (paddr >= 0x410000000ull && paddr < 0x420000000ull)
		paddr -= 0x400000000ull;
	return octeon_hole_phys_to_dma(paddr);
}

static phys_addr_t octeon_gen1_dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	daddr = octeon_hole_dma_to_phys(daddr);

	if (daddr >= 0x10000000ull && daddr < 0x20000000ull)
		daddr += 0x400000000ull;

	return daddr;
}

static const struct octeon_dma_map_ops octeon_gen1_ops = {
	.phys_to_dma	= octeon_gen1_phys_to_dma,
	.dma_to_phys	= octeon_gen1_dma_to_phys,
};

static dma_addr_t octeon_gen2_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return octeon_hole_phys_to_dma(paddr);
}

static phys_addr_t octeon_gen2_dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return octeon_hole_dma_to_phys(daddr);
}

static const struct octeon_dma_map_ops octeon_gen2_ops = {
	.phys_to_dma	= octeon_gen2_phys_to_dma,
	.dma_to_phys	= octeon_gen2_dma_to_phys,
};

static dma_addr_t octeon_big_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	if (paddr >= 0x410000000ull && paddr < 0x420000000ull)
		paddr -= 0x400000000ull;

	/* Anything in the BAR1 hole or above goes via BAR2 */
	if (paddr >= 0xf0000000ull)
		paddr = OCTEON_BAR2_PCI_ADDRESS + paddr;

	return paddr;
}

static phys_addr_t octeon_big_dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	if (daddr >= OCTEON_BAR2_PCI_ADDRESS)
		daddr -= OCTEON_BAR2_PCI_ADDRESS;

	if (daddr >= 0x10000000ull && daddr < 0x20000000ull)
		daddr += 0x400000000ull;
	return daddr;
}

static const struct octeon_dma_map_ops octeon_big_ops = {
	.phys_to_dma	= octeon_big_phys_to_dma,
	.dma_to_phys	= octeon_big_dma_to_phys,
};

static dma_addr_t octeon_small_phys_to_dma(struct device *dev,
					   phys_addr_t paddr)
{
	if (paddr >= 0x410000000ull && paddr < 0x420000000ull)
		paddr -= 0x400000000ull;

	/* Anything not in the BAR1 range goes via BAR2 */
	if (paddr >= octeon_bar1_pci_phys && paddr < octeon_bar1_pci_phys + 0x8000000ull)
		paddr = paddr - octeon_bar1_pci_phys;
	else
		paddr = OCTEON_BAR2_PCI_ADDRESS + paddr;

	return paddr;
}

static phys_addr_t octeon_small_dma_to_phys(struct device *dev,
					    dma_addr_t daddr)
{
	if (daddr >= OCTEON_BAR2_PCI_ADDRESS)
		daddr -= OCTEON_BAR2_PCI_ADDRESS;
	else
		daddr += octeon_bar1_pci_phys;

	if (daddr >= 0x10000000ull && daddr < 0x20000000ull)
		daddr += 0x400000000ull;
	return daddr;
}

static const struct octeon_dma_map_ops octeon_small_ops = {
	.phys_to_dma	= octeon_small_phys_to_dma,
	.dma_to_phys	= octeon_small_dma_to_phys,
};

static const struct octeon_dma_map_ops *octeon_pci_dma_ops;

void __init octeon_pci_dma_init(void)
{
	switch (octeon_dma_bar_type) {
	case OCTEON_DMA_BAR_TYPE_PCIE:
		octeon_pci_dma_ops = &octeon_gen1_ops;
		break;
	case OCTEON_DMA_BAR_TYPE_PCIE2:
		octeon_pci_dma_ops = &octeon_gen2_ops;
		break;
	case OCTEON_DMA_BAR_TYPE_BIG:
		octeon_pci_dma_ops = &octeon_big_ops;
		break;
	case OCTEON_DMA_BAR_TYPE_SMALL:
		octeon_pci_dma_ops = &octeon_small_ops;
		break;
	default:
		BUG();
	}
}
#endif /* CONFIG_PCI */

dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
#ifdef CONFIG_PCI
	if (dev && dev_is_pci(dev))
		return octeon_pci_dma_ops->phys_to_dma(dev, paddr);
#endif
	return paddr;
}

phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t daddr)
{
#ifdef CONFIG_PCI
	if (dev && dev_is_pci(dev))
		return octeon_pci_dma_ops->dma_to_phys(dev, daddr);
#endif
	return daddr;
}

char *octeon_swiotlb;

void __init plat_swiotlb_setup(void)
{
	int i;
	phys_addr_t max_addr;
	phys_addr_t addr_size;
	size_t swiotlbsize;
	unsigned long swiotlb_nslabs;

	max_addr = 0;
	addr_size = 0;

	for (i = 0 ; i < boot_mem_map.nr_map; i++) {
		struct boot_mem_map_entry *e = &boot_mem_map.map[i];
		if (e->type != BOOT_MEM_RAM && e->type != BOOT_MEM_INIT_RAM)
			continue;

		/* These addresses map low for PCI. */
		if (e->addr > 0x410000000ull && !OCTEON_IS_OCTEON2())
			continue;

		addr_size += e->size;

		if (max_addr < e->addr + e->size)
			max_addr = e->addr + e->size;

	}

	swiotlbsize = PAGE_SIZE;

#ifdef CONFIG_PCI
	/*
	 * For OCTEON_DMA_BAR_TYPE_SMALL, size the iotlb at 1/4 memory
	 * size to a maximum of 64MB
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN31XX)
	    || OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2)) {
		swiotlbsize = addr_size / 4;
		if (swiotlbsize > 64 * (1<<20))
			swiotlbsize = 64 * (1<<20);
	} else if (max_addr > 0xf0000000ul) {
		/*
		 * Otherwise only allocate a big iotlb if there is
		 * memory past the BAR1 hole.
		 */
		swiotlbsize = 64 * (1<<20);
	}
#endif
#ifdef CONFIG_USB_OHCI_HCD_PLATFORM
	/* OCTEON II ohci is only 32-bit. */
	if (OCTEON_IS_OCTEON2() && max_addr >= 0x100000000ul)
		swiotlbsize = 64 * (1<<20);
#endif
	swiotlb_nslabs = swiotlbsize >> IO_TLB_SHIFT;
	swiotlb_nslabs = ALIGN(swiotlb_nslabs, IO_TLB_SEGSIZE);
	swiotlbsize = swiotlb_nslabs << IO_TLB_SHIFT;

	octeon_swiotlb = alloc_bootmem_low_pages(swiotlbsize);

	if (swiotlb_init_with_tbl(octeon_swiotlb, swiotlb_nslabs, 1) == -ENOMEM)
		panic("Cannot allocate SWIOTLB buffer");
}

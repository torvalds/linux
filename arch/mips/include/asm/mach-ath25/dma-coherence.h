/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2007  Felix Fietkau <nbd@openwrt.org>
 *
 */
#ifndef __ASM_MACH_ATH25_DMA_COHERENCE_H
#define __ASM_MACH_ATH25_DMA_COHERENCE_H

#include <linux/device.h>

/*
 * We need some arbitrary non-zero value to be programmed to the BAR1 register
 * of PCI host controller to enable DMA. The same value should be used as the
 * offset to calculate the physical address of DMA buffer for PCI devices.
 */
#define AR2315_PCI_HOST_SDRAM_BASEADDR	0x20000000

static inline dma_addr_t ath25_dev_offset(struct device *dev)
{
#ifdef CONFIG_PCI
	extern struct bus_type pci_bus_type;

	if (dev && dev->bus == &pci_bus_type)
		return AR2315_PCI_HOST_SDRAM_BASEADDR;
#endif
	return 0;
}

static inline dma_addr_t
plat_map_dma_mem(struct device *dev, void *addr, size_t size)
{
	return virt_to_phys(addr) + ath25_dev_offset(dev);
}

static inline dma_addr_t
plat_map_dma_mem_page(struct device *dev, struct page *page)
{
	return page_to_phys(page) + ath25_dev_offset(dev);
}

static inline unsigned long
plat_dma_addr_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr - ath25_dev_offset(dev);
}

static inline void
plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr, size_t size,
		   enum dma_data_direction direction)
{
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline int plat_device_is_coherent(struct device *dev)
{
#ifdef CONFIG_DMA_COHERENT
	return 1;
#endif
#ifdef CONFIG_DMA_NONCOHERENT
	return 0;
#endif
}

static inline void plat_post_dma_flush(struct device *dev)
{
}

#endif /* __ASM_MACH_ATH25_DMA_COHERENCE_H */

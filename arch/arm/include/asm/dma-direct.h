/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_ARM_DMA_DIRECT_H
#define ASM_ARM_DMA_DIRECT_H 1

#include <asm/memory.h>

/*
 * dma_to_pfn/pfn_to_dma/virt_to_dma are architecture private
 * functions used internally by the DMA-mapping API to provide DMA
 * addresses. They must not be used by drivers.
 */
static inline dma_addr_t pfn_to_dma(struct device *dev, unsigned long pfn)
{
	if (dev && dev->dma_range_map)
		pfn = PFN_DOWN(translate_phys_to_dma(dev, PFN_PHYS(pfn)));
	return (dma_addr_t)__pfn_to_bus(pfn);
}

static inline unsigned long dma_to_pfn(struct device *dev, dma_addr_t addr)
{
	unsigned long pfn = __bus_to_pfn(addr);

	if (dev && dev->dma_range_map)
		pfn = PFN_DOWN(translate_dma_to_phys(dev, PFN_PHYS(pfn)));
	return pfn;
}

static inline dma_addr_t virt_to_dma(struct device *dev, void *addr)
{
	if (dev)
		return pfn_to_dma(dev, virt_to_pfn(addr));

	return (dma_addr_t)__virt_to_bus((unsigned long)(addr));
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	unsigned int offset = paddr & ~PAGE_MASK;
	return pfn_to_dma(dev, __phys_to_pfn(paddr)) + offset;
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dev_addr)
{
	unsigned int offset = dev_addr & ~PAGE_MASK;
	return __pfn_to_phys(dma_to_pfn(dev, dev_addr)) + offset;
}

#endif /* ASM_ARM_DMA_DIRECT_H */

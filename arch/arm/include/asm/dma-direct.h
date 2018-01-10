/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_ARM_DMA_DIRECT_H
#define ASM_ARM_DMA_DIRECT_H 1

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

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	u64 limit, mask;

	if (!dev->dma_mask)
		return 0;

	mask = *dev->dma_mask;

	limit = (mask + 1) & ~mask;
	if (limit && size > limit)
		return 0;

	if ((addr | (addr + size - 1)) & ~mask)
		return 0;

	return 1;
}

#endif /* ASM_ARM_DMA_DIRECT_H */

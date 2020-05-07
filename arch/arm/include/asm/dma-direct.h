/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_ARM_DMA_DIRECT_H
#define ASM_ARM_DMA_DIRECT_H 1

static inline dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	unsigned int offset = paddr & ~PAGE_MASK;
	return pfn_to_dma(dev, __phys_to_pfn(paddr)) + offset;
}

static inline phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t dev_addr)
{
	unsigned int offset = dev_addr & ~PAGE_MASK;
	return __pfn_to_phys(dma_to_pfn(dev, dev_addr)) + offset;
}

#endif /* ASM_ARM_DMA_DIRECT_H */

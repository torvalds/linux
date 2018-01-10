/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_X86_DMA_DIRECT_H
#define ASM_X86_DMA_DIRECT_H 1

#include <linux/mem_encrypt.h>

#ifdef CONFIG_X86_DMA_REMAP /* Platform code defines bridge-specific code */
bool dma_capable(struct device *dev, dma_addr_t addr, size_t size);
dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr);
phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr);
#else
static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return __sme_set(paddr);
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return __sme_clr(daddr);
}
#endif /* CONFIG_X86_DMA_REMAP */
#endif /* ASM_X86_DMA_DIRECT_H */

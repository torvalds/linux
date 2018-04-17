/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_X86_DMA_DIRECT_H
#define ASM_X86_DMA_DIRECT_H 1

bool dma_capable(struct device *dev, dma_addr_t addr, size_t size);
dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr);
phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t daddr);

#endif /* ASM_X86_DMA_DIRECT_H */

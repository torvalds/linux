/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MACH_FOOTBRIDGE_DMA_DIRECT_H
#define MACH_FOOTBRIDGE_DMA_DIRECT_H 1

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr);
phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dev_addr);

#endif /* MACH_FOOTBRIDGE_DMA_DIRECT_H */

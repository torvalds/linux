// SPDX-License-Identifier:  GPL-2.0
// (C) 2018 Synopsys, Inc. (www.synopsys.com)

#ifndef ASM_ARC_DMA_MAPPING_H
#define ASM_ARC_DMA_MAPPING_H

#include <asm-generic/dma-mapping.h>

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent);
#define arch_setup_dma_ops arch_setup_dma_ops

#endif

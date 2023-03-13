/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2023 ARM Ltd.
 */
#ifndef __ASM_DMA_MAPPING_NOALIAS_H
#define __ASM_DMA_MAPPING_NOALIAS_H

#ifdef CONFIG_ARM64_ERRATUM_2454944
void arm64_noalias_setup_dma_ops(struct device *dev);
#else
static inline void arm64_noalias_setup_dma_ops(struct device *dev)
{
}
#endif
#endif /* __ASM_DMA_MAPPING_NOALIAS_H */

// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef ASMNDS32_DMA_MAPPING_H
#define ASMNDS32_DMA_MAPPING_H

extern struct dma_map_ops nds32_dma_ops;

static inline struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &nds32_dma_ops;
}

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_METAG_DMA_MAPPING_H
#define _ASM_METAG_DMA_MAPPING_H

extern const struct dma_map_ops metag_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &metag_dma_ops;
}

#endif

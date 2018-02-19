/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CRIS_DMA_MAPPING_H
#define _ASM_CRIS_DMA_MAPPING_H

#ifdef CONFIG_PCI
extern const struct dma_map_ops v32_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &v32_dma_ops;
}
#else
static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	BUG();
	return NULL;
}
#endif

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H

extern const struct dma_map_ops *dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	/* sparc32 uses per-device dma_ops */
	return IS_ENABLED(CONFIG_SPARC64) ? dma_ops : NULL;
}

#endif

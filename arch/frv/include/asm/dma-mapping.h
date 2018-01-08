/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <asm/cache.h>
#include <asm/cacheflush.h>

extern unsigned long __nongprelbss dma_coherent_mem_start;
extern unsigned long __nongprelbss dma_coherent_mem_end;

extern const struct dma_map_ops frv_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &frv_dma_ops;
}

#endif  /* _ASM_DMA_MAPPING_H */

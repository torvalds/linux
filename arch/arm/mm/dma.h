/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DMA_H
#define DMA_H

#include <asm/glue-cache.h>

#ifndef MULTI_CACHE
#define dmac_map_area			__glue(_CACHE,_dma_map_area)
#define dmac_unmap_area 		__glue(_CACHE,_dma_unmap_area)

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
extern void dmac_map_area(const void *, size_t, int);
extern void dmac_unmap_area(const void *, size_t, int);

#else

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
#define dmac_map_area			cpu_cache.dma_map_area
#define dmac_unmap_area 		cpu_cache.dma_unmap_area

#endif

#endif

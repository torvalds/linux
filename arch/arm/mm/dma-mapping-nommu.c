// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Based on linux/arch/arm/mm/dma-mapping.c
 *
 *  Copyright (C) 2000-2004 Russell King
 */

#include <linux/dma-map-ops.h>
#include <asm/cachetype.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <asm/cp15.h>

#include "dma.h"

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	dmac_map_area(__va(paddr), size, dir);

	if (dir == DMA_FROM_DEVICE)
		outer_inv_range(paddr, paddr + size);
	else
		outer_clean_range(paddr, paddr + size);
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	if (dir != DMA_TO_DEVICE) {
		outer_inv_range(paddr, paddr + size);
		dmac_unmap_area(__va(paddr), size, dir);
	}
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			bool coherent)
{
	if (IS_ENABLED(CONFIG_CPU_V7M)) {
		/*
		 * Cache support for v7m is optional, so can be treated as
		 * coherent if no cache has been detected. Note that it is not
		 * enough to check if MPU is in use or not since in absense of
		 * MPU system memory map is used.
		 */
		dev->dma_coherent = cacheid ? coherent : true;
	} else {
		/*
		 * Assume coherent DMA in case MMU/MPU has not been set up.
		 */
		dev->dma_coherent = (get_cr() & CR_M) ? coherent : true;
	}
}

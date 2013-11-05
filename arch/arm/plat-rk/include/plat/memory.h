#ifndef __PLAT_MEMORY_H
#define __PLAT_MEMORY_H

#include <linux/version.h>

/*
 * Physical DRAM offset.
 */
#if defined(CONFIG_ARCH_RK319X)
#define PLAT_PHYS_OFFSET	UL(0x00000000)
#else
#define PLAT_PHYS_OFFSET	UL(0x60000000)
#endif

#define CONSISTENT_DMA_SIZE	SZ_8M

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
#define dmac_clean_range(start, end)	dmac_map_area(start, end - start, DMA_TO_DEVICE)
#define dmac_inv_range(start, end)	dmac_unmap_area(start, end - start, DMA_FROM_DEVICE)
#endif

#endif

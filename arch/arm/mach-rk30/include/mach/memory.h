#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

#include <linux/version.h>

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET	UL(0x60000000)

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	0xFEF00100
#define SRAM_CODE_END		0xFEF02FFF
#define SRAM_DATA_OFFSET	0xFEF03000
#define SRAM_DATA_END		0xFEF03FFF

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
#define dmac_clean_range(start, end)	dmac_map_area(start, end - start, DMA_TO_DEVICE)
#define dmac_inv_range(start, end)	dmac_unmap_area(start, end - start, DMA_FROM_DEVICE)
#endif

#endif

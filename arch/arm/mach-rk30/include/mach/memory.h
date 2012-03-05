#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

#include <linux/version.h>
#include <mach/io.h>

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET	UL(0x60000000)

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	(RK30_IMEM_BASE + 0x0100)
#define SRAM_CODE_END		(RK30_IMEM_BASE + 0x2FFF)
#define SRAM_DATA_OFFSET	(RK30_IMEM_BASE + 0x3000)
#define SRAM_DATA_END		(RK30_IMEM_BASE + 0x3FFF)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
#define dmac_clean_range(start, end)	dmac_map_area(start, end - start, DMA_TO_DEVICE)
#define dmac_inv_range(start, end)	dmac_unmap_area(start, end - start, DMA_FROM_DEVICE)
#endif

#endif

/* arch/arm/mach-rk29/include/mach/memory.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_RK29_MEMORY_H
#define __ASM_ARCH_RK29_MEMORY_H

#include <asm/page.h>
#include <asm/sizes.h>
#include <linux/version.h>

/* physical offset of RAM */
#define PHYS_OFFSET		UL(0x60000000)

#define CONSISTENT_DMA_SIZE	SZ_8M

#if !defined(__ASSEMBLY__) && defined(CONFIG_ZONE_DMA)

/*
 * Restrict DMA-able region to workaround silicon bug. The bug
 * restricts memory available for GPU hardware to be below 512M.
 */
#define ARM_DMA_ZONE_SIZE	SZ_512M

static inline void
__arch_adjust_zones(int node, unsigned long *zone_size, unsigned long *zhole_size)
{
	unsigned long dma_size = ARM_DMA_ZONE_SIZE >> PAGE_SHIFT;

	if (node || (zone_size[0] <= dma_size))
		return;

	zone_size[1] = zone_size[0] - dma_size;
	zone_size[0] = dma_size;
	zhole_size[1] = zhole_size[0];
	zhole_size[0] = 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
#define arch_adjust_zones(zone_size, zhole_size) __arch_adjust_zones(0, zone_size, zhole_size)
#else
#define arch_adjust_zones(node, zone_size, zhole_size) __arch_adjust_zones(node, zone_size, zhole_size)
#endif

#endif /* CONFIG_ZONE_DMA */

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	0xFEF00000
#define SRAM_CODE_END		0xFEF02FFF
#define SRAM_DATA_OFFSET	0xFEF03000
#define SRAM_DATA_END		0xFEF03FFF

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
#define dmac_clean_range(start, end)	dmac_map_area(start, end - start, DMA_TO_DEVICE)
#define dmac_inv_range(start, end)	dmac_unmap_area(start, end - start, DMA_FROM_DEVICE)
#endif

#endif


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

/* physical offset of RAM */
#define PHYS_OFFSET		UL(0x60000000)

#define CONSISTENT_DMA_SIZE	SZ_8M

#if !defined(__ASSEMBLY__) && defined(CONFIG_ZONE_DMA)

static inline void
__arch_adjust_zones(int node, unsigned long *zone_size, unsigned long *zhole_size)
{
	unsigned long dma_size = SZ_512M >> PAGE_SHIFT;

	if (node || (zone_size[0] <= dma_size))
		return;

	zone_size[1] = zone_size[0] - dma_size;
	zone_size[0] = dma_size;
	zhole_size[1] = zhole_size[0];
	zhole_size[0] = 0;
}

#define arch_adjust_zones(node, zone_size, zhole_size) \
	__arch_adjust_zones(node, zone_size, zhole_size)

#endif /* CONFIG_ZONE_DMA */

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	0xFEF00000
#define SRAM_CODE_END		0xFEF02FFF
#define SRAM_DATA_OFFSET	0xFEF03000
#define SRAM_DATA_END		0xFEF03FFF

#endif


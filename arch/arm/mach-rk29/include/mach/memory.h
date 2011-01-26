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

/* physical offset of RAM */
#define PHYS_OFFSET		UL(0x60000000)

/* bus address and physical addresses are identical */
#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#define CONSISTENT_DMA_SIZE	SZ_8M

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	0xff400000
#define SRAM_CODE_END		0xff401fff
#define SRAM_DATA_OFFSET	0xff402000
#define SRAM_DATA_END		0xff403fff

#endif


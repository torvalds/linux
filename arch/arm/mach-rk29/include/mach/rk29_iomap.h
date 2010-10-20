/* arch/arm/mach-rk29/include/mach/rk29_iomap.h
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

#ifndef __ASM_ARCH_RK29_IOMAP_H
#define __ASM_ARCH_RK29_IOMAP_H

#include <asm/sizes.h>

/* defines */
#define		SZ_22K				0x5800
/*IO映射方式定义，以物理地址0x20000000为基地址
 *和以0x10000000为基地址分另映射为：0xf5000000,
 *0xf4000000
 */
#define RK29_ADDR_BASE1 			0xF5000000  
#define RK29_ADDR_BASE0 			0xF4000000

#define RK29_SDRAM_PHYS				0x60000000
#define RK29_AXI0_PHYS				0x1012C000			
#define RK29_AXI1_PHYS				0x10000000	
#define RK29_PERI_PHYS				0x10140000	

#define RK29_BOOTROM_PHYS          	0x10100000
#define RK29_BOOTROM_SIZE          	SZ_16K

#define RK29_DDRC_PHYS          	0x10124000
#define RK29_DDRC_BASE          	(RK29_ADDR_BASE0+0x124000)
#define RK29_DDRC_SIZE          	SZ_16K

#define RK29_GICCPU_PHYS			0x1012C000
#define RK29_GICCPU_BASE			(RK29_ADDR_BASE0+0x12C000)
#define RK29_GICCPU_SIZE			SZ_8K

#define RK29_GICPERI_PHYS			0x1012E000
#define RK29_GICPERI_BASE			(RK29_ADDR_BASE0+0x12E000)
#define RK29_GICPERI_SIZE			SZ_8K

#define RK29_USBHOST_PHYS           0x10140000
#define RK29_USBHOST_SIZE           SZ_256K

#define RK29_USBOTG0_PHYS           0x10180000
#define RK29_USBOTG0_SIZE           SZ_256K

#define RK29_USBOTG1_PHYS           0x101c0000
#define RK29_USBOTG1_SIZE           SZ_256K

#define RK29_NANDC_PHYS				0x1012E000
#define RK29_NANDC_BASE				(RK29_ADDR_BASE0+0x500000)
#define RK29_NANDC_SIZE				SZ_16K

#define RK29_TIMER0_BASE			(RK29_ADDR_BASE1+0x38000)
#define RK29_TIMER0_PHYS			0x20038000
#define RK29_TIMER0_SIZE			SZ_8K

#define RK29_UART1_PHYS          	0x20060000
#define RK29_UART1_BASE          	(RK29_ADDR_BASE1+0x60000)
#define RK29_UART1_SIZE          	SZ_16K

#endif

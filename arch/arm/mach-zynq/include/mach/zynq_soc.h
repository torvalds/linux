/* arch/arm/mach-zynq/include/mach/zynq_soc.h
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_XILINX_SOC_H__
#define __MACH_XILINX_SOC_H__

#include <asm/pgtable.h>

#define PERIPHERAL_CLOCK_RATE		2500000

/* Static peripheral mappings are mapped at the top of the vmalloc region.  The
 * early uart mapping causes intermediate problems/failure at certain
 * addresses, including the very top of the vmalloc region.  Map it at an
 * address that is known to work.
 */
#define UART0_PHYS		0xE0000000
#define UART1_PHYS		0xE0001000
#define UART_SIZE		SZ_4K
#define UART_VIRT		0xF0001000

#define TTC0_PHYS		0xF8001000
#define TTC0_SIZE		SZ_4K
#define TTC0_VIRT		(VMALLOC_END - TTC0_SIZE)

#define SCU_PERIPH_PHYS		0xF8F00000
#define SCU_PERIPH_SIZE		SZ_8K
#define SCU_PERIPH_VIRT		(TTC0_VIRT - SCU_PERIPH_SIZE)

#if IS_ENABLED(CONFIG_DEBUG_ZYNQ_UART1)
# define LL_UART_PADDR		UART1_PHYS
#else
# define LL_UART_PADDR		UART0_PHYS
#endif

#define LL_UART_VADDR		UART_VIRT

/* The following are intended for the devices that are mapped early */

#define TTC0_BASE			IOMEM(TTC0_VIRT)
#define SCU_PERIPH_BASE			IOMEM(SCU_PERIPH_VIRT)

#endif

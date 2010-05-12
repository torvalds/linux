/* arch/arm/mach-rk2818/include/mach/rk281x_iomap.h
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
 *
 *
 */

#ifndef __ASM_ARCH_RK2818_IOMAP_H
#define __ASM_ARCH_RK2818_IOMAP_H

#include <asm/sizes.h>

/* defines */

#define		SZ_22K				0x5800

/* Physical base address and size of peripherals.
 * Ordered by the virtual base addresses they will be mapped at.
 *
 * RK2818_VIC_BASE must be an value that can be loaded via a "mov"
 * instruction, otherwise entry-macro.S will not compile.
 *
 * If you add or remove entries here, you'll want to edit the
 * rk2818_io_desc array in arch/arm/mach-rk2818/io.c to reflect your
 * changes.
 *
 */
//内存物理地址
#ifdef CONFIG_DRAM_BASE
#define RK2818_SDRAM_BASE 		0x60000000//CONFIG_DRAM_BASE
#else
#define RK2818_SDRAM_PHYS		0x60000000
#define RK2818_SDRAM_SIZE 		(0x00100000*64)	
#endif
 
#define RK2818_AHB_PHYS			        0x10000000			//AHB 总线设备基物理地址
#define RK2818_AHB_SIZE				    0x00100000			// size:1M

#define RK2818_APB_BASE          	    0xFF100000
#define RK2818_APB_PHYS			        0x18000000			// APB总线设备基物理地址
#define RK2818_APB_SIZE				    0x00100000			// size:1M

#define RK2818_BOOTROM_PHYS          	0x10000000
#define RK2818_BOOTROM_SIZE          	SZ_8K

#define RK2818_SRAM_PHYS   	         	0x10002000
#define RK2818_SRAM_SIZE             	SZ_8K

#define RK2818_USBOTG_PHYS           	0x10040000
#define RK2818_USBOTG_SIZE           	SZ_256K

#define RK2818_MCDMA_BASE            	0xFF080000
#define RK2818_MCDMA_PHYS            	0x10080000
#define RK2818_MCDMA_SIZE            	SZ_8K

#define RK2818_SHAREMEM_PHYS        	0x10090000
#define RK2818_SHAREMEM_SIZE        	SZ_64K

#define RK2818_DWDMA_BASE            	0xFF0A0000
#define RK2818_DWDMA_PHYS            	0x100A0000
#define RK2818_DWDMA_SIZE            	SZ_8K

#define RK2818_HOSTIF_PHYS           	0x100A2000
#define RK2818_HOSTIF_SIZE           	SZ_8K

#define RK2818_LCDC_PHYS             	0x100A4000
#define RK2818_LCDC_SIZE             	SZ_8K

#define RK2818_VIP_PHYS              	0x100A6000
#define RK2818_VIP_SIZE          	    SZ_8K

#define RK2818_SDMMC1_PHYS          	0x100A8000
#define RK2818_SDMMC1_SIZE          	SZ_8K

#define RK2818_INTC_BASE          	    0xFF0AA000
#define RK2818_INTC_PHYS          	    0x100AA000
#define RK2818_INTC_SIZE          	    SZ_8K

#define RK2818_SDMMC0_PHYS          	0x100AC000
#define RK2818_SDMMC0_SIZE          	SZ_8K

#define RK2818_NANDC_BASE				0xFF0AE000
#define RK2818_NANDC_PHYS          	    0x100AE000
#define RK2818_NANDC_SIZE          	    SZ_8K

#define RK2818_SDRAMC_BASE          	0xFF0B0000
#define RK2818_SDRAMC_PHYS          	0x100B0000
#define RK2818_SDRAMC_SIZE          	SZ_8K

#define RK2818_ARMDARBITER_BASE         0xFF0B4000
#define RK2818_ARMDARBITER_PHYS         0x100B4000
#define RK2818_ARMDARBITER_SIZE         SZ_8K

#define RK2818_VIDEOCOP_PHYS            0x100B8000
#define RK2818_VIDEOCOP_SIZE            SZ_8K

#define RK2818_ESRAM_PHYS               0x100BA000
#define RK2818_ESRAM_SIZE               SZ_8K

#define RK2818_USBHOST_PHYS             0x1010000
#define RK2818_USBHOST_SIZE             SZ_256K

#define RK2818_UART0_BASE          	0xFF100000
#define RK2818_UART0_PHYS          	0x18000000
#define RK2818_UART0_SIZE          	SZ_4K

#define RK2818_UART2_BASE          	0xFF101000
#define RK2818_UART2_PHYS          	0x18001000
#define RK2818_UART2_SIZE          	SZ_4K

#define RK2818_UART1_BASE          	0xFF102000
#define RK2818_UART1_PHYS          	0x18002000
#define RK2818_UART1_SIZE          	SZ_4K

#define RK2818_UART3_BASE          	0xFF103000
#define RK2818_UART3_PHYS          	0x18003000
#define RK2818_UART3_SIZE          	SZ_4K

#define RK2818_TIMER_BASE          	0xFF104000
#define RK2818_TIMER_PHYS          	0x18004000
#define RK2818_TIMER_SIZE          	SZ_8K

#define RK2818_eFUSE_BASE          	0xFF106000
#define RK2818_eFUSE_PHYS          	0x18006000
#define RK2818_eFUSE_SIZE          	SZ_8K

#define RK2818_GPIO0_BASE          	0xFF108000
#define RK2818_GPIO0_PHYS          	0x18008000
#define RK2818_GPIO0_SIZE          	SZ_8K

#define RK2818_GPIO1_BASE          	0xFF109000
#define RK2818_GPIO1_PHYS          	0x18009000
#define RK2818_GPIO1_SIZE          	SZ_8K

#define RK2818_I2S_PHYS          	0x1800A000
#define RK2818_I2S_SIZE          	SZ_8K

#define RK2818_I2C0_PHYS          	0x1800C000
#define RK2818_I2C0_SIZE          	SZ_4K

#define RK2818_I2C1_PHYS          	0x1800D000
#define RK2818_I2C1_SIZE          	SZ_4K

#define RK2818_SPIMASTER_PHYS         0x1800E000
#define RK2818_SPIMASTER_SIZE         SZ_4K

#define RK2818_SPISLAVE_BASE          0xFF10F000
#define RK2818_SPISLAVE_PHYS          0x1800F000
#define RK2818_SPISLAVE_SIZE          SZ_4K

#define RK2818_WDT_BASE          	0xFF110000
#define RK2818_WDT_PHYS          	0x18010000
#define RK2818_WDT_SIZE          	SZ_8K

#define RK2818_PWM_BASE          	0xFF112000
#define RK2818_PWM_PHYS          	0x18012000
#define RK2818_PWM_SIZE          	SZ_8K

#define RK2818_RTC_BASE          	0xFF114000
#define RK2818_RTC_PHYS          	0x18014000
#define RK2818_RTC_SIZE          	SZ_8K

#define RK2818_ADC_BASE          	0xFF116000
#define RK2818_ADC_PHYS          	0x18016000
#define RK2818_ADC_SIZE          	SZ_8K

#define RK2818_SCU_BASE          	0xFF118000
#define RK2818_SCU_PHYS          	0x18018000
#define RK2818_SCU_SIZE          	SZ_4K

#define RK2818_REGFILE_BASE           0xFF119000
#define RK2818_REGFILE_PHYS           0x18019000
#define RK2818_REGFILE_SIZE           SZ_4K

#define RK2818_DSP_PHYS               0x80000000
#define RK2818_DSP_SIZE               0x00600000

#endif

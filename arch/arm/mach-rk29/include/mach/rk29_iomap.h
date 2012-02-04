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

/*
 * RK29 IO memory map:
 *
 * Virt         Phys            Size    What
 * ---------------------------------------------------------------------------
 *              10000000        1M      CPU L1 AXI Interconnect
 * FEA00000     10100000        1200K
 *              10300000        1M      Peri AXI Interconnect
 * FEC00000     10500000        16K     NANDC
 *              11000000        1M      SMC Bank0
 *              12000000        1M      SMC Bank1
 *              15000000        1M      CPU L2 AXI Interconnect
 * FED00000     20000000        640K    APB
 * FEF00000     0               16K     SRAM
 */

#define RK29_ADDR_BASE0             0xFEA00000
#define RK29_ADDR_BASE1             0xFED00000

#define RK29_SDRAM_PHYS             0x60000000U
#define RK29_AXI1_PHYS              0x10000000
#define RK29_AXI0_PHYS              0x1012C000
#define RK29_PERI_PHYS              0x10140000

//CPU system AXI 1
#define RK29_BOOTROM_PHYS           0x10100000
#define RK29_BOOTROM_SIZE           SZ_16K
#define RK29_VCODEC_PHYS            0x10104000
#define RK29_VCODEC_SIZE            SZ_16K
#define RK29_VCODEC_BASE            (RK29_ADDR_BASE0+0x104000)
#define RK29_VIP_PHYS               0x10108000
#define RK29_VIP_SIZE               SZ_16K
#define RK29_VIP_BASE               (RK29_ADDR_BASE0+0x108000)
#define RK29_LCDC_PHYS              0x1010C000
#define RK29_LCDC_SIZE              SZ_16K
#define RK29_LCDC_BASE              (RK29_ADDR_BASE0+0x10C000)
#define RK29_IPP_PHYS               0x10110000
#define RK29_IPP_SIZE               SZ_16K
#define RK29_IPP_BASE               (RK29_ADDR_BASE0+0x110000)
#define RK29_EBC_PHYS               0x10114000
#define RK29_EBC_SIZE               SZ_16K
#define RK29_I2S_8CH_PHYS           0x10118000
#define RK29_I2S_8CH_SIZE           SZ_16K
#define RK29_I2S_2CH_PHYS           0x1011C000
#define RK29_I2S_2CH_SIZE           SZ_8K
#define RK29_I2S0_BASE              (RK29_ADDR_BASE0+0x11C000)
#define RK29_SPDIF_PHYS             0x1011E000
#define RK29_SPDIF_SIZE             SZ_8K
#define RK29_GPU_PHYS               0x10120000
#define RK29_GPU_SIZE               SZ_16K
#define RK29_GPU_BASE               (RK29_ADDR_BASE0+0x120000)
#define RK29_DDRC_PHYS              0x10124000
#define RK29_DDRC_BASE              (RK29_ADDR_BASE0+0x124000)
#define RK29_DDRC_SIZE              SZ_16K

//CPU system AXI 0
#define RK29_GICCPU_PHYS            0x1012C000
#define RK29_GICCPU_BASE            (RK29_ADDR_BASE0+0x12C000)
#define RK29_GICCPU_SIZE            SZ_8K
#define RK29_GICPERI_PHYS           0x1012E000
#define RK29_GICPERI_BASE           (RK29_ADDR_BASE0+0x12E000)
#define RK29_GICPERI_SIZE           SZ_8K
#define RK29_CPU_AXI_BUS0_PHYS      0x15000000

//peri system
#define RK29_USBHOST_PHYS           0x10140000
#define RK29_USBHOST_SIZE           SZ_256K
#define RK29_USBOTG0_PHYS           0x10180000
#define RK29_USBOTG0_SIZE           SZ_256K
#define RK29_USBOTG1_PHYS           0x101c0000
#define RK29_USBOTG1_SIZE           SZ_256K
#define RK29_MAC_PHYS               0x10204000
#define RK29_MAC_SIZE               SZ_16K
#define RK29_HOSTIF_PHYS            0x1020C000
#define RK29_HOSTIF_SIZE            SZ_16K
#define RK29_HSADC_PHYS             0x10210000
#define RK29_HSADC_SIZE             SZ_16K
#define RK29_SDMMC0_PHYS            0x10214000
#define RK29_SDMMC0_SIZE            SZ_16K
#define RK29_SDMMC1_PHYS            0x10218000
#define RK29_SDMMC1_SIZE            SZ_16K
#define RK29_EMMC_PHYS              0x1021C000
#define RK29_EMMC_SIZE              SZ_16K
#define RK29_PIDF_PHYS              0x10220000
#define RK29_EMMC_SIZE              SZ_16K
#define RK29_ARBITER0_PHYS          0x10224000
#define RK29_ARBITER0_SIZE          SZ_16K
#define RK29_ARBITER1_PHYS          0x10228000
#define RK29_ARBITER1_SIZE          SZ_16K
#define RK29_PERI_AXI_BUS0_PHYS     0x10300000

#define RK29_NANDC_PHYS             0x10500000
#define RK29_NANDC_BASE             0xFEC00000
#define RK29_NANDC_SIZE             SZ_16K

//CPU AXI 1 APB
#define RK29_CRU_PHYS               0x20000000
#define RK29_CRU_BASE               RK29_ADDR_BASE1
#define RK29_CRU_SIZE               SZ_4K
#define RK29_PMU_PHYS               0x20004000
#define RK29_PMU_BASE               (RK29_ADDR_BASE1 + 0x4000)
#define RK29_PMU_SIZE               SZ_4K
#define RK29_GRF_BASE               (RK29_ADDR_BASE1+0x8000)
#define RK29_GRF_PHYS               0x20008000
#define RK29_GRF_SIZE               SZ_16K
#define RK29_RTC_PHYS               0x2000C000
#define RK29_RTC_SIZE               SZ_16K
#define RK29_EFUSE_PHYS             0x20010000
#define RK29_EFUSE_SIZE             SZ_16K
#define RK29_TZPC_PHYS              0x20014000
#define RK29_TZPC_SIZE              SZ_16K
#define RK29_SDMAC0_PHYS            0x20018000
#define RK29_SDMAC0_SIZE            SZ_16K
#define RK29_SDMAC0_BASE            (RK29_ADDR_BASE1+0x18000)
#define RK29_DMAC0_PHYS             0x2001C000
#define RK29_DMAC0_SIZE             SZ_16K
#define RK29_DMAC0_BASE             (RK29_ADDR_BASE1+0x1C000)
#define RK29_DEBUG_PHYS             0x20024000
#define RK29_DEBUG_SIZE             SZ_16K
#define RK29_I2C0_PHYS              0x2002C000
#define RK29_I2C0_BASE             (RK29_ADDR_BASE1+0x2C000)
#define RK29_I2C0_SIZE              SZ_16K
#define RK29_UART0_PHYS             0x20030000
#define RK29_UART0_SIZE             SZ_4K
#define RK29_GPIO0_BASE             (RK29_ADDR_BASE1+0x34000)
#define RK29_GPIO0_PHYS             0x20034000
#define RK29_GPIO0_SIZE             SZ_16K
#define RK29_TIMER0_BASE            (RK29_ADDR_BASE1+0x38000)
#define RK29_TIMER0_PHYS            0x20038000
#define RK29_TIMER0_SIZE            SZ_8K
#define RK29_TIMER1_BASE            (RK29_ADDR_BASE1+0x3A000)
#define RK29_TIMER1_PHYS            0x2003A000
#define RK29_TIMER1_SIZE            SZ_8K
#define RK29_GPIO4_BASE             (RK29_ADDR_BASE1+0x3C000)
#define RK29_GPIO4_PHYS             0x2003C000
#define RK29_GPIO4_SIZE             SZ_8K
#define RK29_GPIO6_BASE             (RK29_ADDR_BASE1+0x3E000)
#define RK29_GPIO6_PHYS             0x2003E000
#define RK29_GPIO6_SIZE             SZ_8K

//peri system APB
#define RK29_TIMER2_BASE            (RK29_ADDR_BASE1+0x44000)
#define RK29_TIMER2_PHYS            0x20044000
#define RK29_TIMER2_SIZE            SZ_16K
#define RK29_TIMER3_BASE            (RK29_ADDR_BASE1+0x48000)
#define RK29_TIMER3_PHYS            0x20048000
#define RK29_TIMER3_SIZE            SZ_16K
#define RK29_WDT_PHYS               0x2004C000
#define RK29_WDT_SIZE               SZ_16K
#define RK29_PWM_BASE               (RK29_ADDR_BASE1+0x50000)
#define RK29_PWM_PHYS               0x20050000
#define RK29_PWM_SIZE               SZ_16K
#define RK29_I2C1_PHYS              0x20054000
#define RK29_I2C1_BASE              (RK29_ADDR_BASE1+0x54000)
#define RK29_I2C1_SIZE              SZ_16K
#define RK29_I2C2_PHYS              0x20058000
#define RK29_I2C2_BASE              (RK29_ADDR_BASE1+0x58000)
#define RK29_I2C2_SIZE              SZ_16K
#define RK29_I2C3_PHYS              0x2005C000
#define RK29_I2C3_BASE              (RK29_ADDR_BASE1+0x5c000)
#define RK29_I2C3_SIZE              SZ_16K
#define RK29_UART1_PHYS             0x20060000
#define RK29_UART1_BASE             (RK29_ADDR_BASE1+0x60000)
#define RK29_UART1_SIZE             SZ_4K
#define RK29_UART2_PHYS             0x20064000
#define RK29_UART2_SIZE             SZ_4K
#define RK29_UART3_PHYS             0x20068000
#define RK29_UART3_SIZE             SZ_4K
#define RK29_TIMER2_SIZE            SZ_16K
#define RK29_ADC_PHYS               0x2006C000
#define RK29_ADC_SIZE               SZ_16K
#define RK29_SPI0_PHYS              0x20070000
#define RK29_SPI0_BASE              (RK29_ADDR_BASE1+0x70000)
#define RK29_SPI0_SIZE              SZ_16K
#define RK29_SPI1_PHYS              0x20074000
#define RK29_SPI1_BASE              (RK29_ADDR_BASE1+0x74000)
#define RK29_SPI1_SIZE              SZ_16K
#define RK29_DMAC1_PHYS             0x20078000
#define RK29_DMAC1_SIZE             SZ_16K
#define RK29_DMAC1_BASE             (RK29_ADDR_BASE1+0x78000)
#define RK29_SMC_PHYS               0x2007C000
#define RK29_SMC_SIZE               SZ_16K
#define RK29_GPIO1_BASE             (RK29_ADDR_BASE1+0x80000)
#define RK29_GPIO1_PHYS             0x20080000
#define RK29_GPIO1_SIZE             SZ_16K
#define RK29_GPIO2_BASE             (RK29_ADDR_BASE1+0x84000)
#define RK29_GPIO2_PHYS             0x20084000
#define RK29_GPIO2_SIZE             SZ_16K
#define RK29_GPIO3_BASE             (RK29_ADDR_BASE1+0x88000)
#define RK29_GPIO3_PHYS             0x20088000
#define RK29_GPIO3_SIZE             SZ_16K
#define RK29_GPIO5_BASE             (RK29_ADDR_BASE1+0x8C000)
#define RK29_GPIO5_PHYS             0x2008C000
#define RK29_GPIO5_SIZE             SZ_16K
#endif

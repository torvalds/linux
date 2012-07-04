/*
 * arch/arm/mach-spear13xx/include/mach/spear.h
 *
 * spear13xx Machine family specific definition
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_SPEAR13XX_H
#define __MACH_SPEAR13XX_H

#include <asm/memory.h>

#define PERIP_GRP2_BASE				UL(0xB3000000)
#define VA_PERIP_GRP2_BASE			UL(0xFE000000)
#define MCIF_SDHCI_BASE				UL(0xB3000000)
#define SYSRAM0_BASE				UL(0xB3800000)
#define VA_SYSRAM0_BASE				UL(0xFE800000)
#define SYS_LOCATION				(VA_SYSRAM0_BASE + 0x600)

#define PERIP_GRP1_BASE				UL(0xE0000000)
#define VA_PERIP_GRP1_BASE			UL(0xFD000000)
#define UART_BASE				UL(0xE0000000)
#define VA_UART_BASE				UL(0xFD000000)
#define SSP_BASE				UL(0xE0100000)
#define MISC_BASE				UL(0xE0700000)
#define VA_MISC_BASE				IOMEM(UL(0xFD700000))

#define A9SM_AND_MPMC_BASE			UL(0xEC000000)
#define VA_A9SM_AND_MPMC_BASE			UL(0xFC000000)

/* A9SM peripheral offsets */
#define A9SM_PERIP_BASE				UL(0xEC800000)
#define VA_A9SM_PERIP_BASE			UL(0xFC800000)
#define VA_SCU_BASE				(VA_A9SM_PERIP_BASE + 0x00)

#define L2CC_BASE				UL(0xED000000)
#define VA_L2CC_BASE				IOMEM(UL(0xFB000000))

/* others */
#define DMAC0_BASE				UL(0xEA800000)
#define DMAC1_BASE				UL(0xEB000000)
#define MCIF_CF_BASE				UL(0xB2800000)

/* Devices present in SPEAr1310 */
#ifdef CONFIG_MACH_SPEAR1310
#define SPEAR1310_RAS_GRP1_BASE			UL(0xD8000000)
#define VA_SPEAR1310_RAS_GRP1_BASE		UL(0xFA000000)
#define SPEAR1310_RAS_BASE			UL(0xD8400000)
#define VA_SPEAR1310_RAS_BASE			IOMEM(UL(0xFA400000))
#endif /* CONFIG_MACH_SPEAR1310 */

/* Debug uart for linux, will be used for debug and uncompress messages */
#define SPEAR_DBG_UART_BASE			UART_BASE
#define VA_SPEAR_DBG_UART_BASE			VA_UART_BASE

#endif /* __MACH_SPEAR13XX_H */

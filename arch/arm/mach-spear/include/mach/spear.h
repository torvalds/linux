/*
 * SPEAr3xx/6xx Machine family specific definition
 *
 * Copyright (C) 2009,2012 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_SPEAR_H
#define __MACH_SPEAR_H

#include <asm/memory.h>

#if defined(CONFIG_ARCH_SPEAR3XX) || defined (CONFIG_ARCH_SPEAR6XX)

/* ICM1 - Low speed connection */
#define SPEAR_ICM1_2_BASE		UL(0xD0000000)
#define VA_SPEAR_ICM1_2_BASE		IOMEM(0xFD000000)
#define SPEAR_ICM1_UART_BASE		UL(0xD0000000)
#define VA_SPEAR_ICM1_UART_BASE		(VA_SPEAR_ICM1_2_BASE - SPEAR_ICM1_2_BASE + SPEAR_ICM1_UART_BASE)
#define SPEAR3XX_ICM1_SSP_BASE		UL(0xD0100000)

/* ML-1, 2 - Multi Layer CPU Subsystem */
#define SPEAR_ICM3_ML1_2_BASE		UL(0xF0000000)
#define VA_SPEAR6XX_ML_CPU_BASE		IOMEM(0xF0000000)

/* ICM3 - Basic Subsystem */
#define SPEAR_ICM3_SMI_CTRL_BASE	UL(0xFC000000)
#define VA_SPEAR_ICM3_SMI_CTRL_BASE	IOMEM(0xFC000000)
#define SPEAR_ICM3_DMA_BASE		UL(0xFC400000)
#define SPEAR_ICM3_SYS_CTRL_BASE	UL(0xFCA00000)
#define VA_SPEAR_ICM3_SYS_CTRL_BASE	(VA_SPEAR_ICM3_SMI_CTRL_BASE - SPEAR_ICM3_SMI_CTRL_BASE + SPEAR_ICM3_SYS_CTRL_BASE)
#define SPEAR_ICM3_MISC_REG_BASE	UL(0xFCA80000)
#define VA_SPEAR_ICM3_MISC_REG_BASE	(VA_SPEAR_ICM3_SMI_CTRL_BASE - SPEAR_ICM3_SMI_CTRL_BASE + SPEAR_ICM3_MISC_REG_BASE)

/* Debug uart for linux, will be used for debug and uncompress messages */
#define SPEAR_DBG_UART_BASE		SPEAR_ICM1_UART_BASE

/* Sysctl base for spear platform */
#define SPEAR_SYS_CTRL_BASE		SPEAR_ICM3_SYS_CTRL_BASE
#define VA_SPEAR_SYS_CTRL_BASE		VA_SPEAR_ICM3_SYS_CTRL_BASE
#endif /* SPEAR3xx || SPEAR6XX */

/* SPEAr320 Macros */
#define SPEAR320_SOC_CONFIG_BASE	UL(0xB3000000)
#define VA_SPEAR320_SOC_CONFIG_BASE	IOMEM(0xFE000000)

#ifdef CONFIG_ARCH_SPEAR13XX

#define PERIP_GRP2_BASE				UL(0xB3000000)
#define VA_PERIP_GRP2_BASE			IOMEM(0xF9000000)
#define MCIF_SDHCI_BASE				UL(0xB3000000)
#define SYSRAM0_BASE				UL(0xB3800000)
#define VA_SYSRAM0_BASE				IOMEM(0xF9800000)
#define SYS_LOCATION				(VA_SYSRAM0_BASE + 0x600)

#define PERIP_GRP1_BASE				UL(0xE0000000)
#define VA_PERIP_GRP1_BASE			IOMEM(0xFD000000)
#define UART_BASE				UL(0xE0000000)
#define VA_UART_BASE				IOMEM(0xFD000000)
#define SSP_BASE				UL(0xE0100000)
#define MISC_BASE				UL(0xE0700000)
#define VA_MISC_BASE				IOMEM(0xFD700000)

#define A9SM_AND_MPMC_BASE			UL(0xEC000000)
#define VA_A9SM_AND_MPMC_BASE			IOMEM(0xFC000000)

#define SPEAR1310_RAS_BASE			UL(0xD8400000)
#define VA_SPEAR1310_RAS_BASE			IOMEM(UL(0xFA400000))

/* A9SM peripheral offsets */
#define A9SM_PERIP_BASE				UL(0xEC800000)
#define VA_A9SM_PERIP_BASE			IOMEM(0xFC800000)
#define VA_SCU_BASE				(VA_A9SM_PERIP_BASE + 0x00)

#define L2CC_BASE				UL(0xED000000)
#define VA_L2CC_BASE				IOMEM(UL(0xFB000000))

/* others */
#define MCIF_CF_BASE				UL(0xB2800000)

/* Debug uart for linux, will be used for debug and uncompress messages */
#define SPEAR_DBG_UART_BASE			UART_BASE

#endif /* SPEAR13XX */

#endif /* __MACH_SPEAR_H */

/*
 * arch/arm/mach-spear6xx/include/mach/spear.h
 *
 * SPEAr6xx Machine family specific definition
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_SPEAR6XX_H
#define __MACH_SPEAR6XX_H

#include <asm/memory.h>

/* ICM1 - Low speed connection */
#define SPEAR6XX_ICM1_BASE		UL(0xD0000000)
#define VA_SPEAR6XX_ICM1_BASE		UL(0xFD000000)
#define SPEAR6XX_ICM1_UART0_BASE	UL(0xD0000000)
#define VA_SPEAR6XX_ICM1_UART0_BASE	(VA_SPEAR6XX_ICM1_2_BASE | SPEAR6XX_ICM1_UART0_BASE)

/* ML-1, 2 - Multi Layer CPU Subsystem */
#define SPEAR6XX_ML_CPU_BASE		UL(0xF0000000)
#define VA_SPEAR6XX_ML_CPU_BASE		UL(0xF0000000)
#define SPEAR6XX_CPU_TMR_BASE		UL(0xF0000000)

/* ICM3 - Basic Subsystem */
#define SPEAR6XX_ICM3_SMI_CTRL_BASE	UL(0xFC000000)
#define VA_SPEAR6XX_ICM3_SMI_CTRL_BASE	UL(0xFC000000)
#define SPEAR6XX_ICM3_DMA_BASE		UL(0xFC400000)
#define SPEAR6XX_ICM3_SYS_CTRL_BASE	UL(0xFCA00000)
#define VA_SPEAR6XX_ICM3_SYS_CTRL_BASE	(VA_SPEAR6XX_ICM3_SMI_CTRL_BASE | SPEAR6XX_ICM3_SYS_CTRL_BASE)
#define SPEAR6XX_ICM3_MISC_REG_BASE	UL(0xFCA80000)
#define VA_SPEAR6XX_ICM3_MISC_REG_BASE	(VA_SPEAR6XX_ICM3_SMI_CTRL_BASE | SPEAR6XX_ICM3_MISC_REG_BASE)

/* Debug uart for linux, will be used for debug and uncompress messages */
#define SPEAR_DBG_UART_BASE		SPEAR6XX_ICM1_UART0_BASE
#define VA_SPEAR_DBG_UART_BASE		VA_SPEAR6XX_ICM1_UART0_BASE

/* Sysctl base for spear platform */
#define SPEAR_SYS_CTRL_BASE		SPEAR6XX_ICM3_SYS_CTRL_BASE
#define VA_SPEAR_SYS_CTRL_BASE		VA_SPEAR6XX_ICM3_SYS_CTRL_BASE

#endif /* __MACH_SPEAR6XX_H */

/* linux/arch/arm/mach-s5p6440/include/mach/map.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>

/* Chip ID */
#define S5P6440_PA_CHIPID	(0xE0000000)
#define S5P_PA_CHIPID		S5P6440_PA_CHIPID
#define S5P_VA_CHIPID		S3C_ADDR(0x00700000)

/* SYSCON */
#define S5P6440_PA_SYSCON	(0xE0100000)
#define S5P_PA_SYSCON		S5P6440_PA_SYSCON
#define S5P_VA_SYSCON		S3C_VA_SYS

#define S5P6440_PA_CLK		(S5P6440_PA_SYSCON + 0x0)
#define S5P_PA_CLK		S5P6440_PA_CLK
#define S5P_VA_CLK		(S5P_VA_SYSCON + 0x0)

/* GPIO */
#define S5P6440_PA_GPIO		(0xE0308000)
#define S5P_PA_GPIO		S5P6440_PA_GPIO
#define S5P_VA_GPIO		S3C_ADDR(0x00500000)

/* VIC0 */
#define S5P6440_PA_VIC0		(0xE4000000)
#define S5P_PA_VIC0		S5P6440_PA_VIC0
#define S5P_VA_VIC0		(S3C_VA_IRQ + 0x0)
#define VA_VIC0			S5P_VA_VIC0

/* VIC1 */
#define S5P6440_PA_VIC1		(0xE4100000)
#define S5P_PA_VIC1		S5P6440_PA_VIC1
#define S5P_VA_VIC1		(S3C_VA_IRQ + 0x10000)
#define VA_VIC1			S5P_VA_VIC1

/* Timer */
#define S5P6440_PA_TIMER	(0xEA000000)
#define S5P_PA_TIMER		S5P6440_PA_TIMER
#define S5P_VA_TIMER		S3C_VA_TIMER

/* RTC */
#define S5P6440_PA_RTC		(0xEA100000)
#define S5P_PA_RTC		S5P6440_PA_RTC
#define S5P_VA_RTC		S3C_ADDR(0x00600000)

/* WDT */
#define S5P6440_PA_WDT		(0xEA200000)
#define S5P_PA_WDT		S5P6440_PA_WDT
#define S5p_VA_WDT		S3C_VA_WATCHDOG

/* UART */
#define S5P6440_PA_UART		(0xEC000000)
#define S5P_PA_UART		S5P6440_PA_UART
#define S5P_VA_UART		S3C_VA_UART

#define S5P_PA_UART0		(S5P_PA_UART + 0x0)
#define S5P_PA_UART1		(S5P_PA_UART + 0x400)
#define S5P_PA_UART2		(S5P_PA_UART + 0x800)
#define S5P_PA_UART3		(S5P_PA_UART + 0xC00)
#define S5P_UART_OFFSET		(0x400)

#define S5P_VA_UARTx(x)		(S5P_VA_UART + (S5P_PA_UART & 0xfffff) \
				+ ((x) * S5P_UART_OFFSET))

#define S5P_VA_UART0		S5P_VA_UARTx(0)
#define S5P_VA_UART1		S5P_VA_UARTx(1)
#define S5P_VA_UART2		S5P_VA_UARTx(2)
#define S5P_VA_UART3		S5P_VA_UARTx(3)
#define S5P_SZ_UART		SZ_256

/* I2C */
#define S5P6440_PA_IIC0		(0xEC104000)
#define S5P_PA_IIC0		S5P6440_PA_IIC0
#define S5p_VA_IIC0		S3C_ADDR(0x00700000)

/* SDRAM */
#define S5P6440_PA_SDRAM	(0x20000000)
#define S5P_PA_SDRAM		S5P6440_PA_SDRAM

/* compatibiltiy defines. */
#define S3C_PA_UART		S5P_PA_UART
#define S3C_UART_OFFSET		S5P_UART_OFFSET
#define S3C_PA_TIMER		S5P_PA_TIMER
#define S3C_PA_IIC		S5P_PA_IIC0

#endif /* __ASM_ARCH_MAP_H */

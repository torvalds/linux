/* linux/arch/arm/mach-s5pc100/include/mach/map.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * Based on mach-s3c6400/include/mach/map.h
 *
 * S5PC1XX - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>


/* Chip ID */
#define S5PC100_PA_CHIPID	(0xE0000000)
#define S5PC1XX_PA_CHIPID	S5PC100_PA_CHIPID
#define S5PC1XX_VA_CHIPID	S3C_VA_SYS

/* System */
#define S5PC100_PA_SYS		(0xE0100000)
#define S5PC100_PA_CLK		(S5PC100_PA_SYS + 0x0)
#define S5PC100_PA_PWR		(S5PC100_PA_SYS + 0x8000)
#define S5PC1XX_PA_CLK		S5PC100_PA_CLK
#define S5PC1XX_PA_PWR		S5PC100_PA_PWR
#define S5PC1XX_VA_CLK		(S3C_VA_SYS + 0x10000)
#define S5PC1XX_VA_PWR		(S3C_VA_SYS + 0x20000)

/* Interrupt */
#define S5PC100_PA_VIC		(0xE4000000)
#define S5PC100_VA_VIC		S3C_VA_IRQ
#define S5PC100_PA_VIC_OFFSET	0x100000
#define S5PC100_VA_VIC_OFFSET	0x10000
#define S5PC1XX_PA_VIC(x)	(S5PC100_PA_VIC + ((x) * S5PC100_PA_VIC_OFFSET))
#define S5PC1XX_VA_VIC(x)	(S5PC100_VA_VIC + ((x) * S5PC100_VA_VIC_OFFSET))

/* Timer */
#define S5PC100_PA_TIMER	(0xEA000000)
#define S5PC1XX_PA_TIMER	S5PC100_PA_TIMER
#define S5PC1XX_VA_TIMER	S3C_VA_TIMER

/* UART */
#define S5PC100_PA_UART		(0xEC000000)
#define S5PC1XX_PA_UART		S5PC100_PA_UART
#define S5PC1XX_VA_UART		S3C_VA_UART

/* IIC */
#define S5PC100_PA_IIC		(0xEC100000)

/* ETC */
#define S5PC100_PA_SDRAM	(0x20000000)

/* compatibility defines. */
#define S3C_PA_UART		S5PC100_PA_UART
#define S3C_PA_UART0		(S5PC100_PA_UART + 0x0)
#define S3C_PA_UART1		(S5PC100_PA_UART + 0x400)
#define S3C_PA_UART2		(S5PC100_PA_UART + 0x800)
#define S3C_PA_UART3		(S5PC100_PA_UART + 0xC00)
#define S3C_VA_UART0		(S3C_VA_UART + 0x0)
#define S3C_VA_UART1		(S3C_VA_UART + 0x400)
#define S3C_VA_UART2		(S3C_VA_UART + 0x800)
#define S3C_VA_UART3		(S3C_VA_UART + 0xC00)
#define S3C_UART_OFFSET		0x400
#define S3C_VA_VIC0		(S3C_VA_IRQ + 0x0)
#define S3C_VA_VIC1		(S3C_VA_IRQ + 0x10000)
#define S3C_VA_VIC2		(S3C_VA_IRQ + 0x20000)
#define S3C_PA_IIC		S5PC100_PA_IIC

#endif /* __ASM_ARCH_C100_MAP_H */

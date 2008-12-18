/* linux/arch/arm/mach-s3c6400/include/mach/map.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C64XX - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>

#define S3C_PA_UART		(0x7F005000)
#define S3C_PA_UART0		(S3C_PA_UART + 0x00)
#define S3C_PA_UART1		(S3C_PA_UART + 0x400)
#define S3C_PA_UART2		(S3C_PA_UART + 0x800)
#define S3C_PA_UART3		(S3C_PA_UART + 0xC00)
#define S3C_UART_OFFSET		(0x400)

/* See notes on UART VA mapping in debug-macro.S */
#define S3C_VA_UARTx(x)	(S3C_VA_UART + (S3C_PA_UART & 0xfffff) + ((x) * S3C_UART_OFFSET))

#define S3C_VA_UART0		S3C_VA_UARTx(0)
#define S3C_VA_UART1		S3C_VA_UARTx(1)
#define S3C_VA_UART2		S3C_VA_UARTx(2)
#define S3C_VA_UART3		S3C_VA_UARTx(3)

#define S3C64XX_PA_SYSCON	(0x7E00F000)
#define S3C64XX_PA_TIMER	(0x7F006000)

#define S3C64XX_PA_GPIO		(0x7F008000)
#define S3C64XX_VA_GPIO		S3C_ADDR(0x00500000)
#define S3C64XX_SZ_GPIO		SZ_4K

#define S3C64XX_PA_SDRAM	(0x50000000)
#define S3C64XX_PA_VIC0		(0x71200000)
#define S3C64XX_PA_VIC1		(0x71300000)

/* place VICs close together */
#define S3C_VA_VIC0		(S3C_VA_IRQ + 0x00)
#define S3C_VA_VIC1		(S3C_VA_IRQ + 0x10000)

/* compatibiltiy defines. */
#define S3C_PA_TIMER		S3C64XX_PA_TIMER

#endif /* __ASM_ARCH_6400_MAP_H */

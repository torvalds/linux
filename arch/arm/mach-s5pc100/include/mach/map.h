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

/*
 * map-base.h has already defined virtual memory address
 * S3C_VA_IRQ		S3C_ADDR(0x00000000)	irq controller(s)
 * S3C_VA_SYS		S3C_ADDR(0x00100000)	system control
 * S3C_VA_MEM		S3C_ADDR(0x00200000)	system control (not used)
 * S3C_VA_TIMER		S3C_ADDR(0x00300000)	timer block
 * S3C_VA_WATCHDOG	S3C_ADDR(0x00400000)	watchdog
 * S3C_VA_UART		S3C_ADDR(0x01000000)	UART
 *
 * S5PC100 specific virtual memory address can be defined here
 * S5PC1XX_VA_GPIO	S3C_ADDR(0x00500000)	GPIO
 *
 */

/* Chip ID */
#define S5PC100_PA_CHIPID	(0xE0000000)
#define S5PC1XX_PA_CHIPID	S5PC100_PA_CHIPID
#define S5PC1XX_VA_CHIPID	S3C_VA_SYS

/* System */
#define S5PC100_PA_CLK		(0xE0100000)
#define S5PC100_PA_CLK_OTHER	(0xE0200000)
#define S5PC100_PA_PWR		(0xE0108000)
#define S5PC1XX_PA_CLK		S5PC100_PA_CLK
#define S5PC1XX_PA_PWR		S5PC100_PA_PWR
#define S5PC1XX_PA_CLK_OTHER	S5PC100_PA_CLK_OTHER
#define S5PC1XX_VA_CLK		(S3C_VA_SYS + 0x10000)
#define S5PC1XX_VA_PWR		(S3C_VA_SYS + 0x20000)
#define S5PC1XX_VA_CLK_OTHER	(S3C_VA_SYS + 0x30000)

/* GPIO */
#define S5PC100_PA_GPIO		(0xE0300000)
#define S5PC1XX_PA_GPIO		S5PC100_PA_GPIO
#define S5PC1XX_VA_GPIO		S3C_ADDR(0x00500000)

/* Interrupt */
#define S5PC100_PA_VIC		(0xE4000000)
#define S5PC100_VA_VIC		S3C_VA_IRQ
#define S5PC100_PA_VIC_OFFSET	0x100000
#define S5PC100_VA_VIC_OFFSET	0x10000
#define S5PC1XX_PA_VIC(x)	(S5PC100_PA_VIC + ((x) * S5PC100_PA_VIC_OFFSET))
#define S5PC1XX_VA_VIC(x)	(S5PC100_VA_VIC + ((x) * S5PC100_VA_VIC_OFFSET))

/* DMA */
#define S5PC100_PA_MDMA		(0xE8100000)
#define S5PC100_PA_PDMA0	(0xE9000000)
#define S5PC100_PA_PDMA1	(0xE9200000)

/* Timer */
#define S5PC100_PA_TIMER	(0xEA000000)
#define S5PC1XX_PA_TIMER	S5PC100_PA_TIMER
#define S5PC1XX_VA_TIMER	S3C_VA_TIMER

/* RTC */
#define S5PC100_PA_RTC		(0xEA300000)

/* UART */
#define S5PC100_PA_UART		(0xEC000000)
#define S5PC1XX_PA_UART		S5PC100_PA_UART
#define S5PC1XX_VA_UART		S3C_VA_UART

/* I2C */
#define S5PC100_PA_I2C		(0xEC100000)
#define S5PC100_PA_I2C1		(0xEC200000)

/* USB HS OTG */
#define S5PC100_PA_USB_HSOTG	(0xED200000)
#define S5PC100_PA_USB_HSPHY	(0xED300000)

/* SD/MMC */
#define S5PC100_PA_HSMMC(x)	(0xED800000 + ((x) * 0x100000))
#define S5PC100_PA_HSMMC0	S5PC100_PA_HSMMC(0)
#define S5PC100_PA_HSMMC1	S5PC100_PA_HSMMC(1)
#define S5PC100_PA_HSMMC2	S5PC100_PA_HSMMC(2)

/* LCD */
#define S5PC100_PA_FB		(0xEE000000)

/* Multimedia */
#define S5PC100_PA_G2D		(0xEE800000)
#define S5PC100_PA_JPEG		(0xEE500000)
#define S5PC100_PA_ROTATOR	(0xEE100000)
#define S5PC100_PA_G3D		(0xEF000000)

/* I2S */
#define S5PC100_PA_I2S0		(0xF2000000)
#define S5PC100_PA_I2S1		(0xF2100000)
#define S5PC100_PA_I2S2		(0xF2200000)

/* KEYPAD */
#define S5PC100_PA_KEYPAD	(0xF3100000)

/* ADC & TouchScreen */
#define S5PC100_PA_TSADC	(0xF3000000)

/* ETC */
#define S5PC100_PA_SDRAM	(0x20000000)
#define S5PC1XX_PA_SDRAM	S5PC100_PA_SDRAM

/* compatibility defines. */
#define S3C_PA_RTC		S5PC100_PA_RTC
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
#define S3C_VA_UARTx(x)		(S3C_VA_UART + ((x) * S3C_UART_OFFSET))
#define S3C_PA_FB		S5PC100_PA_FB
#define S3C_PA_G2D		S5PC100_PA_G2D
#define S3C_PA_G3D		S5PC100_PA_G3D
#define S3C_PA_JPEG		S5PC100_PA_JPEG
#define S3C_PA_ROTATOR		S5PC100_PA_ROTATOR
#define S3C_VA_VIC0		(S3C_VA_IRQ + 0x0)
#define S3C_VA_VIC1		(S3C_VA_IRQ + 0x10000)
#define S3C_VA_VIC2		(S3C_VA_IRQ + 0x20000)
#define S3C_PA_IIC		S5PC100_PA_I2C
#define S3C_PA_IIC1		S5PC100_PA_I2C1
#define S3C_PA_USB_HSOTG	S5PC100_PA_USB_HSOTG
#define S3C_PA_USB_HSPHY	S5PC100_PA_USB_HSPHY
#define S3C_PA_HSMMC0		S5PC100_PA_HSMMC0
#define S3C_PA_HSMMC1		S5PC100_PA_HSMMC1
#define S3C_PA_HSMMC2		S5PC100_PA_HSMMC2
#define S3C_PA_KEYPAD		S5PC100_PA_KEYPAD
#define S3C_PA_TSADC		S5PC100_PA_TSADC

#endif /* __ASM_ARCH_C100_MAP_H */

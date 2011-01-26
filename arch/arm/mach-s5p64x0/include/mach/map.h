/* linux/arch/arm/mach-s5p64x0/include/mach/map.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map-s5p.h>

#define S5P64X0_PA_SDRAM	(0x20000000)

#define S5P64X0_PA_CHIPID	(0xE0000000)
#define S5P_PA_CHIPID		S5P64X0_PA_CHIPID

#define S5P64X0_PA_SYSCON	(0xE0100000)
#define S5P_PA_SYSCON		S5P64X0_PA_SYSCON

#define S5P64X0_PA_GPIO		(0xE0308000)

#define S5P64X0_PA_VIC0		(0xE4000000)
#define S5P64X0_PA_VIC1		(0xE4100000)

#define S5P64X0_PA_SROMC	(0xE7000000)
#define S5P_PA_SROMC		S5P64X0_PA_SROMC

#define S5P64X0_PA_PDMA		(0xE9000000)

#define S5P64X0_PA_TIMER	(0xEA000000)
#define S5P_PA_TIMER		S5P64X0_PA_TIMER

#define S5P64X0_PA_RTC		(0xEA100000)

#define S5P64X0_PA_WDT		(0xEA200000)

#define S5P6440_PA_UART(x)	(0xEC000000 + ((x) * S3C_UART_OFFSET))
#define S5P6450_PA_UART(x)	((x < 5) ? (0xEC800000 + ((x) * S3C_UART_OFFSET)) : (0xEC000000))

#define S5P_PA_UART0		S5P6450_PA_UART(0)
#define S5P_PA_UART1		S5P6450_PA_UART(1)
#define S5P_PA_UART2		S5P6450_PA_UART(2)
#define S5P_PA_UART3		S5P6450_PA_UART(3)
#define S5P_PA_UART4		S5P6450_PA_UART(4)
#define S5P_PA_UART5		S5P6450_PA_UART(5)

#define S5P_SZ_UART		SZ_256

#define S5P6440_PA_IIC0		(0xEC104000)
#define S5P6440_PA_IIC1		(0xEC20F000)
#define S5P6450_PA_IIC0		(0xEC100000)
#define S5P6450_PA_IIC1		(0xEC200000)

#define S5P64X0_PA_SPI0		(0xEC400000)
#define S5P64X0_PA_SPI1		(0xEC500000)

#define S5P64X0_PA_HSOTG	(0xED100000)

#define S5P64X0_PA_HSMMC(x)	(0xED800000 + ((x) * 0x100000))

#define S5P64X0_PA_I2S		(0xF2000000)
#define S5P6450_PA_I2S1		0xF2800000
#define S5P6450_PA_I2S2		0xF2900000

#define S5P64X0_PA_PCM		(0xF2100000)

#define S5P64X0_PA_ADC		(0xF3000000)

/* compatibiltiy defines. */

#define S3C_PA_HSMMC0		S5P64X0_PA_HSMMC(0)
#define S3C_PA_HSMMC1		S5P64X0_PA_HSMMC(1)
#define S3C_PA_HSMMC2		S5P64X0_PA_HSMMC(2)
#define S3C_PA_IIC		S5P6440_PA_IIC0
#define S3C_PA_IIC1		S5P6440_PA_IIC1
#define S3C_PA_RTC		S5P64X0_PA_RTC
#define S3C_PA_WDT		S5P64X0_PA_WDT

#define SAMSUNG_PA_ADC		S5P64X0_PA_ADC

#endif /* __ASM_ARCH_MAP_H */

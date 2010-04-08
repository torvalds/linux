/*
 * arch/arm/mach-nuc93x/include/mach/map.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H

#define MAP_OFFSET	(0xfff00000)
#define CLK_OFFSET	(0x10)

#ifndef __ASSEMBLY__
#define NUC93X_ADDR(x)	((void __iomem *)(0xF0000000 + ((x)&(~MAP_OFFSET))))
#else
#define NUC93X_ADDR(x)	(0xF0000000 + ((x)&(~MAP_OFFSET)))
#endif

 /*
  * nuc932 hardware register definition
  */

#define NUC93X_PA_IRQ		(0xFFF83000)
#define NUC93X_PA_GCR		(0xFFF00000)
#define NUC93X_PA_EBI		(0xFFF01000)
#define NUC93X_PA_UART		(0xFFF80000)
#define NUC93X_PA_TIMER		(0xFFF81000)
#define NUC93X_PA_GPIO		(0xFFF84000)
#define NUC93X_PA_GDMA		(0xFFF03000)
#define NUC93X_PA_USBHOST	(0xFFF0d000)
#define NUC93X_PA_I2C		(0xFFF89000)
#define NUC93X_PA_LCD		(0xFFF06000)
#define NUC93X_PA_GE		(0xFFF05000)
#define NUC93X_PA_ADC		(0xFFF85000)
#define NUC93X_PA_RTC		(0xFFF87000)
#define NUC93X_PA_PWM		(0xFFF82000)
#define NUC93X_PA_ACTL		(0xFFF0a000)
#define NUC93X_PA_USBDEV	(0xFFF0C000)
#define NUC93X_PA_JEPEG		(0xFFF0e000)
#define NUC93X_PA_CACHE_T	(0xFFF60000)
#define NUC93X_PA_VRAM		(0xFFF0b000)
#define NUC93X_PA_DMAC		(0xFFF09000)
#define NUC93X_PA_I2SM		(0xFFF08000)
#define NUC93X_PA_CACHE		(0xFFF02000)
#define NUC93X_PA_GPU		(0xFFF04000)
#define NUC93X_PA_VIDEOIN	(0xFFF07000)
#define NUC93X_PA_SPI0		(0xFFF86000)
#define NUC93X_PA_SPI1		(0xFFF88000)

 /*
  * nuc932 virtual address mapping.
  * interrupt controller is the first thing we put in, to make
  * the assembly code for the irq detection easier
  */

#define NUC93X_VA_IRQ		NUC93X_ADDR(0x00000000)
#define NUC93X_SZ_IRQ		SZ_4K

#define NUC93X_VA_GCR		NUC93X_ADDR(NUC93X_PA_IRQ)
#define NUC93X_VA_CLKPWR	(NUC93X_VA_GCR+CLK_OFFSET)
#define NUC93X_SZ_GCR		SZ_4K

/* EBI management */

#define NUC93X_VA_EBI		NUC93X_ADDR(NUC93X_PA_EBI)
#define NUC93X_SZ_EBI		SZ_4K

/* UARTs */

#define NUC93X_VA_UART		NUC93X_ADDR(NUC93X_PA_UART)
#define NUC93X_SZ_UART		SZ_4K

/* Timers */

#define NUC93X_VA_TIMER	NUC93X_ADDR(NUC93X_PA_TIMER)
#define NUC93X_SZ_TIMER	SZ_4K

/* GPIO ports */

#define NUC93X_VA_GPIO		NUC93X_ADDR(NUC93X_PA_GPIO)
#define NUC93X_SZ_GPIO		SZ_4K

/* GDMA control */

#define NUC93X_VA_GDMA		NUC93X_ADDR(NUC93X_PA_GDMA)
#define NUC93X_SZ_GDMA		SZ_4K

/* I2C hardware controller */

#define NUC93X_VA_I2C		NUC93X_ADDR(NUC93X_PA_I2C)
#define NUC93X_SZ_I2C		SZ_4K

/* LCD controller*/

#define NUC93X_VA_LCD		NUC93X_ADDR(NUC93X_PA_LCD)
#define NUC93X_SZ_LCD		SZ_4K

/* 2D controller*/

#define NUC93X_VA_GE		NUC93X_ADDR(NUC93X_PA_GE)
#define NUC93X_SZ_GE		SZ_4K

/* ADC */

#define NUC93X_VA_ADC		NUC93X_ADDR(NUC93X_PA_ADC)
#define NUC93X_SZ_ADC		SZ_4K

/* RTC */

#define NUC93X_VA_RTC		NUC93X_ADDR(NUC93X_PA_RTC)
#define NUC93X_SZ_RTC		SZ_4K

/* Pulse Width Modulation(PWM) Registers */

#define NUC93X_VA_PWM		NUC93X_ADDR(NUC93X_PA_PWM)
#define NUC93X_SZ_PWM		SZ_4K

/* Audio Controller controller */

#define NUC93X_VA_ACTL		NUC93X_ADDR(NUC93X_PA_ACTL)
#define NUC93X_SZ_ACTL		SZ_4K

/* USB Device port */

#define NUC93X_VA_USBDEV	NUC93X_ADDR(NUC93X_PA_USBDEV)
#define NUC93X_SZ_USBDEV	SZ_4K

/* USB host controller*/
#define NUC93X_VA_USBHOST	NUC93X_ADDR(NUC93X_PA_USBHOST)
#define NUC93X_SZ_USBHOST	SZ_4K

#endif /* __ASM_ARCH_MAP_H */

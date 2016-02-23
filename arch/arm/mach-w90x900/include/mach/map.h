/*
 * arch/arm/mach-w90x900/include/mach/map.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/map.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H

#ifndef __ASSEMBLY__
#define W90X900_ADDR(x)		((void __iomem *)(0xF0000000 + (x)))
#else
#define W90X900_ADDR(x)		(0xF0000000 + (x))
#endif

#define AHB_IO_BASE		0xB0000000
#define APB_IO_BASE		0xB8000000
#define CLOCKPW_BASE		(APB_IO_BASE+0x200)
#define AIC_IO_BASE		(APB_IO_BASE+0x2000)
#define TIMER_IO_BASE		(APB_IO_BASE+0x1000)

/*
 * interrupt controller is the first thing we put in, to make
 * the assembly code for the irq detection easier
 */
#define W90X900_VA_IRQ		W90X900_ADDR(0x00000000)
#define W90X900_PA_IRQ		(0xB8002000)
#define W90X900_SZ_IRQ		SZ_4K

#define W90X900_VA_GCR		W90X900_ADDR(0x08002000)
#define W90X900_PA_GCR		(0xB0000000)
#define W90X900_SZ_GCR		SZ_4K

/* Clock and Power management */
#define W90X900_VA_CLKPWR	(W90X900_VA_GCR+0x200)
#define W90X900_PA_CLKPWR	(0xB0000200)
#define W90X900_SZ_CLKPWR	SZ_4K

/* EBI management */
#define W90X900_VA_EBI		W90X900_ADDR(0x00001000)
#define W90X900_PA_EBI		(0xB0001000)
#define W90X900_SZ_EBI		SZ_4K

/* UARTs */
#define W90X900_VA_UART		W90X900_ADDR(0x08000000)
#define W90X900_PA_UART		(0xB8000000)
#define W90X900_SZ_UART		SZ_4K

/* Timers */
#define W90X900_VA_TIMER	W90X900_ADDR(0x08001000)
#define W90X900_PA_TIMER	(0xB8001000)
#define W90X900_SZ_TIMER	SZ_4K

/* GPIO ports */
#define W90X900_VA_GPIO		W90X900_ADDR(0x08003000)
#define W90X900_PA_GPIO		(0xB8003000)
#define W90X900_SZ_GPIO		SZ_4K

/* GDMA control */
#define W90X900_VA_GDMA		W90X900_ADDR(0x00004000)
#define W90X900_PA_GDMA		(0xB0004000)
#define W90X900_SZ_GDMA		SZ_4K

/* USB host controller*/
#define W90X900_VA_USBEHCIHOST	W90X900_ADDR(0x00005000)
#define W90X900_PA_USBEHCIHOST	(0xB0005000)
#define W90X900_SZ_USBEHCIHOST	SZ_4K

#define W90X900_VA_USBOHCIHOST	W90X900_ADDR(0x00007000)
#define W90X900_PA_USBOHCIHOST	(0xB0007000)
#define W90X900_SZ_USBOHCIHOST	SZ_4K

/* I2C hardware controller */
#define W90X900_VA_I2C		W90X900_ADDR(0x08006000)
#define W90X900_PA_I2C		(0xB8006000)
#define W90X900_SZ_I2C		SZ_4K

/* Keypad Interface*/
#define W90X900_VA_KPI		W90X900_ADDR(0x08008000)
#define W90X900_PA_KPI		(0xB8008000)
#define W90X900_SZ_KPI		SZ_4K

/* Smart card host*/
#define W90X900_VA_SC		W90X900_ADDR(0x08005000)
#define W90X900_PA_SC		(0xB8005000)
#define W90X900_SZ_SC		SZ_4K

/* LCD controller*/
#define W90X900_VA_LCD		W90X900_ADDR(0x00008000)
#define W90X900_PA_LCD		(0xB0008000)
#define W90X900_SZ_LCD		SZ_4K

/* 2D controller*/
#define W90X900_VA_GE		W90X900_ADDR(0x0000B000)
#define W90X900_PA_GE		(0xB000B000)
#define W90X900_SZ_GE		SZ_4K

/* ATAPI */
#define W90X900_VA_ATAPI	W90X900_ADDR(0x0000A000)
#define W90X900_PA_ATAPI	(0xB000A000)
#define W90X900_SZ_ATAPI	SZ_4K

/* ADC */
#define W90X900_VA_ADC		W90X900_ADDR(0x0800A000)
#define W90X900_PA_ADC		(0xB800A000)
#define W90X900_SZ_ADC		SZ_4K

/* PS2 Interface*/
#define W90X900_VA_PS2		W90X900_ADDR(0x08009000)
#define W90X900_PA_PS2		(0xB8009000)
#define W90X900_SZ_PS2		SZ_4K

/* RTC */
#define W90X900_VA_RTC		W90X900_ADDR(0x08004000)
#define W90X900_PA_RTC		(0xB8004000)
#define W90X900_SZ_RTC		SZ_4K

/* Pulse Width Modulation(PWM) Registers */
#define W90X900_VA_PWM		W90X900_ADDR(0x08007000)
#define W90X900_PA_PWM		(0xB8007000)
#define W90X900_SZ_PWM		SZ_4K

/* Audio Controller controller */
#define W90X900_VA_ACTL		W90X900_ADDR(0x00009000)
#define W90X900_PA_ACTL		(0xB0009000)
#define W90X900_SZ_ACTL		SZ_4K

/* DMA controller */
#define W90X900_VA_DMA		W90X900_ADDR(0x0000c000)
#define W90X900_PA_DMA		(0xB000c000)
#define W90X900_SZ_DMA		SZ_4K

/* FMI controller */
#define W90X900_VA_FMI		W90X900_ADDR(0x0000d000)
#define W90X900_PA_FMI		(0xB000d000)
#define W90X900_SZ_FMI		SZ_4K

/* USB Device port */
#define W90X900_VA_USBDEV	W90X900_ADDR(0x00006000)
#define W90X900_PA_USBDEV	(0xB0006000)
#define W90X900_SZ_USBDEV	SZ_4K

/* External MAC control*/
#define W90X900_VA_EMC		W90X900_ADDR(0x00003000)
#define W90X900_PA_EMC		(0xB0003000)
#define W90X900_SZ_EMC		SZ_4K

#endif /* __ASM_ARCH_MAP_H */

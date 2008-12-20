/* arch/arm/mach-s3c2410/include/mach/map.h
 *
 * Copyright (c) 2003 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H

#include <plat/map.h>

#define S3C2410_ADDR(x)		S3C_ADDR(x)

/* interrupt controller is the first thing we put in, to make
 * the assembly code for the irq detection easier
 */
#define S3C24XX_VA_IRQ	   S3C_VA_IRQ
#define S3C2410_PA_IRQ	   (0x4A000000)
#define S3C24XX_SZ_IRQ	   SZ_1M

/* memory controller registers */
#define S3C24XX_VA_MEMCTRL S3C_VA_MEM
#define S3C2410_PA_MEMCTRL (0x48000000)
#define S3C24XX_SZ_MEMCTRL SZ_1M

/* USB host controller */
#define S3C2410_PA_USBHOST (0x49000000)
#define S3C24XX_SZ_USBHOST SZ_1M

/* DMA controller */
#define S3C2410_PA_DMA	   (0x4B000000)
#define S3C24XX_SZ_DMA	   SZ_1M

/* Clock and Power management */
#define S3C24XX_VA_CLKPWR  S3C_VA_SYS
#define S3C2410_PA_CLKPWR  (0x4C000000)
#define S3C24XX_SZ_CLKPWR  SZ_1M

/* LCD controller */
#define S3C2410_PA_LCD	   (0x4D000000)
#define S3C24XX_SZ_LCD	   SZ_1M

/* NAND flash controller */
#define S3C2410_PA_NAND	   (0x4E000000)
#define S3C24XX_SZ_NAND	   SZ_1M

/* UARTs */
#define S3C24XX_VA_UART	   S3C_VA_UART
#define S3C2410_PA_UART	   (0x50000000)
#define S3C24XX_SZ_UART	   SZ_1M

/* Timers */
#define S3C24XX_VA_TIMER   S3C_VA_TIMER
#define S3C2410_PA_TIMER   (0x51000000)
#define S3C24XX_SZ_TIMER   SZ_1M

/* USB Device port */
#define S3C2410_PA_USBDEV  (0x52000000)
#define S3C24XX_SZ_USBDEV  SZ_1M

/* Watchdog */
#define S3C24XX_VA_WATCHDOG S3C_VA_WATCHDOG
#define S3C2410_PA_WATCHDOG (0x53000000)
#define S3C24XX_SZ_WATCHDOG SZ_1M

/* IIC hardware controller */
#define S3C2410_PA_IIC	   (0x54000000)
#define S3C24XX_SZ_IIC	   SZ_1M

/* IIS controller */
#define S3C2410_PA_IIS	   (0x55000000)
#define S3C24XX_SZ_IIS	   SZ_1M

/* GPIO ports */

/* the calculation for the VA of this must ensure that
 * it is the same distance apart from the UART in the
 * phsyical address space, as the initial mapping for the IO
 * is done as a 1:1 maping. This puts it (currently) at
 * 0xFA800000, which is not in the way of any current mapping
 * by the base system.
*/

#define S3C2410_PA_GPIO	   (0x56000000)
#define S3C24XX_VA_GPIO	   ((S3C2410_PA_GPIO - S3C24XX_PA_UART) + S3C24XX_VA_UART)
#define S3C24XX_SZ_GPIO	   SZ_1M

/* RTC */
#define S3C2410_PA_RTC	   (0x57000000)
#define S3C24XX_SZ_RTC	   SZ_1M

/* ADC */
#define S3C2410_PA_ADC	   (0x58000000)
#define S3C24XX_SZ_ADC	   SZ_1M

/* SPI */
#define S3C2410_PA_SPI	   (0x59000000)
#define S3C24XX_SZ_SPI	   SZ_1M

/* SDI */
#define S3C2410_PA_SDI	   (0x5A000000)
#define S3C24XX_SZ_SDI	   SZ_1M

/* CAMIF */
#define S3C2440_PA_CAMIF   (0x4F000000)
#define S3C2440_SZ_CAMIF   SZ_1M

/* AC97 */

#define S3C2440_PA_AC97	   (0x5B000000)
#define S3C2440_SZ_AC97	   SZ_1M

/* S3C2443 High-speed SD/MMC */
#define S3C2443_PA_HSMMC   (0x4A800000)
#define S3C2443_SZ_HSMMC   (256)

/* ISA style IO, for each machine to sort out mappings for, if it
 * implements it. We reserve two 16M regions for ISA.
 */

#define S3C24XX_VA_ISA_WORD  S3C2410_ADDR(0x02000000)
#define S3C24XX_VA_ISA_BYTE  S3C2410_ADDR(0x03000000)

/* physical addresses of all the chip-select areas */

#define S3C2410_CS0 (0x00000000)
#define S3C2410_CS1 (0x08000000)
#define S3C2410_CS2 (0x10000000)
#define S3C2410_CS3 (0x18000000)
#define S3C2410_CS4 (0x20000000)
#define S3C2410_CS5 (0x28000000)
#define S3C2410_CS6 (0x30000000)
#define S3C2410_CS7 (0x38000000)

#define S3C2410_SDRAM_PA    (S3C2410_CS6)

/* Use a single interface for common resources between S3C24XX cpus */

#define S3C24XX_PA_IRQ      S3C2410_PA_IRQ
#define S3C24XX_PA_MEMCTRL  S3C2410_PA_MEMCTRL
#define S3C24XX_PA_USBHOST  S3C2410_PA_USBHOST
#define S3C24XX_PA_DMA      S3C2410_PA_DMA
#define S3C24XX_PA_CLKPWR   S3C2410_PA_CLKPWR
#define S3C24XX_PA_LCD      S3C2410_PA_LCD
#define S3C24XX_PA_UART     S3C2410_PA_UART
#define S3C24XX_PA_TIMER    S3C2410_PA_TIMER
#define S3C24XX_PA_USBDEV   S3C2410_PA_USBDEV
#define S3C24XX_PA_WATCHDOG S3C2410_PA_WATCHDOG
#define S3C24XX_PA_IIC      S3C2410_PA_IIC
#define S3C24XX_PA_IIS      S3C2410_PA_IIS
#define S3C24XX_PA_GPIO     S3C2410_PA_GPIO
#define S3C24XX_PA_RTC      S3C2410_PA_RTC
#define S3C24XX_PA_ADC      S3C2410_PA_ADC
#define S3C24XX_PA_SPI      S3C2410_PA_SPI

/* deal with the registers that move under the 2412/2413 */

#if defined(CONFIG_CPU_S3C2412) || defined(CONFIG_CPU_S3C2413)
#ifndef __ASSEMBLY__
extern void __iomem *s3c24xx_va_gpio2;
#endif
#ifdef CONFIG_CPU_S3C2412_ONLY
#define S3C24XX_VA_GPIO2 (S3C24XX_VA_GPIO + 0x10)
#else
#define S3C24XX_VA_GPIO2 s3c24xx_va_gpio2
#endif
#else
#define s3c24xx_va_gpio2 S3C24XX_VA_GPIO
#define S3C24XX_VA_GPIO2 S3C24XX_VA_GPIO
#endif

#endif /* __ASM_ARCH_MAP_H */

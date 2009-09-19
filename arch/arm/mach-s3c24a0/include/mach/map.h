/* linux/arch/arm/mach-s3c24a0/include/mach/map.h
 *
 * Copyright 2003,2007  Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24A0 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_24A0_MAP_H
#define __ASM_ARCH_24A0_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map.h>

#define S3C24A0_PA_IO_BASE	(0x40000000)
#define S3C24A0_PA_CLKPWR	(0x40000000)
#define S3C24A0_PA_IRQ		(0x40200000)
#define S3C24A0_PA_DMA		(0x40400000)
#define S3C24A0_PA_MEMCTRL	(0x40C00000)
#define S3C24A0_PA_NAND		(0x40C00000)
#define S3C24A0_PA_SROM		(0x40C20000)
#define S3C24A0_PA_SDRAM	(0x40C40000)
#define S3C24A0_PA_BUSM		(0x40CE0000)
#define S3C24A0_PA_USBHOST	(0x41000000)
#define S3C24A0_PA_MODEMIF	(0x41180000)
#define S3C24A0_PA_IRDA		(0x41800000)
#define S3C24A0_PA_TIMER	(0x44000000)
#define S3C24A0_PA_WATCHDOG	(0x44100000)
#define S3C24A0_PA_RTC		(0x44200000)
#define S3C24A0_PA_UART		(0x44400000)
#define S3C24A0_PA_UART0	(S3C24A0_PA_UART)
#define S3C24A0_PA_UART1	(S3C24A0_PA_UART + 0x4000)
#define S3C24A0_PA_SPI		(0x44500000)
#define S3C24A0_PA_IIC		(0x44600000)
#define S3C24A0_PA_IIS		(0x44700000)
#define S3C24A0_PA_GPIO		(0x44800000)
#define S3C24A0_PA_KEYIF	(0x44900000)
#define S3C24A0_PA_USBDEV	(0x44A00000)
#define S3C24A0_PA_AC97		(0x45000000)
#define S3C24A0_PA_ADC		(0x45800000)
#define S3C24A0_PA_SDI		(0x46000000)
#define S3C24A0_PA_MS		(0x46100000)
#define S3C24A0_PA_LCD		(0x4A000000)
#define S3C24A0_PA_VPOST	(0x4A100000)

/* physical addresses of all the chip-select areas */

#define S3C24A0_CS0	(0x00000000)
#define S3C24A0_CS1	(0x04000000)
#define S3C24A0_CS2	(0x08000000)
#define S3C24A0_CS3	(0x0C000000)
#define S3C24A0_CS4	(0x10000000)
#define S3C24A0_CS5	(0x40000000)

#define S3C24A0_SDRAM_PA	(S3C24A0_CS4)

/* Use a single interface for common resources between S3C24XX cpus */

#define S3C24XX_PA_IRQ		S3C24A0_PA_IRQ
#define S3C24XX_PA_MEMCTRL	S3C24A0_PA_MEMCTRL
#define S3C24XX_PA_USBHOST	S3C24A0_PA_USBHOST
#define S3C24XX_PA_DMA		S3C24A0_PA_DMA
#define S3C24XX_PA_CLKPWR	S3C24A0_PA_CLKPWR
#define S3C24XX_PA_LCD		S3C24A0_PA_LCD
#define S3C24XX_PA_UART		S3C24A0_PA_UART
#define S3C24XX_PA_TIMER	S3C24A0_PA_TIMER
#define S3C24XX_PA_USBDEV	S3C24A0_PA_USBDEV
#define S3C24XX_PA_WATCHDOG	S3C24A0_PA_WATCHDOG
#define S3C24XX_PA_IIS		S3C24A0_PA_IIS
#define S3C24XX_PA_GPIO		S3C24A0_PA_GPIO
#define S3C24XX_PA_RTC		S3C24A0_PA_RTC
#define S3C24XX_PA_ADC		S3C24A0_PA_ADC
#define S3C24XX_PA_SPI		S3C24A0_PA_SPI
#define S3C24XX_PA_SDI		S3C24A0_PA_SDI
#define S3C24XX_PA_NAND		S3C24A0_PA_NAND

#define S3C_PA_UART		S3C24A0_PA_UART
#define S3C_PA_IIC		S3C24A0_PA_IIC
#define S3C_PA_NAND		S3C24XX_PA_NAND

#endif /* __ASM_ARCH_24A0_MAP_H */

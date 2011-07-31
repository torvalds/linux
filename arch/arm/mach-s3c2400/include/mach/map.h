/* arch/arm/mach-s3c2400/include/mach/map.h
 *
 * Copyright 2003-2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Copyright 2003, Lucas Correia Villa Real
 *
 * S3C2400 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C2400_PA_MEMCTRL	(0x14000000)
#define S3C2400_PA_USBHOST	(0x14200000)
#define S3C2400_PA_IRQ		(0x14400000)
#define S3C2400_PA_DMA		(0x14600000)
#define S3C2400_PA_CLKPWR	(0x14800000)
#define S3C2400_PA_LCD		(0x14A00000)
#define S3C2400_PA_UART		(0x15000000)
#define S3C2400_PA_TIMER	(0x15100000)
#define S3C2400_PA_USBDEV	(0x15200140)
#define S3C2400_PA_WATCHDOG	(0x15300000)
#define S3C2400_PA_IIC		(0x15400000)
#define S3C2400_PA_IIS		(0x15508000)
#define S3C2400_PA_GPIO		(0x15600000)
#define S3C2400_PA_RTC		(0x15700040)
#define S3C2400_PA_ADC		(0x15800000)
#define S3C2400_PA_SPI		(0x15900000)

#define S3C2400_PA_MMC		(0x15A00000)
#define S3C2400_SZ_MMC		SZ_1M

/* physical addresses of all the chip-select areas */

#define S3C2400_CS0	(0x00000000)
#define S3C2400_CS1	(0x02000000)
#define S3C2400_CS2	(0x04000000)
#define S3C2400_CS3	(0x06000000)
#define S3C2400_CS4	(0x08000000)
#define S3C2400_CS5	(0x0A000000)
#define S3C2400_CS6	(0x0C000000)
#define S3C2400_CS7	(0x0E000000)

#define S3C2400_SDRAM_PA    (S3C2400_CS6)

/* Use a single interface for common resources between S3C24XX cpus */

#define S3C24XX_PA_IRQ		S3C2400_PA_IRQ
#define S3C24XX_PA_MEMCTRL	S3C2400_PA_MEMCTRL
#define S3C24XX_PA_USBHOST	S3C2400_PA_USBHOST
#define S3C24XX_PA_DMA		S3C2400_PA_DMA
#define S3C24XX_PA_CLKPWR	S3C2400_PA_CLKPWR
#define S3C24XX_PA_LCD		S3C2400_PA_LCD
#define S3C24XX_PA_UART		S3C2400_PA_UART
#define S3C24XX_PA_TIMER	S3C2400_PA_TIMER
#define S3C24XX_PA_USBDEV	S3C2400_PA_USBDEV
#define S3C24XX_PA_WATCHDOG	S3C2400_PA_WATCHDOG
#define S3C24XX_PA_IIC		S3C2400_PA_IIC
#define S3C24XX_PA_IIS		S3C2400_PA_IIS
#define S3C24XX_PA_GPIO		S3C2400_PA_GPIO
#define S3C24XX_PA_RTC		S3C2400_PA_RTC
#define S3C24XX_PA_ADC		S3C2400_PA_ADC
#define S3C24XX_PA_SPI		S3C2400_PA_SPI

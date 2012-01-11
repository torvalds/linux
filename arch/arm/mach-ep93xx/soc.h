/*
 * arch/arm/mach-ep93xx/soc.h
 *
 * Copyright (C) 2012 Open Kernel Labs <www.ok-labs.com>
 * Copyright (C) 2012 Ryan Mallon <rmallon@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _EP93XX_SOC_H
#define _EP93XX_SOC_H

/*
 * EP93xx Physical Memory Map:
 *
 * The ASDO pin is sampled at system reset to select a synchronous or
 * asynchronous boot configuration.  When ASDO is "1" (i.e. pulled-up)
 * the synchronous boot mode is selected.  When ASDO is "0" (i.e
 * pulled-down) the asynchronous boot mode is selected.
 *
 * In synchronous boot mode nSDCE3 is decoded starting at physical address
 * 0x00000000 and nCS0 is decoded starting at 0xf0000000.  For asynchronous
 * boot mode they are swapped with nCS0 decoded at 0x00000000 ann nSDCE3
 * decoded at 0xf0000000.
 *
 * There is known errata for the EP93xx dealing with External Memory
 * Configurations.  Please refer to "AN273: EP93xx Silicon Rev E Design
 * Guidelines" for more information.  This document can be found at:
 *
 *	http://www.cirrus.com/en/pubs/appNote/AN273REV4.pdf
 */

#define EP93XX_CS0_PHYS_BASE_ASYNC	0x00000000	/* ASDO Pin = 0 */
#define EP93XX_SDCE3_PHYS_BASE_SYNC	0x00000000	/* ASDO Pin = 1 */
#define EP93XX_CS1_PHYS_BASE		0x10000000
#define EP93XX_CS2_PHYS_BASE		0x20000000
#define EP93XX_CS3_PHYS_BASE		0x30000000
#define EP93XX_PCMCIA_PHYS_BASE		0x40000000
#define EP93XX_CS6_PHYS_BASE		0x60000000
#define EP93XX_CS7_PHYS_BASE		0x70000000
#define EP93XX_SDCE0_PHYS_BASE		0xc0000000
#define EP93XX_SDCE1_PHYS_BASE		0xd0000000
#define EP93XX_SDCE2_PHYS_BASE		0xe0000000
#define EP93XX_SDCE3_PHYS_BASE_ASYNC	0xf0000000	/* ASDO Pin = 0 */
#define EP93XX_CS0_PHYS_BASE_SYNC	0xf0000000	/* ASDO Pin = 1 */

/* AHB peripherals */
#define EP93XX_DMA_BASE			EP93XX_AHB_IOMEM(0x00000000)

#define EP93XX_ETHERNET_PHYS_BASE	EP93XX_AHB_PHYS(0x00010000)
#define EP93XX_ETHERNET_BASE		EP93XX_AHB_IOMEM(0x00010000)

#define EP93XX_USB_PHYS_BASE		EP93XX_AHB_PHYS(0x00020000)
#define EP93XX_USB_BASE			EP93XX_AHB_IOMEM(0x00020000)

#define EP93XX_RASTER_PHYS_BASE		EP93XX_AHB_PHYS(0x00030000)
#define EP93XX_RASTER_BASE		EP93XX_AHB_IOMEM(0x00030000)

#define EP93XX_GRAPHICS_ACCEL_BASE	EP93XX_AHB_IOMEM(0x00040000)

#define EP93XX_SDRAM_CONTROLLER_BASE	EP93XX_AHB_IOMEM(0x00060000)

#define EP93XX_PCMCIA_CONTROLLER_BASE	EP93XX_AHB_IOMEM(0x00080000)

#define EP93XX_BOOT_ROM_BASE		EP93XX_AHB_IOMEM(0x00090000)

#define EP93XX_IDE_BASE			EP93XX_AHB_IOMEM(0x000a0000)

#define EP93XX_VIC1_BASE		EP93XX_AHB_IOMEM(0x000b0000)

#define EP93XX_VIC2_BASE		EP93XX_AHB_IOMEM(0x000c0000)

/* APB peripherals */
#define EP93XX_TIMER_BASE		EP93XX_APB_IOMEM(0x00010000)

#define EP93XX_I2S_PHYS_BASE		EP93XX_APB_PHYS(0x00020000)
#define EP93XX_I2S_BASE			EP93XX_APB_IOMEM(0x00020000)

#define EP93XX_SECURITY_BASE		EP93XX_APB_IOMEM(0x00030000)

#define EP93XX_AAC_PHYS_BASE		EP93XX_APB_PHYS(0x00080000)
#define EP93XX_AAC_BASE			EP93XX_APB_IOMEM(0x00080000)

#define EP93XX_SPI_PHYS_BASE		EP93XX_APB_PHYS(0x000a0000)
#define EP93XX_SPI_BASE			EP93XX_APB_IOMEM(0x000a0000)

#define EP93XX_IRDA_BASE		EP93XX_APB_IOMEM(0x000b0000)

#define EP93XX_KEY_MATRIX_PHYS_BASE	EP93XX_APB_PHYS(0x000f0000)
#define EP93XX_KEY_MATRIX_BASE		EP93XX_APB_IOMEM(0x000f0000)

#define EP93XX_ADC_BASE			EP93XX_APB_IOMEM(0x00100000)
#define EP93XX_TOUCHSCREEN_BASE		EP93XX_APB_IOMEM(0x00100000)

#define EP93XX_PWM_PHYS_BASE		EP93XX_APB_PHYS(0x00110000)
#define EP93XX_PWM_BASE			EP93XX_APB_IOMEM(0x00110000)

#define EP93XX_RTC_PHYS_BASE		EP93XX_APB_PHYS(0x00120000)
#define EP93XX_RTC_BASE			EP93XX_APB_IOMEM(0x00120000)

#define EP93XX_WATCHDOG_PHYS_BASE	EP93XX_APB_PHYS(0x00140000)
#define EP93XX_WATCHDOG_BASE		EP93XX_APB_IOMEM(0x00140000)

/* EP93xx System Controller software locked register write */
void ep93xx_syscon_swlocked_write(unsigned int val, void __iomem *reg);
void ep93xx_devcfg_set_clear(unsigned int set_bits, unsigned int clear_bits);

static inline void ep93xx_devcfg_set_bits(unsigned int bits)
{
	ep93xx_devcfg_set_clear(bits, 0x00);
}

static inline void ep93xx_devcfg_clear_bits(unsigned int bits)
{
	ep93xx_devcfg_set_clear(0x00, bits);
}

#endif /* _EP93XX_SOC_H */

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

#endif /* _EP93XX_SOC_H */

/*
 * Bubinga board definitions
 *
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 *	SAW (IBM)
 *	2003 (c) MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __BUBINGA_H__
#define __BUBINGA_H__

#include <linux/config.h>
#include <platforms/4xx/ibm405ep.h>
#include <asm/ppcboot.h>

/* Memory map for the Bubinga board.
 * Generic 4xx plus RTC.
 */

#define BUBINGA_RTC_PADDR	((uint)0xf0000000)
#define BUBINGA_RTC_VADDR	BUBINGA_RTC_PADDR
#define BUBINGA_RTC_SIZE	((uint)8*1024)

/* The UART clock is based off an internal clock -
 * define BASE_BAUD based on the internal clock and divider(s).
 * Since BASE_BAUD must be a constant, we will initialize it
 * using clock/divider values which OpenBIOS initializes
 * for typical configurations at various CPU speeds.
 * The base baud is calculated as (FWDA / EXT UART DIV / 16)
 */
#define BASE_BAUD		0

/* Flash */
#define PPC40x_FPGA_BASE	0xF0300000
#define PPC40x_FPGA_REG_OFFS	1	/* offset to flash map reg */
#define PPC40x_FLASH_ONBD_N(x)	(x & 0x02)
#define PPC40x_FLASH_SRAM_SEL(x) (x & 0x01)
#define PPC40x_FLASH_LOW	0xFFF00000
#define PPC40x_FLASH_HIGH	0xFFF80000
#define PPC40x_FLASH_SIZE	0x80000

#define PPC4xx_MACHINE_NAME	"IBM Bubinga"

#endif /* __BUBINGA_H__ */
#endif /* __KERNEL__ */

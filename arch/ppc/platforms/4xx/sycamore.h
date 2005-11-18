/*
 * arch/ppc/platforms/4xx/sycamore.h
 *
 * Sycamore board definitions
 *
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 * 	Armin Kuster <akuster@mvista.com>
 *	2000 (c) MontaVista, Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_SYCAMORE_H__
#define __ASM_SYCAMORE_H__

#include <linux/config.h>
#include <platforms/4xx/ibm405gpr.h>
#include <asm/ppcboot.h>

/* Memory map for the IBM "Sycamore" 405GPr evaluation board.
 * Generic 4xx plus RTC.
 */

#define SYCAMORE_RTC_PADDR	((uint)0xf0000000)
#define SYCAMORE_RTC_VADDR	SYCAMORE_RTC_PADDR
#define SYCAMORE_RTC_SIZE	((uint)8*1024)

#define BASE_BAUD		691200

#define SYCAMORE_PS2_BASE	0xF0100000

/* Flash */
#define PPC40x_FPGA_BASE	0xF0300000
#define PPC40x_FPGA_REG_OFFS	5	/* offset to flash map reg */
#define PPC40x_FLASH_ONBD_N(x)	(x & 0x02)
#define PPC40x_FLASH_SRAM_SEL(x) (x & 0x01)
#define PPC40x_FLASH_LOW	0xFFF00000
#define PPC40x_FLASH_HIGH	0xFFF80000
#define PPC40x_FLASH_SIZE	0x80000

#define PPC4xx_MACHINE_NAME	"IBM Sycamore"

#endif /* __ASM_SYCAMORE_H__ */
#endif /* __KERNEL__ */

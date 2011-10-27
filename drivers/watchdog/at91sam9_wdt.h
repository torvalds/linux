/*
 * drivers/watchdog/at91sam9_wdt.h
 *
 * Copyright (C) 2007 Andrew Victor
 * Copyright (C) 2007 Atmel Corporation.
 *
 * Watchdog Timer (WDT) - System peripherals regsters.
 * Based on AT91SAM9261 datasheet revision D.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_WDT_H
#define AT91_WDT_H

#define AT91_WDT_CR		(AT91_WDT + 0x00)	/* Watchdog Control Register */
#define		AT91_WDT_WDRSTT		(1    << 0)		/* Restart */
#define		AT91_WDT_KEY		(0xa5 << 24)		/* KEY Password */

#define AT91_WDT_MR		(AT91_WDT + 0x04)	/* Watchdog Mode Register */
#define		AT91_WDT_WDV		(0xfff << 0)		/* Counter Value */
#define		AT91_WDT_WDFIEN		(1     << 12)		/* Fault Interrupt Enable */
#define		AT91_WDT_WDRSTEN	(1     << 13)		/* Reset Processor */
#define		AT91_WDT_WDRPROC	(1     << 14)		/* Timer Restart */
#define		AT91_WDT_WDDIS		(1     << 15)		/* Watchdog Disable */
#define		AT91_WDT_WDD		(0xfff << 16)		/* Delta Value */
#define		AT91_WDT_WDDBGHLT	(1     << 28)		/* Debug Halt */
#define		AT91_WDT_WDIDLEHLT	(1     << 29)		/* Idle Halt */

#define AT91_WDT_SR		(AT91_WDT + 0x08)	/* Watchdog Status Register */
#define		AT91_WDT_WDUNF		(1 << 0)		/* Watchdog Underflow */
#define		AT91_WDT_WDERR		(1 << 1)		/* Watchdog Error */

#endif

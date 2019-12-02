/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * drivers/watchdog/at91sam9_wdt.h
 *
 * Copyright (C) 2007 Andrew Victor
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries
 *
 * Watchdog Timer (WDT) - System peripherals regsters.
 * Based on AT91SAM9261 datasheet revision D.
 * Based on SAM9X60 datasheet.
 *
 */

#ifndef AT91_WDT_H
#define AT91_WDT_H

#include <linux/bits.h>

#define AT91_WDT_CR		0x00			/* Watchdog Control Register */
#define  AT91_WDT_WDRSTT	BIT(0)			/* Restart */
#define  AT91_WDT_KEY		(0xa5UL << 24)		/* KEY Password */

#define AT91_WDT_MR		0x04			/* Watchdog Mode Register */
#define  AT91_WDT_WDV		(0xfffUL << 0)		/* Counter Value */
#define  AT91_WDT_SET_WDV(x)	((x) & AT91_WDT_WDV)
#define  AT91_WDT_WDFIEN	BIT(12)		/* Fault Interrupt Enable */
#define  AT91_WDT_WDRSTEN	BIT(13)		/* Reset Processor */
#define  AT91_WDT_WDRPROC	BIT(14)		/* Timer Restart */
#define  AT91_WDT_WDDIS		BIT(15)		/* Watchdog Disable */
#define  AT91_WDT_WDD		(0xfffUL << 16)		/* Delta Value */
#define  AT91_WDT_SET_WDD(x)	(((x) << 16) & AT91_WDT_WDD)
#define  AT91_WDT_WDDBGHLT	BIT(28)		/* Debug Halt */
#define  AT91_WDT_WDIDLEHLT	BIT(29)		/* Idle Halt */

#define AT91_WDT_SR		0x08		/* Watchdog Status Register */
#define  AT91_WDT_WDUNF		BIT(0)		/* Watchdog Underflow */
#define  AT91_WDT_WDERR		BIT(1)		/* Watchdog Error */

#endif

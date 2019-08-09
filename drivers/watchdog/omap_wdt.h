/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  linux/drivers/char/watchdog/omap_wdt.h
 *
 *  BRIEF MODULE DESCRIPTION
 *      OMAP Watchdog timer register definitions
 *
 *  Copyright (C) 2004 Texas Instruments.
 */

#ifndef _OMAP_WATCHDOG_H
#define _OMAP_WATCHDOG_H

#define OMAP_WATCHDOG_REV		(0x00)
#define OMAP_WATCHDOG_SYS_CONFIG	(0x10)
#define OMAP_WATCHDOG_STATUS		(0x14)
#define OMAP_WATCHDOG_CNTRL		(0x24)
#define OMAP_WATCHDOG_CRR		(0x28)
#define OMAP_WATCHDOG_LDR		(0x2c)
#define OMAP_WATCHDOG_TGR		(0x30)
#define OMAP_WATCHDOG_WPS		(0x34)
#define OMAP_WATCHDOG_SPR		(0x48)

/* Using the prescaler, the OMAP watchdog could go for many
 * months before firing.  These limits work without scaling,
 * with the 60 second default assumed by most tools and docs.
 */
#define TIMER_MARGIN_MAX	(24 * 60 * 60)	/* 1 day */
#define TIMER_MARGIN_DEFAULT	60	/* 60 secs */
#define TIMER_MARGIN_MIN	1

#define PTV			0	/* prescale */
#define GET_WLDR_VAL(secs)	(0xffffffff - ((secs) * (32768/(1<<PTV))) + 1)
#define GET_WCCR_SECS(val)	((0xffffffff - (val) + 1) / (32768/(1<<PTV)))

#endif				/* _OMAP_WATCHDOG_H */

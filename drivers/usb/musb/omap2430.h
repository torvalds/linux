/*
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 */

#ifndef __MUSB_OMAP243X_H__
#define __MUSB_OMAP243X_H__

#if defined(CONFIG_ARCH_OMAP2430) || defined(CONFIG_ARCH_OMAP3430)
#include <mach/hardware.h>
#include <mach/usb.h>

/*
 * OMAP2430-specific definitions
 */

#define MENTOR_BASE_OFFSET	0
#if	defined(CONFIG_ARCH_OMAP2430)
#define	OMAP_HSOTG_BASE		(OMAP243X_HS_BASE)
#elif	defined(CONFIG_ARCH_OMAP3430)
#define	OMAP_HSOTG_BASE		(OMAP34XX_HSUSB_OTG_BASE)
#endif
#define OMAP_HSOTG(offset)	(OMAP_HSOTG_BASE + 0x400 + (offset))
#define OTG_REVISION		OMAP_HSOTG(0x0)
#define OTG_SYSCONFIG		OMAP_HSOTG(0x4)
#	define	MIDLEMODE	12	/* bit position */
#	define	FORCESTDBY		(0 << MIDLEMODE)
#	define	NOSTDBY			(1 << MIDLEMODE)
#	define	SMARTSTDBY		(2 << MIDLEMODE)
#	define	SIDLEMODE		3	/* bit position */
#	define	FORCEIDLE		(0 << SIDLEMODE)
#	define	NOIDLE			(1 << SIDLEMODE)
#	define	SMARTIDLE		(2 << SIDLEMODE)
#	define	ENABLEWAKEUP		(1 << 2)
#	define	SOFTRST			(1 << 1)
#	define	AUTOIDLE		(1 << 0)
#define OTG_SYSSTATUS		OMAP_HSOTG(0x8)
#	define	RESETDONE		(1 << 0)
#define OTG_INTERFSEL		OMAP_HSOTG(0xc)
#	define	EXTCP			(1 << 2)
#	define	PHYSEL		0	/* bit position */
#	define	UTMI_8BIT		(0 << PHYSEL)
#	define	ULPI_12PIN		(1 << PHYSEL)
#	define	ULPI_8PIN		(2 << PHYSEL)
#define OTG_SIMENABLE		OMAP_HSOTG(0x10)
#	define	TM1			(1 << 0)
#define OTG_FORCESTDBY		OMAP_HSOTG(0x14)
#	define	ENABLEFORCE		(1 << 0)

#endif	/* CONFIG_ARCH_OMAP2430 */

#endif	/* __MUSB_OMAP243X_H__ */

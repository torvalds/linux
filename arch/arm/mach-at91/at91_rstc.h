/*
 * arch/arm/mach-at91/include/mach/at91_rstc.h
 *
 * Copyright (C) 2007 Andrew Victor
 * Copyright (C) 2007 Atmel Corporation.
 *
 * Reset Controller (RSTC) - System peripherals regsters.
 * Based on AT91SAM9261 datasheet revision D.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_RSTC_H
#define AT91_RSTC_H

#ifndef __ASSEMBLY__
extern void __iomem *at91_rstc_base;

#define at91_rstc_read(field) \
	__raw_readl(at91_rstc_base + field)

#define at91_rstc_write(field, value) \
	__raw_writel(value, at91_rstc_base + field);
#else
.extern at91_rstc_base
#endif

#define AT91_RSTC_CR		0x00			/* Reset Controller Control Register */
#define		AT91_RSTC_PROCRST	(1 << 0)		/* Processor Reset */
#define		AT91_RSTC_PERRST	(1 << 2)		/* Peripheral Reset */
#define		AT91_RSTC_EXTRST	(1 << 3)		/* External Reset */
#define		AT91_RSTC_KEY		(0xa5 << 24)		/* KEY Password */

#define AT91_RSTC_SR		0x04			/* Reset Controller Status Register */
#define		AT91_RSTC_URSTS		(1 << 0)		/* User Reset Status */
#define		AT91_RSTC_RSTTYP	(7 << 8)		/* Reset Type */
#define			AT91_RSTC_RSTTYP_GENERAL	(0 << 8)
#define			AT91_RSTC_RSTTYP_WAKEUP		(1 << 8)
#define			AT91_RSTC_RSTTYP_WATCHDOG	(2 << 8)
#define			AT91_RSTC_RSTTYP_SOFTWARE	(3 << 8)
#define			AT91_RSTC_RSTTYP_USER	(4 << 8)
#define		AT91_RSTC_NRSTL		(1 << 16)		/* NRST Pin Level */
#define		AT91_RSTC_SRCMP		(1 << 17)		/* Software Reset Command in Progress */

#define AT91_RSTC_MR		0x08			/* Reset Controller Mode Register */
#define		AT91_RSTC_URSTEN	(1 << 0)		/* User Reset Enable */
#define		AT91_RSTC_URSTIEN	(1 << 4)		/* User Reset Interrupt Enable */
#define		AT91_RSTC_ERSTL		(0xf << 8)		/* External Reset Length */

#endif

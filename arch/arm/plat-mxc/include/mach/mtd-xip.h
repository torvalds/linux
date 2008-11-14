/*
 * MTD primitives for XIP support. Architecture specific functions
 *
 * Do not include this file directly. It's included from linux/mtd/xip.h
 *
 * Copyright (C) 2008 Darius Augulis <augulis.darius@gmail.com>, Teltonika, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <mach/mxc_timer.h>

#ifndef __ARCH_IMX_MTD_XIP_H__
#define __ARCH_IMX_MTD_XIP_H__

#ifdef CONFIG_ARCH_MX1
/* AITC registers */
#define AITC_BASE	IO_ADDRESS(AVIC_BASE_ADDR)
#define NIPNDH		(AITC_BASE + 0x58)
#define NIPNDL		(AITC_BASE + 0x5C)
#define INTENABLEH	(AITC_BASE + 0x10)
#define INTENABLEL	(AITC_BASE + 0x14)
/* MTD macros */
#define xip_irqpending() ((__raw_readl(INTENABLEH) &  __raw_readl(NIPNDH)) \
			|| (__raw_readl(INTENABLEL) &  __raw_readl(NIPNDL)))
#define xip_currtime()		(__raw_readl(TIMER_BASE + MXC_TCN))
#define xip_elapsed_since(x)	(signed)((__raw_readl(TIMER_BASE + MXC_TCN) - (x)) / 96)
#define xip_cpu_idle()		asm volatile ("mcr p15, 0, %0, c7, c0, 4" :: "r" (0))
#endif /* CONFIG_ARCH_MX1 */

#endif /* __ARCH_IMX_MTD_XIP_H__ */

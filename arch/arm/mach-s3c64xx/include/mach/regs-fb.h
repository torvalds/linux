/*
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * Copyright 2009 Samsung Electronics Co.
 *
 * Pawel Osciak <p.osciak@samsung.com>
 * Based on plat-s3c/include/plat/regs-fb.h by Ben Dooks <ben@simtec.co.uk>
 *
 * Framebuffer register definitions for Samsung S3C64xx.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MACH_REGS_FB_H
#define __ASM_ARCH_MACH_REGS_FB_H __FILE__

#include <plat/regs-fb-v4.h>

/* Palette registers */
#define WIN2_PAL(_entry)			(0x300 + ((_entry) * 2))
#define WIN3_PAL(_entry)			(0x320 + ((_entry) * 2))
#define WIN4_PAL(_entry)			(0x340 + ((_entry) * 2))
#define WIN0_PAL(_entry)			(0x400 + ((_entry) * 4))
#define WIN1_PAL(_entry)			(0x800 + ((_entry) * 4))

static inline unsigned int s3c_fb_pal_reg(unsigned int window, int reg)
{
	switch (window) {
	case 0: return WIN0_PAL(reg);
	case 1: return WIN1_PAL(reg);
	case 2: return WIN2_PAL(reg);
	case 3: return WIN3_PAL(reg);
	case 4: return WIN4_PAL(reg);
	}

	BUG();
}

#endif /* __ASM_ARCH_MACH_REGS_FB_H */

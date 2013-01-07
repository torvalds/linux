/*
 * Copyright (C) 2012 Stephen Warren
 *
 * Derived from code:
 * Copyright (C) 2010 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_BCM2835_BCM2835_SOC_H__
#define __MACH_BCM2835_BCM2835_SOC_H__

#include <asm/sizes.h>

#define BCM2835_PERIPH_PHYS	0x20000000
#define BCM2835_PERIPH_VIRT	0xf0000000
#define BCM2835_PERIPH_SIZE	SZ_16M
#define BCM2835_DEBUG_PHYS	0x20201000
#define BCM2835_DEBUG_VIRT	0xf0201000

#endif

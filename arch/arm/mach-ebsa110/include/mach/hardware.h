/*
 *  arch/arm/mach-ebsa110/include/mach/hardware.h
 *
 *  Copyright (C) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains the hardware definitions of the EBSA-110.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#define ISAMEM_BASE		0xe0000000
#define ISAIO_BASE		0xf0000000

/*
 * RAM definitions
 */
#define UNCACHEABLE_ADDR	0xff000000	/* IRQ_STAT */

#endif


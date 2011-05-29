/*
 * arch/arm/mach-ns9xxx/include/mach/memory.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
*/
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/* x in [0..3] */
#define NS9XXX_CSxSTAT_PHYS(x)	UL(((x) + 4) << 28)

#define NS9XXX_CS0STAT_LENGTH	UL(0x1000)
#define NS9XXX_CS1STAT_LENGTH	UL(0x1000)
#define NS9XXX_CS2STAT_LENGTH	UL(0x1000)
#define NS9XXX_CS3STAT_LENGTH	UL(0x1000)

#define PLAT_PHYS_OFFSET	UL(0x00000000)

#endif

/*
 * AT91 Power Management
 *
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __ARCH_ARM_MACH_AT91_PM
#define __ARCH_ARM_MACH_AT91_PM

#include <asm/proc-fns.h>

#include <mach/at91_ramc.h>

#define AT91_MEMCTRL_MC		0
#define AT91_MEMCTRL_SDRAMC	1
#define AT91_MEMCTRL_DDRSDR	2

#define	AT91_PM_MEMTYPE_MASK	0x0f

#define	AT91_PM_MODE_OFFSET	4
#define	AT91_PM_MODE_MASK	0x01
#define	AT91_PM_MODE(x)		(((x) & AT91_PM_MODE_MASK) << AT91_PM_MODE_OFFSET)

#define	AT91_PM_SLOW_CLOCK	0x01

#endif

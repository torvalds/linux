/*
 * OMAP2+ MPU WD_TIMER-specific function prototypes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_WD_TIMER_H
#define __ARCH_ARM_MACH_OMAP2_WD_TIMER_H

#include <plat/omap_hwmod.h>

extern int omap2_wd_timer_disable(struct omap_hwmod *oh);
extern int omap2_wd_timer_reset(struct omap_hwmod *oh);

#endif

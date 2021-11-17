/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OMAP2+ MPU WD_TIMER-specific function prototypes
 */

#ifndef __ARCH_ARM_MACH_OMAP2_WD_TIMER_H
#define __ARCH_ARM_MACH_OMAP2_WD_TIMER_H

#include "omap_hwmod.h"

extern int omap2_wd_timer_disable(struct omap_hwmod *oh);
extern int omap2_wd_timer_reset(struct omap_hwmod *oh);

#endif

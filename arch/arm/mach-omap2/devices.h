/*
 * arch/arm/mach-omap2/devices.h
 *
 * OMAP2 platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ARCH_ARM_MACH_OMAP_DEVICES_H
#define __ARCH_ARM_MACH_OMAP_DEVICES_H

struct isp_platform_data;

int omap3_init_camera(struct isp_platform_data *pdata);

#endif

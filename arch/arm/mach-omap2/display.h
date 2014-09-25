/*
 * display.h - OMAP2+ integration-specific DSS header
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_DISPLAY_H
#define __ARCH_ARM_MACH_OMAP2_DISPLAY_H

#include <linux/kernel.h>

struct omap_dss_dispc_dev_attr {
	u8	manager_count;
	bool	has_framedonetv_irq;
};

int omap_init_drm(void);
int omap_init_vrfb(void);
int omap_init_fb(void);
int omap_init_vout(void);

struct device_node * __init omapdss_find_dss_of_node(void);

#endif

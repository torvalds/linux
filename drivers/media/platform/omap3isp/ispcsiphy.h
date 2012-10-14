/*
 * ispcsiphy.h
 *
 * TI OMAP3 ISP - CSI PHY module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP3_ISP_CSI_PHY_H
#define OMAP3_ISP_CSI_PHY_H

#include <media/omap3isp.h>

struct isp_csi2_device;
struct regulator;

struct isp_csiphy {
	struct isp_device *isp;
	struct mutex mutex;	/* serialize csiphy configuration */
	u8 phy_in_use;
	struct isp_csi2_device *csi2;
	struct regulator *vdd;

	/* mem resources - enums as defined in enum isp_mem_resources */
	unsigned int cfg_regs;
	unsigned int phy_regs;

	u8 num_data_lanes;	/* number of CSI2 Data Lanes supported */
};

int omap3isp_csiphy_acquire(struct isp_csiphy *phy);
void omap3isp_csiphy_release(struct isp_csiphy *phy);
int omap3isp_csiphy_init(struct isp_device *isp);

#endif	/* OMAP3_ISP_CSI_PHY_H */

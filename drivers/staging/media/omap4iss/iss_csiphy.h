/*
 * TI OMAP4 ISS V4L2 Driver - CSI PHY module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef OMAP4_ISS_CSI_PHY_H
#define OMAP4_ISS_CSI_PHY_H

#include <media/omap4iss.h>

struct iss_csi2_device;

struct iss_csiphy_dphy_cfg {
	u8 ths_term;
	u8 ths_settle;
	u8 tclk_term;
	unsigned tclk_miss:1;
	u8 tclk_settle;
};

struct iss_csiphy {
	struct iss_device *iss;
	struct mutex mutex;	/* serialize csiphy configuration */
	u8 phy_in_use;
	struct iss_csi2_device *csi2;

	/* memory resources, as defined in enum iss_mem_resources */
	unsigned int cfg_regs;
	unsigned int phy_regs;

	u8 max_data_lanes;	/* number of CSI2 Data Lanes supported */
	u8 used_data_lanes;	/* number of CSI2 Data Lanes used */
	struct iss_csiphy_lanes_cfg lanes;
	struct iss_csiphy_dphy_cfg dphy;
};

int omap4iss_csiphy_config(struct iss_device *iss,
			   struct v4l2_subdev *csi2_subdev);
int omap4iss_csiphy_acquire(struct iss_csiphy *phy);
void omap4iss_csiphy_release(struct iss_csiphy *phy);
int omap4iss_csiphy_init(struct iss_device *iss);

#endif	/* OMAP4_ISS_CSI_PHY_H */

/*
 * camss-csiphy.h
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef QC_MSM_CAMSS_CSIPHY_H
#define QC_MSM_CAMSS_CSIPHY_H

#include <linux/clk.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define MSM_CSIPHY_PAD_SINK 0
#define MSM_CSIPHY_PAD_SRC 1
#define MSM_CSIPHY_PADS_NUM 2

struct csiphy_lane {
	u8 pos;
	u8 pol;
};

struct csiphy_lanes_cfg {
	int num_data;
	struct csiphy_lane *data;
	struct csiphy_lane clk;
};

struct csiphy_csi2_cfg {
	struct csiphy_lanes_cfg lane_cfg;
};

struct csiphy_config {
	u8 combo_mode;
	u8 csid_id;
	struct csiphy_csi2_cfg *csi2;
};

struct csiphy_device {
	u8 id;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_CSIPHY_PADS_NUM];
	void __iomem *base;
	void __iomem *base_clk_mux;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	int nclocks;
	u32 timer_clk_rate;
	struct csiphy_config cfg;
	struct v4l2_mbus_framefmt fmt[MSM_CSIPHY_PADS_NUM];
};

struct resources;

int msm_csiphy_subdev_init(struct csiphy_device *csiphy,
			   const struct resources *res, u8 id);

int msm_csiphy_register_entity(struct csiphy_device *csiphy,
			       struct v4l2_device *v4l2_dev);

void msm_csiphy_unregister_entity(struct csiphy_device *csiphy);

#endif /* QC_MSM_CAMSS_CSIPHY_H */

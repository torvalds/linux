/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-csiphy.h
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_CSIPHY_H
#define QC_MSM_CAMSS_CSIPHY_H

#include <linux/clk.h>
#include <linux/interrupt.h>
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

struct csiphy_device;

struct csiphy_hw_ops {
	void (*hw_version_read)(struct csiphy_device *csiphy,
				struct device *dev);
	void (*reset)(struct csiphy_device *csiphy);
	void (*lanes_enable)(struct csiphy_device *csiphy,
			     struct csiphy_config *cfg,
			     s64 link_freq, u8 lane_mask);
	void (*lanes_disable)(struct csiphy_device *csiphy,
			      struct csiphy_config *cfg);
	irqreturn_t (*isr)(int irq, void *dev);
};

struct csiphy_device {
	struct camss *camss;
	u8 id;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_CSIPHY_PADS_NUM];
	void __iomem *base;
	void __iomem *base_clk_mux;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	bool *rate_set;
	int nclocks;
	u32 timer_clk_rate;
	struct csiphy_config cfg;
	struct v4l2_mbus_framefmt fmt[MSM_CSIPHY_PADS_NUM];
	const struct csiphy_hw_ops *ops;
	const struct csiphy_format *formats;
	unsigned int nformats;
};

struct resources;

int msm_csiphy_subdev_init(struct camss *camss,
			   struct csiphy_device *csiphy,
			   const struct resources *res, u8 id);

int msm_csiphy_register_entity(struct csiphy_device *csiphy,
			       struct v4l2_device *v4l2_dev);

void msm_csiphy_unregister_entity(struct csiphy_device *csiphy);

extern const struct csiphy_hw_ops csiphy_ops_2ph_1_0;
extern const struct csiphy_hw_ops csiphy_ops_3ph_1_0;

#endif /* QC_MSM_CAMSS_CSIPHY_H */

/*
 * camss-ispif.h
 *
 * Qualcomm MSM Camera Subsystem - ISPIF (ISP Interface) Module
 *
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2017 Linaro Ltd.
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
#ifndef QC_MSM_CAMSS_ISPIF_H
#define QC_MSM_CAMSS_ISPIF_H

#include <linux/clk.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

/* Number of ISPIF lines - same as number of CSID hardware modules */
#define MSM_ISPIF_LINE_NUM 2

#define MSM_ISPIF_PAD_SINK 0
#define MSM_ISPIF_PAD_SRC 1
#define MSM_ISPIF_PADS_NUM 2

#define MSM_ISPIF_VFE_NUM 1

enum ispif_intf {
	PIX0,
	RDI0,
	PIX1,
	RDI1,
	RDI2
};

struct ispif_intf_cmd_reg {
	u32 cmd_0;
	u32 cmd_1;
};

struct ispif_line {
	u8 id;
	u8 csid_id;
	u8 vfe_id;
	enum ispif_intf interface;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_ISPIF_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[MSM_ISPIF_PADS_NUM];
};

struct ispif_device {
	void __iomem *base;
	void __iomem *base_clk_mux;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	int nclocks;
	struct camss_clock  *clock_for_reset;
	int nclocks_for_reset;
	struct completion reset_complete;
	int power_count;
	struct mutex power_lock;
	struct ispif_intf_cmd_reg intf_cmd[MSM_ISPIF_VFE_NUM];
	struct mutex config_lock;
	struct ispif_line line[MSM_ISPIF_LINE_NUM];
};

struct resources_ispif;

int msm_ispif_subdev_init(struct ispif_device *ispif,
			  const struct resources_ispif *res);

int msm_ispif_register_entities(struct ispif_device *ispif,
				struct v4l2_device *v4l2_dev);

void msm_ispif_unregister_entities(struct ispif_device *ispif);

#endif /* QC_MSM_CAMSS_ISPIF_H */

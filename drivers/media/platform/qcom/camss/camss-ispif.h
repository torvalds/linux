/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-ispif.h
 *
 * Qualcomm MSM Camera Subsystem - ISPIF (ISP Interface) Module
 *
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_ISPIF_H
#define QC_MSM_CAMSS_ISPIF_H

#include <linux/clk.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define MSM_ISPIF_PAD_SINK 0
#define MSM_ISPIF_PAD_SRC 1
#define MSM_ISPIF_PADS_NUM 2

#define MSM_ISPIF_VFE_NUM 2

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
	struct ispif_device *ispif;
	u8 id;
	u8 csid_id;
	u8 vfe_id;
	enum ispif_intf interface;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_ISPIF_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[MSM_ISPIF_PADS_NUM];
	const u32 *formats;
	unsigned int nformats;
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
	struct completion reset_complete[MSM_ISPIF_VFE_NUM];
	int power_count;
	struct mutex power_lock;
	struct ispif_intf_cmd_reg intf_cmd[MSM_ISPIF_VFE_NUM];
	struct mutex config_lock;
	unsigned int line_num;
	struct ispif_line *line;
};

struct resources_ispif;

int msm_ispif_subdev_init(struct ispif_device *ispif,
			  const struct resources_ispif *res);

int msm_ispif_register_entities(struct ispif_device *ispif,
				struct v4l2_device *v4l2_dev);

void msm_ispif_unregister_entities(struct ispif_device *ispif);

#endif /* QC_MSM_CAMSS_ISPIF_H */

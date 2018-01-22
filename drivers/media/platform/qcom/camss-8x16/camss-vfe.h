/*
 * camss-vfe.h
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module
 *
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
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
#ifndef QC_MSM_CAMSS_VFE_H
#define QC_MSM_CAMSS_VFE_H

#include <linux/clk.h>
#include <linux/spinlock_types.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "camss-video.h"

#define MSM_VFE_PAD_SINK 0
#define MSM_VFE_PAD_SRC 1
#define MSM_VFE_PADS_NUM 2

#define MSM_VFE_LINE_NUM 4
#define MSM_VFE_IMAGE_MASTERS_NUM 7
#define MSM_VFE_COMPOSITE_IRQ_NUM 4

#define MSM_VFE_VFE0_UB_SIZE 1023
#define MSM_VFE_VFE0_UB_SIZE_RDI (MSM_VFE_VFE0_UB_SIZE / 3)
#define MSM_VFE_VFE1_UB_SIZE 1535
#define MSM_VFE_VFE1_UB_SIZE_RDI (MSM_VFE_VFE1_UB_SIZE / 3)

enum vfe_output_state {
	VFE_OUTPUT_OFF,
	VFE_OUTPUT_RESERVED,
	VFE_OUTPUT_SINGLE,
	VFE_OUTPUT_CONTINUOUS,
	VFE_OUTPUT_IDLE,
	VFE_OUTPUT_STOPPING
};

enum vfe_line_id {
	VFE_LINE_NONE = -1,
	VFE_LINE_RDI0 = 0,
	VFE_LINE_RDI1 = 1,
	VFE_LINE_RDI2 = 2,
	VFE_LINE_PIX = 3
};

struct vfe_output {
	u8 wm_num;
	u8 wm_idx[3];

	int active_buf;
	struct camss_buffer *buf[2];
	struct camss_buffer *last_buffer;
	struct list_head pending_bufs;

	unsigned int drop_update_idx;

	enum vfe_output_state state;
	unsigned int sequence;
	int wait_sof;
	int wait_reg_update;
	struct completion sof;
	struct completion reg_update;
};

struct vfe_line {
	enum vfe_line_id id;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_VFE_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[MSM_VFE_PADS_NUM];
	struct v4l2_rect compose;
	struct v4l2_rect crop;
	struct camss_video video_out;
	struct vfe_output output;
};

struct vfe_device {
	u8 id;
	void __iomem *base;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	int nclocks;
	struct completion reset_complete;
	struct completion halt_complete;
	struct mutex power_lock;
	int power_count;
	struct mutex stream_lock;
	int stream_count;
	spinlock_t output_lock;
	enum vfe_line_id wm_output_map[MSM_VFE_IMAGE_MASTERS_NUM];
	struct vfe_line line[MSM_VFE_LINE_NUM];
	u32 reg_update;
	u8 was_streaming;
};

struct resources;

int msm_vfe_subdev_init(struct vfe_device *vfe, const struct resources *res);

int msm_vfe_register_entities(struct vfe_device *vfe,
			      struct v4l2_device *v4l2_dev);

void msm_vfe_unregister_entities(struct vfe_device *vfe);

void msm_vfe_get_vfe_id(struct media_entity *entity, u8 *id);
void msm_vfe_get_vfe_line_id(struct media_entity *entity, enum vfe_line_id *id);

void msm_vfe_stop_streaming(struct vfe_device *vfe);

#endif /* QC_MSM_CAMSS_VFE_H */

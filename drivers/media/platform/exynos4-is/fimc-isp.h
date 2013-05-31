/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *          Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_ISP_H_
#define FIMC_ISP_H_

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/s5p_fimc.h>

/* FIXME: revisit these constraints */
#define FIMC_ISP_SINK_WIDTH_MIN		(16 + 8)
#define FIMC_ISP_SINK_HEIGHT_MIN	(12 + 8)
#define FIMC_ISP_SOURCE_WIDTH_MIN	8
#define FIMC_ISP_SOURC_HEIGHT_MIN	8
#define FIMC_ISP_CAC_MARGIN_WIDTH	16
#define FIMC_ISP_CAC_MARGIN_HEIGHT	12

#define FIMC_ISP_SINK_WIDTH_MAX		(4000 - 16)
#define FIMC_ISP_SINK_HEIGHT_MAX	(4000 + 12)
#define FIMC_ISP_SOURCE_WIDTH_MAX	4000
#define FIMC_ISP_SOURC_HEIGHT_MAX	4000

#define FIMC_ISP_NUM_FORMATS		3
#define FIMC_ISP_REQ_BUFS_MIN		2

#define FIMC_ISP_SD_PAD_SINK		0
#define FIMC_ISP_SD_PAD_SRC_FIFO	1
#define FIMC_ISP_SD_PAD_SRC_DMA		2
#define FIMC_ISP_SD_PADS_NUM		3
#define FIMC_ISP_MAX_PLANES		1

/**
 * struct fimc_isp_frame - source/target frame properties
 * @width: full image width
 * @height: full image height
 * @rect: crop/composition rectangle
 */
struct fimc_isp_frame {
	u16 width;
	u16 height;
	struct v4l2_rect rect;
};

struct fimc_isp_ctrls {
	struct v4l2_ctrl_handler handler;

	/* Auto white balance */
	struct v4l2_ctrl *auto_wb;
	/* Auto ISO control cluster */
	struct {
		struct v4l2_ctrl *auto_iso;
		struct v4l2_ctrl *iso;
	};
	/* Adjust - contrast */
	struct v4l2_ctrl *contrast;
	/* Adjust - saturation */
	struct v4l2_ctrl *saturation;
	/* Adjust - sharpness */
	struct v4l2_ctrl *sharpness;
	/* Adjust - brightness */
	struct v4l2_ctrl *brightness;
	/* Adjust - hue */
	struct v4l2_ctrl *hue;

	/* Auto/manual exposure */
	struct v4l2_ctrl *auto_exp;
	/* Manual exposure value */
	struct v4l2_ctrl *exposure;
	/* AE/AWB lock/unlock */
	struct v4l2_ctrl *aewb_lock;
	/* Exposure metering mode */
	struct v4l2_ctrl *exp_metering;
	/* AFC */
	struct v4l2_ctrl *afc;
	/* ISP image effect */
	struct v4l2_ctrl *colorfx;
};

/**
 * struct fimc_is_video - fimc-is video device structure
 * @vdev: video_device structure
 * @type: video device type (CAPTURE/OUTPUT)
 * @pad: video device media (sink) pad
 * @pending_buf_q: pending buffers queue head
 * @active_buf_q: a queue head of buffers scheduled in hardware
 * @vb_queue: vb2 buffer queue
 * @active_buf_count: number of video buffers scheduled in hardware
 * @frame_count: counter of frames dequeued to user space
 * @reqbufs_count: number of buffers requested with REQBUFS ioctl
 * @format: current pixel format
 */
struct fimc_is_video {
	struct video_device	vdev;
	enum v4l2_buf_type	type;
	struct media_pad	pad;
	struct list_head	pending_buf_q;
	struct list_head	active_buf_q;
	struct vb2_queue	vb_queue;
	unsigned int		frame_count;
	unsigned int		reqbufs_count;
	int			streaming;
	const struct fimc_fmt	*format;
};

/**
 * struct fimc_isp - FIMC-IS ISP data structure
 * @pdev: pointer to FIMC-IS platform device
 * @alloc_ctx: videobuf2 memory allocator context
 * @subdev: ISP v4l2_subdev
 * @subdev_pads: the ISP subdev media pads
 * @test_pattern: test pattern controls
 * @ctrls: v4l2 controls structure
 * @video_lock: mutex serializing video device and the subdev operations
 * @cac_margin_x: horizontal CAC margin in pixels
 * @cac_margin_y: vertical CAC margin in pixels
 * @state: driver state flags
 * @video_capture: the ISP block video capture device
 */
struct fimc_isp {
	struct platform_device		*pdev;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct v4l2_subdev		subdev;
	struct media_pad		subdev_pads[FIMC_ISP_SD_PADS_NUM];
	struct v4l2_mbus_framefmt	subdev_fmt;
	struct v4l2_ctrl		*test_pattern;
	struct fimc_isp_ctrls		ctrls;

	struct mutex			video_lock;
	struct mutex			subdev_lock;

	unsigned int			cac_margin_x;
	unsigned int			cac_margin_y;

	unsigned long			state;

	struct fimc_is_video		video_capture;
};

#define ctrl_to_fimc_isp(_ctrl) \
	container_of(ctrl->handler, struct fimc_isp, ctrls.handler)

struct fimc_is;

int fimc_isp_subdev_create(struct fimc_isp *isp);
void fimc_isp_subdev_destroy(struct fimc_isp *isp);
void fimc_isp_irq_handler(struct fimc_is *is);
int fimc_is_create_controls(struct fimc_isp *isp);
int fimc_is_delete_controls(struct fimc_isp *isp);
const struct fimc_fmt *fimc_isp_find_format(const u32 *pixelformat,
					const u32 *mbus_code, int index);
#endif /* FIMC_ISP_H_ */

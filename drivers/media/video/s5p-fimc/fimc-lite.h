/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_LITE_H_
#define FIMC_LITE_H_

#include <asm/sizes.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
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

#include "fimc-core.h"

#define FIMC_LITE_DRV_NAME	"exynos-fimc-lite"
#define FLITE_CLK_NAME		"flite"
#define FIMC_LITE_MAX_DEVS	2
#define FLITE_REQ_BUFS_MIN	2

/* Bit index definitions for struct fimc_lite::state */
enum {
	ST_FLITE_LPM,
	ST_FLITE_PENDING,
	ST_FLITE_RUN,
	ST_FLITE_STREAM,
	ST_FLITE_SUSPENDED,
	ST_FLITE_OFF,
	ST_FLITE_IN_USE,
	ST_FLITE_CONFIG,
	ST_SENSOR_STREAM,
};

#define FLITE_SD_PAD_SINK	0
#define FLITE_SD_PAD_SOURCE	1
#define FLITE_SD_PADS_NUM	2

struct flite_variant {
	unsigned short max_width;
	unsigned short max_height;
	unsigned short out_width_align;
	unsigned short win_hor_offs_align;
	unsigned short out_hor_offs_align;
};

struct flite_drvdata {
	struct flite_variant *variant[FIMC_LITE_MAX_DEVS];
};

#define fimc_lite_get_drvdata(_pdev) \
	((struct flite_drvdata *) platform_get_device_id(_pdev)->driver_data)

struct fimc_lite_events {
	unsigned int data_overflow;
};

#define FLITE_MAX_PLANES	1

/**
 * struct flite_frame - source/target frame properties
 * @f_width: full pixel width
 * @f_height: full pixel height
 * @rect: crop/composition rectangle
 */
struct flite_frame {
	u16 f_width;
	u16 f_height;
	struct v4l2_rect rect;
};

/**
 * struct flite_buffer - video buffer structure
 * @vb:    vb2 buffer
 * @list:  list head for the buffers queue
 * @paddr: precalculated physical address
 */
struct flite_buffer {
	struct vb2_buffer vb;
	struct list_head list;
	dma_addr_t paddr;
};

/**
 * struct fimc_lite - fimc lite structure
 * @pdev: pointer to FIMC-LITE platform device
 * @variant: variant information for this IP
 * @v4l2_dev: pointer to top the level v4l2_device
 * @vfd: video device node
 * @fh: v4l2 file handle
 * @alloc_ctx: videobuf2 memory allocator context
 * @subdev: FIMC-LITE subdev
 * @vd_pad: media (sink) pad for the capture video node
 * @subdev_pads: the subdev media pads
 * @ctrl_handler: v4l2 control handler
 * @test_pattern: test pattern controls
 * @index: FIMC-LITE platform device index
 * @pipeline: video capture pipeline data structure
 * @slock: spinlock protecting this data structure and the hw registers
 * @lock: mutex serializing video device and the subdev operations
 * @clock: FIMC-LITE gate clock
 * @regs: memory mapped io registers
 * @irq_queue: interrupt handler waitqueue
 * @fmt: pointer to color format description structure
 * @payload: image size in bytes (w x h x bpp)
 * @inp_frame: camera input frame structure
 * @out_frame: DMA output frame structure
 * @out_path: output data path (DMA or FIFO)
 * @source_subdev_grp_id: source subdev group id
 * @state: driver state flags
 * @pending_buf_q: pending buffers queue head
 * @active_buf_q: the queue head of buffers scheduled in hardware
 * @vb_queue: vb2 buffers queue
 * @active_buf_count: number of video buffers scheduled in hardware
 * @frame_count: the captured frames counter
 * @reqbufs_count: the number of buffers requested with REQBUFS ioctl
 * @ref_count: driver's private reference counter
 */
struct fimc_lite {
	struct platform_device	*pdev;
	struct flite_variant	*variant;
	struct v4l2_device	*v4l2_dev;
	struct video_device	*vfd;
	struct v4l2_fh		fh;
	struct vb2_alloc_ctx	*alloc_ctx;
	struct v4l2_subdev	subdev;
	struct media_pad	vd_pad;
	struct media_pad	subdev_pads[FLITE_SD_PADS_NUM];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*test_pattern;
	u32			index;
	struct fimc_pipeline	pipeline;

	struct mutex		lock;
	spinlock_t		slock;

	struct clk		*clock;
	void __iomem		*regs;
	wait_queue_head_t	irq_queue;

	const struct fimc_fmt	*fmt;
	unsigned long		payload[FLITE_MAX_PLANES];
	struct flite_frame	inp_frame;
	struct flite_frame	out_frame;
	enum fimc_datapath	out_path;
	unsigned int		source_subdev_grp_id;

	unsigned long		state;
	struct list_head	pending_buf_q;
	struct list_head	active_buf_q;
	struct vb2_queue	vb_queue;
	unsigned int		frame_count;
	unsigned int		reqbufs_count;
	int			ref_count;

	struct fimc_lite_events	events;
};

static inline bool fimc_lite_active(struct fimc_lite *fimc)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&fimc->slock, flags);
	ret = fimc->state & (1 << ST_FLITE_RUN) ||
		fimc->state & (1 << ST_FLITE_PENDING);
	spin_unlock_irqrestore(&fimc->slock, flags);
	return ret;
}

static inline void fimc_lite_active_queue_add(struct fimc_lite *dev,
					 struct flite_buffer *buf)
{
	list_add_tail(&buf->list, &dev->active_buf_q);
}

static inline struct flite_buffer *fimc_lite_active_queue_pop(
					struct fimc_lite *dev)
{
	struct flite_buffer *buf = list_entry(dev->active_buf_q.next,
					      struct flite_buffer, list);
	list_del(&buf->list);
	return buf;
}

static inline void fimc_lite_pending_queue_add(struct fimc_lite *dev,
					struct flite_buffer *buf)
{
	list_add_tail(&buf->list, &dev->pending_buf_q);
}

static inline struct flite_buffer *fimc_lite_pending_queue_pop(
					struct fimc_lite *dev)
{
	struct flite_buffer *buf = list_entry(dev->pending_buf_q.next,
					      struct flite_buffer, list);
	list_del(&buf->list);
	return buf;
}

#endif /* FIMC_LITE_H_ */

/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_LITE_H_
#define FIMC_LITE_H_

#include <linux/sizes.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/drv-intf/exynos-fimc.h>

#define FIMC_LITE_DRV_NAME	"exynos-fimc-lite"
#define FLITE_CLK_NAME		"flite"
#define FIMC_LITE_MAX_DEVS	3
#define FLITE_REQ_BUFS_MIN	2
#define FLITE_DEFAULT_WIDTH	640
#define FLITE_DEFAULT_HEIGHT	480

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
#define FLITE_SD_PAD_SOURCE_DMA	1
#define FLITE_SD_PAD_SOURCE_ISP	2
#define FLITE_SD_PADS_NUM	3

/**
 * struct flite_drvdata - FIMC-LITE IP variant data structure
 * @max_width: maximum camera interface input width in pixels
 * @max_height: maximum camera interface input height in pixels
 * @out_width_align: minimum output width alignment in pixels
 * @win_hor_offs_align: minimum camera interface crop window horizontal
 *			offset alignment in pixels
 * @out_hor_offs_align: minimum output DMA compose rectangle horizontal
 *			offset alignment in pixels
 * @max_dma_bufs: number of output DMA buffer start address registers
 * @num_instances: total number of FIMC-LITE IP instances available
 */
struct flite_drvdata {
	unsigned short max_width;
	unsigned short max_height;
	unsigned short out_width_align;
	unsigned short win_hor_offs_align;
	unsigned short out_hor_offs_align;
	unsigned short max_dma_bufs;
	unsigned short num_instances;
};

struct fimc_lite_events {
	unsigned int data_overflow;
};

#define FLITE_MAX_PLANES	1

/**
 * struct flite_frame - source/target frame properties
 * @f_width: full pixel width
 * @f_height: full pixel height
 * @rect: crop/composition rectangle
 * @fmt: pointer to pixel format description data structure
 */
struct flite_frame {
	u16 f_width;
	u16 f_height;
	struct v4l2_rect rect;
	const struct fimc_fmt *fmt;
};

/**
 * struct flite_buffer - video buffer structure
 * @vb:    vb2 buffer
 * @list:  list head for the buffers queue
 * @paddr: DMA buffer start address
 * @index: DMA start address register's index
 */
struct flite_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	dma_addr_t paddr;
	unsigned short index;
};

/**
 * struct fimc_lite - fimc lite structure
 * @pdev: pointer to FIMC-LITE platform device
 * @dd: SoC specific driver data structure
 * @ve: exynos video device entity structure
 * @v4l2_dev: pointer to top the level v4l2_device
 * @fh: v4l2 file handle
 * @subdev: FIMC-LITE subdev
 * @vd_pad: media (sink) pad for the capture video node
 * @subdev_pads: the subdev media pads
 * @sensor: sensor subdev attached to FIMC-LITE directly or through MIPI-CSIS
 * @ctrl_handler: v4l2 control handler
 * @test_pattern: test pattern controls
 * @index: FIMC-LITE platform device index
 * @pipeline: video capture pipeline data structure
 * @pipeline_ops: media pipeline ops for the video node driver
 * @slock: spinlock protecting this data structure and the hw registers
 * @lock: mutex serializing video device and the subdev operations
 * @clock: FIMC-LITE gate clock
 * @regs: memory mapped io registers
 * @irq_queue: interrupt handler waitqueue
 * @payload: image size in bytes (w x h x bpp)
 * @inp_frame: camera input frame structure
 * @out_frame: DMA output frame structure
 * @out_path: output data path (DMA or FIFO)
 * @source_subdev_grp_id: source subdev group id
 * @state: driver state flags
 * @pending_buf_q: pending buffers queue head
 * @active_buf_q: the queue head of buffers scheduled in hardware
 * @vb_queue: vb2 buffers queue
 * @buf_index: helps to keep track of the DMA start address register index
 * @active_buf_count: number of video buffers scheduled in hardware
 * @frame_count: the captured frames counter
 * @reqbufs_count: the number of buffers requested with REQBUFS ioctl
 */
struct fimc_lite {
	struct platform_device	*pdev;
	struct flite_drvdata	*dd;
	struct exynos_video_entity ve;
	struct v4l2_device	*v4l2_dev;
	struct v4l2_fh		fh;
	struct v4l2_subdev	subdev;
	struct media_pad	vd_pad;
	struct media_pad	subdev_pads[FLITE_SD_PADS_NUM];
	struct v4l2_subdev	*sensor;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*test_pattern;
	int			index;

	struct mutex		lock;
	spinlock_t		slock;

	struct clk		*clock;
	void __iomem		*regs;
	wait_queue_head_t	irq_queue;

	unsigned long		payload[FLITE_MAX_PLANES];
	struct flite_frame	inp_frame;
	struct flite_frame	out_frame;
	atomic_t		out_path;
	unsigned int		source_subdev_grp_id;

	unsigned long		state;
	struct list_head	pending_buf_q;
	struct list_head	active_buf_q;
	struct vb2_queue	vb_queue;
	unsigned short		buf_index;
	unsigned int		frame_count;
	unsigned int		reqbufs_count;

	struct fimc_lite_events	events;
	bool			streaming;
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

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#ifndef __RCAR_ISP__
#define __RCAR_ISP__

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include <media/rppx1.h>
#include <media/vsp1.h>

/* Max 2048 address + value pairs in one VSPX buffer, increase if needed. */
#define RISP_IO_PARAMS_BUF_SIZE	16384

struct rcar_isp_core;

enum risp_core_pads {
	RISP_CORE_INPUT1,
	RISP_CORE_PARAMS,
	RISP_CORE_STATS,
	RISP_CORE_OUTPUT1,
	RISP_CORE_NUM_PADS,
};

/**
 * struct risp_buffer - Describe an IO buffer
 * @vb:		The VB2 buffer
 * @list:	List of buffers queued to the IO queue
 * @vsp_buffer:	Buffer mapped from VSP-X, only used for params IO
 */
struct risp_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	struct vsp1_isp_buffer_desc vsp_buffer;
};

/**
 * struct rcar_isp_core_io - Information for a IO video devices
 * @core:	Backlink to the common ISP core structure
 *
 * @lock:	Protects @vdev, @pad and @queue + open/close fops
 * @vdev:	V4L2 video device associated with this IO port
 * @pad:	Media pad for @vdev
 * @queue:	VB2 buffers queue for $@vdev
 *
 * @streaming:	Flag to indicate if device is streaming, or not
 * @buffers:	List of buffers queued to the device
 *
 * @format:	The active V4L2 format
 */
struct rcar_isp_core_io {
	struct rcar_isp_core *core;

	struct mutex lock; /* See KDoc block. */
	struct video_device vdev;
	struct media_pad pad;
	struct vb2_queue queue;

	bool streaming;
	struct list_head buffers;

	struct v4l2_format format;
};

/**
 * struct rcar_isp_job - R-Car ISP job description
 *
 * Both done_isp and done_vspx shall be set before the job can be considered
 * completely done.
 *
 * @buffers: IO buffers that form a job
 * @vspx_job: VSPX job description
 * @job_queue: list handle
 * @done_isp: Flag to indicate the ISP is done with the job
 * @done_vspx: Flag to indicate the VSPX is done with the job
 */
struct rcar_isp_job {
	struct risp_buffer *buffers[RISP_CORE_NUM_PADS];
	struct vsp1_isp_job_desc vspx_job;
	struct list_head job_queue;
	bool done_isp;
	bool done_vspx;
};

/**
 * struct rcar_isp_vspx - R-Car ISP job description
 *
 * @dev: Device reference to VSPX
 * @job: Job currently being processed by VSPX
 */
struct rcar_isp_vspx {
	struct device *dev;
	struct rcar_isp_job *job;
};

/**
 * struct rcar_isp_core - ISP Core
 * @dev:	(OF) device
 * @rppaddr:	Hardware address of the RPP ISP (from OF)
 * @clk:	The clock for the ISP CORE
 * @rstc:	The reset for the ISP Core
 * @csrstc:	The reset for the ISP Channel Selector
 *
 * @base:	MMIO base of the ISP CORE
 * @csbase:	MMIO base of the ISP CS
 *
 * @subdev:	V4L2 subdevice to represent the ISP CORE
 * @pads:	Media pad for @subdev
 *
 * @v4l2_dev:	V4L2 device
 * @rpp:	Handle to the RPP ISP connected to the ISP CORE
 *
 * @io_lock:	Protect io[*].streaming and io[*].buffers
 * @io:		Array of IO ports to the ISP CORE
 *
 * @lock:	Protects @vspx, @risp_jobs, @sequence and @streaming
 * @vspx:	Handle to the resources used by VSPX connected to the ISP CORE
 * @risp_jobs:	Queue of VSPX transfer jobs
 * @sequence:	V4L2 buffers sequence number
 * @streaming:	Tracks if the device is streaming
 */
struct rcar_isp_core {
	struct device *dev;

	u32 rppaddr;
	struct clk *clk;

	struct reset_control *rstc;
	struct reset_control *csrstc;

	void __iomem *base;
	void __iomem *csbase;

	struct v4l2_subdev subdev;
	struct media_pad pads[RISP_CORE_NUM_PADS];

	struct v4l2_device v4l2_dev;
	struct rppx1 *rpp;

	struct mutex io_lock; /* See KDoc block. */
	struct rcar_isp_core_io io[RISP_CORE_NUM_PADS];

	spinlock_t lock;  /* See KDoc block. */
	struct rcar_isp_vspx vspx;
	struct list_head risp_jobs;
	unsigned int sequence;
	bool streaming;
};

int risp_core_probe(struct rcar_isp_core *core, struct platform_device *pdev,
		    void __iomem *csbase, struct reset_control *csrstc);
void risp_core_remove(struct rcar_isp_core *core);
int risp_core_registered(struct rcar_isp_core *core, struct v4l2_subdev *sd);

int risp_core_job_prepare(struct rcar_isp_core *core);

int risp_core_start_streaming(struct rcar_isp_core *core);
void risp_core_stop_streaming(struct rcar_isp_core *core);

int risp_core_io_create(struct device *dev, struct rcar_isp_core *core,
			struct rcar_isp_core_io *io, unsigned int pad);
void risp_core_io_destroy(struct rcar_isp_core_io *io);

#endif

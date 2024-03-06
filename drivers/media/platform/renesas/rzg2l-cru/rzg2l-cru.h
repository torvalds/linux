/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Renesas RZ/G2L CRU
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 */

#ifndef __RZG2L_CRU__
#define __RZG2L_CRU__

#include <linux/reset.h>

#include <media/v4l2-async.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

/* Number of HW buffers */
#define RZG2L_CRU_HW_BUFFER_MAX		8
#define RZG2L_CRU_HW_BUFFER_DEFAULT	3

/* Address alignment mask for HW buffers */
#define RZG2L_CRU_HW_BUFFER_MASK	0x1ff

/* Maximum number of CSI2 virtual channels */
#define RZG2L_CRU_CSI2_VCHANNEL		4

#define RZG2L_CRU_MIN_INPUT_WIDTH	320
#define RZG2L_CRU_MAX_INPUT_WIDTH	2800
#define RZG2L_CRU_MIN_INPUT_HEIGHT	240
#define RZG2L_CRU_MAX_INPUT_HEIGHT	4095

/**
 * enum rzg2l_cru_dma_state - DMA states
 * @RZG2L_CRU_DMA_STOPPED:   No operation in progress
 * @RZG2L_CRU_DMA_STARTING:  Capture starting up
 * @RZG2L_CRU_DMA_RUNNING:   Operation in progress have buffers
 * @RZG2L_CRU_DMA_STOPPING:  Stopping operation
 */
enum rzg2l_cru_dma_state {
	RZG2L_CRU_DMA_STOPPED = 0,
	RZG2L_CRU_DMA_STARTING,
	RZG2L_CRU_DMA_RUNNING,
	RZG2L_CRU_DMA_STOPPING,
};

struct rzg2l_cru_csi {
	struct v4l2_async_connection *asd;
	struct v4l2_subdev *subdev;
	u32 channel;
};

struct rzg2l_cru_ip {
	struct v4l2_subdev subdev;
	struct media_pad pads[2];
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
};

/**
 * struct rzg2l_cru_dev - Renesas CRU device structure
 * @dev:		(OF) device
 * @base:		device I/O register space remapped to virtual memory
 * @info:		info about CRU instance
 *
 * @presetn:		CRU_PRESETN reset line
 * @aresetn:		CRU_ARESETN reset line
 *
 * @vclk:		CRU Main clock
 *
 * @image_conv_irq:	Holds image conversion interrupt number
 *
 * @vdev:		V4L2 video device associated with CRU
 * @v4l2_dev:		V4L2 device
 * @num_buf:		Holds the current number of buffers enabled
 * @notifier:		V4L2 asynchronous subdevs notifier
 *
 * @ip:			Image processing subdev info
 * @csi:		CSI info
 * @mdev:		media device
 * @mdev_lock:		protects the count, notifier and csi members
 * @pad:		media pad for the video device entity
 *
 * @lock:		protects @queue
 * @queue:		vb2 buffers queue
 * @scratch:		cpu address for scratch buffer
 * @scratch_phys:	physical address of the scratch buffer
 *
 * @qlock:		protects @queue_buf, @buf_list, @sequence
 *			@state
 * @queue_buf:		Keeps track of buffers given to HW slot
 * @buf_list:		list of queued buffers
 * @sequence:		V4L2 buffers sequence number
 * @state:		keeps track of operation state
 *
 * @format:		active V4L2 pixel format
 */
struct rzg2l_cru_dev {
	struct device *dev;
	void __iomem *base;
	const struct rzg2l_cru_info *info;

	struct reset_control *presetn;
	struct reset_control *aresetn;

	struct clk *vclk;

	int image_conv_irq;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	u8 num_buf;

	struct v4l2_async_notifier notifier;

	struct rzg2l_cru_ip ip;
	struct rzg2l_cru_csi csi;
	struct media_device mdev;
	struct mutex mdev_lock;
	struct media_pad pad;

	struct mutex lock;
	struct vb2_queue queue;
	void *scratch;
	dma_addr_t scratch_phys;

	spinlock_t qlock;
	struct vb2_v4l2_buffer *queue_buf[RZG2L_CRU_HW_BUFFER_MAX];
	struct list_head buf_list;
	unsigned int sequence;
	enum rzg2l_cru_dma_state state;

	struct v4l2_pix_format format;
};

void rzg2l_cru_vclk_unprepare(struct rzg2l_cru_dev *cru);
int rzg2l_cru_vclk_prepare(struct rzg2l_cru_dev *cru);

int rzg2l_cru_start_image_processing(struct rzg2l_cru_dev *cru);
void rzg2l_cru_stop_image_processing(struct rzg2l_cru_dev *cru);

int rzg2l_cru_dma_register(struct rzg2l_cru_dev *cru);
void rzg2l_cru_dma_unregister(struct rzg2l_cru_dev *cru);

int rzg2l_cru_video_register(struct rzg2l_cru_dev *cru);
void rzg2l_cru_video_unregister(struct rzg2l_cru_dev *cru);

const struct v4l2_format_info *rzg2l_cru_format_from_pixel(u32 format);

int rzg2l_cru_ip_subdev_register(struct rzg2l_cru_dev *cru);
void rzg2l_cru_ip_subdev_unregister(struct rzg2l_cru_dev *cru);
struct v4l2_mbus_framefmt *rzg2l_cru_ip_get_src_fmt(struct rzg2l_cru_dev *cru);

#endif

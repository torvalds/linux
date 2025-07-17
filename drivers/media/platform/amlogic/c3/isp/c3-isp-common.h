/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#ifndef __C3_ISP_COMMON_H__
#define __C3_ISP_COMMON_H__

#include <linux/clk.h>

#include <media/media-device.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#define C3_ISP_DRIVER_NAME		"c3-isp"
#define C3_ISP_CLOCK_NUM_MAX		3

#define C3_ISP_DEFAULT_WIDTH		1920
#define C3_ISP_DEFAULT_HEIGHT		1080
#define C3_ISP_MAX_WIDTH		2888
#define C3_ISP_MAX_HEIGHT		2240
#define C3_ISP_MIN_WIDTH		160
#define C3_ISP_MIN_HEIGHT		120

#define C3_ISP_DMA_SIZE_ALIGN_BYTES	16

enum c3_isp_core_pads {
	C3_ISP_CORE_PAD_SINK_VIDEO,
	C3_ISP_CORE_PAD_SINK_PARAMS,
	C3_ISP_CORE_PAD_SOURCE_STATS,
	C3_ISP_CORE_PAD_SOURCE_VIDEO_0,
	C3_ISP_CORE_PAD_SOURCE_VIDEO_1,
	C3_ISP_CORE_PAD_SOURCE_VIDEO_2,
	C3_ISP_CORE_PAD_MAX
};

enum c3_isp_resizer_ids {
	C3_ISP_RSZ_0,
	C3_ISP_RSZ_1,
	C3_ISP_RSZ_2,
	C3_ISP_NUM_RSZ
};

enum c3_isp_resizer_pads {
	C3_ISP_RSZ_PAD_SINK,
	C3_ISP_RSZ_PAD_SOURCE,
	C3_ISP_RSZ_PAD_MAX
};

enum c3_isp_cap_devs {
	C3_ISP_CAP_DEV_0,
	C3_ISP_CAP_DEV_1,
	C3_ISP_CAP_DEV_2,
	C3_ISP_NUM_CAP_DEVS
};

enum c3_isp_planes {
	C3_ISP_PLANE_Y,
	C3_ISP_PLANE_UV,
	C3_ISP_NUM_PLANES
};

/*
 * struct c3_isp_cap_format_info - The image format of capture device
 *
 * @mbus_code: the mbus code
 * @fourcc: the pixel format
 * @format: defines the output format of hardware
 * @planes: defines the mutil plane of hardware
 * @ch0_pix_bits: defines the channel 0 pixel bits mode of hardware
 * @uv_swap: defines the uv swap flag of hardware
 * @in_bits: defines the input bits of hardware
 * @hdiv: horizontal chroma subsampling factor of hardware
 * @vdiv: vertical chroma subsampling factor of hardware
 */
struct c3_isp_cap_format_info {
	u32 mbus_code;
	u32 fourcc;
	u32 format;
	u32 planes;
	u32 ch0_pix_bits;
	u8 uv_swap;
	u8 in_bits;
	u8 hdiv;
	u8 vdiv;
};

/*
 * struct c3_isp_cap_buffer - A container of vb2 buffer used by the video
 *                            devices: capture video devices
 *
 * @vb: vb2 buffer
 * @dma_addr: buffer physical address
 * @list: entry of the buffer in the queue
 */
struct c3_isp_cap_buffer {
	struct vb2_v4l2_buffer vb;
	dma_addr_t dma_addr[C3_ISP_NUM_PLANES];
	struct list_head list;
};

/*
 * struct c3_isp_stats_dma_buffer - A container of vb2 buffer used by the video
 *                                  devices: stats video devices
 *
 * @vb: vb2 buffer
 * @dma_addr: buffer physical address
 * @list: entry of the buffer in the queue
 */
struct c3_isp_stats_buffer {
	struct vb2_v4l2_buffer vb;
	dma_addr_t dma_addr;
	struct list_head list;
};

/*
 * struct c3_isp_params_buffer - A container of vb2 buffer used by the
 *                               params video device
 *
 * @vb: vb2 buffer
 * @cfg: scratch buffer used for caching the ISP configuration parameters
 * @list: entry of the buffer in the queue
 */
struct c3_isp_params_buffer {
	struct vb2_v4l2_buffer vb;
	void *cfg;
	struct list_head list;
};

/*
 * struct c3_isp_dummy_buffer - A buffer to write the next frame to in case
 *				there are no vb2 buffers available.
 *
 * @vaddr:	return value of call to dma_alloc_attrs
 * @dma_addr:	dma address of the buffer
 * @size:	size of the buffer
 */
struct c3_isp_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

/*
 * struct c3_isp_core - ISP core subdev
 *
 * @sd: ISP sub-device
 * @pads: ISP sub-device pads
 * @src_pad: source sub-device pad
 * @isp: pointer to c3_isp_device
 */
struct c3_isp_core {
	struct v4l2_subdev sd;
	struct media_pad pads[C3_ISP_CORE_PAD_MAX];
	struct media_pad *src_pad;
	struct c3_isp_device *isp;
};

/*
 * struct c3_isp_resizer - ISP resizer subdev
 *
 * @id: resizer id
 * @sd: resizer sub-device
 * @pads: resizer sub-device pads
 * @src_sd: source sub-device
 * @isp: pointer to c3_isp_device
 * @src_pad: the pad of source sub-device
 */
struct c3_isp_resizer {
	enum c3_isp_resizer_ids id;
	struct v4l2_subdev sd;
	struct media_pad pads[C3_ISP_RSZ_PAD_MAX];
	struct v4l2_subdev *src_sd;
	struct c3_isp_device *isp;
	u32 src_pad;
};

/*
 * struct c3_isp_stats - ISP statistics device
 *
 * @vb2_q: vb2 buffer queue
 * @vdev: video node
 * @vfmt: v4l2_format of the metadata format
 * @pad: media pad
 * @lock: protects vb2_q, vdev
 * @isp: pointer to c3_isp_device
 * @buff: in use buffer
 * @buff_lock: protects stats buffer
 * @pending: stats buffer list head
 */
struct c3_isp_stats {
	struct vb2_queue vb2_q;
	struct video_device vdev;
	struct v4l2_format vfmt;
	struct media_pad pad;

	struct mutex lock; /* Protects vb2_q, vdev */
	struct c3_isp_device *isp;

	struct c3_isp_stats_buffer *buff;
	spinlock_t buff_lock; /* Protects stats buffer */
	struct list_head pending;
};

/*
 * struct c3_isp_params - ISP parameters device
 *
 * @vb2_q: vb2 buffer queue
 * @vdev: video node
 * @vfmt: v4l2_format of the metadata format
 * @pad: media pad
 * @lock: protects vb2_q, vdev
 * @isp: pointer to c3_isp_device
 * @buff: in use buffer
 * @buff_lock: protects stats buffer
 * @pending: stats buffer list head
 */
struct c3_isp_params {
	struct vb2_queue vb2_q;
	struct video_device vdev;
	struct v4l2_format vfmt;
	struct media_pad pad;

	struct mutex lock; /* Protects vb2_q, vdev */
	struct c3_isp_device *isp;

	struct c3_isp_params_buffer *buff;
	spinlock_t buff_lock; /* Protects params buffer */
	struct list_head pending;
};

/*
 * struct c3_isp_capture - ISP capture device
 *
 * @id: capture device ID
 * @vb2_q: vb2 buffer queue
 * @vdev: video node
 * @pad: media pad
 * @lock: protects vb2_q, vdev
 * @isp: pointer to c3_isp_device
 * @rsz: pointer to c3_isp_resizer
 * @buff: in use buffer
 * @buff_lock: protects capture buffer
 * @pending: capture buffer list head
 * @format.info: a pointer to the c3_isp_capture_format of the pixel format
 * @format.fmt: buffer format
 */
struct c3_isp_capture {
	enum c3_isp_cap_devs id;
	struct vb2_queue vb2_q;
	struct video_device vdev;
	struct media_pad pad;

	struct mutex lock; /* Protects vb2_q, vdev */
	struct c3_isp_device *isp;
	struct c3_isp_resizer *rsz;

	struct c3_isp_dummy_buffer dummy_buff;
	struct c3_isp_cap_buffer *buff;
	spinlock_t buff_lock; /* Protects stream buffer */
	struct list_head pending;
	struct {
		const struct c3_isp_cap_format_info *info;
		struct v4l2_pix_format_mplane pix_mp;
	} format;
};

/**
 * struct c3_isp_info - ISP information
 *
 * @clocks: array of ISP clock names
 * @clock_num: actual clock number
 */
struct c3_isp_info {
	char *clocks[C3_ISP_CLOCK_NUM_MAX];
	u32 clock_num;
};

/**
 * struct c3_isp_device - ISP platform device
 *
 * @dev: pointer to the struct device
 * @base: base register address
 * @clks: array of clocks
 * @notifier: notifier to register on the v4l2-async API
 * @v4l2_dev: v4l2_device variable
 * @media_dev: media device variable
 * @pipe: media pipeline
 * @core: ISP core subdev
 * @resizers: ISP resizer subdev
 * @stats: ISP stats device
 * @params: ISP params device
 * @caps: array of ISP capture device
 * @frm_sequence: used to record frame id
 * @info: version-specific ISP information
 */
struct c3_isp_device {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clks[C3_ISP_CLOCK_NUM_MAX];

	struct v4l2_async_notifier notifier;
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct media_pipeline pipe;

	struct c3_isp_core core;
	struct c3_isp_resizer resizers[C3_ISP_NUM_RSZ];
	struct c3_isp_stats stats;
	struct c3_isp_params params;
	struct c3_isp_capture caps[C3_ISP_NUM_CAP_DEVS];

	u32 frm_sequence;
	const struct c3_isp_info *info;
};

u32 c3_isp_read(struct c3_isp_device *isp, u32 reg);
void c3_isp_write(struct c3_isp_device *isp, u32 reg, u32 val);
void c3_isp_update_bits(struct c3_isp_device *isp, u32 reg, u32 mask, u32 val);

void c3_isp_core_queue_sof(struct c3_isp_device *isp);
int c3_isp_core_register(struct c3_isp_device *isp);
void c3_isp_core_unregister(struct c3_isp_device *isp);
int c3_isp_resizers_register(struct c3_isp_device *isp);
void c3_isp_resizers_unregister(struct c3_isp_device *isp);
int c3_isp_captures_register(struct c3_isp_device *isp);
void c3_isp_captures_unregister(struct c3_isp_device *isp);
void c3_isp_captures_isr(struct c3_isp_device *isp);
void c3_isp_stats_pre_cfg(struct c3_isp_device *isp);
int c3_isp_stats_register(struct c3_isp_device *isp);
void c3_isp_stats_unregister(struct c3_isp_device *isp);
void c3_isp_stats_isr(struct c3_isp_device *isp);
void c3_isp_params_pre_cfg(struct c3_isp_device *isp);
int c3_isp_params_register(struct c3_isp_device *isp);
void c3_isp_params_unregister(struct c3_isp_device *isp);
void c3_isp_params_isr(struct c3_isp_device *isp);

#endif

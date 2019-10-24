/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_CORE_H_
#define __MESON_VDEC_CORE_H_

#include <linux/irqreturn.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "vdec_platform.h"

/* 32 buffers in 3-plane YUV420 */
#define MAX_CANVAS (32 * 3)

struct amvdec_buffer {
	struct list_head list;
	struct vb2_buffer *vb;
};

/**
 * struct amvdec_timestamp - stores a src timestamp along with a VIFIFO offset
 *
 * @list: used to make lists out of this struct
 * @ts: timestamp
 * @offset: offset in the VIFIFO where the associated packet was written
 */
struct amvdec_timestamp {
	struct list_head list;
	u64 ts;
	u32 offset;
};

struct amvdec_session;

/**
 * struct amvdec_core - device parameters, singleton
 *
 * @dos_base: DOS memory base address
 * @esparser_base: PARSER memory base address
 * @regmap_ao: regmap for the AO bus
 * @dev: core device
 * @dev_dec: decoder device
 * @platform: platform-specific data
 * @canvas: canvas provider reference
 * @dos_parser_clk: DOS_PARSER clock
 * @dos_clk: DOS clock
 * @vdec_1_clk: VDEC_1 clock
 * @vdec_hevc_clk: VDEC_HEVC clock
 * @esparser_reset: RESET for the PARSER
 * @vdec_dec: video device for the decoder
 * @v4l2_dev: v4l2 device
 * @cur_sess: current decoding session
 */
struct amvdec_core {
	void __iomem *dos_base;
	void __iomem *esparser_base;
	struct regmap *regmap_ao;

	struct device *dev;
	struct device *dev_dec;
	const struct vdec_platform *platform;

	struct meson_canvas *canvas;

	struct clk *dos_parser_clk;
	struct clk *dos_clk;
	struct clk *vdec_1_clk;
	struct clk *vdec_hevc_clk;

	struct reset_control *esparser_reset;

	struct video_device *vdev_dec;
	struct v4l2_device v4l2_dev;

	struct amvdec_session *cur_sess;
	struct mutex lock; /* video device lock */
};

/**
 * struct amvdec_ops - vdec operations
 *
 * @start: mandatory call when the vdec needs to initialize
 * @stop: mandatory call when the vdec needs to stop
 * @conf_esparser: mandatory call to let the vdec configure the ESPARSER
 * @vififo_level: mandatory call to get the current amount of data
 *		  in the VIFIFO
 * @use_offsets: mandatory call. Returns 1 if the VDEC supports vififo offsets
 */
struct amvdec_ops {
	int (*start)(struct amvdec_session *sess);
	int (*stop)(struct amvdec_session *sess);
	void (*conf_esparser)(struct amvdec_session *sess);
	u32 (*vififo_level)(struct amvdec_session *sess);
};

/**
 * struct amvdec_codec_ops - codec operations
 *
 * @start: mandatory call when the codec needs to initialize
 * @stop: mandatory call when the codec needs to stop
 * @load_extended_firmware: optional call to load additional firmware bits
 * @num_pending_bufs: optional call to get the number of dst buffers on hold
 * @can_recycle: optional call to know if the codec is ready to recycle
 *		 a dst buffer
 * @recycle: optional call to tell the codec to recycle a dst buffer. Must go
 *	     in pair with @can_recycle
 * @drain: optional call if the codec has a custom way of draining
 * @eos_sequence: optional call to get an end sequence to send to esparser
 *		  for flush. Mutually exclusive with @drain.
 * @isr: mandatory call when the ISR triggers
 * @threaded_isr: mandatory call for the threaded ISR
 */
struct amvdec_codec_ops {
	int (*start)(struct amvdec_session *sess);
	int (*stop)(struct amvdec_session *sess);
	int (*load_extended_firmware)(struct amvdec_session *sess,
				      const u8 *data, u32 len);
	u32 (*num_pending_bufs)(struct amvdec_session *sess);
	int (*can_recycle)(struct amvdec_core *core);
	void (*recycle)(struct amvdec_core *core, u32 buf_idx);
	void (*drain)(struct amvdec_session *sess);
	void (*resume)(struct amvdec_session *sess);
	const u8 * (*eos_sequence)(u32 *len);
	irqreturn_t (*isr)(struct amvdec_session *sess);
	irqreturn_t (*threaded_isr)(struct amvdec_session *sess);
};

/**
 * struct amvdec_format - describes one of the OUTPUT (src) format supported
 *
 * @pixfmt: V4L2 pixel format
 * @min_buffers: minimum amount of CAPTURE (dst) buffers
 * @max_buffers: maximum amount of CAPTURE (dst) buffers
 * @max_width: maximum picture width supported
 * @max_height: maximum picture height supported
 * @flags: enum flags associated with this pixfmt
 * @vdec_ops: the VDEC operations that support this format
 * @codec_ops: the codec operations that support this format
 * @firmware_path: Path to the firmware that supports this format
 * @pixfmts_cap: list of CAPTURE pixel formats available with pixfmt
 */
struct amvdec_format {
	u32 pixfmt;
	u32 min_buffers;
	u32 max_buffers;
	u32 max_width;
	u32 max_height;
	u32 flags;

	struct amvdec_ops *vdec_ops;
	struct amvdec_codec_ops *codec_ops;

	char *firmware_path;
	u32 pixfmts_cap[4];
};

enum amvdec_status {
	STATUS_STOPPED,
	STATUS_RUNNING,
	STATUS_NEEDS_RESUME,
};

/**
 * struct amvdec_session - decoding session parameters
 *
 * @core: reference to the vdec core struct
 * @fh: v4l2 file handle
 * @m2m_dev: v4l2 m2m device
 * @m2m_ctx: v4l2 m2m context
 * @ctrl_handler: V4L2 control handler
 * @ctrl_min_buf_capture: V4L2 control V4L2_CID_MIN_BUFFERS_FOR_CAPTURE
 * @fmt_out: vdec pixel format for the OUTPUT queue
 * @pixfmt_cap: V4L2 pixel format for the CAPTURE queue
 * @width: current picture width
 * @height: current picture height
 * @colorspace: current colorspace
 * @ycbcr_enc: current ycbcr_enc
 * @quantization: current quantization
 * @xfer_func: current transfer function
 * @pixelaspect: Pixel Aspect Ratio reported by the decoder
 * @esparser_queued_bufs: number of buffers currently queued into ESPARSER
 * @esparser_queue_work: work struct for the ESPARSER to process src buffers
 * @streamon_cap: stream on flag for capture queue
 * @streamon_out: stream on flag for output queue
 * @sequence_cap: capture sequence counter
 * @should_stop: flag set if userspace signaled EOS via command
 *		 or empty buffer
 * @keyframe_found: flag set once a keyframe has been parsed
 * @canvas_alloc: array of all the canvas IDs allocated
 * @canvas_num: number of canvas IDs allocated
 * @vififo_vaddr: virtual address for the VIFIFO
 * @vififo_paddr: physical address for the VIFIFO
 * @vififo_size: size of the VIFIFO dma alloc
 * @bufs_recycle: list of buffers that need to be recycled
 * @bufs_recycle_lock: lock for the bufs_recycle list
 * @recycle_thread: task struct for the recycling thread
 * @timestamps: chronological list of src timestamps
 * @ts_spinlock: spinlock for the timestamps list
 * @last_irq_jiffies: tracks last time the vdec triggered an IRQ
 * @status: current decoding status
 * @priv: codec private data
 */
struct amvdec_session {
	struct amvdec_core *core;

	struct v4l2_fh fh;
	struct v4l2_m2m_dev *m2m_dev;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *ctrl_min_buf_capture;
	struct mutex lock; /* cap & out queues lock */

	const struct amvdec_format *fmt_out;
	u32 pixfmt_cap;

	u32 width;
	u32 height;
	u32 colorspace;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;

	struct v4l2_fract pixelaspect;

	atomic_t esparser_queued_bufs;
	struct work_struct esparser_queue_work;

	unsigned int streamon_cap, streamon_out;
	unsigned int sequence_cap;
	unsigned int should_stop;
	unsigned int keyframe_found;
	unsigned int num_dst_bufs;

	u8 canvas_alloc[MAX_CANVAS];
	u32 canvas_num;

	void *vififo_vaddr;
	dma_addr_t vififo_paddr;
	u32 vififo_size;

	struct list_head bufs_recycle;
	struct mutex bufs_recycle_lock; /* bufs_recycle list lock */
	struct task_struct *recycle_thread;

	struct list_head timestamps;
	spinlock_t ts_spinlock; /* timestamp list lock */

	u64 last_irq_jiffies;
	u32 last_offset;
	u32 wrap_count;
	u32 fw_idx_to_vb2_idx[32];

	enum amvdec_status status;
	void *priv;
};

u32 amvdec_get_output_size(struct amvdec_session *sess);

#endif

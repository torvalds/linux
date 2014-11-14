/*
 * Rockchip RK3288 VPU codec driver
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef RK3288_VPU_COMMON_H_
#define RK3288_VPU_COMMON_H_

/* Enable debugging by default for now. */
#define DEBUG

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "rk3288_vpu_hw.h"

#define RK3288_VPU_NAME			"rk3288-vpu"
#define RK3288_VPU_DEC_NAME		"rk3288-vpu-dec"
#define RK3288_VPU_ENC_NAME		"rk3288-vpu-enc"

#define V4L2_CID_CUSTOM_BASE		(V4L2_CID_USER_BASE | 0x1000)

#define DST_QUEUE_OFF_BASE		(TASK_SIZE / 2)

#define RK3288_VPU_MAX_CTRLS		32

#define MB_DIM				16
#define MB_WIDTH(x_size)		DIV_ROUND_UP(x_size, MB_DIM)
#define MB_HEIGHT(y_size)		DIV_ROUND_UP(y_size, MB_DIM)

struct rk3288_vpu_variant;
struct rk3288_vpu_ctx;
struct rk3288_vpu_codec_ops;

/**
 * enum rk3288_vpu_codec_mode - codec operating mode.
 * @RK_VPU_CODEC_NONE:	No operating mode. Used for RAW video formats.
 * @RK_VPU_CODEC_H264D:	H264 decoder.
 * @RK_VPU_CODEC_VP8D:	VP8 decoder.
 * @RK_VPU_CODEC_H264E: H264 encoder.
 * @RK_VPU_CODEC_VP8E:	VP8 encoder.
 */
enum rk3288_vpu_codec_mode {
	RK_VPU_CODEC_NONE = -1,
	RK_VPU_CODEC_H264D,
	RK_VPU_CODEC_VP8D,
	RK_VPU_CODEC_H264E,
	RK_VPU_CODEC_VP8E
};

/**
 * enum rk3288_vpu_plane - indices of planes inside a VB2 buffer.
 * @PLANE_Y:		Plane containing luminance data (also denoted as Y).
 * @PLANE_CB_CR:	Plane containing interleaved chrominance data (also
 *			denoted as CbCr).
 * @PLANE_CB:		Plane containing CB part of chrominance data.
 * @PLANE_CR:		Plane containing CR part of chrominance data.
 */
enum rk3288_vpu_plane {
	PLANE_Y		= 0,
	PLANE_CB_CR	= 1,
	PLANE_CB	= 1,
	PLANE_CR	= 2,
};

/**
 * struct rk3288_vpu_vp8e_buf_data - mode-specific per-buffer data
 * @dct_offset:		Offset inside the buffer to DCT partition.
 * @hdr_size:		Size of header data in the buffer.
 * @ext_hdr_size:	Size of ext header data in the buffer.
 * @dct_size:		Size of DCT partition in the buffer.
 * @header:		Frame header to copy to destination buffer.
 */
struct rk3288_vpu_vp8e_buf_data {
	size_t dct_offset;
	size_t hdr_size;
	size_t ext_hdr_size;
	size_t dct_size;
	u8 header[RK3288_HEADER_SIZE];
};

/**
 * struct rk3288_vpu_buf - Private data related to each VB2 buffer.
 * @list:		List head for queuing in buffer queue.
 * @b:			Pointer to related VB2 buffer.
 * @flags:		Buffer state. See enum rk3288_vpu_buf_flags.
 */
struct rk3288_vpu_buf {
	struct vb2_buffer b;
	struct list_head list;

	/* Mode-specific data. */
	union {
		struct rk3288_vpu_vp8e_buf_data vp8e;
	};
};

/**
 * enum rk3288_vpu_state - bitwise flags indicating hardware state.
 * @VPU_RUNNING:	The hardware has been programmed for operation
 *			and is running at the moment.
 */
enum rk3288_vpu_state {
	VPU_RUNNING	= BIT(0),
};

/**
 * struct rk3288_vpu_dev - driver data
 * @v4l2_dev:		V4L2 device to register video devices for.
 * @vfd_dec:		Video device for decoder.
 * @vfd_enc:		Video device for encoder.
 * @pdev:		Pointer to VPU platform device.
 * @dev:		Pointer to device for convenient logging using
 *			dev_ macros.
 * @alloc_ctx:		VB2 allocator context.
 * @aclk_vcodec:	Handle of ACLK clock.
 * @hclk_vcodec:	Handle of HCLK clock.
 * @base:		Mapped address of VPU registers.
 * @enc_base:		Mapped address of VPU encoder register for convenience.
 * @dec_base:		Mapped address of VPU decoder register for convenience.
 * @mapping:		DMA IOMMU mapping.
 * @vpu_mutex:		Mutex to synchronize V4L2 calls.
 * @irqlock:		Spinlock to synchronize access to data structures
 *			shared with interrupt handlers.
 * @state:		Device state.
 * @ready_ctxs:		List of contexts ready to run.
 * @variant:		Hardware variant-specfic parameters.
 * @current_ctx:	Context being currently processed by hardware.
 * @run_wq:		Wait queue to wait for run completion.
 * @watchdog_work:	Delayed work for hardware timeout handling.
 */
struct rk3288_vpu_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vfd_dec;
	struct video_device *vfd_enc;
	struct platform_device *pdev;
	struct device *dev;
	void *alloc_ctx;
	struct clk *aclk_vcodec;
	struct clk *hclk_vcodec;
	void __iomem *base;
	void __iomem *enc_base;
	void __iomem *dec_base;
	struct dma_iommu_mapping *mapping;

	struct mutex vpu_mutex;	/* video_device lock */
	spinlock_t irqlock;
	unsigned long state;
	struct list_head ready_ctxs;
	const struct rk3288_vpu_variant *variant;
	struct rk3288_vpu_ctx *current_ctx;
	wait_queue_head_t run_wq;
	struct delayed_work watchdog_work;
};

/**
 * struct rk3288_vpu_run_ops - per context operations on run data.
 * @prepare_run:	Called when the context was selected for running
 *			to prepare operating mode specific data.
 * @run_done:		Called when hardware completed the run to collect
 *			operating mode specific data from hardware and
 *			finalize the processing.
 */
struct rk3288_vpu_run_ops {
	void (*prepare_run)(struct rk3288_vpu_ctx *);
	void (*run_done)(struct rk3288_vpu_ctx *, enum vb2_buffer_state);
};

/**
 * struct rk3288_vpu_vp8e_run - per-run data specific to VP8 encoding.
 * @reg_params:	Pointer to a buffer containing register values prepared
 *		by user space.
 */
struct rk3288_vpu_vp8e_run {
	const struct rk3288_vp8e_reg_params *reg_params;
};

/**
 * struct rk3288_vpu_run - per-run data for hardware code.
 * @src:		Source buffer to be processed.
 * @dst:		Destination buffer to be processed.
 * @priv_src:		Hardware private source buffer.
 * @priv_dst:		Hardware private destination buffer.
 */
struct rk3288_vpu_run {
	/* Generic for more than one operating mode. */
	struct rk3288_vpu_buf *src;
	struct rk3288_vpu_buf *dst;

	struct rk3288_vpu_aux_buf priv_src;
	struct rk3288_vpu_aux_buf priv_dst;

	/* Specific for particular operating modes. */
	union {
		struct rk3288_vpu_vp8e_run vp8e;
		/* Other modes will need different data. */
	};
};

/**
 * struct rk3288_vpu_ctx - Context (instance) private data.
 *
 * @dev:		VPU driver data to which the context belongs.
 * @fh:			V4L2 file handler.
 *
 * @vpu_src_fmt:	Descriptor of active source format.
 * @src_fmt:		V4L2 pixel format of active source format.
 * @vpu_dst_fmt:	Descriptor of active destination format.
 * @dst_fmt:		V4L2 pixel format of active destination format.
 *
 * @vq_src:		Videobuf2 source queue.
 * @src_queue:		Internal source buffer queue.
 * @src_crop:		Configured source crop rectangle (encoder-only).
 * @vq_dst:		Videobuf2 destination queue
 * @dst_queue:		Internal destination buffer queue.
 *
 * @ctrls:		Array containing pointer to registered controls.
 * @ctrl_handler:	Control handler used to register controls.
 * @num_ctrls:		Number of registered controls.
 *
 * @list:		List head for queue of ready contexts.
 *
 * @run:		Structure containing data about currently scheduled
 *			processing run.
 * @run_ops:		Set of operations related to currently scheduled run.
 * @hw:			Structure containing hardware-related context.
 */
struct rk3288_vpu_ctx {
	struct rk3288_vpu_dev *dev;
	struct v4l2_fh fh;

	/* Format info */
	struct rk3288_vpu_fmt *vpu_src_fmt;
	struct v4l2_pix_format_mplane src_fmt;
	struct rk3288_vpu_fmt *vpu_dst_fmt;
	struct v4l2_pix_format_mplane dst_fmt;

	/* VB2 queue data */
	struct vb2_queue vq_src;
	struct list_head src_queue;
	struct v4l2_rect src_crop;
	struct vb2_queue vq_dst;
	struct list_head dst_queue;

	/* Controls */
	struct v4l2_ctrl *ctrls[RK3288_VPU_MAX_CTRLS];
	struct v4l2_ctrl_handler ctrl_handler;
	unsigned num_ctrls;

	/* Various runtime data */
	struct list_head list;

	struct rk3288_vpu_run run;
	const struct rk3288_vpu_run_ops *run_ops;
	struct rk3288_vpu_hw_ctx hw;
};

/**
 * struct rk3288_vpu_fmt - information about supported video formats.
 * @name:	Human readable name of the format.
 * @fourcc:	FourCC code of the format. See V4L2_PIX_FMT_*.
 * @codec_mode:	Codec mode related to this format. See
 *		enum rk3288_vpu_codec_mode.
 * @num_planes:	Number of planes used by this format.
 * @depth:	Depth of each plane in bits per pixel.
 * @enc_fmt:	Format identifier for encoder registers.
 */
struct rk3288_vpu_fmt {
	char *name;
	u32 fourcc;
	enum rk3288_vpu_codec_mode codec_mode;
	int num_planes;
	u8 depth[VIDEO_MAX_PLANES];
	enum rk3288_vpu_enc_fmt enc_fmt;
};

/**
 * struct rk3288_vpu_control - information about controls to be registered.
 * @id:			Control ID.
 * @type:		Type of the control.
 * @name:		Human readable name of the control.
 * @minimum:		Minimum value of the control.
 * @maximum:		Maximum value of the control.
 * @step:		Control value increase step.
 * @menu_skip_mask:	Mask of invalid menu positions.
 * @default_value:	Initial value of the control.
 * @max_stores:		Maximum number of configration stores.
 * @dims:		Size of each dimension of compound control.
 * @elem_size:		Size of individual element of compound control.
 * @is_volatile:	Control is volatile.
 * @is_read_only:	Control is read-only.
 * @can_store:		Control uses configuration stores.
 *
 * See also struct v4l2_ctrl_config.
 */
struct rk3288_vpu_control {
	u32 id;

	enum v4l2_ctrl_type type;
	const char *name;
	s32 minimum;
	s32 maximum;
	s32 step;
	u32 menu_skip_mask;
	s32 default_value;
	s32 max_stores;
	u32 dims[V4L2_CTRL_MAX_DIMS];
	u32 elem_size;

	bool is_volatile:1;
	bool is_read_only:1;
	bool can_store:1;
};

/* Logging helpers */

/**
 * debug - Module parameter to control level of debugging messages.
 *
 * Level of debugging messages can be controlled by bits of module parameter
 * called "debug". Meaning of particular bits is as follows:
 *
 * bit 0 - global information: mode, size, init, release
 * bit 1 - each run start/result information
 * bit 2 - contents of small controls from userspace
 * bit 3 - contents of big controls from userspace
 * bit 4 - detail fmt, ctrl, buffer q/dq information
 * bit 5 - detail function enter/leave trace information
 * bit 6 - register write/read information
 */
extern int debug;

#define vpu_debug(level, fmt, args...)				\
	do {							\
		if (debug & BIT(level))				\
			pr_debug("%s:%d: " fmt,	                \
				 __func__, __LINE__, ##args);	\
	} while (0)

#define vpu_debug_enter()	vpu_debug(5, "enter\n")
#define vpu_debug_leave()	vpu_debug(5, "leave\n")

#define vpu_err(fmt, args...)					\
	pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

static inline char *fmt2str(u32 fmt, char *str)
{
	char a = fmt & 0xFF;
	char b = (fmt >> 8) & 0xFF;
	char c = (fmt >> 16) & 0xFF;
	char d = (fmt >> 24) & 0xFF;

	sprintf(str, "%c%c%c%c", a, b, c, d);

	return str;
}

/* Structure access helpers. */
static inline struct rk3288_vpu_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct rk3288_vpu_ctx, fh);
}

static inline struct rk3288_vpu_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct rk3288_vpu_ctx, ctrl_handler);
}

static inline struct rk3288_vpu_buf *vb_to_buf(struct vb2_buffer *vb)
{
	return container_of(vb, struct rk3288_vpu_buf, b);
}

int rk3288_vpu_ctrls_setup(struct rk3288_vpu_ctx *ctx,
			   const struct v4l2_ctrl_ops *ctrl_ops,
			   struct rk3288_vpu_control *controls,
			   unsigned num_ctrls,
			   const char *const *(*get_menu)(u32));
void rk3288_vpu_ctrls_delete(struct rk3288_vpu_ctx *ctx);

void rk3288_vpu_try_context(struct rk3288_vpu_dev *dev,
			    struct rk3288_vpu_ctx *ctx);

void rk3288_vpu_run_done(struct rk3288_vpu_ctx *ctx,
			 enum vb2_buffer_state result);

int rk3288_vpu_aux_buf_alloc(struct rk3288_vpu_dev *vpu,
			    struct rk3288_vpu_aux_buf *buf, size_t size);
void rk3288_vpu_aux_buf_free(struct rk3288_vpu_dev *vpu,
			     struct rk3288_vpu_aux_buf *buf);

/* Register accessors. */
static inline void vepu_write_relaxed(struct rk3288_vpu_dev *vpu,
				       u32 val, u32 reg)
{
	vpu_debug(6, "MARK: set reg[%03d]: %08x\n", reg / 4, val);
	writel_relaxed(val, vpu->enc_base + reg);
}

static inline void vepu_write(struct rk3288_vpu_dev *vpu, u32 val, u32 reg)
{
	vpu_debug(6, "MARK: set reg[%03d]: %08x\n", reg / 4, val);
	writel(val, vpu->enc_base + reg);
}

static inline u32 vepu_read(struct rk3288_vpu_dev *vpu, u32 reg)
{
	u32 val = readl(vpu->enc_base + reg);

	vpu_debug(6, "MARK: get reg[%03d]: %08x\n", reg / 4, val);
	return val;
}

#endif /* RK3288_VPU_COMMON_H_ */

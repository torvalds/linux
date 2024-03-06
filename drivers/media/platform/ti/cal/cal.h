/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI Camera Access Layer (CAL)
 *
 * Copyright (c) 2015-2020 Texas Instruments Inc.
 *
 * Authors:
 *	Benoit Parrot <bparrot@ti.com>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */
#ifndef __TI_CAL_H__
#define __TI_CAL_H__

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#define CAL_MODULE_NAME			"cal"
#define CAL_MAX_NUM_CONTEXT		8
#define CAL_NUM_CSI2_PORTS		2

/*
 * The width is limited by the size of the CAL_WR_DMA_XSIZE_j.XSIZE field,
 * expressed in multiples of 64 bits. The height is limited by the size of the
 * CAL_CSI2_CTXi_j.CTXi_LINES and CAL_WR_DMA_CTRL_j.YSIZE fields, expressed in
 * lines.
 */
#define CAL_MIN_WIDTH_BYTES		16
#define CAL_MAX_WIDTH_BYTES		(8192 * 8)
#define CAL_MIN_HEIGHT_LINES		1
#define CAL_MAX_HEIGHT_LINES		16383

#define CAL_CAMERARX_PAD_SINK		0
#define CAL_CAMERARX_PAD_FIRST_SOURCE	1
#define CAL_CAMERARX_NUM_SOURCE_PADS	1
#define CAL_CAMERARX_NUM_PADS		(1 + CAL_CAMERARX_NUM_SOURCE_PADS)

static inline bool cal_rx_pad_is_sink(u32 pad)
{
	/* Camera RX has 1 sink pad, and N source pads */
	return pad == 0;
}

static inline bool cal_rx_pad_is_source(u32 pad)
{
	/* Camera RX has 1 sink pad, and N source pads */
	return pad >= CAL_CAMERARX_PAD_FIRST_SOURCE &&
	       pad <= CAL_CAMERARX_NUM_SOURCE_PADS;
}

struct device;
struct device_node;
struct resource;
struct regmap;
struct regmap_fied;

/* CTRL_CORE_CAMERRX_CONTROL register field id */
enum cal_camerarx_field {
	F_CTRLCLKEN,
	F_CAMMODE,
	F_LANEENABLE,
	F_CSI_MODE,
	F_MAX_FIELDS,
};

enum cal_dma_state {
	CAL_DMA_RUNNING,
	CAL_DMA_STOP_REQUESTED,
	CAL_DMA_STOP_PENDING,
	CAL_DMA_STOPPED,
};

struct cal_format_info {
	u32	fourcc;
	u32	code;
	/* Bits per pixel */
	u8	bpp;
	bool	meta;
};

/* buffer for one video frame */
struct cal_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

/**
 * struct cal_dmaqueue - Queue of DMA buffers
 */
struct cal_dmaqueue {
	/**
	 * @lock: Protects all fields in the cal_dmaqueue.
	 */
	spinlock_t		lock;

	/**
	 * @queue: Buffers queued to the driver and waiting for DMA processing.
	 * Buffers are added to the list by the vb2 .buffer_queue() operation,
	 * and move to @pending when they are scheduled for the next frame.
	 */
	struct list_head	queue;
	/**
	 * @pending: Buffer provided to the hardware to DMA the next frame.
	 * Will move to @active at the end of the current frame.
	 */
	struct cal_buffer	*pending;
	/**
	 * @active: Buffer being DMA'ed to for the current frame. Will be
	 * retired and given back to vb2 at the end of the current frame if
	 * a @pending buffer has been scheduled to replace it.
	 */
	struct cal_buffer	*active;

	/** @state: State of the DMA engine. */
	enum cal_dma_state	state;
	/** @wait: Wait queue to signal a @state transition to CAL_DMA_STOPPED. */
	struct wait_queue_head	wait;
};

struct cal_camerarx_data {
	struct {
		unsigned int lsb;
		unsigned int msb;
	} fields[F_MAX_FIELDS];
	unsigned int num_lanes;
};

struct cal_data {
	const struct cal_camerarx_data *camerarx;
	unsigned int num_csi2_phy;
	unsigned int flags;
};

/*
 * The Camera Adaptation Layer (CAL) module is paired with one or more complex
 * I/O PHYs (CAMERARX). It contains multiple instances of CSI-2, processing and
 * DMA contexts.
 *
 * The cal_dev structure represents the whole subsystem, including the CAL and
 * the CAMERARX instances. Instances of struct cal_dev are named cal through the
 * driver.
 *
 * The cal_camerarx structure represents one CAMERARX instance. Instances of
 * cal_camerarx are named phy through the driver.
 *
 * The cal_ctx structure represents the combination of one CSI-2 context, one
 * processing context and one DMA context. Instance of struct cal_ctx are named
 * ctx through the driver.
 */

struct cal_camerarx {
	void __iomem		*base;
	struct resource		*res;
	struct regmap_field	*fields[F_MAX_FIELDS];

	struct cal_dev		*cal;
	unsigned int		instance;

	struct v4l2_fwnode_endpoint	endpoint;
	struct device_node	*source_ep_node;
	struct device_node	*source_node;
	struct v4l2_subdev	*source;

	struct v4l2_subdev	subdev;
	struct media_pad	pads[CAL_CAMERARX_NUM_PADS];

	/* protects the vc_* fields below */
	spinlock_t		vc_lock;
	u8			vc_enable_count[4];
	u16			vc_frame_number[4];
	u32			vc_sequence[4];

	unsigned int		enable_count;
};

struct cal_dev {
	struct clk		*fclk;
	int			irq;
	void __iomem		*base;
	struct resource		*res;
	struct device		*dev;

	const struct cal_data	*data;
	u32			revision;

	/* Control Module handle */
	struct regmap		*syscon_camerrx;
	u32			syscon_camerrx_offset;

	/* Camera Core Module handle */
	struct cal_camerarx	*phy[CAL_NUM_CSI2_PORTS];

	u32 num_contexts;
	struct cal_ctx		*ctx[CAL_MAX_NUM_CONTEXT];

	struct media_device	mdev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_async_notifier notifier;

	unsigned long		reserved_pix_proc_mask;
};

/*
 * There is one cal_ctx structure for each camera core context.
 */
struct cal_ctx {
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_device	vdev;
	struct media_pad	pad;

	struct cal_dev		*cal;
	struct cal_camerarx	*phy;

	/* v4l2_ioctl mutex */
	struct mutex		mutex;

	struct cal_dmaqueue	dma;

	/* video capture */
	const struct cal_format_info	*fmtinfo;
	/* Used to store current pixel format */
	struct v4l2_format	v_fmt;

	/* Current subdev enumerated format (legacy) */
	const struct cal_format_info	**active_fmt;
	unsigned int		num_active_fmt;

	struct vb2_queue	vb_vidq;
	u8			dma_ctx;
	u8			cport;
	u8			csi2_ctx;
	u8			pix_proc;
	u8			vc;
	u8			datatype;

	bool			use_pix_proc;
};

extern unsigned int cal_debug;
extern int cal_video_nr;
extern bool cal_mc_api;

#define cal_dbg(level, cal, fmt, arg...)				\
	do {								\
		if (cal_debug >= (level))				\
			dev_printk(KERN_DEBUG, (cal)->dev, fmt, ##arg);	\
	} while (0)
#define cal_info(cal, fmt, arg...)					\
	dev_info((cal)->dev, fmt, ##arg)
#define cal_err(cal, fmt, arg...)					\
	dev_err((cal)->dev, fmt, ##arg)

#define ctx_dbg(level, ctx, fmt, arg...)				\
	cal_dbg(level, (ctx)->cal, "ctx%u: " fmt, (ctx)->dma_ctx, ##arg)
#define ctx_info(ctx, fmt, arg...)					\
	cal_info((ctx)->cal, "ctx%u: " fmt, (ctx)->dma_ctx, ##arg)
#define ctx_err(ctx, fmt, arg...)					\
	cal_err((ctx)->cal, "ctx%u: " fmt, (ctx)->dma_ctx, ##arg)

#define phy_dbg(level, phy, fmt, arg...)				\
	cal_dbg(level, (phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)
#define phy_info(phy, fmt, arg...)					\
	cal_info((phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)
#define phy_err(phy, fmt, arg...)					\
	cal_err((phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)

static inline u32 cal_read(struct cal_dev *cal, u32 offset)
{
	return ioread32(cal->base + offset);
}

static inline void cal_write(struct cal_dev *cal, u32 offset, u32 val)
{
	iowrite32(val, cal->base + offset);
}

static __always_inline u32 cal_read_field(struct cal_dev *cal, u32 offset, u32 mask)
{
	return FIELD_GET(mask, cal_read(cal, offset));
}

static inline void cal_write_field(struct cal_dev *cal, u32 offset, u32 value,
				   u32 mask)
{
	u32 val = cal_read(cal, offset);

	val &= ~mask;
	val |= (value << __ffs(mask)) & mask;
	cal_write(cal, offset, val);
}

static inline void cal_set_field(u32 *valp, u32 field, u32 mask)
{
	u32 val = *valp;

	val &= ~mask;
	val |= (field << __ffs(mask)) & mask;
	*valp = val;
}

extern const struct cal_format_info cal_formats[];
extern const unsigned int cal_num_formats;
const struct cal_format_info *cal_format_by_fourcc(u32 fourcc);
const struct cal_format_info *cal_format_by_code(u32 code);

void cal_quickdump_regs(struct cal_dev *cal);

void cal_camerarx_disable(struct cal_camerarx *phy);
void cal_camerarx_i913_errata(struct cal_camerarx *phy);
struct cal_camerarx *cal_camerarx_create(struct cal_dev *cal,
					 unsigned int instance);
void cal_camerarx_destroy(struct cal_camerarx *phy);

int cal_ctx_prepare(struct cal_ctx *ctx);
void cal_ctx_unprepare(struct cal_ctx *ctx);
void cal_ctx_set_dma_addr(struct cal_ctx *ctx, dma_addr_t addr);
void cal_ctx_start(struct cal_ctx *ctx);
void cal_ctx_stop(struct cal_ctx *ctx);

int cal_ctx_v4l2_register(struct cal_ctx *ctx);
void cal_ctx_v4l2_unregister(struct cal_ctx *ctx);
int cal_ctx_v4l2_init(struct cal_ctx *ctx);
void cal_ctx_v4l2_cleanup(struct cal_ctx *ctx);

#endif /* __TI_CAL_H__ */

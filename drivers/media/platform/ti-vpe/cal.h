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

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-v4l2.h>

#define CAL_MODULE_NAME			"cal"
#define CAL_NUM_CONTEXT			2
#define CAL_NUM_CSI2_PORTS		2

#define MAX_WIDTH_BYTES			(8192 * 8)
#define MAX_HEIGHT_LINES		16383

struct device;
struct device_node;
struct resource;
struct regmap;
struct regmap_fied;
struct v4l2_subdev;

/* CTRL_CORE_CAMERRX_CONTROL register field id */
enum cal_camerarx_field {
	F_CTRLCLKEN,
	F_CAMMODE,
	F_LANEENABLE,
	F_CSI_MODE,
	F_MAX_FIELDS,
};

struct cal_fmt {
	u32	fourcc;
	u32	code;
	/* Bits per pixel */
	u8	bpp;
};

/* buffer for one video frame */
struct cal_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

struct cal_dmaqueue {
	struct list_head	active;
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
	struct device		*dev;
	struct regmap_field	*fields[F_MAX_FIELDS];

	struct cal_dev		*cal;
	unsigned int		instance;

	struct v4l2_fwnode_endpoint	endpoint;
	struct device_node	*sensor_node;
	struct v4l2_subdev	*sensor;
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

	struct cal_ctx		*ctx[CAL_NUM_CONTEXT];

	struct media_device	mdev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_async_notifier notifier;
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
	/* v4l2 buffers lock */
	spinlock_t		slock;

	struct cal_dmaqueue	vidq;

	/* video capture */
	const struct cal_fmt	*fmt;
	/* Used to store current pixel format */
	struct v4l2_format		v_fmt;
	/* Used to store current mbus frame format */
	struct v4l2_mbus_framefmt	m_fmt;

	/* Current subdev enumerated format */
	const struct cal_fmt	**active_fmt;
	unsigned int		num_active_fmt;

	unsigned int		sequence;
	struct vb2_queue	vb_vidq;
	unsigned int		index;
	unsigned int		cport;

	/* Pointer pointing to current v4l2_buffer */
	struct cal_buffer	*cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct cal_buffer	*next_frm;

	bool dma_act;
};

extern unsigned int cal_debug;
extern int cal_video_nr;

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
	cal_dbg(level, (ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)
#define ctx_info(ctx, fmt, arg...)					\
	cal_info((ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)
#define ctx_err(ctx, fmt, arg...)					\
	cal_err((ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)

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

static inline u32 cal_read_field(struct cal_dev *cal, u32 offset, u32 mask)
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

void cal_quickdump_regs(struct cal_dev *cal);

void cal_camerarx_disable(struct cal_camerarx *phy);
int cal_camerarx_start(struct cal_camerarx *phy, const struct cal_fmt *fmt);
void cal_camerarx_stop(struct cal_camerarx *phy);
void cal_camerarx_enable_irqs(struct cal_camerarx *phy);
void cal_camerarx_disable_irqs(struct cal_camerarx *phy);
void cal_camerarx_ppi_enable(struct cal_camerarx *phy);
void cal_camerarx_ppi_disable(struct cal_camerarx *phy);
void cal_camerarx_i913_errata(struct cal_camerarx *phy);
struct cal_camerarx *cal_camerarx_create(struct cal_dev *cal,
					 unsigned int instance);
void cal_camerarx_destroy(struct cal_camerarx *phy);

void cal_ctx_csi2_config(struct cal_ctx *ctx);
void cal_ctx_pix_proc_config(struct cal_ctx *ctx);
void cal_ctx_wr_dma_config(struct cal_ctx *ctx, unsigned int width,
			   unsigned int height);
void cal_ctx_wr_dma_addr(struct cal_ctx *ctx, unsigned int dmaaddr);

int cal_ctx_v4l2_register(struct cal_ctx *ctx);
void cal_ctx_v4l2_unregister(struct cal_ctx *ctx);
int cal_ctx_v4l2_init(struct cal_ctx *ctx);
void cal_ctx_v4l2_cleanup(struct cal_ctx *ctx);

#endif /* __TI_CAL_H__ */

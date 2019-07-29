/*
 * TI CAL camera interface driver
 *
 * Copyright (c) 2015 Texas Instruments Inc.
 * Benoit Parrot, <bparrot@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#include <media/v4l2-fwnode.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include "cal_regs.h"

#define CAL_MODULE_NAME "cal"

#define MAX_WIDTH 1920
#define MAX_HEIGHT 1200

#define CAL_VERSION "0.1.0"

MODULE_DESCRIPTION("TI CAL driver");
MODULE_AUTHOR("Benoit Parrot, <bparrot@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CAL_VERSION);

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

static unsigned debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

/* timeperframe: min/max and default */
static const struct v4l2_fract
	tpf_default = {.numerator = 1001,	.denominator = 30000};

#define cal_dbg(level, caldev, fmt, arg...)	\
		v4l2_dbg(level, debug, &caldev->v4l2_dev, fmt, ##arg)
#define cal_info(caldev, fmt, arg...)	\
		v4l2_info(&caldev->v4l2_dev, fmt, ##arg)
#define cal_err(caldev, fmt, arg...)	\
		v4l2_err(&caldev->v4l2_dev, fmt, ##arg)

#define ctx_dbg(level, ctx, fmt, arg...)	\
		v4l2_dbg(level, debug, &ctx->v4l2_dev, fmt, ##arg)
#define ctx_info(ctx, fmt, arg...)	\
		v4l2_info(&ctx->v4l2_dev, fmt, ##arg)
#define ctx_err(ctx, fmt, arg...)	\
		v4l2_err(&ctx->v4l2_dev, fmt, ##arg)

#define CAL_NUM_INPUT 1
#define CAL_NUM_CONTEXT 2

#define bytes_per_line(pixel, bpp) (ALIGN(pixel * bpp, 16))

#define reg_read(dev, offset) ioread32(dev->base + offset)
#define reg_write(dev, offset, val) iowrite32(val, dev->base + offset)

#define reg_read_field(dev, offset, mask) get_field(reg_read(dev, offset), \
						    mask)
#define reg_write_field(dev, offset, field, mask) { \
	u32 val = reg_read(dev, offset); \
	set_field(&val, field, mask); \
	reg_write(dev, offset, val); }

/* ------------------------------------------------------------------
 *	Basic structures
 * ------------------------------------------------------------------
 */

struct cal_fmt {
	u32	fourcc;
	u32	code;
	u8	depth;
};

static struct cal_fmt cal_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.code		= MEDIA_BUS_FMT_YVYU8_2X8,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.code		= MEDIA_BUS_FMT_VYUY8_2X8,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_BE,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB24, /* rgb */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_LE,
		.depth		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_BGR24, /* bgr */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_BE,
		.depth		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB32, /* argb */
		.code		= MEDIA_BUS_FMT_ARGB8888_1X32,
		.depth		= 32,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.code		= MEDIA_BUS_FMT_SBGGR8_1X8,
		.depth		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.code		= MEDIA_BUS_FMT_SGBRG8_1X8,
		.depth		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.code		= MEDIA_BUS_FMT_SGRBG8_1X8,
		.depth		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.code		= MEDIA_BUS_FMT_SRGGB8_1X8,
		.depth		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
		.depth		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
		.depth		= 16,
	},
};

/*  Print Four-character-code (FOURCC) */
static char *fourcc_to_str(u32 fmt)
{
	static char code[5];

	code[0] = (unsigned char)(fmt & 0xff);
	code[1] = (unsigned char)((fmt >> 8) & 0xff);
	code[2] = (unsigned char)((fmt >> 16) & 0xff);
	code[3] = (unsigned char)((fmt >> 24) & 0xff);
	code[4] = '\0';

	return code;
}

/* buffer for one video frame */
struct cal_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
	const struct cal_fmt	*fmt;
};

struct cal_dmaqueue {
	struct list_head	active;

	/* Counters to control fps rate */
	int			frame;
	int			ini_jiffies;
};

struct cm_data {
	void __iomem		*base;
	struct resource		*res;

	unsigned int		camerrx_control;

	struct platform_device *pdev;
};

struct cc_data {
	void __iomem		*base;
	struct resource		*res;

	struct platform_device *pdev;
};

/*
 * there is one cal_dev structure in the driver, it is shared by
 * all instances.
 */
struct cal_dev {
	int			irq;
	void __iomem		*base;
	struct resource		*res;
	struct platform_device	*pdev;
	struct v4l2_device	v4l2_dev;

	/* Control Module handle */
	struct cm_data		*cm;
	/* Camera Core Module handle */
	struct cc_data		*cc[CAL_NUM_CSI2_PORTS];

	struct cal_ctx		*ctx[CAL_NUM_CONTEXT];
};

/*
 * There is one cal_ctx structure for each camera core context.
 */
struct cal_ctx {
	struct v4l2_device	v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_device	vdev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev	*sensor;
	struct v4l2_fwnode_endpoint	endpoint;

	struct v4l2_async_subdev asd;

	struct v4l2_fh		fh;
	struct cal_dev		*dev;
	struct cc_data		*cc;

	/* v4l2_ioctl mutex */
	struct mutex		mutex;
	/* v4l2 buffers lock */
	spinlock_t		slock;

	/* Several counters */
	unsigned long		jiffies;

	struct cal_dmaqueue	vidq;

	/* Input Number */
	int			input;

	/* video capture */
	const struct cal_fmt	*fmt;
	/* Used to store current pixel format */
	struct v4l2_format		v_fmt;
	/* Used to store current mbus frame format */
	struct v4l2_mbus_framefmt	m_fmt;

	/* Current subdev enumerated format */
	struct cal_fmt		*active_fmt[ARRAY_SIZE(cal_formats)];
	int			num_active_fmt;

	struct v4l2_fract	timeperframe;
	unsigned int		sequence;
	unsigned int		external_rate;
	struct vb2_queue	vb_vidq;
	unsigned int		seq_count;
	unsigned int		csi2_port;
	unsigned int		virtual_channel;

	/* Pointer pointing to current v4l2_buffer */
	struct cal_buffer	*cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct cal_buffer	*next_frm;
};

static const struct cal_fmt *find_format_by_pix(struct cal_ctx *ctx,
						u32 pixelformat)
{
	const struct cal_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ctx->num_active_fmt; k++) {
		fmt = ctx->active_fmt[k];
		if (fmt->fourcc == pixelformat)
			return fmt;
	}

	return NULL;
}

static const struct cal_fmt *find_format_by_code(struct cal_ctx *ctx,
						 u32 code)
{
	const struct cal_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ctx->num_active_fmt; k++) {
		fmt = ctx->active_fmt[k];
		if (fmt->code == code)
			return fmt;
	}

	return NULL;
}

static inline struct cal_ctx *notifier_to_ctx(struct v4l2_async_notifier *n)
{
	return container_of(n, struct cal_ctx, notifier);
}

static inline int get_field(u32 value, u32 mask)
{
	return (value & mask) >> __ffs(mask);
}

static inline void set_field(u32 *valp, u32 field, u32 mask)
{
	u32 val = *valp;

	val &= ~mask;
	val |= (field << __ffs(mask)) & mask;
	*valp = val;
}

/*
 * Control Module block access
 */
static struct cm_data *cm_create(struct cal_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	struct cm_data *cm;

	cm = devm_kzalloc(&pdev->dev, sizeof(*cm), GFP_KERNEL);
	if (!cm)
		return ERR_PTR(-ENOMEM);

	cm->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"camerrx_control");
	cm->base = devm_ioremap_resource(&pdev->dev, cm->res);
	if (IS_ERR(cm->base)) {
		cal_err(dev, "failed to ioremap\n");
		return ERR_CAST(cm->base);
	}

	cal_dbg(1, dev, "ioresource %s at %pa - %pa\n",
		cm->res->name, &cm->res->start, &cm->res->end);

	return cm;
}

static void camerarx_phy_enable(struct cal_ctx *ctx)
{
	u32 val;

	if (!ctx->dev->cm->base) {
		ctx_err(ctx, "cm not mapped\n");
		return;
	}

	val = reg_read(ctx->dev->cm, CM_CTRL_CORE_CAMERRX_CONTROL);
	if (ctx->csi2_port == 1) {
		set_field(&val, 1, CM_CAMERRX_CTRL_CSI0_CTRLCLKEN_MASK);
		set_field(&val, 0, CM_CAMERRX_CTRL_CSI0_CAMMODE_MASK);
		/* enable all lanes by default */
		set_field(&val, 0xf, CM_CAMERRX_CTRL_CSI0_LANEENABLE_MASK);
		set_field(&val, 1, CM_CAMERRX_CTRL_CSI0_MODE_MASK);
	} else if (ctx->csi2_port == 2) {
		set_field(&val, 1, CM_CAMERRX_CTRL_CSI1_CTRLCLKEN_MASK);
		set_field(&val, 0, CM_CAMERRX_CTRL_CSI1_CAMMODE_MASK);
		/* enable all lanes by default */
		set_field(&val, 0x3, CM_CAMERRX_CTRL_CSI1_LANEENABLE_MASK);
		set_field(&val, 1, CM_CAMERRX_CTRL_CSI1_MODE_MASK);
	}
	reg_write(ctx->dev->cm, CM_CTRL_CORE_CAMERRX_CONTROL, val);
}

static void camerarx_phy_disable(struct cal_ctx *ctx)
{
	u32 val;

	if (!ctx->dev->cm->base) {
		ctx_err(ctx, "cm not mapped\n");
		return;
	}

	val = reg_read(ctx->dev->cm, CM_CTRL_CORE_CAMERRX_CONTROL);
	if (ctx->csi2_port == 1)
		set_field(&val, 0x0, CM_CAMERRX_CTRL_CSI0_CTRLCLKEN_MASK);
	else if (ctx->csi2_port == 2)
		set_field(&val, 0x0, CM_CAMERRX_CTRL_CSI1_CTRLCLKEN_MASK);
	reg_write(ctx->dev->cm, CM_CTRL_CORE_CAMERRX_CONTROL, val);
}

/*
 * Camera Instance access block
 */
static struct cc_data *cc_create(struct cal_dev *dev, unsigned int core)
{
	struct platform_device *pdev = dev->pdev;
	struct cc_data *cc;

	cc = devm_kzalloc(&pdev->dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return ERR_PTR(-ENOMEM);

	cc->res = platform_get_resource_byname(pdev,
					       IORESOURCE_MEM,
					       (core == 0) ?
						"cal_rx_core0" :
						"cal_rx_core1");
	cc->base = devm_ioremap_resource(&pdev->dev, cc->res);
	if (IS_ERR(cc->base)) {
		cal_err(dev, "failed to ioremap\n");
		return ERR_CAST(cc->base);
	}

	cal_dbg(1, dev, "ioresource %s at %pa - %pa\n",
		cc->res->name, &cc->res->start, &cc->res->end);

	return cc;
}

/*
 * Get Revision and HW info
 */
static void cal_get_hwinfo(struct cal_dev *dev)
{
	u32 revision = 0;
	u32 hwinfo = 0;

	revision = reg_read(dev, CAL_HL_REVISION);
	cal_dbg(3, dev, "CAL_HL_REVISION = 0x%08x (expecting 0x40000200)\n",
		revision);

	hwinfo = reg_read(dev, CAL_HL_HWINFO);
	cal_dbg(3, dev, "CAL_HL_HWINFO = 0x%08x (expecting 0xA3C90469)\n",
		hwinfo);
}

static inline int cal_runtime_get(struct cal_dev *dev)
{
	return pm_runtime_get_sync(&dev->pdev->dev);
}

static inline void cal_runtime_put(struct cal_dev *dev)
{
	pm_runtime_put_sync(&dev->pdev->dev);
}

static void cal_quickdump_regs(struct cal_dev *dev)
{
	cal_info(dev, "CAL Registers @ 0x%pa:\n", &dev->res->start);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
		       (__force const void *)dev->base,
		       resource_size(dev->res), false);

	if (dev->ctx[0]) {
		cal_info(dev, "CSI2 Core 0 Registers @ %pa:\n",
			 &dev->ctx[0]->cc->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)dev->ctx[0]->cc->base,
			       resource_size(dev->ctx[0]->cc->res),
			       false);
	}

	if (dev->ctx[1]) {
		cal_info(dev, "CSI2 Core 1 Registers @ %pa:\n",
			 &dev->ctx[1]->cc->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)dev->ctx[1]->cc->base,
			       resource_size(dev->ctx[1]->cc->res),
			       false);
	}

	cal_info(dev, "CAMERRX_Control Registers @ %pa:\n",
		 &dev->cm->res->start);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
		       (__force const void *)dev->cm->base,
		       resource_size(dev->cm->res), false);
}

/*
 * Enable the expected IRQ sources
 */
static void enable_irqs(struct cal_ctx *ctx)
{
	/* Enable IRQ_WDMA_END 0/1 */
	reg_write_field(ctx->dev,
			CAL_HL_IRQENABLE_SET(2),
			CAL_HL_IRQ_ENABLE,
			CAL_HL_IRQ_MASK(ctx->csi2_port));
	/* Enable IRQ_WDMA_START 0/1 */
	reg_write_field(ctx->dev,
			CAL_HL_IRQENABLE_SET(3),
			CAL_HL_IRQ_ENABLE,
			CAL_HL_IRQ_MASK(ctx->csi2_port));
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	reg_write(ctx->dev, CAL_CSI2_VC_IRQENABLE(1), 0xFF000000);
}

static void disable_irqs(struct cal_ctx *ctx)
{
	/* Disable IRQ_WDMA_END 0/1 */
	reg_write_field(ctx->dev,
			CAL_HL_IRQENABLE_CLR(2),
			CAL_HL_IRQ_CLEAR,
			CAL_HL_IRQ_MASK(ctx->csi2_port));
	/* Disable IRQ_WDMA_START 0/1 */
	reg_write_field(ctx->dev,
			CAL_HL_IRQENABLE_CLR(3),
			CAL_HL_IRQ_CLEAR,
			CAL_HL_IRQ_MASK(ctx->csi2_port));
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	reg_write(ctx->dev, CAL_CSI2_VC_IRQENABLE(1), 0);
}

static void csi2_init(struct cal_ctx *ctx)
{
	int i;
	u32 val;

	val = reg_read(ctx->dev, CAL_CSI2_TIMING(ctx->csi2_port));
	set_field(&val, CAL_GEN_ENABLE,
		  CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK);
	set_field(&val, CAL_GEN_ENABLE,
		  CAL_CSI2_TIMING_STOP_STATE_X16_IO1_MASK);
	set_field(&val, CAL_GEN_DISABLE,
		  CAL_CSI2_TIMING_STOP_STATE_X4_IO1_MASK);
	set_field(&val, 407, CAL_CSI2_TIMING_STOP_STATE_COUNTER_IO1_MASK);
	reg_write(ctx->dev, CAL_CSI2_TIMING(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_CSI2_TIMING(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_CSI2_TIMING(ctx->csi2_port)));

	val = reg_read(ctx->dev, CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port));
	set_field(&val, CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_OPERATIONAL,
		  CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);
	set_field(&val, CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_STATE_ON,
		  CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_MASK);
	reg_write(ctx->dev, CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port), val);
	for (i = 0; i < 10; i++) {
		if (reg_read_field(ctx->dev,
				   CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port),
				   CAL_CSI2_COMPLEXIO_CFG_PWR_STATUS_MASK) ==
		    CAL_CSI2_COMPLEXIO_CFG_PWR_STATUS_STATE_ON)
			break;
		usleep_range(1000, 1100);
	}
	ctx_dbg(3, ctx, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port)));

	val = reg_read(ctx->dev, CAL_CTRL);
	set_field(&val, CAL_CTRL_BURSTSIZE_BURST128, CAL_CTRL_BURSTSIZE_MASK);
	set_field(&val, 0xF, CAL_CTRL_TAGCNT_MASK);
	set_field(&val, CAL_CTRL_POSTED_WRITES_NONPOSTED,
		  CAL_CTRL_POSTED_WRITES_MASK);
	set_field(&val, 0xFF, CAL_CTRL_MFLAGL_MASK);
	set_field(&val, 0xFF, CAL_CTRL_MFLAGH_MASK);
	reg_write(ctx->dev, CAL_CTRL, val);
	ctx_dbg(3, ctx, "CAL_CTRL = 0x%08x\n", reg_read(ctx->dev, CAL_CTRL));
}

static void csi2_lane_config(struct cal_ctx *ctx)
{
	u32 val = reg_read(ctx->dev, CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port));
	u32 lane_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK;
	u32 polarity_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POL_MASK;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2 =
		&ctx->endpoint.bus.mipi_csi2;
	int lane;

	set_field(&val, mipi_csi2->clock_lane + 1, lane_mask);
	set_field(&val, mipi_csi2->lane_polarities[0], polarity_mask);
	for (lane = 0; lane < mipi_csi2->num_data_lanes; lane++) {
		/*
		 * Every lane are one nibble apart starting with the
		 * clock followed by the data lanes so shift masks by 4.
		 */
		lane_mask <<= 4;
		polarity_mask <<= 4;
		set_field(&val, mipi_csi2->data_lanes[lane] + 1, lane_mask);
		set_field(&val, mipi_csi2->lane_polarities[lane + 1],
			  polarity_mask);
	}

	reg_write(ctx->dev, CAL_CSI2_COMPLEXIO_CFG(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x\n",
		ctx->csi2_port, val);
}

static void csi2_ppi_enable(struct cal_ctx *ctx)
{
	reg_write_field(ctx->dev, CAL_CSI2_PPI_CTRL(ctx->csi2_port),
			CAL_GEN_ENABLE, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static void csi2_ppi_disable(struct cal_ctx *ctx)
{
	reg_write_field(ctx->dev, CAL_CSI2_PPI_CTRL(ctx->csi2_port),
			CAL_GEN_DISABLE, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static void csi2_ctx_config(struct cal_ctx *ctx)
{
	u32 val;

	val = reg_read(ctx->dev, CAL_CSI2_CTX0(ctx->csi2_port));
	set_field(&val, ctx->csi2_port, CAL_CSI2_CTX_CPORT_MASK);
	/*
	 * DT type: MIPI CSI-2 Specs
	 *   0x1: All - DT filter is disabled
	 *  0x24: RGB888 1 pixel  = 3 bytes
	 *  0x2B: RAW10  4 pixels = 5 bytes
	 *  0x2A: RAW8   1 pixel  = 1 byte
	 *  0x1E: YUV422 2 pixels = 4 bytes
	 */
	set_field(&val, 0x1, CAL_CSI2_CTX_DT_MASK);
	/* Virtual Channel from the CSI2 sensor usually 0! */
	set_field(&val, ctx->virtual_channel, CAL_CSI2_CTX_VC_MASK);
	/* NUM_LINES_PER_FRAME => 0 means auto detect */
	set_field(&val, 0, CAL_CSI2_CTX_LINES_MASK);
	set_field(&val, CAL_CSI2_CTX_ATT_PIX, CAL_CSI2_CTX_ATT_MASK);
	set_field(&val, CAL_CSI2_CTX_PACK_MODE_LINE,
		  CAL_CSI2_CTX_PACK_MODE_MASK);
	reg_write(ctx->dev, CAL_CSI2_CTX0(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_CSI2_CTX0(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_CSI2_CTX0(ctx->csi2_port)));
}

static void pix_proc_config(struct cal_ctx *ctx)
{
	u32 val;

	val = reg_read(ctx->dev, CAL_PIX_PROC(ctx->csi2_port));
	set_field(&val, CAL_PIX_PROC_EXTRACT_B8, CAL_PIX_PROC_EXTRACT_MASK);
	set_field(&val, CAL_PIX_PROC_DPCMD_BYPASS, CAL_PIX_PROC_DPCMD_MASK);
	set_field(&val, CAL_PIX_PROC_DPCME_BYPASS, CAL_PIX_PROC_DPCME_MASK);
	set_field(&val, CAL_PIX_PROC_PACK_B8, CAL_PIX_PROC_PACK_MASK);
	set_field(&val, ctx->csi2_port, CAL_PIX_PROC_CPORT_MASK);
	set_field(&val, CAL_GEN_ENABLE, CAL_PIX_PROC_EN_MASK);
	reg_write(ctx->dev, CAL_PIX_PROC(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_PIX_PROC(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_PIX_PROC(ctx->csi2_port)));
}

static void cal_wr_dma_config(struct cal_ctx *ctx,
			      unsigned int width)
{
	u32 val;

	val = reg_read(ctx->dev, CAL_WR_DMA_CTRL(ctx->csi2_port));
	set_field(&val, ctx->csi2_port, CAL_WR_DMA_CTRL_CPORT_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_DTAG_PIX_DAT,
		  CAL_WR_DMA_CTRL_DTAG_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_MODE_CONST,
		  CAL_WR_DMA_CTRL_MODE_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_PATTERN_LINEAR,
		  CAL_WR_DMA_CTRL_PATTERN_MASK);
	set_field(&val, CAL_GEN_ENABLE, CAL_WR_DMA_CTRL_STALL_RD_MASK);
	reg_write(ctx->dev, CAL_WR_DMA_CTRL(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_CTRL(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_WR_DMA_CTRL(ctx->csi2_port)));

	/*
	 * width/16 not sure but giving it a whirl.
	 * zero does not work right
	 */
	reg_write_field(ctx->dev,
			CAL_WR_DMA_OFST(ctx->csi2_port),
			(width / 16),
			CAL_WR_DMA_OFST_MASK);
	ctx_dbg(3, ctx, "CAL_WR_DMA_OFST(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_WR_DMA_OFST(ctx->csi2_port)));

	val = reg_read(ctx->dev, CAL_WR_DMA_XSIZE(ctx->csi2_port));
	/* 64 bit word means no skipping */
	set_field(&val, 0, CAL_WR_DMA_XSIZE_XSKIP_MASK);
	/*
	 * (width*8)/64 this should be size of an entire line
	 * in 64bit word but 0 means all data until the end
	 * is detected automagically
	 */
	set_field(&val, (width / 8), CAL_WR_DMA_XSIZE_MASK);
	reg_write(ctx->dev, CAL_WR_DMA_XSIZE(ctx->csi2_port), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_XSIZE(%d) = 0x%08x\n", ctx->csi2_port,
		reg_read(ctx->dev, CAL_WR_DMA_XSIZE(ctx->csi2_port)));
}

static void cal_wr_dma_addr(struct cal_ctx *ctx, unsigned int dmaaddr)
{
	reg_write(ctx->dev, CAL_WR_DMA_ADDR(ctx->csi2_port), dmaaddr);
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14
#define THS_SETTLE	15

static void csi2_phy_config(struct cal_ctx *ctx)
{
	unsigned int reg0, reg1;
	unsigned int ths_term, ths_settle;
	unsigned int ddrclkperiod_us;

	/*
	 * THS_TERM: Programmed value = floor(20 ns/DDRClk period) - 2.
	 */
	ddrclkperiod_us = ctx->external_rate / 2000000;
	ddrclkperiod_us = 1000000 / ddrclkperiod_us;
	ctx_dbg(1, ctx, "ddrclkperiod_us: %d\n", ddrclkperiod_us);

	ths_term = 20000 / ddrclkperiod_us;
	ths_term = (ths_term >= 2) ? ths_term - 2 : ths_term;
	ctx_dbg(1, ctx, "ths_term: %d (0x%02x)\n", ths_term, ths_term);

	/*
	 * THS_SETTLE: Programmed value = floor(176.3 ns/CtrlClk period) - 1.
	 *	Since CtrlClk is fixed at 96Mhz then we get
	 *	ths_settle = floor(176.3 / 10.416) - 1 = 15
	 * If we ever switch to a dynamic clock then this code might be useful
	 *
	 * unsigned int ctrlclkperiod_us;
	 * ctrlclkperiod_us = 96000000 / 1000000;
	 * ctrlclkperiod_us = 1000000 / ctrlclkperiod_us;
	 * ctx_dbg(1, ctx, "ctrlclkperiod_us: %d\n", ctrlclkperiod_us);

	 * ths_settle = 176300  / ctrlclkperiod_us;
	 * ths_settle = (ths_settle > 1) ? ths_settle - 1 : ths_settle;
	 */

	ths_settle = THS_SETTLE;
	ctx_dbg(1, ctx, "ths_settle: %d (0x%02x)\n", ths_settle, ths_settle);

	reg0 = reg_read(ctx->cc, CAL_CSI2_PHY_REG0);
	set_field(&reg0, CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_DISABLE,
		  CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_MASK);
	set_field(&reg0, ths_term, CAL_CSI2_PHY_REG0_THS_TERM_MASK);
	set_field(&reg0, ths_settle, CAL_CSI2_PHY_REG0_THS_SETTLE_MASK);

	ctx_dbg(1, ctx, "CSI2_%d_REG0 = 0x%08x\n", (ctx->csi2_port - 1), reg0);
	reg_write(ctx->cc, CAL_CSI2_PHY_REG0, reg0);

	reg1 = reg_read(ctx->cc, CAL_CSI2_PHY_REG1);
	set_field(&reg1, TCLK_TERM, CAL_CSI2_PHY_REG1_TCLK_TERM_MASK);
	set_field(&reg1, 0xb8, CAL_CSI2_PHY_REG1_DPHY_HS_SYNC_PATTERN_MASK);
	set_field(&reg1, TCLK_MISS, CAL_CSI2_PHY_REG1_CTRLCLK_DIV_FACTOR_MASK);
	set_field(&reg1, TCLK_SETTLE, CAL_CSI2_PHY_REG1_TCLK_SETTLE_MASK);

	ctx_dbg(1, ctx, "CSI2_%d_REG1 = 0x%08x\n", (ctx->csi2_port - 1), reg1);
	reg_write(ctx->cc, CAL_CSI2_PHY_REG1, reg1);
}

static int cal_get_external_info(struct cal_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;

	if (!ctx->sensor)
		return -ENODEV;

	ctrl = v4l2_ctrl_find(ctx->sensor->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		ctx_err(ctx, "no pixel rate control in subdev: %s\n",
			ctx->sensor->name);
		return -EPIPE;
	}

	ctx->external_rate = v4l2_ctrl_g_ctrl_int64(ctrl);
	ctx_dbg(3, ctx, "sensor Pixel Rate: %d\n", ctx->external_rate);

	return 0;
}

static inline void cal_schedule_next_buffer(struct cal_ctx *ctx)
{
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf;
	unsigned long addr;

	buf = list_entry(dma_q->active.next, struct cal_buffer, list);
	ctx->next_frm = buf;
	list_del(&buf->list);

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	cal_wr_dma_addr(ctx, addr);
}

static inline void cal_process_buffer_complete(struct cal_ctx *ctx)
{
	ctx->cur_frm->vb.vb2_buf.timestamp = ktime_get_ns();
	ctx->cur_frm->vb.field = ctx->m_fmt.field;
	ctx->cur_frm->vb.sequence = ctx->sequence++;

	vb2_buffer_done(&ctx->cur_frm->vb.vb2_buf, VB2_BUF_STATE_DONE);
	ctx->cur_frm = ctx->next_frm;
}

#define isvcirqset(irq, vc, ff) (irq & \
	(CAL_CSI2_VC_IRQENABLE_ ##ff ##_IRQ_##vc ##_MASK))

#define isportirqset(irq, port) (irq & CAL_HL_IRQ_MASK(port))

static irqreturn_t cal_irq(int irq_cal, void *data)
{
	struct cal_dev *dev = (struct cal_dev *)data;
	struct cal_ctx *ctx;
	struct cal_dmaqueue *dma_q;
	u32 irqst2, irqst3;

	/* Check which DMA just finished */
	irqst2 = reg_read(dev, CAL_HL_IRQSTATUS(2));
	if (irqst2) {
		/* Clear Interrupt status */
		reg_write(dev, CAL_HL_IRQSTATUS(2), irqst2);

		/* Need to check both port */
		if (isportirqset(irqst2, 1)) {
			ctx = dev->ctx[0];

			if (ctx->cur_frm != ctx->next_frm)
				cal_process_buffer_complete(ctx);
		}

		if (isportirqset(irqst2, 2)) {
			ctx = dev->ctx[1];

			if (ctx->cur_frm != ctx->next_frm)
				cal_process_buffer_complete(ctx);
		}
	}

	/* Check which DMA just started */
	irqst3 = reg_read(dev, CAL_HL_IRQSTATUS(3));
	if (irqst3) {
		/* Clear Interrupt status */
		reg_write(dev, CAL_HL_IRQSTATUS(3), irqst3);

		/* Need to check both port */
		if (isportirqset(irqst3, 1)) {
			ctx = dev->ctx[0];
			dma_q = &ctx->vidq;

			spin_lock(&ctx->slock);
			if (!list_empty(&dma_q->active) &&
			    ctx->cur_frm == ctx->next_frm)
				cal_schedule_next_buffer(ctx);
			spin_unlock(&ctx->slock);
		}

		if (isportirqset(irqst3, 2)) {
			ctx = dev->ctx[1];
			dma_q = &ctx->vidq;

			spin_lock(&ctx->slock);
			if (!list_empty(&dma_q->active) &&
			    ctx->cur_frm == ctx->next_frm)
				cal_schedule_next_buffer(ctx);
			spin_unlock(&ctx->slock);
		}
	}

	return IRQ_HANDLED;
}

/*
 * video ioctls
 */
static int cal_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct cal_ctx *ctx = video_drvdata(file);

	strscpy(cap->driver, CAL_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, CAL_MODULE_NAME, sizeof(cap->card));

	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", ctx->v4l2_dev.name);
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int cal_enum_fmt_vid_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_fmt *fmt = NULL;

	if (f->index >= ctx->num_active_fmt)
		return -EINVAL;

	fmt = ctx->active_fmt[f->index];

	f->pixelformat = fmt->fourcc;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	return 0;
}

static int __subdev_get_format(struct cal_ctx *ctx,
			       struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_mbus_framefmt *mbus_fmt = &sd_fmt.format;
	int ret;

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = 0;

	ret = v4l2_subdev_call(ctx->sensor, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	*fmt = *mbus_fmt;

	ctx_dbg(1, ctx, "%s %dx%d code:%04X\n", __func__,
		fmt->width, fmt->height, fmt->code);

	return 0;
}

static int __subdev_set_format(struct cal_ctx *ctx,
			       struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_mbus_framefmt *mbus_fmt = &sd_fmt.format;
	int ret;

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = 0;
	*mbus_fmt = *fmt;

	ret = v4l2_subdev_call(ctx->sensor, pad, set_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	ctx_dbg(1, ctx, "%s %dx%d code:%04X\n", __func__,
		fmt->width, fmt->height, fmt->code);

	return 0;
}

static int cal_calc_format_size(struct cal_ctx *ctx,
				const struct cal_fmt *fmt,
				struct v4l2_format *f)
{
	if (!fmt) {
		ctx_dbg(3, ctx, "No cal_fmt provided!\n");
		return -EINVAL;
	}

	v4l_bound_align_image(&f->fmt.pix.width, 48, MAX_WIDTH, 2,
			      &f->fmt.pix.height, 32, MAX_HEIGHT, 0, 0);
	f->fmt.pix.bytesperline = bytes_per_line(f->fmt.pix.width,
						 fmt->depth >> 3);
	f->fmt.pix.sizeimage = f->fmt.pix.height *
			       f->fmt.pix.bytesperline;

	ctx_dbg(3, ctx, "%s: fourcc: %s size: %dx%d bpl:%d img_size:%d\n",
		__func__, fourcc_to_str(f->fmt.pix.pixelformat),
		f->fmt.pix.width, f->fmt.pix.height,
		f->fmt.pix.bytesperline, f->fmt.pix.sizeimage);

	return 0;
}

static int cal_g_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);

	*f = ctx->v_fmt;

	return 0;
}

static int cal_try_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_fmt *fmt;
	struct v4l2_subdev_frame_size_enum fse;
	int ret, found;

	fmt = find_format_by_pix(ctx, f->fmt.pix.pixelformat);
	if (!fmt) {
		ctx_dbg(3, ctx, "Fourcc format (0x%08x) not found.\n",
			f->fmt.pix.pixelformat);

		/* Just get the first one enumerated */
		fmt = ctx->active_fmt[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	f->fmt.pix.field = ctx->v_fmt.fmt.pix.field;

	/* check for/find a valid width/height */
	ret = 0;
	found = false;
	fse.pad = 0;
	fse.code = fmt->code;
	fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	for (fse.index = 0; ; fse.index++) {
		ret = v4l2_subdev_call(ctx->sensor, pad, enum_frame_size,
				       NULL, &fse);
		if (ret)
			break;

		if ((f->fmt.pix.width == fse.max_width) &&
		    (f->fmt.pix.height == fse.max_height)) {
			found = true;
			break;
		} else if ((f->fmt.pix.width >= fse.min_width) &&
			 (f->fmt.pix.width <= fse.max_width) &&
			 (f->fmt.pix.height >= fse.min_height) &&
			 (f->fmt.pix.height <= fse.max_height)) {
			found = true;
			break;
		}
	}

	if (!found) {
		/* use existing values as default */
		f->fmt.pix.width = ctx->v_fmt.fmt.pix.width;
		f->fmt.pix.height =  ctx->v_fmt.fmt.pix.height;
	}

	/*
	 * Use current colorspace for now, it will get
	 * updated properly during s_fmt
	 */
	f->fmt.pix.colorspace = ctx->v_fmt.fmt.pix.colorspace;
	return cal_calc_format_size(ctx, fmt, f);
}

static int cal_s_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	struct vb2_queue *q = &ctx->vb_vidq;
	const struct cal_fmt *fmt;
	struct v4l2_mbus_framefmt mbus_fmt;
	int ret;

	if (vb2_is_busy(q)) {
		ctx_dbg(3, ctx, "%s device busy\n", __func__);
		return -EBUSY;
	}

	ret = cal_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	fmt = find_format_by_pix(ctx, f->fmt.pix.pixelformat);

	v4l2_fill_mbus_format(&mbus_fmt, &f->fmt.pix, fmt->code);

	ret = __subdev_set_format(ctx, &mbus_fmt);
	if (ret)
		return ret;

	/* Just double check nothing has gone wrong */
	if (mbus_fmt.code != fmt->code) {
		ctx_dbg(3, ctx,
			"%s subdev changed format on us, this should not happen\n",
			__func__);
		return -EINVAL;
	}

	v4l2_fill_pix_format(&ctx->v_fmt.fmt.pix, &mbus_fmt);
	ctx->v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ctx->v_fmt.fmt.pix.pixelformat  = fmt->fourcc;
	cal_calc_format_size(ctx, fmt, &ctx->v_fmt);
	ctx->fmt = fmt;
	ctx->m_fmt = mbus_fmt;
	*f = ctx->v_fmt;

	return 0;
}

static int cal_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_fmt *fmt;
	struct v4l2_subdev_frame_size_enum fse;
	int ret;

	/* check for valid format */
	fmt = find_format_by_pix(ctx, fsize->pixel_format);
	if (!fmt) {
		ctx_dbg(3, ctx, "Invalid pixel code: %x\n",
			fsize->pixel_format);
		return -EINVAL;
	}

	fse.index = fsize->index;
	fse.pad = 0;
	fse.code = fmt->code;

	ret = v4l2_subdev_call(ctx->sensor, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	ctx_dbg(1, ctx, "%s: index: %d code: %x W:[%d,%d] H:[%d,%d]\n",
		__func__, fse.index, fse.code, fse.min_width, fse.max_width,
		fse.min_height, fse.max_height);

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.max_width;
	fsize->discrete.height = fse.max_height;

	return 0;
}

static int cal_enum_input(struct file *file, void *priv,
			  struct v4l2_input *inp)
{
	if (inp->index >= CAL_NUM_INPUT)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

static int cal_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct cal_ctx *ctx = video_drvdata(file);

	*i = ctx->input;
	return 0;
}

static int cal_s_input(struct file *file, void *priv, unsigned int i)
{
	struct cal_ctx *ctx = video_drvdata(file);

	if (i >= CAL_NUM_INPUT)
		return -EINVAL;

	ctx->input = i;
	return 0;
}

/* timeperframe is arbitrary and continuous */
static int cal_enum_frameintervals(struct file *file, void *priv,
				   struct v4l2_frmivalenum *fival)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_fmt *fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = find_format_by_pix(ctx, fival->pixel_format);
	if (!fmt)
		return -EINVAL;

	fie.code = fmt->code;
	ret = v4l2_subdev_call(ctx->sensor, pad, enum_frame_interval,
			       NULL, &fie);
	if (ret)
		return ret;
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

/*
 * Videobuf operations
 */
static int cal_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	unsigned size = ctx->v_fmt.fmt.pix.sizeimage;

	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		size = sizes[0];
	}

	*nplanes = 1;
	sizes[0] = size;

	ctx_dbg(3, ctx, "nbuffers=%d, size=%d\n", *nbuffers, sizes[0]);

	return 0;
}

static int cal_buffer_prepare(struct vb2_buffer *vb)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct cal_buffer *buf = container_of(vb, struct cal_buffer,
					      vb.vb2_buf);
	unsigned long size;

	if (WARN_ON(!ctx->fmt))
		return -EINVAL;

	size = ctx->v_fmt.fmt.pix.sizeimage;
	if (vb2_plane_size(vb, 0) < size) {
		ctx_err(ctx,
			"data will not fit into plane (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, size);
	return 0;
}

static void cal_buffer_queue(struct vb2_buffer *vb)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct cal_buffer *buf = container_of(vb, struct cal_buffer,
					      vb.vb2_buf);
	struct cal_dmaqueue *vidq = &ctx->vidq;
	unsigned long flags = 0;

	/* recheck locking */
	spin_lock_irqsave(&ctx->slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static int cal_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf, *tmp;
	unsigned long addr = 0;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctx->slock, flags);
	if (list_empty(&dma_q->active)) {
		spin_unlock_irqrestore(&ctx->slock, flags);
		ctx_dbg(3, ctx, "buffer queue is empty\n");
		return -EIO;
	}

	buf = list_entry(dma_q->active.next, struct cal_buffer, list);
	ctx->cur_frm = buf;
	ctx->next_frm = buf;
	list_del(&buf->list);
	spin_unlock_irqrestore(&ctx->slock, flags);

	addr = vb2_dma_contig_plane_dma_addr(&ctx->cur_frm->vb.vb2_buf, 0);
	ctx->sequence = 0;

	ret = cal_get_external_info(ctx);
	if (ret < 0)
		goto err;

	cal_runtime_get(ctx->dev);

	enable_irqs(ctx);
	camerarx_phy_enable(ctx);
	csi2_init(ctx);
	csi2_phy_config(ctx);
	csi2_lane_config(ctx);
	csi2_ctx_config(ctx);
	pix_proc_config(ctx);
	cal_wr_dma_config(ctx, ctx->v_fmt.fmt.pix.bytesperline);
	cal_wr_dma_addr(ctx, addr);
	csi2_ppi_enable(ctx);

	ret = v4l2_subdev_call(ctx->sensor, video, s_stream, 1);
	if (ret) {
		ctx_err(ctx, "stream on failed in subdev\n");
		cal_runtime_put(ctx->dev);
		goto err;
	}

	if (debug >= 4)
		cal_quickdump_regs(ctx->dev);

	return 0;

err:
	list_for_each_entry_safe(buf, tmp, &dma_q->active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	return ret;
}

static void cal_stop_streaming(struct vb2_queue *vq)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf, *tmp;
	unsigned long flags;

	if (v4l2_subdev_call(ctx->sensor, video, s_stream, 0))
		ctx_err(ctx, "stream off failed in subdev\n");

	csi2_ppi_disable(ctx);
	disable_irqs(ctx);

	/* Release all active buffers */
	spin_lock_irqsave(&ctx->slock, flags);
	list_for_each_entry_safe(buf, tmp, &dma_q->active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	if (ctx->cur_frm == ctx->next_frm) {
		vb2_buffer_done(&ctx->cur_frm->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		vb2_buffer_done(&ctx->cur_frm->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		vb2_buffer_done(&ctx->next_frm->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
	}
	ctx->cur_frm = NULL;
	ctx->next_frm = NULL;
	spin_unlock_irqrestore(&ctx->slock, flags);

	cal_runtime_put(ctx->dev);
}

static const struct vb2_ops cal_video_qops = {
	.queue_setup		= cal_queue_setup,
	.buf_prepare		= cal_buffer_prepare,
	.buf_queue		= cal_buffer_queue,
	.start_streaming	= cal_start_streaming,
	.stop_streaming		= cal_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static const struct v4l2_file_operations cal_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops cal_ioctl_ops = {
	.vidioc_querycap      = cal_querycap,
	.vidioc_enum_fmt_vid_cap  = cal_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = cal_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = cal_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = cal_s_fmt_vid_cap,
	.vidioc_enum_framesizes   = cal_enum_framesizes,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_enum_input    = cal_enum_input,
	.vidioc_g_input       = cal_g_input,
	.vidioc_s_input       = cal_s_input,
	.vidioc_enum_frameintervals = cal_enum_frameintervals,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_log_status    = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device cal_videodev = {
	.name		= CAL_MODULE_NAME,
	.fops		= &cal_fops,
	.ioctl_ops	= &cal_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
};

/* -----------------------------------------------------------------
 *	Initialization and module stuff
 * ------------------------------------------------------------------
 */
static int cal_complete_ctx(struct cal_ctx *ctx);

static int cal_async_bound(struct v4l2_async_notifier *notifier,
			   struct v4l2_subdev *subdev,
			   struct v4l2_async_subdev *asd)
{
	struct cal_ctx *ctx = notifier_to_ctx(notifier);
	struct v4l2_subdev_mbus_code_enum mbus_code;
	int ret = 0;
	int i, j, k;

	if (ctx->sensor) {
		ctx_info(ctx, "Rejecting subdev %s (Already set!!)",
			 subdev->name);
		return 0;
	}

	ctx->sensor = subdev;
	ctx_dbg(1, ctx, "Using sensor %s for capture\n", subdev->name);

	/* Enumerate sub device formats and enable all matching local formats */
	ctx->num_active_fmt = 0;
	for (j = 0, i = 0; ret != -EINVAL; ++j) {
		struct cal_fmt *fmt;

		memset(&mbus_code, 0, sizeof(mbus_code));
		mbus_code.index = j;
		ret = v4l2_subdev_call(subdev, pad, enum_mbus_code,
				       NULL, &mbus_code);
		if (ret)
			continue;

		ctx_dbg(2, ctx,
			"subdev %s: code: %04x idx: %d\n",
			subdev->name, mbus_code.code, j);

		for (k = 0; k < ARRAY_SIZE(cal_formats); k++) {
			fmt = &cal_formats[k];

			if (mbus_code.code == fmt->code) {
				ctx->active_fmt[i] = fmt;
				ctx_dbg(2, ctx,
					"matched fourcc: %s: code: %04x idx: %d\n",
					fourcc_to_str(fmt->fourcc),
					fmt->code, i);
				ctx->num_active_fmt = ++i;
			}
		}
	}

	if (i == 0) {
		ctx_err(ctx, "No suitable format reported by subdev %s\n",
			subdev->name);
		return -EINVAL;
	}

	cal_complete_ctx(ctx);

	return 0;
}

static int cal_async_complete(struct v4l2_async_notifier *notifier)
{
	struct cal_ctx *ctx = notifier_to_ctx(notifier);
	const struct cal_fmt *fmt;
	struct v4l2_mbus_framefmt mbus_fmt;
	int ret;

	ret = __subdev_get_format(ctx, &mbus_fmt);
	if (ret)
		return ret;

	fmt = find_format_by_code(ctx, mbus_fmt.code);
	if (!fmt) {
		ctx_dbg(3, ctx, "mbus code format (0x%08x) not found.\n",
			mbus_fmt.code);
		return -EINVAL;
	}

	/* Save current subdev format */
	v4l2_fill_pix_format(&ctx->v_fmt.fmt.pix, &mbus_fmt);
	ctx->v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ctx->v_fmt.fmt.pix.pixelformat  = fmt->fourcc;
	cal_calc_format_size(ctx, fmt, &ctx->v_fmt);
	ctx->fmt = fmt;
	ctx->m_fmt = mbus_fmt;

	return 0;
}

static const struct v4l2_async_notifier_operations cal_async_ops = {
	.bound = cal_async_bound,
	.complete = cal_async_complete,
};

static int cal_complete_ctx(struct cal_ctx *ctx)
{
	struct video_device *vfd;
	struct vb2_queue *q;
	int ret;

	ctx->timeperframe = tpf_default;
	ctx->external_rate = 192000000;

	/* initialize locks */
	spin_lock_init(&ctx->slock);
	mutex_init(&ctx->mutex);

	/* initialize queue */
	q = &ctx->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = ctx;
	q->buf_struct_size = sizeof(struct cal_buffer);
	q->ops = &cal_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &ctx->mutex;
	q->min_buffers_needed = 3;
	q->dev = ctx->v4l2_dev.dev;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	/* init video dma queues */
	INIT_LIST_HEAD(&ctx->vidq.active);

	vfd = &ctx->vdev;
	*vfd = cal_videodev;
	vfd->v4l2_dev = &ctx->v4l2_dev;
	vfd->queue = q;

	/*
	 * Provide a mutex to v4l2 core. It will be used to protect
	 * all fops and v4l2 ioctls.
	 */
	vfd->lock = &ctx->mutex;
	video_set_drvdata(vfd, ctx);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		return ret;

	v4l2_info(&ctx->v4l2_dev, "V4L2 device registered as %s\n",
		  video_device_node_name(vfd));

	return 0;
}

static struct device_node *
of_get_next_port(const struct device_node *parent,
		 struct device_node *prev)
{
	struct device_node *port = NULL;

	if (!parent)
		return NULL;

	if (!prev) {
		struct device_node *ports;
		/*
		 * It's the first call, we have to find a port subnode
		 * within this node or within an optional 'ports' node.
		 */
		ports = of_get_child_by_name(parent, "ports");
		if (ports)
			parent = ports;

		port = of_get_child_by_name(parent, "port");

		/* release the 'ports' node */
		of_node_put(ports);
	} else {
		struct device_node *ports;

		ports = of_get_parent(prev);
		if (!ports)
			return NULL;

		do {
			port = of_get_next_child(ports, prev);
			if (!port) {
				of_node_put(ports);
				return NULL;
			}
			prev = port;
		} while (!of_node_name_eq(port, "port"));
	}

	return port;
}

static struct device_node *
of_get_next_endpoint(const struct device_node *parent,
		     struct device_node *prev)
{
	struct device_node *ep = NULL;

	if (!parent)
		return NULL;

	do {
		ep = of_get_next_child(parent, prev);
		if (!ep)
			return NULL;
		prev = ep;
	} while (!of_node_name_eq(ep, "endpoint"));

	return ep;
}

static int of_cal_create_instance(struct cal_ctx *ctx, int inst)
{
	struct platform_device *pdev = ctx->dev->pdev;
	struct device_node *ep_node, *port, *remote_ep,
			*sensor_node, *parent;
	struct v4l2_fwnode_endpoint *endpoint;
	struct v4l2_async_subdev *asd;
	u32 regval = 0;
	int ret, index, found_port = 0, lane;

	parent = pdev->dev.of_node;

	asd = &ctx->asd;
	endpoint = &ctx->endpoint;

	ep_node = NULL;
	port = NULL;
	remote_ep = NULL;
	sensor_node = NULL;
	ret = -EINVAL;

	ctx_dbg(3, ctx, "Scanning Port node for csi2 port: %d\n", inst);
	for (index = 0; index < CAL_NUM_CSI2_PORTS; index++) {
		port = of_get_next_port(parent, port);
		if (!port) {
			ctx_dbg(1, ctx, "No port node found for csi2 port:%d\n",
				index);
			goto cleanup_exit;
		}

		/* Match the slice number with <REG> */
		of_property_read_u32(port, "reg", &regval);
		ctx_dbg(3, ctx, "port:%d inst:%d <reg>:%d\n",
			index, inst, regval);
		if ((regval == inst) && (index == inst)) {
			found_port = 1;
			break;
		}
	}

	if (!found_port) {
		ctx_dbg(1, ctx, "No port node matches csi2 port:%d\n",
			inst);
		goto cleanup_exit;
	}

	ctx_dbg(3, ctx, "Scanning sub-device for csi2 port: %d\n",
		inst);

	ep_node = of_get_next_endpoint(port, ep_node);
	if (!ep_node) {
		ctx_dbg(3, ctx, "can't get next endpoint\n");
		goto cleanup_exit;
	}

	sensor_node = of_graph_get_remote_port_parent(ep_node);
	if (!sensor_node) {
		ctx_dbg(3, ctx, "can't get remote parent\n");
		goto cleanup_exit;
	}
	asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
	asd->match.fwnode = of_fwnode_handle(sensor_node);

	remote_ep = of_graph_get_remote_endpoint(ep_node);
	if (!remote_ep) {
		ctx_dbg(3, ctx, "can't get remote-endpoint\n");
		goto cleanup_exit;
	}
	v4l2_fwnode_endpoint_parse(of_fwnode_handle(remote_ep), endpoint);

	if (endpoint->bus_type != V4L2_MBUS_CSI2_DPHY) {
		ctx_err(ctx, "Port:%d sub-device %pOFn is not a CSI2 device\n",
			inst, sensor_node);
		goto cleanup_exit;
	}

	/* Store Virtual Channel number */
	ctx->virtual_channel = endpoint->base.id;

	ctx_dbg(3, ctx, "Port:%d v4l2-endpoint: CSI2\n", inst);
	ctx_dbg(3, ctx, "Virtual Channel=%d\n", ctx->virtual_channel);
	ctx_dbg(3, ctx, "flags=0x%08x\n", endpoint->bus.mipi_csi2.flags);
	ctx_dbg(3, ctx, "clock_lane=%d\n", endpoint->bus.mipi_csi2.clock_lane);
	ctx_dbg(3, ctx, "num_data_lanes=%d\n",
		endpoint->bus.mipi_csi2.num_data_lanes);
	ctx_dbg(3, ctx, "data_lanes= <\n");
	for (lane = 0; lane < endpoint->bus.mipi_csi2.num_data_lanes; lane++)
		ctx_dbg(3, ctx, "\t%d\n",
			endpoint->bus.mipi_csi2.data_lanes[lane]);
	ctx_dbg(3, ctx, "\t>\n");

	ctx_dbg(1, ctx, "Port: %d found sub-device %pOFn\n",
		inst, sensor_node);

	v4l2_async_notifier_init(&ctx->notifier);

	ret = v4l2_async_notifier_add_subdev(&ctx->notifier, asd);
	if (ret) {
		ctx_err(ctx, "Error adding asd\n");
		goto cleanup_exit;
	}

	ctx->notifier.ops = &cal_async_ops;
	ret = v4l2_async_notifier_register(&ctx->v4l2_dev,
					   &ctx->notifier);
	if (ret) {
		ctx_err(ctx, "Error registering async notifier\n");
		v4l2_async_notifier_cleanup(&ctx->notifier);
		ret = -EINVAL;
	}

	/*
	 * On success we need to keep reference on sensor_node, or
	 * if notifier_cleanup was called above, sensor_node was
	 * already put.
	 */
	sensor_node = NULL;

cleanup_exit:
	of_node_put(remote_ep);
	of_node_put(sensor_node);
	of_node_put(ep_node);
	of_node_put(port);

	return ret;
}

static struct cal_ctx *cal_create_instance(struct cal_dev *dev, int inst)
{
	struct cal_ctx *ctx;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	ctx = devm_kzalloc(&dev->pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	/* save the cal_dev * for future ref */
	ctx->dev = dev;

	snprintf(ctx->v4l2_dev.name, sizeof(ctx->v4l2_dev.name),
		 "%s-%03d", CAL_MODULE_NAME, inst);
	ret = v4l2_device_register(&dev->pdev->dev, &ctx->v4l2_dev);
	if (ret)
		goto err_exit;

	hdl = &ctx->ctrl_handler;
	ret = v4l2_ctrl_handler_init(hdl, 11);
	if (ret) {
		ctx_err(ctx, "Failed to init ctrl handler\n");
		goto unreg_dev;
	}
	ctx->v4l2_dev.ctrl_handler = hdl;

	/* Make sure Camera Core H/W register area is available */
	ctx->cc = dev->cc[inst];

	/* Store the instance id */
	ctx->csi2_port = inst + 1;

	ret = of_cal_create_instance(ctx, inst);
	if (ret) {
		ret = -EINVAL;
		goto free_hdl;
	}
	return ctx;

free_hdl:
	v4l2_ctrl_handler_free(hdl);
unreg_dev:
	v4l2_device_unregister(&ctx->v4l2_dev);
err_exit:
	return NULL;
}

static int cal_probe(struct platform_device *pdev)
{
	struct cal_dev *dev;
	struct cal_ctx *ctx;
	int ret;
	int irq;
	int i;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* set pseudo v4l2 device name so we can use v4l2_printk */
	strscpy(dev->v4l2_dev.name, CAL_MODULE_NAME,
		sizeof(dev->v4l2_dev.name));

	/* save pdev pointer */
	dev->pdev = pdev;

	dev->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"cal_top");
	dev->base = devm_ioremap_resource(&pdev->dev, dev->res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	cal_dbg(1, dev, "ioresource %s at %pa - %pa\n",
		dev->res->name, &dev->res->start, &dev->res->end);

	irq = platform_get_irq(pdev, 0);
	cal_dbg(1, dev, "got irq# %d\n", irq);
	ret = devm_request_irq(&pdev->dev, irq, cal_irq, 0, CAL_MODULE_NAME,
			       dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dev);

	dev->cm = cm_create(dev);
	if (IS_ERR(dev->cm))
		return PTR_ERR(dev->cm);

	dev->cc[0] = cc_create(dev, 0);
	if (IS_ERR(dev->cc[0]))
		return PTR_ERR(dev->cc[0]);

	dev->cc[1] = cc_create(dev, 1);
	if (IS_ERR(dev->cc[1]))
		return PTR_ERR(dev->cc[1]);

	dev->ctx[0] = NULL;
	dev->ctx[1] = NULL;

	dev->ctx[0] = cal_create_instance(dev, 0);
	dev->ctx[1] = cal_create_instance(dev, 1);
	if (!dev->ctx[0] && !dev->ctx[1]) {
		cal_err(dev, "Neither port is configured, no point in staying up\n");
		return -ENODEV;
	}

	pm_runtime_enable(&pdev->dev);

	ret = cal_runtime_get(dev);
	if (ret)
		goto runtime_disable;

	/* Just check we can actually access the module */
	cal_get_hwinfo(dev);

	cal_runtime_put(dev);

	return 0;

runtime_disable:
	pm_runtime_disable(&pdev->dev);
	for (i = 0; i < CAL_NUM_CONTEXT; i++) {
		ctx = dev->ctx[i];
		if (ctx) {
			v4l2_async_notifier_unregister(&ctx->notifier);
			v4l2_async_notifier_cleanup(&ctx->notifier);
			v4l2_ctrl_handler_free(&ctx->ctrl_handler);
			v4l2_device_unregister(&ctx->v4l2_dev);
		}
	}

	return ret;
}

static int cal_remove(struct platform_device *pdev)
{
	struct cal_dev *dev =
		(struct cal_dev *)platform_get_drvdata(pdev);
	struct cal_ctx *ctx;
	int i;

	cal_dbg(1, dev, "Removing %s\n", CAL_MODULE_NAME);

	cal_runtime_get(dev);

	for (i = 0; i < CAL_NUM_CONTEXT; i++) {
		ctx = dev->ctx[i];
		if (ctx) {
			ctx_dbg(1, ctx, "unregistering %s\n",
				video_device_node_name(&ctx->vdev));
			camerarx_phy_disable(ctx);
			v4l2_async_notifier_unregister(&ctx->notifier);
			v4l2_async_notifier_cleanup(&ctx->notifier);
			v4l2_ctrl_handler_free(&ctx->ctrl_handler);
			v4l2_device_unregister(&ctx->v4l2_dev);
			video_unregister_device(&ctx->vdev);
		}
	}

	cal_runtime_put(dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id cal_of_match[] = {
	{ .compatible = "ti,dra72-cal", },
	{},
};
MODULE_DEVICE_TABLE(of, cal_of_match);
#endif

static struct platform_driver cal_pdrv = {
	.probe		= cal_probe,
	.remove		= cal_remove,
	.driver		= {
		.name	= CAL_MODULE_NAME,
		.of_match_table = of_match_ptr(cal_of_match),
	},
};

module_platform_driver(cal_pdrv);

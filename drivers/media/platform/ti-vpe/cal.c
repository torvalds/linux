// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI CAL camera interface driver
 *
 * Copyright (c) 2015 Texas Instruments Inc.
 * Benoit Parrot, <bparrot@ti.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "cal_regs.h"

#define CAL_MODULE_NAME "cal"

MODULE_DESCRIPTION("TI CAL driver");
MODULE_AUTHOR("Benoit Parrot, <bparrot@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1.0");

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

static unsigned debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define cal_dbg(level, cal, fmt, arg...)				\
	do {								\
		if (debug >= (level))					\
			dev_printk(KERN_DEBUG, &(cal)->pdev->dev,	\
				   fmt,	##arg);				\
	} while (0)
#define cal_info(cal, fmt, arg...)	\
	dev_info(&(cal)->pdev->dev, fmt, ##arg)
#define cal_err(cal, fmt, arg...)	\
	dev_err(&(cal)->pdev->dev, fmt, ##arg)

#define ctx_dbg(level, ctx, fmt, arg...)	\
	cal_dbg(level, (ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)
#define ctx_info(ctx, fmt, arg...)	\
	cal_info((ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)
#define ctx_err(ctx, fmt, arg...)	\
	cal_err((ctx)->cal, "ctx%u: " fmt, (ctx)->index, ##arg)

#define phy_dbg(level, phy, fmt, arg...)	\
	cal_dbg(level, (phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)
#define phy_info(phy, fmt, arg...)	\
	cal_info((phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)
#define phy_err(phy, fmt, arg...)	\
	cal_err((phy)->cal, "phy%u: " fmt, (phy)->instance, ##arg)

#define CAL_NUM_CONTEXT 2

#define MAX_WIDTH_BYTES (8192 * 8)
#define MAX_HEIGHT_LINES 16383

/* ------------------------------------------------------------------
 *	Format Handling
 * ------------------------------------------------------------------
 */

struct cal_fmt {
	u32	fourcc;
	u32	code;
	/* Bits per pixel */
	u8	bpp;
};

static const struct cal_fmt cal_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.code		= MEDIA_BUS_FMT_YVYU8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.code		= MEDIA_BUS_FMT_VYUY8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_BE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB24, /* rgb */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_LE,
		.bpp		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_BGR24, /* bgr */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_BE,
		.bpp		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB32, /* argb */
		.code		= MEDIA_BUS_FMT_ARGB8888_1X32,
		.bpp		= 32,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.code		= MEDIA_BUS_FMT_SBGGR8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.code		= MEDIA_BUS_FMT_SGBRG8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.code		= MEDIA_BUS_FMT_SGRBG8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.code		= MEDIA_BUS_FMT_SRGGB8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
		.bpp		= 12,
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

/* ------------------------------------------------------------------
 *	Driver Structures
 * ------------------------------------------------------------------
 */

/* buffer for one video frame */
struct cal_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

struct cal_dmaqueue {
	struct list_head	active;
};

/* CTRL_CORE_CAMERRX_CONTROL register field id */
enum cal_camerarx_field {
	F_CTRLCLKEN,
	F_CAMMODE,
	F_LANEENABLE,
	F_CSI_MODE,

	F_MAX_FIELDS,
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
	struct platform_device	*pdev;
	struct regmap_field	*fields[F_MAX_FIELDS];

	struct cal_dev		*cal;
	unsigned int		instance;

	struct v4l2_fwnode_endpoint	endpoint;
	struct device_node	*sensor_node;
	struct v4l2_subdev	*sensor;
	unsigned int		external_rate;
};

struct cal_dev {
	struct clk		*fclk;
	int			irq;
	void __iomem		*base;
	struct resource		*res;
	struct platform_device	*pdev;

	const struct cal_data	*data;

	/* Control Module handle */
	struct regmap		*syscon_camerrx;
	u32			syscon_camerrx_offset;

	/* Camera Core Module handle */
	struct cal_camerarx	*phy[CAL_NUM_CSI2_PORTS];

	struct cal_ctx		*ctx[CAL_NUM_CONTEXT];

	struct v4l2_device	v4l2_dev;
};

/*
 * There is one cal_ctx structure for each camera core context.
 */
struct cal_ctx {
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_device	vdev;
	struct v4l2_async_notifier notifier;

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
	const struct cal_fmt	*active_fmt[ARRAY_SIZE(cal_formats)];
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

static inline struct cal_ctx *notifier_to_ctx(struct v4l2_async_notifier *n)
{
	return container_of(n, struct cal_ctx, notifier);
}

/* ------------------------------------------------------------------
 *	Platform Data
 * ------------------------------------------------------------------
 */

static const struct cal_camerarx_data dra72x_cal_camerarx[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 10, 10 },
			[F_CAMMODE] = { 11, 12 },
			[F_LANEENABLE] = { 13, 16 },
			[F_CSI_MODE] = { 17, 17 },
		},
		.num_lanes = 4,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_LANEENABLE] = { 3, 4 },
			[F_CSI_MODE] = { 5, 5 },
		},
		.num_lanes = 2,
	},
};

static const struct cal_data dra72x_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
};

static const struct cal_data dra72x_es1_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
	.flags = DRA72_CAL_PRE_ES2_LDO_DISABLE,
};

static const struct cal_camerarx_data dra76x_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 8, 8 },
			[F_CAMMODE] = { 9, 10 },
			[F_CSI_MODE] = { 11, 11 },
			[F_LANEENABLE] = { 27, 31 },
		},
		.num_lanes = 5,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_CSI_MODE] = { 3, 3 },
			[F_LANEENABLE] = { 24, 26 },
		},
		.num_lanes = 3,
	},
};

static const struct cal_data dra76x_cal_data = {
	.camerarx = dra76x_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(dra76x_cal_csi_phy),
};

static const struct cal_camerarx_data am654_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 15, 15 },
			[F_CAMMODE] = { 24, 25 },
			[F_LANEENABLE] = { 0, 4 },
		},
		.num_lanes = 5,
	},
};

static const struct cal_data am654_cal_data = {
	.camerarx = am654_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(am654_cal_csi_phy),
};

/* ------------------------------------------------------------------
 *	I/O Register Accessors
 * ------------------------------------------------------------------
 */

#define reg_read(dev, offset) ioread32(dev->base + offset)
#define reg_write(dev, offset, val) iowrite32(val, dev->base + offset)

static inline u32 reg_read_field(struct cal_dev *cal, u32 offset, u32 mask)
{
	return FIELD_GET(mask, reg_read(cal, offset));
}

static inline void reg_write_field(struct cal_dev *cal, u32 offset, u32 value,
				   u32 mask)
{
	u32 val = reg_read(cal, offset);

	val &= ~mask;
	val |= FIELD_PREP(mask, value);
	reg_write(cal, offset, val);
}

static inline void set_field(u32 *valp, u32 field, u32 mask)
{
	u32 val = *valp;

	val &= ~mask;
	val |= (field << __ffs(mask)) & mask;
	*valp = val;
}

static void cal_quickdump_regs(struct cal_dev *cal)
{
	cal_info(cal, "CAL Registers @ 0x%pa:\n", &cal->res->start);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
		       (__force const void *)cal->base,
		       resource_size(cal->res), false);

	if (cal->ctx[0]) {
		cal_info(cal, "CSI2 Core 0 Registers @ %pa:\n",
			 &cal->ctx[0]->phy->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)cal->ctx[0]->phy->base,
			       resource_size(cal->ctx[0]->phy->res),
			       false);
	}

	if (cal->ctx[1]) {
		cal_info(cal, "CSI2 Core 1 Registers @ %pa:\n",
			 &cal->ctx[1]->phy->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)cal->ctx[1]->phy->base,
			       resource_size(cal->ctx[1]->phy->res),
			       false);
	}
}

/* ------------------------------------------------------------------
 *	CAMERARX Management
 * ------------------------------------------------------------------
 */

static u32 cal_camerarx_max_lanes(struct cal_camerarx *phy)
{
	return phy->cal->data->camerarx[phy->instance].num_lanes;
}

static void cal_camerarx_enable(struct cal_camerarx *phy)
{
	u32 max_lanes;

	regmap_field_write(phy->fields[F_CAMMODE], 0);
	/* Always enable all lanes at the phy control level */
	max_lanes = (1 << cal_camerarx_max_lanes(phy)) - 1;
	regmap_field_write(phy->fields[F_LANEENABLE], max_lanes);
	/* F_CSI_MODE is not present on every architecture */
	if (phy->fields[F_CSI_MODE])
		regmap_field_write(phy->fields[F_CSI_MODE], 1);
	regmap_field_write(phy->fields[F_CTRLCLKEN], 1);
}

static void cal_camerarx_disable(struct cal_camerarx *phy)
{
	regmap_field_write(phy->fields[F_CTRLCLKEN], 0);
}

/*
 *   Errata i913: CSI2 LDO Needs to be disabled when module is powered on
 *
 *   Enabling CSI2 LDO shorts it to core supply. It is crucial the 2 CSI2
 *   LDOs on the device are disabled if CSI-2 module is powered on
 *   (0x4845 B304 | 0x4845 B384 [28:27] = 0x1) or in ULPS (0x4845 B304
 *   | 0x4845 B384 [28:27] = 0x2) mode. Common concerns include: high
 *   current draw on the module supply in active mode.
 *
 *   Errata does not apply when CSI-2 module is powered off
 *   (0x4845 B304 | 0x4845 B384 [28:27] = 0x0).
 *
 * SW Workaround:
 *	Set the following register bits to disable the LDO,
 *	which is essentially CSI2 REG10 bit 6:
 *
 *		Core 0:  0x4845 B828 = 0x0000 0040
 *		Core 1:  0x4845 B928 = 0x0000 0040
 */
static void cal_camerarx_i913_errata(struct cal_camerarx *phy)
{
	u32 reg10 = reg_read(phy, CAL_CSI2_PHY_REG10);

	set_field(&reg10, 1, CAL_CSI2_PHY_REG10_I933_LDO_DISABLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG10 = 0x%08x\n", phy->instance, reg10);
	reg_write(phy, CAL_CSI2_PHY_REG10, reg10);
}

/*
 * Enable the expected IRQ sources
 */
static void cal_camerarx_enable_irqs(struct cal_camerarx *phy)
{
	u32 val;

	const u32 cio_err_mask =
		CAL_CSI2_COMPLEXIO_IRQ_LANE_ERRORS_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_FIFO_OVR_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_SHORT_PACKET_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_ECC_NO_CORRECTION_MASK;

	/* Enable CIO error irqs */
	reg_write(phy->cal, CAL_HL_IRQENABLE_SET(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance));
	reg_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance),
		  cio_err_mask);

	/* Always enable OCPO error */
	reg_write(phy->cal, CAL_HL_IRQENABLE_SET(0), CAL_HL_IRQ_OCPO_ERR_MASK);

	/* Enable IRQ_WDMA_END 0/1 */
	val = 0;
	set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	reg_write(phy->cal, CAL_HL_IRQENABLE_SET(1), val);
	/* Enable IRQ_WDMA_START 0/1 */
	val = 0;
	set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	reg_write(phy->cal, CAL_HL_IRQENABLE_SET(2), val);
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	reg_write(phy->cal, CAL_CSI2_VC_IRQENABLE(0), 0xFF000000);
}

static void cal_camerarx_disable_irqs(struct cal_camerarx *phy)
{
	u32 val;

	/* Disable CIO error irqs */
	reg_write(phy->cal, CAL_HL_IRQENABLE_CLR(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance));
	reg_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance),
		  0);

	/* Disable IRQ_WDMA_END 0/1 */
	val = 0;
	set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	reg_write(phy->cal, CAL_HL_IRQENABLE_CLR(1), val);
	/* Disable IRQ_WDMA_START 0/1 */
	val = 0;
	set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	reg_write(phy->cal, CAL_HL_IRQENABLE_CLR(2), val);
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	reg_write(phy->cal, CAL_CSI2_VC_IRQENABLE(0), 0);
}

static void cal_camerarx_power(struct cal_camerarx *phy, bool enable)
{
	u32 target_state;
	unsigned int i;

	target_state = enable ? CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_STATE_ON :
		       CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_STATE_OFF;

	reg_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			target_state, CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_MASK);

	for (i = 0; i < 10; i++) {
		u32 current_state;

		current_state = reg_read_field(phy->cal,
					       CAL_CSI2_COMPLEXIO_CFG(phy->instance),
					       CAL_CSI2_COMPLEXIO_CFG_PWR_STATUS_MASK);

		if (current_state == target_state)
			break;

		usleep_range(1000, 1100);
	}

	if (i == 10)
		phy_err(phy, "Failed to power %s complexio\n",
			enable ? "up" : "down");
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static void cal_camerarx_config(struct cal_camerarx *phy,
				const struct cal_fmt *fmt)
{
	unsigned int reg0, reg1;
	unsigned int ths_term, ths_settle;
	unsigned int csi2_ddrclk_khz;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2 =
			&phy->endpoint.bus.mipi_csi2;
	u32 num_lanes = mipi_csi2->num_data_lanes;

	/* DPHY timing configuration */
	/* CSI-2 is DDR and we only count used lanes. */
	csi2_ddrclk_khz = phy->external_rate / 1000
		/ (2 * num_lanes) * fmt->bpp;
	phy_dbg(1, phy, "csi2_ddrclk_khz: %d\n", csi2_ddrclk_khz);

	/* THS_TERM: Programmed value = floor(20 ns/DDRClk period) */
	ths_term = 20 * csi2_ddrclk_khz / 1000000;
	phy_dbg(1, phy, "ths_term: %d (0x%02x)\n", ths_term, ths_term);

	/* THS_SETTLE: Programmed value = floor(105 ns/DDRClk period) + 4 */
	ths_settle = (105 * csi2_ddrclk_khz / 1000000) + 4;
	phy_dbg(1, phy, "ths_settle: %d (0x%02x)\n", ths_settle, ths_settle);

	reg0 = reg_read(phy, CAL_CSI2_PHY_REG0);
	set_field(&reg0, CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_DISABLE,
		  CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_MASK);
	set_field(&reg0, ths_term, CAL_CSI2_PHY_REG0_THS_TERM_MASK);
	set_field(&reg0, ths_settle, CAL_CSI2_PHY_REG0_THS_SETTLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG0 = 0x%08x\n", phy->instance, reg0);
	reg_write(phy, CAL_CSI2_PHY_REG0, reg0);

	reg1 = reg_read(phy, CAL_CSI2_PHY_REG1);
	set_field(&reg1, TCLK_TERM, CAL_CSI2_PHY_REG1_TCLK_TERM_MASK);
	set_field(&reg1, 0xb8, CAL_CSI2_PHY_REG1_DPHY_HS_SYNC_PATTERN_MASK);
	set_field(&reg1, TCLK_MISS, CAL_CSI2_PHY_REG1_CTRLCLK_DIV_FACTOR_MASK);
	set_field(&reg1, TCLK_SETTLE, CAL_CSI2_PHY_REG1_TCLK_SETTLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG1 = 0x%08x\n", phy->instance, reg1);
	reg_write(phy, CAL_CSI2_PHY_REG1, reg1);
}

static void cal_camerarx_init(struct cal_camerarx *phy,
			      const struct cal_fmt *fmt)
{
	u32 val;
	u32 sscounter;

	/* Steps
	 *  1. Configure D-PHY mode and enable required lanes
	 *  2. Reset complex IO - Wait for completion of reset
	 *          Note if the external sensor is not sending byte clock,
	 *          the reset will timeout
	 *  3 Program Stop States
	 *      A. Program THS_TERM, THS_SETTLE, etc... Timings parameters
	 *              in terms of DDR clock periods
	 *      B. Enable stop state transition timeouts
	 *  4.Force FORCERXMODE
	 *      D. Enable pull down using pad control
	 *      E. Power up PHY
	 *      F. Wait for power up completion
	 *      G. Wait for all enabled lane to reach stop state
	 *      H. Disable pull down using pad control
	 */

	/* 1. Configure D-PHY mode and enable required lanes */
	cal_camerarx_enable(phy);

	/* 2. Reset complex IO - Do not wait for reset completion */
	reg_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_OPERATIONAL,
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x De-assert Complex IO Reset\n",
		phy->instance,
		reg_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance)));

	/* Dummy read to allow SCP reset to complete */
	reg_read(phy, CAL_CSI2_PHY_REG0);

	/* 3.A. Program Phy Timing Parameters */
	cal_camerarx_config(phy, fmt);

	/* 3.B. Program Stop States */
	/*
	 * The stop-state-counter is based on fclk cycles, and we always use
	 * the x16 and x4 settings, so stop-state-timeout =
	 * fclk-cycle * 16 * 4 * counter.
	 *
	 * Stop-state-timeout must be more than 100us as per CSI2 spec, so we
	 * calculate a timeout that's 100us (rounding up).
	 */
	sscounter = DIV_ROUND_UP(clk_get_rate(phy->cal->fclk), 10000 *  16 * 4);

	val = reg_read(phy->cal, CAL_CSI2_TIMING(phy->instance));
	set_field(&val, 1, CAL_CSI2_TIMING_STOP_STATE_X16_IO1_MASK);
	set_field(&val, 1, CAL_CSI2_TIMING_STOP_STATE_X4_IO1_MASK);
	set_field(&val, sscounter, CAL_CSI2_TIMING_STOP_STATE_COUNTER_IO1_MASK);
	reg_write(phy->cal, CAL_CSI2_TIMING(phy->instance), val);
	phy_dbg(3, phy, "CAL_CSI2_TIMING(%d) = 0x%08x Stop States\n",
		phy->instance,
		reg_read(phy->cal, CAL_CSI2_TIMING(phy->instance)));

	/* 4. Force FORCERXMODE */
	reg_write_field(phy->cal, CAL_CSI2_TIMING(phy->instance),
			1, CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK);
	phy_dbg(3, phy, "CAL_CSI2_TIMING(%d) = 0x%08x Force RXMODE\n",
		phy->instance,
		reg_read(phy->cal, CAL_CSI2_TIMING(phy->instance)));

	/* E. Power up the PHY using the complex IO */
	cal_camerarx_power(phy, true);
}

static void cal_camerarx_wait_reset(struct cal_camerarx *phy)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(750);
	while (time_before(jiffies, timeout)) {
		if (reg_read_field(phy->cal,
				   CAL_CSI2_COMPLEXIO_CFG(phy->instance),
				   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) ==
		    CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETCOMPLETED)
			break;
		usleep_range(500, 5000);
	}

	if (reg_read_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) !=
			   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETCOMPLETED)
		phy_err(phy, "Timeout waiting for Complex IO reset done\n");
}

static void cal_camerarx_wait_stop_state(struct cal_camerarx *phy)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(750);
	while (time_before(jiffies, timeout)) {
		if (reg_read_field(phy->cal,
				   CAL_CSI2_TIMING(phy->instance),
				   CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK) == 0)
			break;
		usleep_range(500, 5000);
	}

	if (reg_read_field(phy->cal, CAL_CSI2_TIMING(phy->instance),
			   CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK) != 0)
		phy_err(phy, "Timeout waiting for stop state\n");
}

static void cal_camerarx_wait_ready(struct cal_camerarx *phy)
{
	/* Steps
	 *  2. Wait for completion of reset
	 *          Note if the external sensor is not sending byte clock,
	 *          the reset will timeout
	 *  4.Force FORCERXMODE
	 *      G. Wait for all enabled lane to reach stop state
	 *      H. Disable pull down using pad control
	 */

	/* 2. Wait for reset completion */
	cal_camerarx_wait_reset(phy);

	/* 4. G. Wait for all enabled lane to reach stop state */
	cal_camerarx_wait_stop_state(phy);

	phy_dbg(1, phy, "CSI2_%d_REG1 = 0x%08x (Bit(31,28) should be set!)\n",
		phy->instance, reg_read(phy, CAL_CSI2_PHY_REG1));
}

static void cal_camerarx_deinit(struct cal_camerarx *phy)
{
	unsigned int i;

	cal_camerarx_power(phy, false);

	/* Assert Comple IO Reset */
	reg_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL,
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);

	/* Wait for power down completion */
	for (i = 0; i < 10; i++) {
		if (reg_read_field(phy->cal,
				   CAL_CSI2_COMPLEXIO_CFG(phy->instance),
				   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) ==
		    CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETONGOING)
			break;
		usleep_range(1000, 1100);
	}
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x Complex IO in Reset (%d) %s\n",
		phy->instance,
		reg_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance)), i,
		(i >= 10) ? "(timeout)" : "");

	/* Disable the phy */
	cal_camerarx_disable(phy);
}

static void cal_camerarx_lane_config(struct cal_camerarx *phy)
{
	u32 val = reg_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance));
	u32 lane_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK;
	u32 polarity_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POL_MASK;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2 =
		&phy->endpoint.bus.mipi_csi2;
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

	reg_write(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance), val);
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x\n",
		phy->instance, val);
}

static void cal_camerarx_ppi_enable(struct cal_camerarx *phy)
{
	reg_write(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance), BIT(3));
	reg_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			1, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static void cal_camerarx_ppi_disable(struct cal_camerarx *phy)
{
	reg_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			0, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static int cal_camerarx_get_external_info(struct cal_camerarx *phy)
{
	struct v4l2_ctrl *ctrl;

	if (!phy->sensor)
		return -ENODEV;

	ctrl = v4l2_ctrl_find(phy->sensor->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		phy_err(phy, "no pixel rate control in subdev: %s\n",
			phy->sensor->name);
		return -EPIPE;
	}

	phy->external_rate = v4l2_ctrl_g_ctrl_int64(ctrl);
	phy_dbg(3, phy, "sensor Pixel Rate: %u\n", phy->external_rate);

	return 0;
}

static int cal_camerarx_regmap_init(struct cal_dev *cal,
				    struct cal_camerarx *phy)
{
	const struct cal_camerarx_data *phy_data;
	unsigned int i;

	if (!cal->data)
		return -EINVAL;

	phy_data = &cal->data->camerarx[phy->instance];

	for (i = 0; i < F_MAX_FIELDS; i++) {
		struct reg_field field = {
			.reg = cal->syscon_camerrx_offset,
			.lsb = phy_data->fields[i].lsb,
			.msb = phy_data->fields[i].msb,
		};

		/*
		 * Here we update the reg offset with the
		 * value found in DT
		 */
		phy->fields[i] = devm_regmap_field_alloc(&cal->pdev->dev,
							 cal->syscon_camerrx,
							 field);
		if (IS_ERR(phy->fields[i])) {
			cal_err(cal, "Unable to allocate regmap fields\n");
			return PTR_ERR(phy->fields[i]);
		}
	}

	return 0;
}

static int cal_camerarx_parse_dt(struct cal_camerarx *phy)
{
	struct v4l2_fwnode_endpoint *endpoint = &phy->endpoint;
	struct platform_device *pdev = phy->cal->pdev;
	struct device_node *ep_node;
	char data_lanes[V4L2_FWNODE_CSI2_MAX_DATA_LANES * 2];
	unsigned int i;
	int ret;

	/*
	 * Find the endpoint node for the port corresponding to the PHY
	 * instance, and parse its CSI-2-related properties.
	 */
	ep_node = of_graph_get_endpoint_by_regs(pdev->dev.of_node,
						phy->instance, 0);
	if (!ep_node) {
		/*
		 * The endpoint is not mandatory, not all PHY instances need to
		 * be connected in DT.
		 */
		phy_dbg(3, phy, "Port has no endpoint\n");
		return 0;
	}

	endpoint->bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep_node), endpoint);
	if (ret < 0) {
		phy_err(phy, "Failed to parse endpoint\n");
		goto done;
	}

	for (i = 0; i < endpoint->bus.mipi_csi2.num_data_lanes; i++) {
		unsigned int lane = endpoint->bus.mipi_csi2.data_lanes[i];

		if (lane > 4) {
			phy_err(phy, "Invalid position %u for data lane %u\n",
				lane, i);
			ret = -EINVAL;
			goto done;
		}

		data_lanes[i*2] = '0' + lane;
		data_lanes[i*2+1] = ' ';
	}

	data_lanes[i*2-1] = '\0';

	phy_dbg(3, phy,
		"CSI-2 bus: clock lane <%u>, data lanes <%s>, flags 0x%08x\n",
		endpoint->bus.mipi_csi2.clock_lane, data_lanes,
		endpoint->bus.mipi_csi2.flags);

	/* Retrieve the connected device and store it for later use. */
	phy->sensor_node = of_graph_get_remote_port_parent(ep_node);
	if (!phy->sensor_node) {
		phy_dbg(3, phy, "Can't get remote parent\n");
		ret = -EINVAL;
		goto done;
	}

	phy_dbg(1, phy, "Found connected device %pOFn\n", phy->sensor_node);

done:
	of_node_put(ep_node);
	return ret;
}

static struct cal_camerarx *cal_camerarx_create(struct cal_dev *cal,
						unsigned int instance)
{
	struct platform_device *pdev = cal->pdev;
	struct cal_camerarx *phy;
	int ret;

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return ERR_PTR(-ENOMEM);

	phy->cal = cal;
	phy->instance = instance;
	phy->external_rate = 192000000;

	phy->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						(instance == 0) ?
						"cal_rx_core0" :
						"cal_rx_core1");
	phy->base = devm_ioremap_resource(&pdev->dev, phy->res);
	if (IS_ERR(phy->base)) {
		cal_err(cal, "failed to ioremap\n");
		ret = PTR_ERR(phy->base);
		goto error;
	}

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		phy->res->name, &phy->res->start, &phy->res->end);

	ret = cal_camerarx_regmap_init(cal, phy);
	if (ret)
		goto error;

	ret = cal_camerarx_parse_dt(phy);
	if (ret)
		goto error;

	return phy;

error:
	kfree(phy);
	return ERR_PTR(ret);
}

static void cal_camerarx_destroy(struct cal_camerarx *phy)
{
	if (!phy)
		return;

	of_node_put(phy->sensor_node);
	kfree(phy);
}

static int cal_camerarx_init_regmap(struct cal_dev *cal)
{
	struct device_node *np = cal->pdev->dev.of_node;
	struct regmap_config config = { };
	struct regmap *syscon;
	struct resource *res;
	unsigned int offset;
	void __iomem *base;

	syscon = syscon_regmap_lookup_by_phandle_args(np, "ti,camerrx-control",
						      1, &offset);
	if (!IS_ERR(syscon)) {
		cal->syscon_camerrx = syscon;
		cal->syscon_camerrx_offset = offset;
		return 0;
	}

	dev_warn(&cal->pdev->dev, "failed to get ti,camerrx-control: %ld\n",
		 PTR_ERR(syscon));

	/*
	 * Backward DTS compatibility. If syscon entry is not present then
	 * check if the camerrx_control resource is present.
	 */
	res = platform_get_resource_byname(cal->pdev, IORESOURCE_MEM,
					   "camerrx_control");
	base = devm_ioremap_resource(&cal->pdev->dev, res);
	if (IS_ERR(base)) {
		cal_err(cal, "failed to ioremap camerrx_control\n");
		return PTR_ERR(base);
	}

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		res->name, &res->start, &res->end);

	config.reg_bits = 32;
	config.reg_stride = 4;
	config.val_bits = 32;
	config.max_register = resource_size(res) - 4;

	syscon = regmap_init_mmio(NULL, base, &config);
	if (IS_ERR(syscon)) {
		pr_err("regmap init failed\n");
		return PTR_ERR(syscon);
	}

	/*
	 * In this case the base already point to the direct CM register so no
	 * need for an offset.
	 */
	cal->syscon_camerrx = syscon;
	cal->syscon_camerrx_offset = 0;

	return 0;
}

/* ------------------------------------------------------------------
 *	Context Management
 * ------------------------------------------------------------------
 */

static void cal_ctx_csi2_config(struct cal_ctx *ctx)
{
	u32 val;

	val = reg_read(ctx->cal, CAL_CSI2_CTX0(ctx->index));
	set_field(&val, ctx->cport, CAL_CSI2_CTX_CPORT_MASK);
	/*
	 * DT type: MIPI CSI-2 Specs
	 *   0x1: All - DT filter is disabled
	 *  0x24: RGB888 1 pixel  = 3 bytes
	 *  0x2B: RAW10  4 pixels = 5 bytes
	 *  0x2A: RAW8   1 pixel  = 1 byte
	 *  0x1E: YUV422 2 pixels = 4 bytes
	 */
	set_field(&val, 0x1, CAL_CSI2_CTX_DT_MASK);
	set_field(&val, 0, CAL_CSI2_CTX_VC_MASK);
	set_field(&val, ctx->v_fmt.fmt.pix.height, CAL_CSI2_CTX_LINES_MASK);
	set_field(&val, CAL_CSI2_CTX_ATT_PIX, CAL_CSI2_CTX_ATT_MASK);
	set_field(&val, CAL_CSI2_CTX_PACK_MODE_LINE,
		  CAL_CSI2_CTX_PACK_MODE_MASK);
	reg_write(ctx->cal, CAL_CSI2_CTX0(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_CSI2_CTX0(%d) = 0x%08x\n", ctx->index,
		reg_read(ctx->cal, CAL_CSI2_CTX0(ctx->index)));
}

static void cal_ctx_pix_proc_config(struct cal_ctx *ctx)
{
	u32 val, extract, pack;

	switch (ctx->fmt->bpp) {
	case 8:
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	case 10:
		extract = CAL_PIX_PROC_EXTRACT_B10_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 12:
		extract = CAL_PIX_PROC_EXTRACT_B12_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 16:
		extract = CAL_PIX_PROC_EXTRACT_B16_LE;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	default:
		/*
		 * If you see this warning then it means that you added
		 * some new entry in the cal_formats[] array with a different
		 * bit per pixel values then the one supported below.
		 * Either add support for the new bpp value below or adjust
		 * the new entry to use one of the value below.
		 *
		 * Instead of failing here just use 8 bpp as a default.
		 */
		dev_warn_once(&ctx->cal->pdev->dev,
			      "%s:%d:%s: bpp:%d unsupported! Overwritten with 8.\n",
			      __FILE__, __LINE__, __func__, ctx->fmt->bpp);
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	}

	val = reg_read(ctx->cal, CAL_PIX_PROC(ctx->index));
	set_field(&val, extract, CAL_PIX_PROC_EXTRACT_MASK);
	set_field(&val, CAL_PIX_PROC_DPCMD_BYPASS, CAL_PIX_PROC_DPCMD_MASK);
	set_field(&val, CAL_PIX_PROC_DPCME_BYPASS, CAL_PIX_PROC_DPCME_MASK);
	set_field(&val, pack, CAL_PIX_PROC_PACK_MASK);
	set_field(&val, ctx->cport, CAL_PIX_PROC_CPORT_MASK);
	set_field(&val, 1, CAL_PIX_PROC_EN_MASK);
	reg_write(ctx->cal, CAL_PIX_PROC(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_PIX_PROC(%d) = 0x%08x\n", ctx->index,
		reg_read(ctx->cal, CAL_PIX_PROC(ctx->index)));
}

static void cal_ctx_wr_dma_config(struct cal_ctx *ctx,
				  unsigned int width, unsigned int height)
{
	u32 val;

	val = reg_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index));
	set_field(&val, ctx->cport, CAL_WR_DMA_CTRL_CPORT_MASK);
	set_field(&val, height, CAL_WR_DMA_CTRL_YSIZE_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_DTAG_PIX_DAT,
		  CAL_WR_DMA_CTRL_DTAG_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_MODE_CONST,
		  CAL_WR_DMA_CTRL_MODE_MASK);
	set_field(&val, CAL_WR_DMA_CTRL_PATTERN_LINEAR,
		  CAL_WR_DMA_CTRL_PATTERN_MASK);
	set_field(&val, 1, CAL_WR_DMA_CTRL_STALL_RD_MASK);
	reg_write(ctx->cal, CAL_WR_DMA_CTRL(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_CTRL(%d) = 0x%08x\n", ctx->index,
		reg_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index)));

	/*
	 * width/16 not sure but giving it a whirl.
	 * zero does not work right
	 */
	reg_write_field(ctx->cal,
			CAL_WR_DMA_OFST(ctx->index),
			(width / 16),
			CAL_WR_DMA_OFST_MASK);
	ctx_dbg(3, ctx, "CAL_WR_DMA_OFST(%d) = 0x%08x\n", ctx->index,
		reg_read(ctx->cal, CAL_WR_DMA_OFST(ctx->index)));

	val = reg_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index));
	/* 64 bit word means no skipping */
	set_field(&val, 0, CAL_WR_DMA_XSIZE_XSKIP_MASK);
	/*
	 * (width*8)/64 this should be size of an entire line
	 * in 64bit word but 0 means all data until the end
	 * is detected automagically
	 */
	set_field(&val, (width / 8), CAL_WR_DMA_XSIZE_MASK);
	reg_write(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_XSIZE(%d) = 0x%08x\n", ctx->index,
		reg_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index)));

	val = reg_read(ctx->cal, CAL_CTRL);
	set_field(&val, CAL_CTRL_BURSTSIZE_BURST128, CAL_CTRL_BURSTSIZE_MASK);
	set_field(&val, 0xF, CAL_CTRL_TAGCNT_MASK);
	set_field(&val, CAL_CTRL_POSTED_WRITES_NONPOSTED,
		  CAL_CTRL_POSTED_WRITES_MASK);
	set_field(&val, 0xFF, CAL_CTRL_MFLAGL_MASK);
	set_field(&val, 0xFF, CAL_CTRL_MFLAGH_MASK);
	reg_write(ctx->cal, CAL_CTRL, val);
	ctx_dbg(3, ctx, "CAL_CTRL = 0x%08x\n", reg_read(ctx->cal, CAL_CTRL));
}

static void cal_ctx_wr_dma_addr(struct cal_ctx *ctx, unsigned int dmaaddr)
{
	reg_write(ctx->cal, CAL_WR_DMA_ADDR(ctx->index), dmaaddr);
}

/* ------------------------------------------------------------------
 *	IRQ Handling
 * ------------------------------------------------------------------
 */

static inline void cal_schedule_next_buffer(struct cal_ctx *ctx)
{
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf;
	unsigned long addr;

	buf = list_entry(dma_q->active.next, struct cal_buffer, list);
	ctx->next_frm = buf;
	list_del(&buf->list);

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	cal_ctx_wr_dma_addr(ctx, addr);
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
	struct cal_dev *cal = data;
	struct cal_ctx *ctx;
	struct cal_dmaqueue *dma_q;
	u32 status;

	status = reg_read(cal, CAL_HL_IRQSTATUS(0));
	if (status) {
		unsigned int i;

		reg_write(cal, CAL_HL_IRQSTATUS(0), status);

		if (status & CAL_HL_IRQ_OCPO_ERR_MASK)
			dev_err_ratelimited(&cal->pdev->dev, "OCPO ERROR\n");

		for (i = 0; i < 2; ++i) {
			if (status & CAL_HL_IRQ_CIO_MASK(i)) {
				u32 cio_stat = reg_read(cal,
							CAL_CSI2_COMPLEXIO_IRQSTATUS(i));

				dev_err_ratelimited(&cal->pdev->dev,
						    "CIO%u error: %#08x\n", i, cio_stat);

				reg_write(cal, CAL_CSI2_COMPLEXIO_IRQSTATUS(i),
					  cio_stat);
			}
		}
	}

	/* Check which DMA just finished */
	status = reg_read(cal, CAL_HL_IRQSTATUS(1));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		reg_write(cal, CAL_HL_IRQSTATUS(1), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (isportirqset(status, i)) {
				ctx = cal->ctx[i];

				spin_lock(&ctx->slock);
				ctx->dma_act = false;

				if (ctx->cur_frm != ctx->next_frm)
					cal_process_buffer_complete(ctx);

				spin_unlock(&ctx->slock);
			}
		}
	}

	/* Check which DMA just started */
	status = reg_read(cal, CAL_HL_IRQSTATUS(2));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		reg_write(cal, CAL_HL_IRQSTATUS(2), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (isportirqset(status, i)) {
				ctx = cal->ctx[i];
				dma_q = &ctx->vidq;

				spin_lock(&ctx->slock);
				ctx->dma_act = true;
				if (!list_empty(&dma_q->active) &&
				    ctx->cur_frm == ctx->next_frm)
					cal_schedule_next_buffer(ctx);
				spin_unlock(&ctx->slock);
			}
		}
	}

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------
 *	V4L2 Video IOCTLs
 * ------------------------------------------------------------------
 */

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

static int cal_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct cal_ctx *ctx = video_drvdata(file);

	strscpy(cap->driver, CAL_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, CAL_MODULE_NAME, sizeof(cap->card));

	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(&ctx->cal->pdev->dev));
	return 0;
}

static int cal_enum_fmt_vid_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_fmt *fmt;

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

	ret = v4l2_subdev_call(ctx->phy->sensor, pad, get_fmt, NULL, &sd_fmt);
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

	ret = v4l2_subdev_call(ctx->phy->sensor, pad, set_fmt, NULL, &sd_fmt);
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
	u32 bpl, max_width;

	if (!fmt) {
		ctx_dbg(3, ctx, "No cal_fmt provided!\n");
		return -EINVAL;
	}

	/*
	 * Maximum width is bound by the DMA max width in bytes.
	 * We need to recalculate the actual maxi width depending on the
	 * number of bytes per pixels required.
	 */
	max_width = MAX_WIDTH_BYTES / (ALIGN(fmt->bpp, 8) >> 3);
	v4l_bound_align_image(&f->fmt.pix.width, 48, max_width, 2,
			      &f->fmt.pix.height, 32, MAX_HEIGHT_LINES, 0, 0);

	bpl = (f->fmt.pix.width * ALIGN(fmt->bpp, 8)) >> 3;
	f->fmt.pix.bytesperline = ALIGN(bpl, 16);

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
		ret = v4l2_subdev_call(ctx->phy->sensor, pad, enum_frame_size,
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
	fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(ctx->phy->sensor, pad, enum_frame_size, NULL,
			       &fse);
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
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

static int cal_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cal_s_input(struct file *file, void *priv, unsigned int i)
{
	return i > 0 ? -EINVAL : 0;
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
	ret = v4l2_subdev_call(ctx->phy->sensor, pad, enum_frame_interval,
			       NULL, &fie);
	if (ret)
		return ret;
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

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
	.vidioc_expbuf        = vb2_ioctl_expbuf,
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

/* ------------------------------------------------------------------
 *	videobuf2 Operations
 * ------------------------------------------------------------------
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
	unsigned long flags;

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
	unsigned long addr;
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

	ret = cal_camerarx_get_external_info(ctx->phy);
	if (ret < 0)
		goto err;

	ret = v4l2_subdev_call(ctx->phy->sensor, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		ctx_err(ctx, "power on failed in subdev\n");
		goto err;
	}

	pm_runtime_get_sync(&ctx->cal->pdev->dev);

	cal_ctx_csi2_config(ctx);
	cal_ctx_pix_proc_config(ctx);
	cal_ctx_wr_dma_config(ctx, ctx->v_fmt.fmt.pix.bytesperline,
			      ctx->v_fmt.fmt.pix.height);
	cal_camerarx_lane_config(ctx->phy);

	cal_camerarx_enable_irqs(ctx->phy);
	cal_camerarx_init(ctx->phy, ctx->fmt);

	ret = v4l2_subdev_call(ctx->phy->sensor, video, s_stream, 1);
	if (ret) {
		v4l2_subdev_call(ctx->phy->sensor, core, s_power, 0);
		ctx_err(ctx, "stream on failed in subdev\n");
		pm_runtime_put_sync(&ctx->cal->pdev->dev);
		goto err;
	}

	cal_camerarx_wait_ready(ctx->phy);
	cal_ctx_wr_dma_addr(ctx, addr);
	cal_camerarx_ppi_enable(ctx->phy);

	if (debug >= 4)
		cal_quickdump_regs(ctx->cal);

	return 0;

err:
	spin_lock_irqsave(&ctx->slock, flags);
	vb2_buffer_done(&ctx->cur_frm->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	ctx->cur_frm = NULL;
	ctx->next_frm = NULL;
	list_for_each_entry_safe(buf, tmp, &dma_q->active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&ctx->slock, flags);
	return ret;
}

static void cal_stop_streaming(struct vb2_queue *vq)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf, *tmp;
	unsigned long timeout;
	unsigned long flags;
	int ret;
	bool dma_act;

	cal_camerarx_ppi_disable(ctx->phy);

	/* wait for stream and dma to finish */
	dma_act = true;
	timeout = jiffies + msecs_to_jiffies(500);
	while (dma_act && time_before(jiffies, timeout)) {
		msleep(50);

		spin_lock_irqsave(&ctx->slock, flags);
		dma_act = ctx->dma_act;
		spin_unlock_irqrestore(&ctx->slock, flags);
	}

	if (dma_act)
		ctx_err(ctx, "failed to disable dma cleanly\n");

	cal_camerarx_disable_irqs(ctx->phy);
	cal_camerarx_deinit(ctx->phy);

	if (v4l2_subdev_call(ctx->phy->sensor, video, s_stream, 0))
		ctx_err(ctx, "stream off failed in subdev\n");

	ret = v4l2_subdev_call(ctx->phy->sensor, core, s_power, 0);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		ctx_err(ctx, "power off failed in subdev\n");

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

	pm_runtime_put_sync(&ctx->cal->pdev->dev);
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

/* ------------------------------------------------------------------
 *	V4L2 Initialization and Registration
 * ------------------------------------------------------------------
 */

static const struct video_device cal_videodev = {
	.name		= CAL_MODULE_NAME,
	.fops		= &cal_fops,
	.ioctl_ops	= &cal_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.device_caps	= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			  V4L2_CAP_READWRITE,
};

static int cal_ctx_v4l2_register(struct cal_ctx *ctx)
{
	struct v4l2_ctrl_handler *hdl = &ctx->ctrl_handler;
	struct video_device *vfd = &ctx->vdev;
	int ret;

	ret = v4l2_ctrl_add_handler(hdl, ctx->phy->sensor->ctrl_handler, NULL,
				    true);
	if (ret < 0) {
		ctx_err(ctx, "Failed to add sensor ctrl handler\n");
		return ret;
	}

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, video_nr);
	if (ret < 0) {
		ctx_err(ctx, "Failed to register video device\n");
		return ret;
	}

	ctx_info(ctx, "V4L2 device registered as %s\n",
		 video_device_node_name(vfd));

	return 0;
}

static void cal_ctx_v4l2_unregister(struct cal_ctx *ctx)
{
	video_unregister_device(&ctx->vdev);
}

static int cal_ctx_v4l2_init_formats(struct cal_ctx *ctx)
{
	struct v4l2_subdev_mbus_code_enum mbus_code;
	struct v4l2_mbus_framefmt mbus_fmt;
	const struct cal_fmt *fmt;
	unsigned int i, j, k;
	int ret = 0;

	/* Enumerate sub device formats and enable all matching local formats */
	ctx->num_active_fmt = 0;
	for (j = 0, i = 0; ret != -EINVAL; ++j) {

		memset(&mbus_code, 0, sizeof(mbus_code));
		mbus_code.index = j;
		mbus_code.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(ctx->phy->sensor, pad, enum_mbus_code,
				       NULL, &mbus_code);
		if (ret)
			continue;

		ctx_dbg(2, ctx,
			"subdev %s: code: %04x idx: %u\n",
			ctx->phy->sensor->name, mbus_code.code, j);

		for (k = 0; k < ARRAY_SIZE(cal_formats); k++) {
			const struct cal_fmt *fmt = &cal_formats[k];

			if (mbus_code.code == fmt->code) {
				ctx->active_fmt[i] = fmt;
				ctx_dbg(2, ctx,
					"matched fourcc: %s: code: %04x idx: %u\n",
					fourcc_to_str(fmt->fourcc),
					fmt->code, i);
				ctx->num_active_fmt = ++i;
			}
		}
	}

	if (i == 0) {
		ctx_err(ctx, "No suitable format reported by subdev %s\n",
			ctx->phy->sensor->name);
		return -EINVAL;
	}

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

static int cal_ctx_v4l2_init(struct cal_ctx *ctx)
{
	struct v4l2_ctrl_handler *hdl = &ctx->ctrl_handler;
	struct video_device *vfd = &ctx->vdev;
	struct vb2_queue *q = &ctx->vb_vidq;
	int ret;

	/* initialize locks */
	spin_lock_init(&ctx->slock);
	mutex_init(&ctx->mutex);

	/* initialize queue */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = ctx;
	q->buf_struct_size = sizeof(struct cal_buffer);
	q->ops = &cal_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &ctx->mutex;
	q->min_buffers_needed = 3;
	q->dev = &ctx->cal->pdev->dev;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	/* init video dma queues */
	INIT_LIST_HEAD(&ctx->vidq.active);

	*vfd = cal_videodev;
	vfd->v4l2_dev = &ctx->cal->v4l2_dev;
	vfd->queue = q;

	/* Initialize the control handler. */
	ret = v4l2_ctrl_handler_init(hdl, 11);
	if (ret < 0) {
		ctx_err(ctx, "Failed to init ctrl handler\n");
		return ret;
	}

	vfd->ctrl_handler = hdl;

	/*
	 * Provide a mutex to v4l2 core. It will be used to protect
	 * all fops and v4l2 ioctls.
	 */
	vfd->lock = &ctx->mutex;
	video_set_drvdata(vfd, ctx);

	return 0;
}

static void cal_ctx_v4l2_cleanup(struct cal_ctx *ctx)
{
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
}

/* ------------------------------------------------------------------
 *	Asynchronous V4L2 subdev binding
 * ------------------------------------------------------------------
 */

struct cal_v4l2_async_subdev {
	struct v4l2_async_subdev asd;
	struct cal_ctx *ctx;
};

static inline struct cal_v4l2_async_subdev *
to_cal_asd(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct cal_v4l2_async_subdev, asd);
}

static int cal_async_bound(struct v4l2_async_notifier *notifier,
			   struct v4l2_subdev *subdev,
			   struct v4l2_async_subdev *asd)
{
	struct cal_ctx *ctx = to_cal_asd(asd)->ctx;

	if (ctx->phy->sensor) {
		ctx_info(ctx, "Rejecting subdev %s (Already set!!)",
			 subdev->name);
		return 0;
	}

	ctx->phy->sensor = subdev;
	ctx_dbg(1, ctx, "Using sensor %s for capture\n", subdev->name);

	return cal_ctx_v4l2_init_formats(ctx);
}

static int cal_async_complete(struct v4l2_async_notifier *notifier)
{
	struct cal_ctx *ctx = notifier_to_ctx(notifier);

	cal_ctx_v4l2_register(ctx);
	return 0;
}

static const struct v4l2_async_notifier_operations cal_async_ops = {
	.bound = cal_async_bound,
	.complete = cal_async_complete,
};

static int of_cal_create_instance(struct cal_ctx *ctx, int inst)
{
	struct cal_v4l2_async_subdev *casd;
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	int ret;

	v4l2_async_notifier_init(&ctx->notifier);
	ctx->notifier.ops = &cal_async_ops;

	fwnode = of_fwnode_handle(ctx->phy->sensor_node);
	asd = v4l2_async_notifier_add_fwnode_subdev(&ctx->notifier, fwnode,
						    sizeof(*asd));
	if (IS_ERR(asd)) {
		ctx_err(ctx, "Failed to add subdev to notifier\n");
		return PTR_ERR(asd);
	}

	casd = to_cal_asd(asd);
	casd->ctx = ctx;

	ret = v4l2_async_notifier_register(&ctx->cal->v4l2_dev,
					   &ctx->notifier);
	if (ret) {
		ctx_err(ctx, "Error registering async notifier\n");
		v4l2_async_notifier_cleanup(&ctx->notifier);
		return ret;
	}

	return 0;
}

/* ------------------------------------------------------------------
 *	Initialization and module stuff
 * ------------------------------------------------------------------
 */

static struct cal_ctx *cal_ctx_create(struct cal_dev *cal, int inst)
{
	struct cal_ctx *ctx;
	int ret;

	ctx = devm_kzalloc(&cal->pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->cal = cal;
	ctx->phy = cal->phy[inst];
	ctx->index = inst;
	ctx->cport = inst;

	ret = cal_ctx_v4l2_init(ctx);
	if (ret)
		return NULL;

	ret = of_cal_create_instance(ctx, inst);
	if (ret)
		return NULL;

	return ctx;
}

static const struct of_device_id cal_of_match[] = {
	{
		.compatible = "ti,dra72-cal",
		.data = (void *)&dra72x_cal_data,
	},
	{
		.compatible = "ti,dra72-pre-es2-cal",
		.data = (void *)&dra72x_es1_cal_data,
	},
	{
		.compatible = "ti,dra76-cal",
		.data = (void *)&dra76x_cal_data,
	},
	{
		.compatible = "ti,am654-cal",
		.data = (void *)&am654_cal_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, cal_of_match);

/*
 * Get Revision and HW info
 */
static void cal_get_hwinfo(struct cal_dev *cal)
{
	u32 revision;
	u32 hwinfo;

	revision = reg_read(cal, CAL_HL_REVISION);
	cal_dbg(3, cal, "CAL_HL_REVISION = 0x%08x (expecting 0x40000200)\n",
		revision);

	hwinfo = reg_read(cal, CAL_HL_HWINFO);
	cal_dbg(3, cal, "CAL_HL_HWINFO = 0x%08x (expecting 0xA3C90469)\n",
		hwinfo);
}

static int cal_probe(struct platform_device *pdev)
{
	struct cal_dev *cal;
	struct cal_ctx *ctx;
	bool connected = false;
	unsigned int i;
	int ret;
	int irq;

	cal = devm_kzalloc(&pdev->dev, sizeof(*cal), GFP_KERNEL);
	if (!cal)
		return -ENOMEM;

	cal->data = of_device_get_match_data(&pdev->dev);
	if (!cal->data) {
		dev_err(&pdev->dev, "Could not get feature data based on compatible version\n");
		return -ENODEV;
	}

	cal->pdev = pdev;
	platform_set_drvdata(pdev, cal);

	/* Acquire resources: clocks, CAMERARX regmap, I/O memory and IRQ. */
	cal->fclk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(cal->fclk)) {
		dev_err(&pdev->dev, "cannot get CAL fclk\n");
		return PTR_ERR(cal->fclk);
	}

	ret = cal_camerarx_init_regmap(cal);
	if (ret < 0)
		return ret;

	cal->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"cal_top");
	cal->base = devm_ioremap_resource(&pdev->dev, cal->res);
	if (IS_ERR(cal->base))
		return PTR_ERR(cal->base);

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		cal->res->name, &cal->res->start, &cal->res->end);

	irq = platform_get_irq(pdev, 0);
	cal_dbg(1, cal, "got irq# %d\n", irq);
	ret = devm_request_irq(&pdev->dev, irq, cal_irq, 0, CAL_MODULE_NAME,
			       cal);
	if (ret)
		return ret;

	/* Create CAMERARX PHYs. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		cal->phy[i] = cal_camerarx_create(cal, i);
		if (IS_ERR(cal->phy[i])) {
			ret = PTR_ERR(cal->phy[i]);
			cal->phy[i] = NULL;
			goto error_camerarx;
		}

		if (cal->phy[i]->sensor_node)
			connected = true;
	}

	if (!connected) {
		cal_err(cal, "Neither port is configured, no point in staying up\n");
		ret = -ENODEV;
		goto error_camerarx;
	}

	/* Register the V4L2 device. */
	ret = v4l2_device_register(&pdev->dev, &cal->v4l2_dev);
	if (ret) {
		cal_err(cal, "Failed to register V4L2 device\n");
		goto error_camerarx;
	}

	/* Create contexts. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		if (!cal->phy[i]->sensor_node)
			continue;

		cal->ctx[i] = cal_ctx_create(cal, i);
		if (!cal->ctx[i]) {
			cal_err(cal, "Failed to create context %u\n", i);
			ret = -ENODEV;
			goto error_context;
		}
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	/* Read the revision and hardware info to verify hardware access. */
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		goto error_pm_runtime;

	cal_get_hwinfo(cal);
	pm_runtime_put_sync(&pdev->dev);

	return 0;

error_pm_runtime:
	vb2_dma_contig_clear_max_seg_size(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

error_context:
	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		ctx = cal->ctx[i];
		if (ctx) {
			v4l2_async_notifier_unregister(&ctx->notifier);
			v4l2_async_notifier_cleanup(&ctx->notifier);
			cal_ctx_v4l2_cleanup(ctx);
		}
	}

	v4l2_device_unregister(&cal->v4l2_dev);

error_camerarx:
	for (i = 0; i < ARRAY_SIZE(cal->phy); i++)
		cal_camerarx_destroy(cal->phy[i]);

	return ret;
}

static int cal_remove(struct platform_device *pdev)
{
	struct cal_dev *cal = platform_get_drvdata(pdev);
	struct cal_ctx *ctx;
	unsigned int i;

	cal_dbg(1, cal, "Removing %s\n", CAL_MODULE_NAME);

	pm_runtime_get_sync(&pdev->dev);

	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		ctx = cal->ctx[i];
		if (ctx) {
			ctx_dbg(1, ctx, "unregistering %s\n",
				video_device_node_name(&ctx->vdev));
			cal_ctx_v4l2_unregister(ctx);
			cal_camerarx_disable(ctx->phy);
			v4l2_async_notifier_unregister(&ctx->notifier);
			v4l2_async_notifier_cleanup(&ctx->notifier);
			cal_ctx_v4l2_cleanup(ctx);
		}
	}

	v4l2_device_unregister(&cal->v4l2_dev);

	for (i = 0; i < ARRAY_SIZE(cal->phy); i++)
		cal_camerarx_destroy(cal->phy[i]);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	vb2_dma_contig_clear_max_seg_size(&pdev->dev);

	return 0;
}

static int cal_runtime_resume(struct device *dev)
{
	struct cal_dev *cal = dev_get_drvdata(dev);

	if (cal->data->flags & DRA72_CAL_PRE_ES2_LDO_DISABLE) {
		/*
		 * Apply errata on both port everytime we (re-)enable
		 * the clock
		 */
		cal_camerarx_i913_errata(cal->phy[0]);
		cal_camerarx_i913_errata(cal->phy[1]);
	}

	return 0;
}

static const struct dev_pm_ops cal_pm_ops = {
	.runtime_resume = cal_runtime_resume,
};

static struct platform_driver cal_pdrv = {
	.probe		= cal_probe,
	.remove		= cal_remove,
	.driver		= {
		.name	= CAL_MODULE_NAME,
		.pm	= &cal_pm_ops,
		.of_match_table = cal_of_match,
	},
};

module_platform_driver(cal_pdrv);

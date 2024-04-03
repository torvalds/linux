// SPDX-License-Identifier: GPL-2.0
/*
 * NXP i.MX8MQ SoC series MIPI-CSI2 receiver driver
 *
 * Copyright (C) 2021 Purism SPC
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#define MIPI_CSI2_DRIVER_NAME			"imx8mq-mipi-csi2"
#define MIPI_CSI2_SUBDEV_NAME			MIPI_CSI2_DRIVER_NAME

#define MIPI_CSI2_PAD_SINK			0
#define MIPI_CSI2_PAD_SOURCE			1
#define MIPI_CSI2_PADS_NUM			2

#define MIPI_CSI2_DEF_PIX_WIDTH			640
#define MIPI_CSI2_DEF_PIX_HEIGHT		480

/* Register map definition */

/* i.MX8MQ CSI-2 controller CSR */
#define CSI2RX_CFG_NUM_LANES			0x100
#define CSI2RX_CFG_DISABLE_DATA_LANES		0x104
#define CSI2RX_BIT_ERR				0x108
#define CSI2RX_IRQ_STATUS			0x10c
#define CSI2RX_IRQ_MASK				0x110
#define CSI2RX_IRQ_MASK_ALL			0x1ff
#define CSI2RX_IRQ_MASK_ULPS_STATUS_CHANGE	0x8
#define CSI2RX_ULPS_STATUS			0x114
#define CSI2RX_PPI_ERRSOT_HS			0x118
#define CSI2RX_PPI_ERRSOTSYNC_HS		0x11c
#define CSI2RX_PPI_ERRESC			0x120
#define CSI2RX_PPI_ERRSYNCESC			0x124
#define CSI2RX_PPI_ERRCONTROL			0x128
#define CSI2RX_CFG_DISABLE_PAYLOAD_0		0x12c
#define CSI2RX_CFG_VID_VC_IGNORE		0x180
#define CSI2RX_CFG_VID_VC			0x184
#define CSI2RX_CFG_VID_P_FIFO_SEND_LEVEL	0x188
#define CSI2RX_CFG_DISABLE_PAYLOAD_1		0x130

enum {
	ST_POWERED	= 1,
	ST_STREAMING	= 2,
	ST_SUSPENDED	= 4,
};

enum imx8mq_mipi_csi_clk {
	CSI2_CLK_CORE,
	CSI2_CLK_ESC,
	CSI2_CLK_UI,
	CSI2_NUM_CLKS,
};

static const char * const imx8mq_mipi_csi_clk_id[CSI2_NUM_CLKS] = {
	[CSI2_CLK_CORE] = "core",
	[CSI2_CLK_ESC] = "esc",
	[CSI2_CLK_UI] = "ui",
};

#define CSI2_NUM_CLKS	ARRAY_SIZE(imx8mq_mipi_csi_clk_id)

#define	GPR_CSI2_1_RX_ENABLE		BIT(13)
#define	GPR_CSI2_1_VID_INTFC_ENB	BIT(12)
#define	GPR_CSI2_1_HSEL			BIT(10)
#define	GPR_CSI2_1_CONT_CLK_MODE	BIT(8)
#define	GPR_CSI2_1_S_PRG_RXHS_SETTLE(x)	(((x) & 0x3f) << 2)

/*
 * The send level configures the number of entries that must accumulate in
 * the Pixel FIFO before the data will be transferred to the video output.
 * The exact value needed for this configuration is dependent on the rate at
 * which the sensor transfers data to the CSI-2 Controller and the user
 * video clock.
 *
 * The calculation is the classical rate-in rate-out type of problem: If the
 * video bandwidth is 10% faster than the incoming mipi data and the video
 * line length is 500 pixels, then the fifo should be allowed to fill
 * 10% of the line length or 50 pixels. If the gap data is ok, then the level
 * can be set to 16 and ignored.
 */
#define CSI2RX_SEND_LEVEL			64

struct csi_state {
	struct device *dev;
	void __iomem *regs;
	struct clk_bulk_data clks[CSI2_NUM_CLKS];
	struct reset_control *rst;
	struct regulator *mipi_phy_regulator;

	struct v4l2_subdev sd;
	struct media_pad pads[MIPI_CSI2_PADS_NUM];
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *src_sd;

	struct v4l2_mbus_config_mipi_csi2 bus;

	struct mutex lock; /* Protect state */
	u32 state;

	struct regmap *phy_gpr;
	u8 phy_gpr_reg;

	struct icc_path			*icc_path;
	s32				icc_path_bw;
};

/* -----------------------------------------------------------------------------
 * Format helpers
 */

struct csi2_pix_format {
	u32 code;
	u8 width;
};

static const struct csi2_pix_format imx8mq_mipi_csi_formats[] = {
	/* RAW (Bayer and greyscale) formats. */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_Y12_1X12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.width = 14,
	},
	/* YUV formats */
	{
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.width = 16,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.width = 16,
	}
};

static const struct csi2_pix_format *find_csi2_format(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx8mq_mipi_csi_formats); i++)
		if (code == imx8mq_mipi_csi_formats[i].code)
			return &imx8mq_mipi_csi_formats[i];
	return NULL;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration
 */

static inline void imx8mq_mipi_csi_write(struct csi_state *state, u32 reg, u32 val)
{
	writel(val, state->regs + reg);
}

static int imx8mq_mipi_csi_sw_reset(struct csi_state *state)
{
	int ret;

	/*
	 * these are most likely self-clearing reset bits. to make it
	 * more clear, the reset-imx7 driver should implement the
	 * .reset() operation.
	 */
	ret = reset_control_assert(state->rst);
	if (ret < 0) {
		dev_err(state->dev, "Failed to assert resets: %d\n", ret);
		return ret;
	}

	return 0;
}

static void imx8mq_mipi_csi_set_params(struct csi_state *state)
{
	int lanes = state->bus.num_data_lanes;

	imx8mq_mipi_csi_write(state, CSI2RX_CFG_NUM_LANES, lanes - 1);
	imx8mq_mipi_csi_write(state, CSI2RX_CFG_DISABLE_DATA_LANES,
			      (0xf << lanes) & 0xf);
	imx8mq_mipi_csi_write(state, CSI2RX_IRQ_MASK, CSI2RX_IRQ_MASK_ALL);
	/*
	 * 0x180 bit 0 controls the Virtual Channel behaviour: when set the
	 * interface ignores the Virtual Channel (VC) field in received packets;
	 * when cleared it causes the interface to only accept packets whose VC
	 * matches the value to which VC is set at offset 0x184.
	 */
	imx8mq_mipi_csi_write(state, CSI2RX_CFG_VID_VC_IGNORE, 1);
	imx8mq_mipi_csi_write(state, CSI2RX_CFG_VID_P_FIFO_SEND_LEVEL,
			      CSI2RX_SEND_LEVEL);
}

static int imx8mq_mipi_csi_clk_enable(struct csi_state *state)
{
	return clk_bulk_prepare_enable(CSI2_NUM_CLKS, state->clks);
}

static void imx8mq_mipi_csi_clk_disable(struct csi_state *state)
{
	clk_bulk_disable_unprepare(CSI2_NUM_CLKS, state->clks);
}

static int imx8mq_mipi_csi_clk_get(struct csi_state *state)
{
	unsigned int i;

	for (i = 0; i < CSI2_NUM_CLKS; i++)
		state->clks[i].id = imx8mq_mipi_csi_clk_id[i];

	return devm_clk_bulk_get(state->dev, CSI2_NUM_CLKS, state->clks);
}

static int imx8mq_mipi_csi_calc_hs_settle(struct csi_state *state,
					  struct v4l2_subdev_state *sd_state,
					  u32 *hs_settle)
{
	s64 link_freq;
	u32 lane_rate;
	unsigned long esc_clk_rate;
	u32 min_ths_settle, max_ths_settle, ths_settle_ns, esc_clk_period_ns;
	const struct v4l2_mbus_framefmt *fmt;
	const struct csi2_pix_format *csi2_fmt;

	/* Calculate the line rate from the pixel rate. */

	fmt = v4l2_subdev_state_get_format(sd_state, MIPI_CSI2_PAD_SINK);
	csi2_fmt = find_csi2_format(fmt->code);

	link_freq = v4l2_get_link_freq(state->src_sd->ctrl_handler,
				       csi2_fmt->width,
				       state->bus.num_data_lanes * 2);
	if (link_freq < 0) {
		dev_err(state->dev, "Unable to obtain link frequency: %d\n",
			(int)link_freq);
		return link_freq;
	}

	lane_rate = link_freq * 2;
	if (lane_rate < 80000000 || lane_rate > 1500000000) {
		dev_dbg(state->dev, "Out-of-bound lane rate %u\n", lane_rate);
		return -EINVAL;
	}

	/*
	 * The D-PHY specification requires Ths-settle to be in the range
	 * 85ns + 6*UI to 140ns + 10*UI, with the unit interval UI being half
	 * the clock period.
	 *
	 * The Ths-settle value is expressed in the hardware as a multiple of
	 * the Esc clock period:
	 *
	 * Ths-settle = (PRG_RXHS_SETTLE + 1) * Tperiod of RxClkInEsc
	 *
	 * Due to the one cycle inaccuracy introduced by rounding, the
	 * documentation recommends picking a value away from the boundaries.
	 * Let's pick the average.
	 */
	esc_clk_rate = clk_get_rate(state->clks[CSI2_CLK_ESC].clk);
	if (!esc_clk_rate) {
		dev_err(state->dev, "Could not get esc clock rate.\n");
		return -EINVAL;
	}

	dev_dbg(state->dev, "esc clk rate: %lu\n", esc_clk_rate);
	esc_clk_period_ns = 1000000000 / esc_clk_rate;

	min_ths_settle = 85 + 6 * 1000000 / (lane_rate / 1000);
	max_ths_settle = 140 + 10 * 1000000 / (lane_rate / 1000);
	ths_settle_ns = (min_ths_settle + max_ths_settle) / 2;

	*hs_settle = ths_settle_ns / esc_clk_period_ns - 1;

	dev_dbg(state->dev, "lane rate %u Ths_settle %u hs_settle %u\n",
		lane_rate, ths_settle_ns, *hs_settle);

	return 0;
}

static int imx8mq_mipi_csi_start_stream(struct csi_state *state,
					struct v4l2_subdev_state *sd_state)
{
	int ret;
	u32 hs_settle = 0;

	ret = imx8mq_mipi_csi_sw_reset(state);
	if (ret)
		return ret;

	imx8mq_mipi_csi_set_params(state);
	ret = imx8mq_mipi_csi_calc_hs_settle(state, sd_state, &hs_settle);
	if (ret)
		return ret;

	regmap_update_bits(state->phy_gpr,
			   state->phy_gpr_reg,
			   0x3fff,
			   GPR_CSI2_1_RX_ENABLE |
			   GPR_CSI2_1_VID_INTFC_ENB |
			   GPR_CSI2_1_HSEL |
			   GPR_CSI2_1_CONT_CLK_MODE |
			   GPR_CSI2_1_S_PRG_RXHS_SETTLE(hs_settle));

	return 0;
}

static void imx8mq_mipi_csi_stop_stream(struct csi_state *state)
{
	imx8mq_mipi_csi_write(state, CSI2RX_CFG_DISABLE_DATA_LANES, 0xf);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static struct csi_state *mipi_sd_to_csi2_state(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi_state, sd);
}

static int imx8mq_mipi_csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi_state *state = mipi_sd_to_csi2_state(sd);
	struct v4l2_subdev_state *sd_state;
	int ret = 0;

	if (enable) {
		ret = pm_runtime_resume_and_get(state->dev);
		if (ret < 0)
			return ret;
	}

	mutex_lock(&state->lock);

	if (enable) {
		if (state->state & ST_SUSPENDED) {
			ret = -EBUSY;
			goto unlock;
		}

		sd_state = v4l2_subdev_lock_and_get_active_state(sd);
		ret = imx8mq_mipi_csi_start_stream(state, sd_state);
		v4l2_subdev_unlock_state(sd_state);

		if (ret < 0)
			goto unlock;

		ret = v4l2_subdev_call(state->src_sd, video, s_stream, 1);
		if (ret < 0)
			goto unlock;

		state->state |= ST_STREAMING;
	} else {
		v4l2_subdev_call(state->src_sd, video, s_stream, 0);
		imx8mq_mipi_csi_stop_stream(state);
		state->state &= ~ST_STREAMING;
	}

unlock:
	mutex_unlock(&state->lock);

	if (!enable || ret < 0)
		pm_runtime_put(state->dev);

	return ret;
}

static int imx8mq_mipi_csi_init_state(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *fmt_sink;
	struct v4l2_mbus_framefmt *fmt_source;

	fmt_sink = v4l2_subdev_state_get_format(sd_state, MIPI_CSI2_PAD_SINK);
	fmt_source = v4l2_subdev_state_get_format(sd_state,
						  MIPI_CSI2_PAD_SOURCE);

	fmt_sink->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	fmt_sink->width = MIPI_CSI2_DEF_PIX_WIDTH;
	fmt_sink->height = MIPI_CSI2_DEF_PIX_HEIGHT;
	fmt_sink->field = V4L2_FIELD_NONE;

	fmt_sink->colorspace = V4L2_COLORSPACE_RAW;
	fmt_sink->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt_sink->colorspace);
	fmt_sink->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt_sink->colorspace);
	fmt_sink->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, fmt_sink->colorspace,
					      fmt_sink->ycbcr_enc);

	*fmt_source = *fmt_sink;

	return 0;
}

static int imx8mq_mipi_csi_enum_mbus_code(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *sd_state,
					  struct v4l2_subdev_mbus_code_enum *code)
{
	/*
	 * We can't transcode in any way, the source format is identical
	 * to the sink format.
	 */
	if (code->pad == MIPI_CSI2_PAD_SOURCE) {
		struct v4l2_mbus_framefmt *fmt;

		if (code->index > 0)
			return -EINVAL;

		fmt = v4l2_subdev_state_get_format(sd_state, code->pad);
		code->code = fmt->code;
		return 0;
	}

	if (code->pad != MIPI_CSI2_PAD_SINK)
		return -EINVAL;

	if (code->index >= ARRAY_SIZE(imx8mq_mipi_csi_formats))
		return -EINVAL;

	code->code = imx8mq_mipi_csi_formats[code->index].code;

	return 0;
}

static int imx8mq_mipi_csi_set_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_format *sdformat)
{
	const struct csi2_pix_format *csi2_fmt;
	struct v4l2_mbus_framefmt *fmt;

	/*
	 * The device can't transcode in any way, the source format can't be
	 * modified.
	 */
	if (sdformat->pad == MIPI_CSI2_PAD_SOURCE)
		return v4l2_subdev_get_fmt(sd, sd_state, sdformat);

	if (sdformat->pad != MIPI_CSI2_PAD_SINK)
		return -EINVAL;

	csi2_fmt = find_csi2_format(sdformat->format.code);
	if (!csi2_fmt)
		csi2_fmt = &imx8mq_mipi_csi_formats[0];

	fmt = v4l2_subdev_state_get_format(sd_state, sdformat->pad);

	fmt->code = csi2_fmt->code;
	fmt->width = sdformat->format.width;
	fmt->height = sdformat->format.height;

	sdformat->format = *fmt;

	/* Propagate the format from sink to source. */
	fmt = v4l2_subdev_state_get_format(sd_state, MIPI_CSI2_PAD_SOURCE);
	*fmt = sdformat->format;

	return 0;
}

static const struct v4l2_subdev_video_ops imx8mq_mipi_csi_video_ops = {
	.s_stream	= imx8mq_mipi_csi_s_stream,
};

static const struct v4l2_subdev_pad_ops imx8mq_mipi_csi_pad_ops = {
	.enum_mbus_code		= imx8mq_mipi_csi_enum_mbus_code,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= imx8mq_mipi_csi_set_fmt,
};

static const struct v4l2_subdev_ops imx8mq_mipi_csi_subdev_ops = {
	.video	= &imx8mq_mipi_csi_video_ops,
	.pad	= &imx8mq_mipi_csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx8mq_mipi_csi_internal_ops = {
	.init_state		= imx8mq_mipi_csi_init_state,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

static const struct media_entity_operations imx8mq_mipi_csi_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

/* -----------------------------------------------------------------------------
 * Async subdev notifier
 */

static struct csi_state *
mipi_notifier_to_csi2_state(struct v4l2_async_notifier *n)
{
	return container_of(n, struct csi_state, notifier);
}

static int imx8mq_mipi_csi_notify_bound(struct v4l2_async_notifier *notifier,
					struct v4l2_subdev *sd,
					struct v4l2_async_connection *asd)
{
	struct csi_state *state = mipi_notifier_to_csi2_state(notifier);
	struct media_pad *sink = &state->sd.entity.pads[MIPI_CSI2_PAD_SINK];

	state->src_sd = sd;

	return v4l2_create_fwnode_links_to_pad(sd, sink, MEDIA_LNK_FL_ENABLED |
					       MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations imx8mq_mipi_csi_notify_ops = {
	.bound = imx8mq_mipi_csi_notify_bound,
};

static int imx8mq_mipi_csi_async_register(struct csi_state *state)
{
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep;
	unsigned int i;
	int ret;

	v4l2_async_subdev_nf_init(&state->notifier, &state->sd);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(state->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENOTCONN;

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		goto err_parse;

	for (i = 0; i < vep.bus.mipi_csi2.num_data_lanes; ++i) {
		if (vep.bus.mipi_csi2.data_lanes[i] != i + 1) {
			dev_err(state->dev,
				"data lanes reordering is not supported");
			ret = -EINVAL;
			goto err_parse;
		}
	}

	state->bus = vep.bus.mipi_csi2;

	dev_dbg(state->dev, "data lanes: %d flags: 0x%08x\n",
		state->bus.num_data_lanes,
		state->bus.flags);

	asd = v4l2_async_nf_add_fwnode_remote(&state->notifier, ep,
					      struct v4l2_async_connection);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto err_parse;
	}

	fwnode_handle_put(ep);

	state->notifier.ops = &imx8mq_mipi_csi_notify_ops;

	ret = v4l2_async_nf_register(&state->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&state->sd);

err_parse:
	fwnode_handle_put(ep);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Suspend/resume
 */

static void imx8mq_mipi_csi_pm_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	mutex_lock(&state->lock);

	if (state->state & ST_POWERED) {
		imx8mq_mipi_csi_stop_stream(state);
		imx8mq_mipi_csi_clk_disable(state);
		state->state &= ~ST_POWERED;
	}

	mutex_unlock(&state->lock);
}

static int imx8mq_mipi_csi_pm_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);
	struct v4l2_subdev_state *sd_state;
	int ret = 0;

	mutex_lock(&state->lock);

	if (!(state->state & ST_POWERED)) {
		state->state |= ST_POWERED;
		ret = imx8mq_mipi_csi_clk_enable(state);
	}
	if (state->state & ST_STREAMING) {
		sd_state = v4l2_subdev_lock_and_get_active_state(sd);
		ret = imx8mq_mipi_csi_start_stream(state, sd_state);
		v4l2_subdev_unlock_state(sd_state);
		if (ret)
			goto unlock;
	}

	state->state &= ~ST_SUSPENDED;

unlock:
	mutex_unlock(&state->lock);

	return ret ? -EAGAIN : 0;
}

static int __maybe_unused imx8mq_mipi_csi_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	imx8mq_mipi_csi_pm_suspend(dev);

	state->state |= ST_SUSPENDED;

	return 0;
}

static int __maybe_unused imx8mq_mipi_csi_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	if (!(state->state & ST_SUSPENDED))
		return 0;

	return imx8mq_mipi_csi_pm_resume(dev);
}

static int __maybe_unused imx8mq_mipi_csi_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);
	int ret;

	imx8mq_mipi_csi_pm_suspend(dev);

	ret = icc_set_bw(state->icc_path, 0, 0);
	if (ret)
		dev_err(dev, "icc_set_bw failed with %d\n", ret);

	return ret;
}

static int __maybe_unused imx8mq_mipi_csi_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);
	int ret;

	ret = icc_set_bw(state->icc_path, 0, state->icc_path_bw);
	if (ret) {
		dev_err(dev, "icc_set_bw failed with %d\n", ret);
		return ret;
	}

	return imx8mq_mipi_csi_pm_resume(dev);
}

static const struct dev_pm_ops imx8mq_mipi_csi_pm_ops = {
	SET_RUNTIME_PM_OPS(imx8mq_mipi_csi_runtime_suspend,
			   imx8mq_mipi_csi_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(imx8mq_mipi_csi_suspend, imx8mq_mipi_csi_resume)
};

/* -----------------------------------------------------------------------------
 * Probe/remove & platform driver
 */

static int imx8mq_mipi_csi_subdev_init(struct csi_state *state)
{
	struct v4l2_subdev *sd = &state->sd;
	int ret;

	v4l2_subdev_init(sd, &imx8mq_mipi_csi_subdev_ops);
	sd->internal_ops = &imx8mq_mipi_csi_internal_ops;
	sd->owner = THIS_MODULE;
	snprintf(sd->name, sizeof(sd->name), "%s %s",
		 MIPI_CSI2_SUBDEV_NAME, dev_name(state->dev));

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &imx8mq_mipi_csi_entity_ops;

	sd->dev = state->dev;

	state->pads[MIPI_CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK
					 | MEDIA_PAD_FL_MUST_CONNECT;
	state->pads[MIPI_CSI2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE
					   | MEDIA_PAD_FL_MUST_CONNECT;
	ret = media_entity_pads_init(&sd->entity, MIPI_CSI2_PADS_NUM,
				     state->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret) {
		media_entity_cleanup(&sd->entity);
		return ret;
	}

	return 0;
}

static void imx8mq_mipi_csi_release_icc(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(&pdev->dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	icc_put(state->icc_path);
}

static int imx8mq_mipi_csi_init_icc(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(&pdev->dev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	/* Optional interconnect request */
	state->icc_path = of_icc_get(&pdev->dev, "dram");
	if (IS_ERR_OR_NULL(state->icc_path))
		return PTR_ERR_OR_ZERO(state->icc_path);

	state->icc_path_bw = MBps_to_icc(700);

	return 0;
}

static int imx8mq_mipi_csi_parse_dt(struct csi_state *state)
{
	struct device *dev = state->dev;
	struct device_node *np = state->dev->of_node;
	struct device_node *node;
	phandle ph;
	u32 out_val[2];
	int ret = 0;

	state->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(state->rst)) {
		dev_err(dev, "Failed to get reset: %pe\n", state->rst);
		return PTR_ERR(state->rst);
	}

	ret = of_property_read_u32_array(np, "fsl,mipi-phy-gpr", out_val,
					 ARRAY_SIZE(out_val));
	if (ret) {
		dev_err(dev, "no fsl,mipi-phy-gpr property found: %d\n", ret);
		return ret;
	}

	ph = *out_val;

	node = of_find_node_by_phandle(ph);
	if (!node) {
		dev_err(dev, "Error finding node by phandle\n");
		return -ENODEV;
	}
	state->phy_gpr = syscon_node_to_regmap(node);
	of_node_put(node);
	if (IS_ERR(state->phy_gpr)) {
		dev_err(dev, "failed to get gpr regmap: %pe\n", state->phy_gpr);
		return PTR_ERR(state->phy_gpr);
	}

	state->phy_gpr_reg = out_val[1];
	dev_dbg(dev, "phy gpr register set to 0x%x\n", state->phy_gpr_reg);

	return ret;
}

static int imx8mq_mipi_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi_state *state;
	int ret;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->dev = dev;

	ret = imx8mq_mipi_csi_parse_dt(state);
	if (ret < 0) {
		dev_err(dev, "Failed to parse device tree: %d\n", ret);
		return ret;
	}

	/* Acquire resources. */
	state->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	ret = imx8mq_mipi_csi_clk_get(state);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, &state->sd);

	mutex_init(&state->lock);

	ret = imx8mq_mipi_csi_subdev_init(state);
	if (ret < 0)
		goto mutex;

	ret = imx8mq_mipi_csi_init_icc(pdev);
	if (ret)
		goto mutex;

	/* Enable runtime PM. */
	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = imx8mq_mipi_csi_runtime_resume(dev);
		if (ret < 0)
			goto icc;
	}

	ret = imx8mq_mipi_csi_async_register(state);
	if (ret < 0)
		goto cleanup;

	return 0;

cleanup:
	pm_runtime_disable(&pdev->dev);
	imx8mq_mipi_csi_runtime_suspend(&pdev->dev);

	media_entity_cleanup(&state->sd.entity);
	v4l2_subdev_cleanup(&state->sd);
	v4l2_async_nf_unregister(&state->notifier);
	v4l2_async_nf_cleanup(&state->notifier);
	v4l2_async_unregister_subdev(&state->sd);
icc:
	imx8mq_mipi_csi_release_icc(pdev);
mutex:
	mutex_destroy(&state->lock);

	return ret;
}

static void imx8mq_mipi_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi_state *state = mipi_sd_to_csi2_state(sd);

	v4l2_async_nf_unregister(&state->notifier);
	v4l2_async_nf_cleanup(&state->notifier);
	v4l2_async_unregister_subdev(&state->sd);

	pm_runtime_disable(&pdev->dev);
	imx8mq_mipi_csi_runtime_suspend(&pdev->dev);
	media_entity_cleanup(&state->sd.entity);
	v4l2_subdev_cleanup(&state->sd);
	mutex_destroy(&state->lock);
	pm_runtime_set_suspended(&pdev->dev);
	imx8mq_mipi_csi_release_icc(pdev);
}

static const struct of_device_id imx8mq_mipi_csi_of_match[] = {
	{ .compatible = "fsl,imx8mq-mipi-csi2", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx8mq_mipi_csi_of_match);

static struct platform_driver imx8mq_mipi_csi_driver = {
	.probe		= imx8mq_mipi_csi_probe,
	.remove_new	= imx8mq_mipi_csi_remove,
	.driver		= {
		.of_match_table = imx8mq_mipi_csi_of_match,
		.name		= MIPI_CSI2_DRIVER_NAME,
		.pm		= &imx8mq_mipi_csi_pm_ops,
	},
};

module_platform_driver(imx8mq_mipi_csi_driver);

MODULE_DESCRIPTION("i.MX8MQ MIPI CSI-2 receiver driver");
MODULE_AUTHOR("Martin Kepplinger <martin.kepplinger@puri.sm>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx8mq-mipi-csi2");

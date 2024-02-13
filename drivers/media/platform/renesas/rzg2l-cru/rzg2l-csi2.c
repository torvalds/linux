// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Renesas RZ/G2L MIPI CSI-2 Receiver
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>
#include <linux/units.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

/* LINK registers */
/* Module Configuration Register */
#define CSI2nMCG			0x0
#define CSI2nMCG_SDLN			GENMASK(11, 8)

/* Module Control Register 0 */
#define CSI2nMCT0			0x10
#define CSI2nMCT0_VDLN(x)		((x) << 0)

/* Module Control Register 2 */
#define CSI2nMCT2			0x18
#define CSI2nMCT2_FRRSKW(x)		((x) << 16)
#define CSI2nMCT2_FRRCLK(x)		((x) << 0)

/* Module Control Register 3 */
#define CSI2nMCT3			0x1c
#define CSI2nMCT3_RXEN			BIT(0)

/* Reset Control Register */
#define CSI2nRTCT			0x28
#define CSI2nRTCT_VSRST			BIT(0)

/* Reset Status Register */
#define CSI2nRTST			0x2c
#define CSI2nRTST_VSRSTS		BIT(0)

/* Receive Data Type Enable Low Register */
#define CSI2nDTEL			0x60

/* Receive Data Type Enable High Register */
#define CSI2nDTEH			0x64

/* DPHY registers */
/* D-PHY Control Register 0 */
#define CSIDPHYCTRL0			0x400
#define CSIDPHYCTRL0_EN_LDO1200		BIT(1)
#define CSIDPHYCTRL0_EN_BGR		BIT(0)

/* D-PHY Timing Register 0 */
#define CSIDPHYTIM0			0x404
#define CSIDPHYTIM0_TCLK_MISS(x)	((x) << 24)
#define CSIDPHYTIM0_T_INIT(x)		((x) << 0)

/* D-PHY Timing Register 1 */
#define CSIDPHYTIM1			0x408
#define CSIDPHYTIM1_THS_PREPARE(x)	((x) << 24)
#define CSIDPHYTIM1_TCLK_PREPARE(x)	((x) << 16)
#define CSIDPHYTIM1_THS_SETTLE(x)	((x) << 8)
#define CSIDPHYTIM1_TCLK_SETTLE(x)	((x) << 0)

/* D-PHY Skew Adjustment Function */
#define CSIDPHYSKW0			0x460
#define CSIDPHYSKW0_UTIL_DL0_SKW_ADJ(x)	((x) & 0x3)
#define CSIDPHYSKW0_UTIL_DL1_SKW_ADJ(x)	(((x) & 0x3) << 4)
#define CSIDPHYSKW0_UTIL_DL2_SKW_ADJ(x)	(((x) & 0x3) << 8)
#define CSIDPHYSKW0_UTIL_DL3_SKW_ADJ(x)	(((x) & 0x3) << 12)
#define CSIDPHYSKW0_DEFAULT_SKW		(CSIDPHYSKW0_UTIL_DL0_SKW_ADJ(1) | \
					 CSIDPHYSKW0_UTIL_DL1_SKW_ADJ(1) | \
					 CSIDPHYSKW0_UTIL_DL2_SKW_ADJ(1) | \
					 CSIDPHYSKW0_UTIL_DL3_SKW_ADJ(1))

#define VSRSTS_RETRIES			20

#define RZG2L_CSI2_MIN_WIDTH		320
#define RZG2L_CSI2_MIN_HEIGHT		240
#define RZG2L_CSI2_MAX_WIDTH		2800
#define RZG2L_CSI2_MAX_HEIGHT		4095

#define RZG2L_CSI2_DEFAULT_WIDTH	RZG2L_CSI2_MIN_WIDTH
#define RZG2L_CSI2_DEFAULT_HEIGHT	RZG2L_CSI2_MIN_HEIGHT
#define RZG2L_CSI2_DEFAULT_FMT		MEDIA_BUS_FMT_UYVY8_1X16

enum rzg2l_csi2_pads {
	RZG2L_CSI2_SINK = 0,
	RZG2L_CSI2_SOURCE,
	NR_OF_RZG2L_CSI2_PAD,
};

struct rzg2l_csi2 {
	struct device *dev;
	void __iomem *base;
	struct reset_control *presetn;
	struct reset_control *cmn_rstb;
	struct clk *sysclk;
	struct clk *vclk;
	unsigned long vclk_rate;

	struct v4l2_subdev subdev;
	struct media_pad pads[NR_OF_RZG2L_CSI2_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote_source;

	unsigned short lanes;
	unsigned long hsfreq;

	bool dphy_enabled;
};

struct rzg2l_csi2_timings {
	u32 t_init;
	u32 tclk_miss;
	u32 tclk_settle;
	u32 ths_settle;
	u32 tclk_prepare;
	u32 ths_prepare;
	u32 max_hsfreq;
};

static const struct rzg2l_csi2_timings rzg2l_csi2_global_timings[] = {
	{
		.max_hsfreq = 80,
		.t_init = 79801,
		.tclk_miss = 4,
		.tclk_settle = 23,
		.ths_settle = 31,
		.tclk_prepare = 10,
		.ths_prepare = 19,
	},
	{
		.max_hsfreq = 125,
		.t_init = 79801,
		.tclk_miss = 4,
		.tclk_settle = 23,
		.ths_settle = 28,
		.tclk_prepare = 10,
		.ths_prepare = 19,
	},
	{
		.max_hsfreq = 250,
		.t_init = 79801,
		.tclk_miss = 4,
		.tclk_settle = 23,
		.ths_settle = 22,
		.tclk_prepare = 10,
		.ths_prepare = 16,
	},
	{
		.max_hsfreq = 360,
		.t_init = 79801,
		.tclk_miss = 4,
		.tclk_settle = 18,
		.ths_settle = 19,
		.tclk_prepare = 10,
		.ths_prepare = 10,
	},
	{
		.max_hsfreq = 1500,
		.t_init = 79801,
		.tclk_miss = 4,
		.tclk_settle = 18,
		.ths_settle = 18,
		.tclk_prepare = 10,
		.ths_prepare = 10,
	},
};

struct rzg2l_csi2_format {
	u32 code;
	unsigned int datatype;
	unsigned int bpp;
};

static const struct rzg2l_csi2_format rzg2l_csi2_formats[] = {
	{ .code = MEDIA_BUS_FMT_UYVY8_1X16,	.datatype = 0x1e, .bpp = 16 },
};

static inline struct rzg2l_csi2 *sd_to_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rzg2l_csi2, subdev);
}

static const struct rzg2l_csi2_format *rzg2l_csi2_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rzg2l_csi2_formats); i++)
		if (rzg2l_csi2_formats[i].code == code)
			return &rzg2l_csi2_formats[i];

	return NULL;
}

static inline struct rzg2l_csi2 *notifier_to_csi2(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rzg2l_csi2, notifier);
}

static u32 rzg2l_csi2_read(struct rzg2l_csi2 *csi2, unsigned int reg)
{
	return ioread32(csi2->base + reg);
}

static void rzg2l_csi2_write(struct rzg2l_csi2 *csi2, unsigned int reg,
			     u32 data)
{
	iowrite32(data, csi2->base + reg);
}

static void rzg2l_csi2_set(struct rzg2l_csi2 *csi2, unsigned int reg, u32 set)
{
	rzg2l_csi2_write(csi2, reg, rzg2l_csi2_read(csi2, reg) | set);
}

static void rzg2l_csi2_clr(struct rzg2l_csi2 *csi2, unsigned int reg, u32 clr)
{
	rzg2l_csi2_write(csi2, reg, rzg2l_csi2_read(csi2, reg) & ~clr);
}

static int rzg2l_csi2_calc_mbps(struct rzg2l_csi2 *csi2)
{
	struct v4l2_subdev *source = csi2->remote_source;
	const struct rzg2l_csi2_format *format;
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_state *state;
	struct v4l2_ctrl *ctrl;
	u64 mbps;

	/* Read the pixel rate control from remote. */
	ctrl = v4l2_ctrl_find(source->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(csi2->dev, "no pixel rate control in subdev %s\n",
			source->name);
		return -EINVAL;
	}

	state = v4l2_subdev_lock_and_get_active_state(&csi2->subdev);
	fmt = v4l2_subdev_state_get_format(state, RZG2L_CSI2_SINK);
	format = rzg2l_csi2_code_to_fmt(fmt->code);
	v4l2_subdev_unlock_state(state);

	/*
	 * Calculate hsfreq in Mbps
	 * hsfreq = (pixel_rate * bits_per_sample) / number_of_lanes
	 */
	mbps = v4l2_ctrl_g_ctrl_int64(ctrl) * format->bpp;
	do_div(mbps, csi2->lanes * 1000000);

	return mbps;
}

/* -----------------------------------------------------------------------------
 * DPHY setting
 */

static int rzg2l_csi2_dphy_disable(struct rzg2l_csi2 *csi2)
{
	int ret;

	/* Reset the CRU (D-PHY) */
	ret = reset_control_assert(csi2->cmn_rstb);
	if (ret)
		return ret;

	/* Stop the D-PHY clock */
	clk_disable_unprepare(csi2->sysclk);

	/* Cancel the EN_LDO1200 register setting */
	rzg2l_csi2_clr(csi2, CSIDPHYCTRL0, CSIDPHYCTRL0_EN_LDO1200);

	/* Cancel the EN_BGR register setting */
	rzg2l_csi2_clr(csi2, CSIDPHYCTRL0, CSIDPHYCTRL0_EN_BGR);

	csi2->dphy_enabled = false;

	return 0;
}

static int rzg2l_csi2_dphy_enable(struct rzg2l_csi2 *csi2)
{
	const struct rzg2l_csi2_timings *dphy_timing;
	u32 dphytim0, dphytim1;
	unsigned int i;
	int mbps;
	int ret;

	mbps = rzg2l_csi2_calc_mbps(csi2);
	if (mbps < 0)
		return mbps;

	csi2->hsfreq = mbps;

	/* Set DPHY timing parameters */
	for (i = 0; i < ARRAY_SIZE(rzg2l_csi2_global_timings); ++i) {
		dphy_timing = &rzg2l_csi2_global_timings[i];

		if (csi2->hsfreq <= dphy_timing->max_hsfreq)
			break;
	}

	if (i >= ARRAY_SIZE(rzg2l_csi2_global_timings))
		return -EINVAL;

	/* Set D-PHY timing parameters */
	dphytim0 = CSIDPHYTIM0_TCLK_MISS(dphy_timing->tclk_miss) |
			CSIDPHYTIM0_T_INIT(dphy_timing->t_init);
	dphytim1 = CSIDPHYTIM1_THS_PREPARE(dphy_timing->ths_prepare) |
			CSIDPHYTIM1_TCLK_PREPARE(dphy_timing->tclk_prepare) |
			CSIDPHYTIM1_THS_SETTLE(dphy_timing->ths_settle) |
			CSIDPHYTIM1_TCLK_SETTLE(dphy_timing->tclk_settle);
	rzg2l_csi2_write(csi2, CSIDPHYTIM0, dphytim0);
	rzg2l_csi2_write(csi2, CSIDPHYTIM1, dphytim1);

	/* Enable D-PHY power control 0 */
	rzg2l_csi2_write(csi2, CSIDPHYSKW0, CSIDPHYSKW0_DEFAULT_SKW);

	/* Set the EN_BGR bit */
	rzg2l_csi2_set(csi2, CSIDPHYCTRL0, CSIDPHYCTRL0_EN_BGR);

	/* Delay 20us to be stable */
	usleep_range(20, 40);

	/* Enable D-PHY power control 1 */
	rzg2l_csi2_set(csi2, CSIDPHYCTRL0, CSIDPHYCTRL0_EN_LDO1200);

	/* Delay 10us to be stable */
	usleep_range(10, 20);

	/* Start supplying the internal clock for the D-PHY block */
	ret = clk_prepare_enable(csi2->sysclk);
	if (ret)
		rzg2l_csi2_dphy_disable(csi2);

	csi2->dphy_enabled = true;

	return ret;
}

static int rzg2l_csi2_dphy_setting(struct v4l2_subdev *sd, bool on)
{
	struct rzg2l_csi2 *csi2 = sd_to_csi2(sd);

	if (on)
		return rzg2l_csi2_dphy_enable(csi2);

	return rzg2l_csi2_dphy_disable(csi2);
}

static int rzg2l_csi2_mipi_link_enable(struct rzg2l_csi2 *csi2)
{
	unsigned long vclk_rate = csi2->vclk_rate / HZ_PER_MHZ;
	u32 frrskw, frrclk, frrskw_coeff, frrclk_coeff;

	/* Select data lanes */
	rzg2l_csi2_write(csi2, CSI2nMCT0, CSI2nMCT0_VDLN(csi2->lanes));

	frrskw_coeff = 3 * vclk_rate * 8;
	frrclk_coeff = frrskw_coeff / 2;
	frrskw = DIV_ROUND_UP(frrskw_coeff, csi2->hsfreq);
	frrclk = DIV_ROUND_UP(frrclk_coeff, csi2->hsfreq);
	rzg2l_csi2_write(csi2, CSI2nMCT2, CSI2nMCT2_FRRSKW(frrskw) |
			 CSI2nMCT2_FRRCLK(frrclk));

	/*
	 * Select data type.
	 * FS, FE, LS, LE, Generic Short Packet Codes 1 to 8,
	 * Generic Long Packet Data Types 1 to 4 YUV422 8-bit,
	 * RGB565, RGB888, RAW8 to RAW20, User-defined 8-bit
	 * data types 1 to 8
	 */
	rzg2l_csi2_write(csi2, CSI2nDTEL, 0xf778ff0f);
	rzg2l_csi2_write(csi2, CSI2nDTEH, 0x00ffff1f);

	clk_disable_unprepare(csi2->vclk);

	/* Enable LINK reception */
	rzg2l_csi2_write(csi2, CSI2nMCT3, CSI2nMCT3_RXEN);

	return clk_prepare_enable(csi2->vclk);
}

static int rzg2l_csi2_mipi_link_disable(struct rzg2l_csi2 *csi2)
{
	unsigned int timeout = VSRSTS_RETRIES;

	/* Stop LINK reception */
	rzg2l_csi2_clr(csi2, CSI2nMCT3, CSI2nMCT3_RXEN);

	/* Request a software reset of the LINK Video Pixel Interface */
	rzg2l_csi2_write(csi2, CSI2nRTCT, CSI2nRTCT_VSRST);

	/* Make sure CSI2nRTST.VSRSTS bit is cleared */
	while (--timeout) {
		if (!(rzg2l_csi2_read(csi2, CSI2nRTST) & CSI2nRTST_VSRSTS))
			break;
		usleep_range(100, 200);
	}

	if (!timeout)
		dev_err(csi2->dev, "Clearing CSI2nRTST.VSRSTS timed out\n");

	return 0;
}

static int rzg2l_csi2_mipi_link_setting(struct v4l2_subdev *sd, bool on)
{
	struct rzg2l_csi2 *csi2 = sd_to_csi2(sd);
	int ret;

	if (on)
		ret = rzg2l_csi2_mipi_link_enable(csi2);
	else
		ret = rzg2l_csi2_mipi_link_disable(csi2);

	return ret;
}

static int rzg2l_csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rzg2l_csi2 *csi2 = sd_to_csi2(sd);
	int s_stream_ret = 0;
	int ret;

	if (enable) {
		ret = pm_runtime_resume_and_get(csi2->dev);
		if (ret)
			return ret;

		ret = rzg2l_csi2_mipi_link_setting(sd, 1);
		if (ret)
			goto err_pm_put;

		ret = reset_control_deassert(csi2->cmn_rstb);
		if (ret)
			goto err_mipi_link_disable;
	}

	ret = v4l2_subdev_call(csi2->remote_source, video, s_stream, enable);
	if (ret)
		s_stream_ret = ret;

	if (enable && ret)
		goto err_assert_rstb;

	if (!enable) {
		ret = rzg2l_csi2_dphy_setting(sd, 0);
		if (ret && !s_stream_ret)
			s_stream_ret = ret;
		ret = rzg2l_csi2_mipi_link_setting(sd, 0);
		if (ret && !s_stream_ret)
			s_stream_ret = ret;

		pm_runtime_put_sync(csi2->dev);
	}

	return s_stream_ret;

err_assert_rstb:
	reset_control_assert(csi2->cmn_rstb);
err_mipi_link_disable:
	rzg2l_csi2_mipi_link_setting(sd, 0);
err_pm_put:
	pm_runtime_put_sync(csi2->dev);
	return ret;
}

static int rzg2l_csi2_pre_streamon(struct v4l2_subdev *sd, u32 flags)
{
	return rzg2l_csi2_dphy_setting(sd, 1);
}

static int rzg2l_csi2_post_streamoff(struct v4l2_subdev *sd)
{
	struct rzg2l_csi2 *csi2 = sd_to_csi2(sd);

	/*
	 * In ideal case D-PHY will be disabled in s_stream(0) callback
	 * as mentioned in the HW manual. The below will only happen when
	 * pre_streamon succeeds and further down the line s_stream(1)
	 * fails so we need to undo things in post_streamoff.
	 */
	if (csi2->dphy_enabled)
		return rzg2l_csi2_dphy_setting(sd, 0);

	return 0;
}

static int rzg2l_csi2_set_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *src_format;
	struct v4l2_mbus_framefmt *sink_format;

	src_format = v4l2_subdev_state_get_format(state, RZG2L_CSI2_SOURCE);
	if (fmt->pad == RZG2L_CSI2_SOURCE) {
		fmt->format = *src_format;
		return 0;
	}

	sink_format = v4l2_subdev_state_get_format(state, RZG2L_CSI2_SINK);

	if (!rzg2l_csi2_code_to_fmt(fmt->format.code))
		sink_format->code = rzg2l_csi2_formats[0].code;
	else
		sink_format->code = fmt->format.code;

	sink_format->field = V4L2_FIELD_NONE;
	sink_format->colorspace = fmt->format.colorspace;
	sink_format->xfer_func = fmt->format.xfer_func;
	sink_format->ycbcr_enc = fmt->format.ycbcr_enc;
	sink_format->quantization = fmt->format.quantization;
	sink_format->width = clamp_t(u32, fmt->format.width,
				     RZG2L_CSI2_MIN_WIDTH, RZG2L_CSI2_MAX_WIDTH);
	sink_format->height = clamp_t(u32, fmt->format.height,
				      RZG2L_CSI2_MIN_HEIGHT, RZG2L_CSI2_MAX_HEIGHT);
	fmt->format = *sink_format;

	/* propagate format to source pad */
	*src_format = *sink_format;

	return 0;
}

static int rzg2l_csi2_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = { .pad = RZG2L_CSI2_SINK, };

	fmt.format.width = RZG2L_CSI2_DEFAULT_WIDTH;
	fmt.format.height = RZG2L_CSI2_DEFAULT_HEIGHT;
	fmt.format.field = V4L2_FIELD_NONE;
	fmt.format.code = RZG2L_CSI2_DEFAULT_FMT;
	fmt.format.colorspace = V4L2_COLORSPACE_SRGB;
	fmt.format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt.format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt.format.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return rzg2l_csi2_set_format(sd, sd_state, &fmt);
}

static int rzg2l_csi2_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(rzg2l_csi2_formats))
		return -EINVAL;

	code->code = rzg2l_csi2_formats[code->index].code;

	return 0;
}

static int rzg2l_csi2_enum_frame_size(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index != 0)
		return -EINVAL;

	fse->min_width = RZG2L_CSI2_MIN_WIDTH;
	fse->min_height = RZG2L_CSI2_MIN_HEIGHT;
	fse->max_width = RZG2L_CSI2_MAX_WIDTH;
	fse->max_height = RZG2L_CSI2_MAX_HEIGHT;

	return 0;
}

static const struct v4l2_subdev_video_ops rzg2l_csi2_video_ops = {
	.s_stream = rzg2l_csi2_s_stream,
	.pre_streamon = rzg2l_csi2_pre_streamon,
	.post_streamoff = rzg2l_csi2_post_streamoff,
};

static const struct v4l2_subdev_pad_ops rzg2l_csi2_pad_ops = {
	.enum_mbus_code = rzg2l_csi2_enum_mbus_code,
	.enum_frame_size = rzg2l_csi2_enum_frame_size,
	.set_fmt = rzg2l_csi2_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
};

static const struct v4l2_subdev_ops rzg2l_csi2_subdev_ops = {
	.video	= &rzg2l_csi2_video_ops,
	.pad	= &rzg2l_csi2_pad_ops,
};

static const struct v4l2_subdev_internal_ops rzg2l_csi2_internal_ops = {
	.init_state = rzg2l_csi2_init_state,
};

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int rzg2l_csi2_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asd)
{
	struct rzg2l_csi2 *csi2 = notifier_to_csi2(notifier);

	csi2->remote_source = subdev;

	dev_dbg(csi2->dev, "Bound subdev: %s pad\n", subdev->name);

	return media_create_pad_link(&subdev->entity, RZG2L_CSI2_SINK,
				     &csi2->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void rzg2l_csi2_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_connection *asd)
{
	struct rzg2l_csi2 *csi2 = notifier_to_csi2(notifier);

	csi2->remote_source = NULL;

	dev_dbg(csi2->dev, "Unbind subdev %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations rzg2l_csi2_notify_ops = {
	.bound = rzg2l_csi2_notify_bound,
	.unbind = rzg2l_csi2_notify_unbind,
};

static int rzg2l_csi2_parse_v4l2(struct rzg2l_csi2 *csi2,
				 struct v4l2_fwnode_endpoint *vep)
{
	/* Only port 0 endpoint 0 is valid. */
	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	csi2->lanes = vep->bus.mipi_csi2.num_data_lanes;

	return 0;
}

static int rzg2l_csi2_parse_dt(struct rzg2l_csi2 *csi2)
{
	struct v4l2_fwnode_endpoint v4l2_ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct v4l2_async_connection *asd;
	struct fwnode_handle *fwnode;
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi2->dev), 0, 0, 0);
	if (!ep) {
		dev_err(csi2->dev, "Not connected to subdevice\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &v4l2_ep);
	if (ret) {
		dev_err(csi2->dev, "Could not parse v4l2 endpoint\n");
		fwnode_handle_put(ep);
		return -EINVAL;
	}

	ret = rzg2l_csi2_parse_v4l2(csi2, &v4l2_ep);
	if (ret) {
		fwnode_handle_put(ep);
		return ret;
	}

	fwnode = fwnode_graph_get_remote_endpoint(ep);
	fwnode_handle_put(ep);

	v4l2_async_subdev_nf_init(&csi2->notifier, &csi2->subdev);
	csi2->notifier.ops = &rzg2l_csi2_notify_ops;

	asd = v4l2_async_nf_add_fwnode(&csi2->notifier, fwnode,
				       struct v4l2_async_connection);
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	ret = v4l2_async_nf_register(&csi2->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&csi2->notifier);

	return ret;
}

static int rzg2l_validate_csi2_lanes(struct rzg2l_csi2 *csi2)
{
	int lanes;
	int ret;

	if (csi2->lanes != 1 && csi2->lanes != 2 && csi2->lanes != 4) {
		dev_err(csi2->dev, "Unsupported number of data-lanes: %u\n",
			csi2->lanes);
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(csi2->dev);
	if (ret)
		return ret;

	/* Checking the maximum lanes support for CSI-2 module */
	lanes = (rzg2l_csi2_read(csi2, CSI2nMCG) & CSI2nMCG_SDLN) >> 8;
	if (lanes < csi2->lanes) {
		dev_err(csi2->dev,
			"Failed to support %d data lanes\n", csi2->lanes);
		ret = -EINVAL;
	}

	pm_runtime_put_sync(csi2->dev);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver.
 */

static const struct media_entity_operations rzg2l_csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int rzg2l_csi2_probe(struct platform_device *pdev)
{
	struct rzg2l_csi2 *csi2;
	int ret;

	csi2 = devm_kzalloc(&pdev->dev, sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return -ENOMEM;

	csi2->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi2->base))
		return PTR_ERR(csi2->base);

	csi2->cmn_rstb = devm_reset_control_get_exclusive(&pdev->dev, "cmn-rstb");
	if (IS_ERR(csi2->cmn_rstb))
		return dev_err_probe(&pdev->dev, PTR_ERR(csi2->cmn_rstb),
				     "Failed to get cpg cmn-rstb\n");

	csi2->presetn = devm_reset_control_get_shared(&pdev->dev, "presetn");
	if (IS_ERR(csi2->presetn))
		return dev_err_probe(&pdev->dev, PTR_ERR(csi2->presetn),
				     "Failed to get cpg presetn\n");

	csi2->sysclk = devm_clk_get(&pdev->dev, "system");
	if (IS_ERR(csi2->sysclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(csi2->sysclk),
				     "Failed to get system clk\n");

	csi2->vclk = devm_clk_get(&pdev->dev, "video");
	if (IS_ERR(csi2->vclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(csi2->vclk),
				     "Failed to get video clock\n");
	csi2->vclk_rate = clk_get_rate(csi2->vclk);

	csi2->dev = &pdev->dev;

	platform_set_drvdata(pdev, csi2);

	ret = rzg2l_csi2_parse_dt(csi2);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);

	ret = rzg2l_validate_csi2_lanes(csi2);
	if (ret)
		goto error_pm;

	csi2->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&csi2->subdev, &rzg2l_csi2_subdev_ops);
	csi2->subdev.internal_ops = &rzg2l_csi2_internal_ops;
	v4l2_set_subdevdata(&csi2->subdev, &pdev->dev);
	snprintf(csi2->subdev.name, sizeof(csi2->subdev.name),
		 "csi-%s", dev_name(&pdev->dev));
	csi2->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	csi2->subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2->subdev.entity.ops = &rzg2l_csi2_entity_ops;

	csi2->pads[RZG2L_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	/*
	 * TODO: RZ/G2L CSI2 supports 4 virtual channels, as virtual
	 * channels should be implemented by streams API which is under
	 * development lets hardcode to VC0 for now.
	 */
	csi2->pads[RZG2L_CSI2_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&csi2->subdev.entity, 2, csi2->pads);
	if (ret)
		goto error_pm;

	ret = v4l2_subdev_init_finalize(&csi2->subdev);
	if (ret < 0)
		goto error_async;

	ret = v4l2_async_register_subdev(&csi2->subdev);
	if (ret < 0)
		goto error_subdev;

	return 0;

error_subdev:
	v4l2_subdev_cleanup(&csi2->subdev);
error_async:
	v4l2_async_nf_unregister(&csi2->notifier);
	v4l2_async_nf_cleanup(&csi2->notifier);
	media_entity_cleanup(&csi2->subdev.entity);
error_pm:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void rzg2l_csi2_remove(struct platform_device *pdev)
{
	struct rzg2l_csi2 *csi2 = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&csi2->notifier);
	v4l2_async_nf_cleanup(&csi2->notifier);
	v4l2_async_unregister_subdev(&csi2->subdev);
	v4l2_subdev_cleanup(&csi2->subdev);
	media_entity_cleanup(&csi2->subdev.entity);
	pm_runtime_disable(&pdev->dev);
}

static int rzg2l_csi2_pm_runtime_suspend(struct device *dev)
{
	struct rzg2l_csi2 *csi2 = dev_get_drvdata(dev);

	reset_control_assert(csi2->presetn);

	return 0;
}

static int rzg2l_csi2_pm_runtime_resume(struct device *dev)
{
	struct rzg2l_csi2 *csi2 = dev_get_drvdata(dev);

	return reset_control_deassert(csi2->presetn);
}

static const struct dev_pm_ops rzg2l_csi2_pm_ops = {
	RUNTIME_PM_OPS(rzg2l_csi2_pm_runtime_suspend,
		       rzg2l_csi2_pm_runtime_resume, NULL)
};

static const struct of_device_id rzg2l_csi2_of_table[] = {
	{ .compatible = "renesas,rzg2l-csi2", },
	{ /* sentinel */ }
};

static struct platform_driver rzg2l_csi2_pdrv = {
	.remove_new = rzg2l_csi2_remove,
	.probe	= rzg2l_csi2_probe,
	.driver	= {
		.name = "rzg2l-csi2",
		.of_match_table = rzg2l_csi2_of_table,
		.pm = pm_ptr(&rzg2l_csi2_pm_ops),
	},
};

module_platform_driver(rzg2l_csi2_pdrv);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L MIPI CSI2 receiver driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csid.c
 *
 * Qualcomm MSM Camera Subsystem - CSID (CSI Decoder) Module
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "camss-csid.h"
#include "camss.h"

#define MSM_CSID_NAME "msm_csid"

#define CAMSS_CSID_HW_VERSION		0x0
#define CAMSS_CSID_CORE_CTRL_0		0x004
#define CAMSS_CSID_CORE_CTRL_1		0x008
#define CAMSS_CSID_RST_CMD(v)		((v) == CAMSS_8x16 ? 0x00c : 0x010)
#define CAMSS_CSID_CID_LUT_VC_n(v, n)	\
			(((v) == CAMSS_8x16 ? 0x010 : 0x014) + 0x4 * (n))
#define CAMSS_CSID_CID_n_CFG(v, n)	\
			(((v) == CAMSS_8x16 ? 0x020 : 0x024) + 0x4 * (n))
#define CAMSS_CSID_CID_n_CFG_ISPIF_EN	BIT(0)
#define CAMSS_CSID_CID_n_CFG_RDI_EN	BIT(1)
#define CAMSS_CSID_CID_n_CFG_DECODE_FORMAT_SHIFT	4
#define CAMSS_CSID_CID_n_CFG_PLAIN_FORMAT_8		(0 << 8)
#define CAMSS_CSID_CID_n_CFG_PLAIN_FORMAT_16		(1 << 8)
#define CAMSS_CSID_CID_n_CFG_PLAIN_ALIGNMENT_LSB	(0 << 9)
#define CAMSS_CSID_CID_n_CFG_PLAIN_ALIGNMENT_MSB	(1 << 9)
#define CAMSS_CSID_CID_n_CFG_RDI_MODE_RAW_DUMP		(0 << 10)
#define CAMSS_CSID_CID_n_CFG_RDI_MODE_PLAIN_PACKING	(1 << 10)
#define CAMSS_CSID_IRQ_CLEAR_CMD(v)	((v) == CAMSS_8x16 ? 0x060 : 0x064)
#define CAMSS_CSID_IRQ_MASK(v)		((v) == CAMSS_8x16 ? 0x064 : 0x068)
#define CAMSS_CSID_IRQ_STATUS(v)	((v) == CAMSS_8x16 ? 0x068 : 0x06c)
#define CAMSS_CSID_TG_CTRL(v)		((v) == CAMSS_8x16 ? 0x0a0 : 0x0a8)
#define CAMSS_CSID_TG_CTRL_DISABLE	0xa06436
#define CAMSS_CSID_TG_CTRL_ENABLE	0xa06437
#define CAMSS_CSID_TG_VC_CFG(v)		((v) == CAMSS_8x16 ? 0x0a4 : 0x0ac)
#define CAMSS_CSID_TG_VC_CFG_H_BLANKING		0x3ff
#define CAMSS_CSID_TG_VC_CFG_V_BLANKING		0x7f
#define CAMSS_CSID_TG_DT_n_CGG_0(v, n)	\
			(((v) == CAMSS_8x16 ? 0x0ac : 0x0b4) + 0xc * (n))
#define CAMSS_CSID_TG_DT_n_CGG_1(v, n)	\
			(((v) == CAMSS_8x16 ? 0x0b0 : 0x0b8) + 0xc * (n))
#define CAMSS_CSID_TG_DT_n_CGG_2(v, n)	\
			(((v) == CAMSS_8x16 ? 0x0b4 : 0x0bc) + 0xc * (n))

#define DATA_TYPE_EMBEDDED_DATA_8BIT	0x12
#define DATA_TYPE_YUV422_8BIT		0x1e
#define DATA_TYPE_RAW_6BIT		0x28
#define DATA_TYPE_RAW_8BIT		0x2a
#define DATA_TYPE_RAW_10BIT		0x2b
#define DATA_TYPE_RAW_12BIT		0x2c
#define DATA_TYPE_RAW_14BIT		0x2d

#define DECODE_FORMAT_UNCOMPRESSED_6_BIT	0x0
#define DECODE_FORMAT_UNCOMPRESSED_8_BIT	0x1
#define DECODE_FORMAT_UNCOMPRESSED_10_BIT	0x2
#define DECODE_FORMAT_UNCOMPRESSED_12_BIT	0x3
#define DECODE_FORMAT_UNCOMPRESSED_14_BIT	0x8

#define CSID_RESET_TIMEOUT_MS 500

struct csid_format {
	u32 code;
	u8 data_type;
	u8 decode_format;
	u8 bpp;
	u8 spp; /* bus samples per pixel */
};

static const struct csid_format csid_formats_8x16[] = {
	{
		MEDIA_BUS_FMT_UYVY8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_VYUY8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YUYV8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YVYU8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_SBGGR8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SBGGR10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SBGGR12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
};

static const struct csid_format csid_formats_8x96[] = {
	{
		MEDIA_BUS_FMT_UYVY8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_VYUY8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YUYV8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YVYU8_2X8,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_SBGGR8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_SBGGR10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
	{
		MEDIA_BUS_FMT_SBGGR12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB12_1X12,
		DATA_TYPE_RAW_12BIT,
		DECODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
		1,
	},
	{
		MEDIA_BUS_FMT_SBGGR14_1X14,
		DATA_TYPE_RAW_14BIT,
		DECODE_FORMAT_UNCOMPRESSED_14_BIT,
		14,
		1,
	},
	{
		MEDIA_BUS_FMT_SGBRG14_1X14,
		DATA_TYPE_RAW_14BIT,
		DECODE_FORMAT_UNCOMPRESSED_14_BIT,
		14,
		1,
	},
	{
		MEDIA_BUS_FMT_SGRBG14_1X14,
		DATA_TYPE_RAW_14BIT,
		DECODE_FORMAT_UNCOMPRESSED_14_BIT,
		14,
		1,
	},
	{
		MEDIA_BUS_FMT_SRGGB14_1X14,
		DATA_TYPE_RAW_14BIT,
		DECODE_FORMAT_UNCOMPRESSED_14_BIT,
		14,
		1,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
		DATA_TYPE_RAW_10BIT,
		DECODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
		1,
	},
};

static u32 csid_find_code(u32 *code, unsigned int n_code,
			  unsigned int index, u32 req_code)
{
	int i;

	if (!req_code && (index >= n_code))
		return 0;

	for (i = 0; i < n_code; i++)
		if (req_code) {
			if (req_code == code[i])
				return req_code;
		} else {
			if (i == index)
				return code[i];
		}

	return code[0];
}

static u32 csid_src_pad_code(struct csid_device *csid, u32 sink_code,
			     unsigned int index, u32 src_req_code)
{
	if (csid->camss->version == CAMSS_8x16) {
		if (index > 0)
			return 0;

		return sink_code;
	} else if (csid->camss->version == CAMSS_8x96 ||
		   csid->camss->version == CAMSS_660) {
		switch (sink_code) {
		case MEDIA_BUS_FMT_SBGGR10_1X10:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_SBGGR10_1X10,
				MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE,
			};

			return csid_find_code(src_code, ARRAY_SIZE(src_code),
					      index, src_req_code);
		}
		case MEDIA_BUS_FMT_Y10_1X10:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_Y10_1X10,
				MEDIA_BUS_FMT_Y10_2X8_PADHI_LE,
			};

			return csid_find_code(src_code, ARRAY_SIZE(src_code),
					      index, src_req_code);
		}
		default:
			if (index > 0)
				return 0;

			return sink_code;
		}
	} else {
		return 0;
	}
}

static const struct csid_format *csid_get_fmt_entry(
					const struct csid_format *formats,
					unsigned int nformat,
					u32 code)
{
	unsigned int i;

	for (i = 0; i < nformat; i++)
		if (code == formats[i].code)
			return &formats[i];

	WARN(1, "Unknown format\n");

	return &formats[0];
}

/*
 * csid_isr - CSID module interrupt handler
 * @irq: Interrupt line
 * @dev: CSID device
 *
 * Return IRQ_HANDLED on success
 */
static irqreturn_t csid_isr(int irq, void *dev)
{
	struct csid_device *csid = dev;
	enum camss_version ver = csid->camss->version;
	u32 value;

	value = readl_relaxed(csid->base + CAMSS_CSID_IRQ_STATUS(ver));
	writel_relaxed(value, csid->base + CAMSS_CSID_IRQ_CLEAR_CMD(ver));

	if ((value >> 11) & 0x1)
		complete(&csid->reset_complete);

	return IRQ_HANDLED;
}

/*
 * csid_set_clock_rates - Calculate and set clock rates on CSID module
 * @csiphy: CSID device
 */
static int csid_set_clock_rates(struct csid_device *csid)
{
	struct device *dev = csid->camss->dev;
	const struct csid_format *fmt;
	s64 link_freq;
	int i, j;
	int ret;

	fmt = csid_get_fmt_entry(csid->formats, csid->nformats,
				 csid->fmt[MSM_CSIPHY_PAD_SINK].code);
	link_freq = camss_get_link_freq(&csid->subdev.entity, fmt->bpp,
					csid->phy.lane_cnt);
	if (link_freq < 0)
		link_freq = 0;

	for (i = 0; i < csid->nclocks; i++) {
		struct camss_clock *clock = &csid->clock[i];

		if (!strcmp(clock->name, "csi0") ||
		    !strcmp(clock->name, "csi1") ||
		    !strcmp(clock->name, "csi2") ||
		    !strcmp(clock->name, "csi3")) {
			u64 min_rate = link_freq / 4;
			long rate;

			camss_add_clock_margin(&min_rate);

			for (j = 0; j < clock->nfreqs; j++)
				if (min_rate < clock->freq[j])
					break;

			if (j == clock->nfreqs) {
				dev_err(dev,
					"Pixel clock is too high for CSID\n");
				return -EINVAL;
			}

			/* if sensor pixel clock is not available */
			/* set highest possible CSID clock rate */
			if (min_rate == 0)
				j = clock->nfreqs - 1;

			rate = clk_round_rate(clock->clk, clock->freq[j]);
			if (rate < 0) {
				dev_err(dev, "clk round rate failed: %ld\n",
					rate);
				return -EINVAL;
			}

			ret = clk_set_rate(clock->clk, rate);
			if (ret < 0) {
				dev_err(dev, "clk set rate failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

/*
 * csid_reset - Trigger reset on CSID module and wait to complete
 * @csid: CSID device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_reset(struct csid_device *csid)
{
	unsigned long time;

	reinit_completion(&csid->reset_complete);

	writel_relaxed(0x7fff, csid->base +
		       CAMSS_CSID_RST_CMD(csid->camss->version));

	time = wait_for_completion_timeout(&csid->reset_complete,
		msecs_to_jiffies(CSID_RESET_TIMEOUT_MS));
	if (!time) {
		dev_err(csid->camss->dev, "CSID reset timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * csid_set_power - Power on/off CSID module
 * @sd: CSID V4L2 subdevice
 * @on: Requested power state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_set_power(struct v4l2_subdev *sd, int on)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct device *dev = csid->camss->dev;
	int ret;

	if (on) {
		u32 hw_version;

		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = regulator_enable(csid->vdda);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = csid_set_clock_rates(csid);
		if (ret < 0) {
			regulator_disable(csid->vdda);
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = camss_enable_clocks(csid->nclocks, csid->clock, dev);
		if (ret < 0) {
			regulator_disable(csid->vdda);
			pm_runtime_put_sync(dev);
			return ret;
		}

		enable_irq(csid->irq);

		ret = csid_reset(csid);
		if (ret < 0) {
			disable_irq(csid->irq);
			camss_disable_clocks(csid->nclocks, csid->clock);
			regulator_disable(csid->vdda);
			pm_runtime_put_sync(dev);
			return ret;
		}

		hw_version = readl_relaxed(csid->base + CAMSS_CSID_HW_VERSION);
		dev_dbg(dev, "CSID HW Version = 0x%08x\n", hw_version);
	} else {
		disable_irq(csid->irq);
		camss_disable_clocks(csid->nclocks, csid->clock);
		ret = regulator_disable(csid->vdda);
		pm_runtime_put_sync(dev);
	}

	return ret;
}

/*
 * csid_set_stream - Enable/disable streaming on CSID module
 * @sd: CSID V4L2 subdevice
 * @enable: Requested streaming state
 *
 * Main configuration of CSID module is also done here.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct csid_testgen_config *tg = &csid->testgen;
	enum camss_version ver = csid->camss->version;
	u32 val;

	if (enable) {
		u8 vc = 0; /* Virtual Channel 0 */
		u8 cid = vc * 4; /* id of Virtual Channel and Data Type set */
		u8 dt, dt_shift, df;
		int ret;

		ret = v4l2_ctrl_handler_setup(&csid->ctrls);
		if (ret < 0) {
			dev_err(csid->camss->dev,
				"could not sync v4l2 controls: %d\n", ret);
			return ret;
		}

		if (!tg->enabled &&
		    !media_entity_remote_pad(&csid->pads[MSM_CSID_PAD_SINK]))
			return -ENOLINK;

		if (tg->enabled) {
			/* Config Test Generator */
			struct v4l2_mbus_framefmt *f =
					&csid->fmt[MSM_CSID_PAD_SRC];
			const struct csid_format *format = csid_get_fmt_entry(
					csid->formats, csid->nformats, f->code);
			u32 num_bytes_per_line =
				f->width * format->bpp * format->spp / 8;
			u32 num_lines = f->height;

			/* 31:24 V blank, 23:13 H blank, 3:2 num of active DT */
			/* 1:0 VC */
			val = ((CAMSS_CSID_TG_VC_CFG_V_BLANKING & 0xff) << 24) |
			      ((CAMSS_CSID_TG_VC_CFG_H_BLANKING & 0x7ff) << 13);
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_VC_CFG(ver));

			/* 28:16 bytes per lines, 12:0 num of lines */
			val = ((num_bytes_per_line & 0x1fff) << 16) |
			      (num_lines & 0x1fff);
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_DT_n_CGG_0(ver, 0));

			dt = format->data_type;

			/* 5:0 data type */
			val = dt;
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_DT_n_CGG_1(ver, 0));

			/* 2:0 output test pattern */
			val = tg->payload_mode;
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_DT_n_CGG_2(ver, 0));

			df = format->decode_format;
		} else {
			struct v4l2_mbus_framefmt *f =
					&csid->fmt[MSM_CSID_PAD_SINK];
			const struct csid_format *format = csid_get_fmt_entry(
					csid->formats, csid->nformats, f->code);
			struct csid_phy_config *phy = &csid->phy;

			val = phy->lane_cnt - 1;
			val |= phy->lane_assign << 4;

			writel_relaxed(val,
				       csid->base + CAMSS_CSID_CORE_CTRL_0);

			val = phy->csiphy_id << 17;
			val |= 0x9;

			writel_relaxed(val,
				       csid->base + CAMSS_CSID_CORE_CTRL_1);

			dt = format->data_type;
			df = format->decode_format;
		}

		/* Config LUT */

		dt_shift = (cid % 4) * 8;

		val = readl_relaxed(csid->base +
				    CAMSS_CSID_CID_LUT_VC_n(ver, vc));
		val &= ~(0xff << dt_shift);
		val |= dt << dt_shift;
		writel_relaxed(val, csid->base +
			       CAMSS_CSID_CID_LUT_VC_n(ver, vc));

		val = CAMSS_CSID_CID_n_CFG_ISPIF_EN;
		val |= CAMSS_CSID_CID_n_CFG_RDI_EN;
		val |= df << CAMSS_CSID_CID_n_CFG_DECODE_FORMAT_SHIFT;
		val |= CAMSS_CSID_CID_n_CFG_RDI_MODE_RAW_DUMP;

		if (csid->camss->version == CAMSS_8x96 ||
		    csid->camss->version == CAMSS_660) {
			u32 sink_code = csid->fmt[MSM_CSID_PAD_SINK].code;
			u32 src_code = csid->fmt[MSM_CSID_PAD_SRC].code;

			if ((sink_code == MEDIA_BUS_FMT_SBGGR10_1X10 &&
			     src_code == MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE) ||
			    (sink_code == MEDIA_BUS_FMT_Y10_1X10 &&
			     src_code == MEDIA_BUS_FMT_Y10_2X8_PADHI_LE)) {
				val |= CAMSS_CSID_CID_n_CFG_RDI_MODE_PLAIN_PACKING;
				val |= CAMSS_CSID_CID_n_CFG_PLAIN_FORMAT_16;
				val |= CAMSS_CSID_CID_n_CFG_PLAIN_ALIGNMENT_LSB;
			}
		}

		writel_relaxed(val, csid->base +
			       CAMSS_CSID_CID_n_CFG(ver, cid));

		if (tg->enabled) {
			val = CAMSS_CSID_TG_CTRL_ENABLE;
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_CTRL(ver));
		}
	} else {
		if (tg->enabled) {
			val = CAMSS_CSID_TG_CTRL_DISABLE;
			writel_relaxed(val, csid->base +
				       CAMSS_CSID_TG_CTRL(ver));
		}
	}

	return 0;
}

/*
 * __csid_get_format - Get pointer to format structure
 * @csid: CSID device
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad from which format is requested
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE format structure
 */
static struct v4l2_mbus_framefmt *
__csid_get_format(struct csid_device *csid,
		  struct v4l2_subdev_pad_config *cfg,
		  unsigned int pad,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&csid->subdev, cfg, pad);

	return &csid->fmt[pad];
}

/*
 * csid_try_format - Handle try format by pad subdev method
 * @csid: CSID device
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad on which format is requested
 * @fmt: pointer to v4l2 format structure
 * @which: wanted subdev format
 */
static void csid_try_format(struct csid_device *csid,
			    struct v4l2_subdev_pad_config *cfg,
			    unsigned int pad,
			    struct v4l2_mbus_framefmt *fmt,
			    enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case MSM_CSID_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < csid->nformats; i++)
			if (fmt->code == csid->formats[i].code)
				break;

		/* If not found, use UYVY as default */
		if (i >= csid->nformats)
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

		fmt->width = clamp_t(u32, fmt->width, 1, 8191);
		fmt->height = clamp_t(u32, fmt->height, 1, 8191);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		break;

	case MSM_CSID_PAD_SRC:
		if (csid->testgen_mode->cur.val == 0) {
			/* Test generator is disabled, */
			/* keep pad formats in sync */
			u32 code = fmt->code;

			*fmt = *__csid_get_format(csid, cfg,
						      MSM_CSID_PAD_SINK, which);
			fmt->code = csid_src_pad_code(csid, fmt->code, 0, code);
		} else {
			/* Test generator is enabled, set format on source */
			/* pad to allow test generator usage */

			for (i = 0; i < csid->nformats; i++)
				if (csid->formats[i].code == fmt->code)
					break;

			/* If not found, use UYVY as default */
			if (i >= csid->nformats)
				fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

			fmt->width = clamp_t(u32, fmt->width, 1, 8191);
			fmt->height = clamp_t(u32, fmt->height, 1, 8191);

			fmt->field = V4L2_FIELD_NONE;
		}
		break;
	}

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

/*
 * csid_enum_mbus_code - Handle pixel format enumeration
 * @sd: CSID V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int csid_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);

	if (code->pad == MSM_CSID_PAD_SINK) {
		if (code->index >= csid->nformats)
			return -EINVAL;

		code->code = csid->formats[code->index].code;
	} else {
		if (csid->testgen_mode->cur.val == 0) {
			struct v4l2_mbus_framefmt *sink_fmt;

			sink_fmt = __csid_get_format(csid, cfg,
						     MSM_CSID_PAD_SINK,
						     code->which);

			code->code = csid_src_pad_code(csid, sink_fmt->code,
						       code->index, 0);
			if (!code->code)
				return -EINVAL;
		} else {
			if (code->index >= csid->nformats)
				return -EINVAL;

			code->code = csid->formats[code->index].code;
		}
	}

	return 0;
}

/*
 * csid_enum_frame_size - Handle frame size enumeration
 * @sd: CSID V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fse: pointer to v4l2_subdev_frame_size_enum structure
 * return -EINVAL or zero on success
 */
static int csid_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csid_try_format(csid, cfg, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csid_try_format(csid, cfg, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * csid_get_format - Handle get format by pads subdev method
 * @sd: CSID V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csid_get_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csid_get_format(csid, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/*
 * csid_set_format - Handle set format by pads subdev method
 * @sd: CSID V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csid_set_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csid_get_format(csid, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	csid_try_format(csid, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == MSM_CSID_PAD_SINK) {
		format = __csid_get_format(csid, cfg, MSM_CSID_PAD_SRC,
					   fmt->which);

		*format = fmt->format;
		csid_try_format(csid, cfg, MSM_CSID_PAD_SRC, format,
				fmt->which);
	}

	return 0;
}

/*
 * csid_init_formats - Initialize formats on all pads
 * @sd: CSID V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = MSM_CSID_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
			      V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_UYVY8_2X8,
			.width = 1920,
			.height = 1080
		}
	};

	return csid_set_format(sd, fh ? fh->pad : NULL, &format);
}

static const char * const csid_test_pattern_menu[] = {
	"Disabled",
	"Incrementing",
	"Alternating 0x55/0xAA",
	"All Zeros 0x00",
	"All Ones 0xFF",
	"Pseudo-random Data",
};

/*
 * csid_set_test_pattern - Set test generator's pattern mode
 * @csid: CSID device
 * @value: desired test pattern mode
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_set_test_pattern(struct csid_device *csid, s32 value)
{
	struct csid_testgen_config *tg = &csid->testgen;

	/* If CSID is linked to CSIPHY, do not allow to enable test generator */
	if (value && media_entity_remote_pad(&csid->pads[MSM_CSID_PAD_SINK]))
		return -EBUSY;

	tg->enabled = !!value;

	switch (value) {
	case 1:
		tg->payload_mode = CSID_PAYLOAD_MODE_INCREMENTING;
		break;
	case 2:
		tg->payload_mode = CSID_PAYLOAD_MODE_ALTERNATING_55_AA;
		break;
	case 3:
		tg->payload_mode = CSID_PAYLOAD_MODE_ALL_ZEROES;
		break;
	case 4:
		tg->payload_mode = CSID_PAYLOAD_MODE_ALL_ONES;
		break;
	case 5:
		tg->payload_mode = CSID_PAYLOAD_MODE_RANDOM;
		break;
	}

	return 0;
}

/*
 * csid_s_ctrl - Handle set control subdev method
 * @ctrl: pointer to v4l2 control structure
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct csid_device *csid = container_of(ctrl->handler,
						struct csid_device, ctrls);
	int ret = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		ret = csid_set_test_pattern(csid, ctrl->val);
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops csid_ctrl_ops = {
	.s_ctrl = csid_s_ctrl,
};

/*
 * msm_csid_subdev_init - Initialize CSID device structure and resources
 * @csid: CSID device
 * @res: CSID module resources table
 * @id: CSID module id
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_csid_subdev_init(struct camss *camss, struct csid_device *csid,
			 const struct resources *res, u8 id)
{
	struct device *dev = camss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *r;
	int i, j;
	int ret;

	csid->camss = camss;
	csid->id = id;

	if (camss->version == CAMSS_8x16) {
		csid->formats = csid_formats_8x16;
		csid->nformats =
				ARRAY_SIZE(csid_formats_8x16);
	} else if (camss->version == CAMSS_8x96 ||
		   camss->version == CAMSS_660) {
		csid->formats = csid_formats_8x96;
		csid->nformats =
				ARRAY_SIZE(csid_formats_8x96);
	} else {
		return -EINVAL;
	}

	/* Memory */

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, res->reg[0]);
	csid->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(csid->base)) {
		dev_err(dev, "could not map memory\n");
		return PTR_ERR(csid->base);
	}

	/* Interrupt */

	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					 res->interrupt[0]);
	if (!r) {
		dev_err(dev, "missing IRQ\n");
		return -EINVAL;
	}

	csid->irq = r->start;
	snprintf(csid->irq_name, sizeof(csid->irq_name), "%s_%s%d",
		 dev_name(dev), MSM_CSID_NAME, csid->id);
	ret = devm_request_irq(dev, csid->irq, csid_isr,
		IRQF_TRIGGER_RISING, csid->irq_name, csid);
	if (ret < 0) {
		dev_err(dev, "request_irq failed: %d\n", ret);
		return ret;
	}

	disable_irq(csid->irq);

	/* Clocks */

	csid->nclocks = 0;
	while (res->clock[csid->nclocks])
		csid->nclocks++;

	csid->clock = devm_kcalloc(dev, csid->nclocks, sizeof(*csid->clock),
				    GFP_KERNEL);
	if (!csid->clock)
		return -ENOMEM;

	for (i = 0; i < csid->nclocks; i++) {
		struct camss_clock *clock = &csid->clock[i];

		clock->clk = devm_clk_get(dev, res->clock[i]);
		if (IS_ERR(clock->clk))
			return PTR_ERR(clock->clk);

		clock->name = res->clock[i];

		clock->nfreqs = 0;
		while (res->clock_rate[i][clock->nfreqs])
			clock->nfreqs++;

		if (!clock->nfreqs) {
			clock->freq = NULL;
			continue;
		}

		clock->freq = devm_kcalloc(dev,
					   clock->nfreqs,
					   sizeof(*clock->freq),
					   GFP_KERNEL);
		if (!clock->freq)
			return -ENOMEM;

		for (j = 0; j < clock->nfreqs; j++)
			clock->freq[j] = res->clock_rate[i][j];
	}

	/* Regulator */

	csid->vdda = devm_regulator_get(dev, res->regulator[0]);
	if (IS_ERR(csid->vdda)) {
		dev_err(dev, "could not get regulator\n");
		return PTR_ERR(csid->vdda);
	}

	init_completion(&csid->reset_complete);

	return 0;
}

/*
 * msm_csid_get_csid_id - Get CSID HW module id
 * @entity: Pointer to CSID media entity structure
 * @id: Return CSID HW module id here
 */
void msm_csid_get_csid_id(struct media_entity *entity, u8 *id)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct csid_device *csid = v4l2_get_subdevdata(sd);

	*id = csid->id;
}

/*
 * csid_get_lane_assign - Calculate CSI2 lane assign configuration parameter
 * @lane_cfg - CSI2 lane configuration
 *
 * Return lane assign
 */
static u32 csid_get_lane_assign(struct csiphy_lanes_cfg *lane_cfg)
{
	u32 lane_assign = 0;
	int i;

	for (i = 0; i < lane_cfg->num_data; i++)
		lane_assign |= lane_cfg->data[i].pos << (i * 4);

	return lane_assign;
}

/*
 * csid_link_setup - Setup CSID connections
 * @entity: Pointer to media entity structure
 * @local: Pointer to local pad
 * @remote: Pointer to remote pad
 * @flags: Link flags
 *
 * Return 0 on success
 */
static int csid_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_entity_remote_pad(local))
			return -EBUSY;

	if ((local->flags & MEDIA_PAD_FL_SINK) &&
	    (flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct csid_device *csid;
		struct csiphy_device *csiphy;
		struct csiphy_lanes_cfg *lane_cfg;
		struct v4l2_subdev_format format = { 0 };

		sd = media_entity_to_v4l2_subdev(entity);
		csid = v4l2_get_subdevdata(sd);

		/* If test generator is enabled */
		/* do not allow a link from CSIPHY to CSID */
		if (csid->testgen_mode->cur.val != 0)
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(remote->entity);
		csiphy = v4l2_get_subdevdata(sd);

		/* If a sensor is not linked to CSIPHY */
		/* do no allow a link from CSIPHY to CSID */
		if (!csiphy->cfg.csi2)
			return -EPERM;

		csid->phy.csiphy_id = csiphy->id;

		lane_cfg = &csiphy->cfg.csi2->lane_cfg;
		csid->phy.lane_cnt = lane_cfg->num_data;
		csid->phy.lane_assign = csid_get_lane_assign(lane_cfg);

		/* Reset format on source pad to sink pad format */
		format.pad = MSM_CSID_PAD_SRC;
		format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		csid_set_format(&csid->subdev, NULL, &format);
	}

	return 0;
}

static const struct v4l2_subdev_core_ops csid_core_ops = {
	.s_power = csid_set_power,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops csid_video_ops = {
	.s_stream = csid_set_stream,
};

static const struct v4l2_subdev_pad_ops csid_pad_ops = {
	.enum_mbus_code = csid_enum_mbus_code,
	.enum_frame_size = csid_enum_frame_size,
	.get_fmt = csid_get_format,
	.set_fmt = csid_set_format,
};

static const struct v4l2_subdev_ops csid_v4l2_ops = {
	.core = &csid_core_ops,
	.video = &csid_video_ops,
	.pad = &csid_pad_ops,
};

static const struct v4l2_subdev_internal_ops csid_v4l2_internal_ops = {
	.open = csid_init_formats,
};

static const struct media_entity_operations csid_media_ops = {
	.link_setup = csid_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * msm_csid_register_entity - Register subdev node for CSID module
 * @csid: CSID device
 * @v4l2_dev: V4L2 device
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_csid_register_entity(struct csid_device *csid,
			     struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &csid->subdev;
	struct media_pad *pads = csid->pads;
	struct device *dev = csid->camss->dev;
	int ret;

	v4l2_subdev_init(sd, &csid_v4l2_ops);
	sd->internal_ops = &csid_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		 MSM_CSID_NAME, csid->id);
	v4l2_set_subdevdata(sd, csid);

	ret = v4l2_ctrl_handler_init(&csid->ctrls, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to init ctrl handler: %d\n", ret);
		return ret;
	}

	csid->testgen_mode = v4l2_ctrl_new_std_menu_items(&csid->ctrls,
				&csid_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(csid_test_pattern_menu) - 1, 0, 0,
				csid_test_pattern_menu);

	if (csid->ctrls.error) {
		dev_err(dev, "Failed to init ctrl: %d\n", csid->ctrls.error);
		ret = csid->ctrls.error;
		goto free_ctrl;
	}

	csid->subdev.ctrl_handler = &csid->ctrls;

	ret = csid_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		goto free_ctrl;
	}

	pads[MSM_CSID_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[MSM_CSID_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &csid_media_ops;
	ret = media_entity_pads_init(&sd->entity, MSM_CSID_PADS_NUM, pads);
	if (ret < 0) {
		dev_err(dev, "Failed to init media entity: %d\n", ret);
		goto free_ctrl;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		goto media_cleanup;
	}

	return 0;

media_cleanup:
	media_entity_cleanup(&sd->entity);
free_ctrl:
	v4l2_ctrl_handler_free(&csid->ctrls);

	return ret;
}

/*
 * msm_csid_unregister_entity - Unregister CSID module subdev node
 * @csid: CSID device
 */
void msm_csid_unregister_entity(struct csid_device *csid)
{
	v4l2_device_unregister_subdev(&csid->subdev);
	media_entity_cleanup(&csid->subdev.entity);
	v4l2_ctrl_handler_free(&csid->ctrls);
}

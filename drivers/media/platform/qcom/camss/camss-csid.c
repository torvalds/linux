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
#include "camss-csid-gen1.h"
#include "camss.h"

/* offset of CSID registers in VFE region for VFE 480 */
#define VFE_480_CSID_OFFSET 0x1200
#define VFE_480_LITE_CSID_OFFSET 0x200

#define MSM_CSID_NAME "msm_csid"

const char * const csid_testgen_modes[] = {
	"Disabled",
	"Incrementing",
	"Alternating 0x55/0xAA",
	"All Zeros 0x00",
	"All Ones 0xFF",
	"Pseudo-random Data",
	"User Specified",
	"Complex pattern",
	"Color box",
	"Color bars",
	NULL
};

static const struct csid_format_info formats_4_1[] = {
	{
		MEDIA_BUS_FMT_UYVY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_VYUY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YUYV8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YVYU8_1X16,
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

static const struct csid_format_info formats_4_7[] = {
	{
		MEDIA_BUS_FMT_UYVY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_VYUY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YUYV8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YVYU8_1X16,
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

static const struct csid_format_info formats_gen2[] = {
	{
		MEDIA_BUS_FMT_UYVY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_VYUY8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YUYV8_1X16,
		DATA_TYPE_YUV422_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		2,
	},
	{
		MEDIA_BUS_FMT_YVYU8_1X16,
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
		MEDIA_BUS_FMT_Y8_1X8,
		DATA_TYPE_RAW_8BIT,
		DECODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
		1,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
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
};

const struct csid_formats csid_formats_4_1 = {
	.nformats = ARRAY_SIZE(formats_4_1),
	.formats = formats_4_1
};

const struct csid_formats csid_formats_4_7 = {
	.nformats = ARRAY_SIZE(formats_4_7),
	.formats = formats_4_7
};

const struct csid_formats csid_formats_gen2 = {
	.nformats = ARRAY_SIZE(formats_gen2),
	.formats = formats_gen2
};

u32 csid_find_code(u32 *codes, unsigned int ncodes,
		   unsigned int match_format_idx, u32 match_code)
{
	int i;

	if (!match_code && (match_format_idx >= ncodes))
		return 0;

	for (i = 0; i < ncodes; i++)
		if (match_code) {
			if (codes[i] == match_code)
				return match_code;
		} else {
			if (i == match_format_idx)
				return codes[i];
		}

	return codes[0];
}

const struct csid_format_info *csid_get_fmt_entry(const struct csid_format_info *formats,
						  unsigned int nformats,
						  u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return &formats[i];

	WARN(1, "Unknown format\n");

	return &formats[0];
}

/*
 * csid_set_clock_rates - Calculate and set clock rates on CSID module
 * @csiphy: CSID device
 */
static int csid_set_clock_rates(struct csid_device *csid)
{
	struct device *dev = csid->camss->dev;
	const struct csid_format_info *fmt;
	s64 link_freq;
	int i, j;
	int ret;

	fmt = csid_get_fmt_entry(csid->res->formats->formats, csid->res->formats->nformats,
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
		} else if (clock->nfreqs) {
			clk_set_rate(clock->clk, clock->freq[0]);
		}
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
	struct camss *camss = csid->camss;
	struct device *dev = camss->dev;
	int ret = 0;

	if (on) {
		/*
		 * From SDM845 onwards, the VFE needs to be powered on before
		 * switching on the CSID. Do so unconditionally, as there is no
		 * drawback in following the same powering order on older SoCs.
		 */
		ret = csid->res->parent_dev_ops->get(camss, csid->id);
		if (ret < 0)
			return ret;

		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			return ret;

		ret = regulator_bulk_enable(csid->num_supplies,
					    csid->supplies);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = csid_set_clock_rates(csid);
		if (ret < 0) {
			regulator_bulk_disable(csid->num_supplies,
					       csid->supplies);
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = camss_enable_clocks(csid->nclocks, csid->clock, dev);
		if (ret < 0) {
			regulator_bulk_disable(csid->num_supplies,
					       csid->supplies);
			pm_runtime_put_sync(dev);
			return ret;
		}

		csid->phy.need_vc_update = true;

		enable_irq(csid->irq);

		ret = csid->res->hw_ops->reset(csid);
		if (ret < 0) {
			disable_irq(csid->irq);
			camss_disable_clocks(csid->nclocks, csid->clock);
			regulator_bulk_disable(csid->num_supplies,
					       csid->supplies);
			pm_runtime_put_sync(dev);
			return ret;
		}

		csid->res->hw_ops->hw_version(csid);
	} else {
		disable_irq(csid->irq);
		camss_disable_clocks(csid->nclocks, csid->clock);
		regulator_bulk_disable(csid->num_supplies,
				       csid->supplies);
		pm_runtime_put_sync(dev);
		csid->res->parent_dev_ops->put(camss, csid->id);
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
	int ret;

	if (enable) {
		ret = v4l2_ctrl_handler_setup(&csid->ctrls);
		if (ret < 0) {
			dev_err(csid->camss->dev,
				"could not sync v4l2 controls: %d\n", ret);
			return ret;
		}

		if (!csid->testgen.enabled &&
		    !media_pad_remote_pad_first(&csid->pads[MSM_CSID_PAD_SINK]))
			return -ENOLINK;
	}

	if (csid->phy.need_vc_update) {
		csid->res->hw_ops->configure_stream(csid, enable);
		csid->phy.need_vc_update = false;
	}

	return 0;
}

/*
 * __csid_get_format - Get pointer to format structure
 * @csid: CSID device
 * @sd_state: V4L2 subdev state
 * @pad: pad from which format is requested
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE format structure
 */
static struct v4l2_mbus_framefmt *
__csid_get_format(struct csid_device *csid,
		  struct v4l2_subdev_state *sd_state,
		  unsigned int pad,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_state_get_format(sd_state, pad);

	return &csid->fmt[pad];
}

/*
 * csid_try_format - Handle try format by pad subdev method
 * @csid: CSID device
 * @sd_state: V4L2 subdev state
 * @pad: pad on which format is requested
 * @fmt: pointer to v4l2 format structure
 * @which: wanted subdev format
 */
static void csid_try_format(struct csid_device *csid,
			    struct v4l2_subdev_state *sd_state,
			    unsigned int pad,
			    struct v4l2_mbus_framefmt *fmt,
			    enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case MSM_CSID_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < csid->res->formats->nformats; i++)
			if (fmt->code == csid->res->formats->formats[i].code)
				break;

		/* If not found, use UYVY as default */
		if (i >= csid->res->formats->nformats)
			fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;

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

			*fmt = *__csid_get_format(csid, sd_state,
						      MSM_CSID_PAD_SINK, which);
			fmt->code = csid->res->hw_ops->src_pad_code(csid, fmt->code, 0, code);
		} else {
			/* Test generator is enabled, set format on source */
			/* pad to allow test generator usage */

			for (i = 0; i < csid->res->formats->nformats; i++)
				if (csid->res->formats->formats[i].code == fmt->code)
					break;

			/* If not found, use UYVY as default */
			if (i >= csid->res->formats->nformats)
				fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;

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
 * @sd_state: V4L2 subdev state
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int csid_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);

	if (code->pad == MSM_CSID_PAD_SINK) {
		if (code->index >= csid->res->formats->nformats)
			return -EINVAL;

		code->code = csid->res->formats->formats[code->index].code;
	} else {
		if (csid->testgen_mode->cur.val == 0) {
			struct v4l2_mbus_framefmt *sink_fmt;

			sink_fmt = __csid_get_format(csid, sd_state,
						     MSM_CSID_PAD_SINK,
						     code->which);

			code->code = csid->res->hw_ops->src_pad_code(csid, sink_fmt->code,
								     code->index, 0);
			if (!code->code)
				return -EINVAL;
		} else {
			if (code->index >= csid->res->formats->nformats)
				return -EINVAL;

			code->code = csid->res->formats->formats[code->index].code;
		}
	}

	return 0;
}

/*
 * csid_enum_frame_size - Handle frame size enumeration
 * @sd: CSID V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fse: pointer to v4l2_subdev_frame_size_enum structure
 * return -EINVAL or zero on success
 */
static int csid_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csid_try_format(csid, sd_state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csid_try_format(csid, sd_state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * csid_get_format - Handle get format by pads subdev method
 * @sd: CSID V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csid_get_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csid_get_format(csid, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/*
 * csid_set_format - Handle set format by pads subdev method
 * @sd: CSID V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csid_set_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct csid_device *csid = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	int i;

	format = __csid_get_format(csid, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	csid_try_format(csid, sd_state, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source pads */
	if (fmt->pad == MSM_CSID_PAD_SINK) {
		for (i = MSM_CSID_PAD_FIRST_SRC; i < MSM_CSID_PADS_NUM; ++i) {
			format = __csid_get_format(csid, sd_state, i, fmt->which);

			*format = fmt->format;
			csid_try_format(csid, sd_state, i, format, fmt->which);
		}
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
			.code = MEDIA_BUS_FMT_UYVY8_1X16,
			.width = 1920,
			.height = 1080
		}
	};

	return csid_set_format(sd, fh ? fh->state : NULL, &format);
}

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
	if (value && media_pad_remote_pad_first(&csid->pads[MSM_CSID_PAD_SINK]))
		return -EBUSY;

	tg->enabled = !!value;

	return csid->res->hw_ops->configure_testgen_pattern(csid, value);
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
			 const struct camss_subdev_resources *res, u8 id)
{
	struct device *dev = camss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i, j;
	int ret;

	csid->camss = camss;
	csid->id = id;
	csid->res = &res->csid;

	if (dev_WARN_ONCE(dev, !csid->res->parent_dev_ops,
			  "Error: CSID depends on VFE/IFE device ops!\n")) {
		return -EINVAL;
	}

	csid->res->hw_ops->subdev_init(csid);

	/* Memory */

	if (camss->res->version == CAMSS_8250) {
		/* for titan 480, CSID registers are inside the VFE region,
		 * between the VFE "top" and "bus" registers. this requires
		 * VFE to be initialized before CSID
		 */
		if (id >= 2) /* VFE/CSID lite */
			csid->base = csid->res->parent_dev_ops->get_base_address(camss, id)
				+ VFE_480_LITE_CSID_OFFSET;
		else
			csid->base = csid->res->parent_dev_ops->get_base_address(camss, id)
				 + VFE_480_CSID_OFFSET;
	} else {
		csid->base = devm_platform_ioremap_resource_byname(pdev, res->reg[0]);
		if (IS_ERR(csid->base))
			return PTR_ERR(csid->base);
	}

	/* Interrupt */

	ret = platform_get_irq_byname(pdev, res->interrupt[0]);
	if (ret < 0)
		return ret;

	csid->irq = ret;
	snprintf(csid->irq_name, sizeof(csid->irq_name), "%s_%s%d",
		 dev_name(dev), MSM_CSID_NAME, csid->id);
	ret = devm_request_irq(dev, csid->irq, csid->res->hw_ops->isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN,
			       csid->irq_name, csid);
	if (ret < 0) {
		dev_err(dev, "request_irq failed: %d\n", ret);
		return ret;
	}

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
	for (i = 0; i < ARRAY_SIZE(res->regulators); i++) {
		if (res->regulators[i])
			csid->num_supplies++;
	}

	if (csid->num_supplies) {
		csid->supplies = devm_kmalloc_array(camss->dev,
						    csid->num_supplies,
						    sizeof(*csid->supplies),
						    GFP_KERNEL);
		if (!csid->supplies)
			return -ENOMEM;
	}

	for (i = 0; i < csid->num_supplies; i++)
		csid->supplies[i].supply = res->regulators[i];

	ret = devm_regulator_bulk_get(camss->dev, csid->num_supplies,
				      csid->supplies);
	if (ret)
		return ret;

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
		if (media_pad_remote_pad_first(local))
			return -EBUSY;

	if ((local->flags & MEDIA_PAD_FL_SINK) &&
	    (flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct csid_device *csid;
		struct csiphy_device *csiphy;
		struct csiphy_lanes_cfg *lane_cfg;

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
	}
	/* Decide which virtual channels to enable based on which source pads are enabled */
	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
		struct csid_device *csid = v4l2_get_subdevdata(sd);
		struct device *dev = csid->camss->dev;

		if (flags & MEDIA_LNK_FL_ENABLED)
			csid->phy.en_vc |= BIT(local->index - 1);
		else
			csid->phy.en_vc &= ~BIT(local->index - 1);

		csid->phy.need_vc_update = true;

		dev_dbg(dev, "%s: Enabled CSID virtual channels mask 0x%x\n",
			__func__, csid->phy.en_vc);
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
	int i;
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
				csid->testgen.nmodes, 0, 0,
				csid->testgen.modes);

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
	for (i = MSM_CSID_PAD_FIRST_SRC; i < MSM_CSID_PADS_NUM; ++i)
		pads[i].flags = MEDIA_PAD_FL_SOURCE;

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

inline bool csid_is_lite(struct csid_device *csid)
{
	return csid->camss->res->csid_res[csid->id].csid.is_lite;
}

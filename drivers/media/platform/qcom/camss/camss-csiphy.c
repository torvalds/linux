// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "camss-csiphy.h"
#include "camss.h"

#define MSM_CSIPHY_NAME "msm_csiphy"

struct csiphy_format {
	u32 code;
	u8 bpp;
};

static const struct csiphy_format csiphy_formats_8x16[] = {
	{ MEDIA_BUS_FMT_UYVY8_1X16, 8 },
	{ MEDIA_BUS_FMT_VYUY8_1X16, 8 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, 8 },
	{ MEDIA_BUS_FMT_YVYU8_1X16, 8 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
	{ MEDIA_BUS_FMT_Y10_1X10, 10 },
};

static const struct csiphy_format csiphy_formats_8x96[] = {
	{ MEDIA_BUS_FMT_UYVY8_1X16, 8 },
	{ MEDIA_BUS_FMT_VYUY8_1X16, 8 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, 8 },
	{ MEDIA_BUS_FMT_YVYU8_1X16, 8 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
	{ MEDIA_BUS_FMT_SBGGR14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGBRG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGRBG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, 14 },
	{ MEDIA_BUS_FMT_Y10_1X10, 10 },
};

static const struct csiphy_format csiphy_formats_sdm845[] = {
	{ MEDIA_BUS_FMT_UYVY8_1X16, 8 },
	{ MEDIA_BUS_FMT_VYUY8_1X16, 8 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, 8 },
	{ MEDIA_BUS_FMT_YVYU8_1X16, 8 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
	{ MEDIA_BUS_FMT_SBGGR14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGBRG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGRBG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, 14 },
	{ MEDIA_BUS_FMT_Y8_1X8, 8 },
	{ MEDIA_BUS_FMT_Y10_1X10, 10 },
};

/*
 * csiphy_get_bpp - map media bus format to bits per pixel
 * @formats: supported media bus formats array
 * @nformats: size of @formats array
 * @code: media bus format code
 *
 * Return number of bits per pixel
 */
static u8 csiphy_get_bpp(const struct csiphy_format *formats,
			 unsigned int nformats, u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return formats[i].bpp;

	WARN(1, "Unknown format\n");

	return formats[0].bpp;
}

/*
 * csiphy_set_clock_rates - Calculate and set clock rates on CSIPHY module
 * @csiphy: CSIPHY device
 */
static int csiphy_set_clock_rates(struct csiphy_device *csiphy)
{
	struct device *dev = csiphy->camss->dev;
	s64 link_freq;
	int i, j;
	int ret;

	u8 bpp = csiphy_get_bpp(csiphy->formats, csiphy->nformats,
				csiphy->fmt[MSM_CSIPHY_PAD_SINK].code);
	u8 num_lanes = csiphy->cfg.csi2->lane_cfg.num_data;

	link_freq = camss_get_link_freq(&csiphy->subdev.entity, bpp, num_lanes);
	if (link_freq < 0)
		link_freq  = 0;

	for (i = 0; i < csiphy->nclocks; i++) {
		struct camss_clock *clock = &csiphy->clock[i];

		if (csiphy->rate_set[i]) {
			u64 min_rate = link_freq / 4;
			long round_rate;

			camss_add_clock_margin(&min_rate);

			for (j = 0; j < clock->nfreqs; j++)
				if (min_rate < clock->freq[j])
					break;

			if (j == clock->nfreqs) {
				dev_err(dev,
					"Pixel clock is too high for CSIPHY\n");
				return -EINVAL;
			}

			/* if sensor pixel clock is not available */
			/* set highest possible CSIPHY clock rate */
			if (min_rate == 0)
				j = clock->nfreqs - 1;

			round_rate = clk_round_rate(clock->clk, clock->freq[j]);
			if (round_rate < 0) {
				dev_err(dev, "clk round rate failed: %ld\n",
					round_rate);
				return -EINVAL;
			}

			csiphy->timer_clk_rate = round_rate;

			ret = clk_set_rate(clock->clk, csiphy->timer_clk_rate);
			if (ret < 0) {
				dev_err(dev, "clk set rate failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

/*
 * csiphy_set_power - Power on/off CSIPHY module
 * @sd: CSIPHY V4L2 subdevice
 * @on: Requested power state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csiphy_set_power(struct v4l2_subdev *sd, int on)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	struct device *dev = csiphy->camss->dev;

	if (on) {
		int ret;

		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			return ret;

		ret = csiphy_set_clock_rates(csiphy);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = camss_enable_clocks(csiphy->nclocks, csiphy->clock, dev);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		enable_irq(csiphy->irq);

		csiphy->ops->reset(csiphy);

		csiphy->ops->hw_version_read(csiphy, dev);
	} else {
		disable_irq(csiphy->irq);

		camss_disable_clocks(csiphy->nclocks, csiphy->clock);

		pm_runtime_put_sync(dev);
	}

	return 0;
}

/*
 * csiphy_stream_on - Enable streaming on CSIPHY module
 * @csiphy: CSIPHY device
 *
 * Helper function to enable streaming on CSIPHY module.
 * Main configuration of CSIPHY module is also done here.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csiphy_stream_on(struct csiphy_device *csiphy)
{
	struct csiphy_config *cfg = &csiphy->cfg;
	s64 link_freq;
	u8 lane_mask = csiphy->ops->get_lane_mask(&cfg->csi2->lane_cfg);
	u8 bpp = csiphy_get_bpp(csiphy->formats, csiphy->nformats,
				csiphy->fmt[MSM_CSIPHY_PAD_SINK].code);
	u8 num_lanes = csiphy->cfg.csi2->lane_cfg.num_data;
	u8 val;

	link_freq = camss_get_link_freq(&csiphy->subdev.entity, bpp, num_lanes);

	if (link_freq < 0) {
		dev_err(csiphy->camss->dev,
			"Cannot get CSI2 transmitter's link frequency\n");
		return -EINVAL;
	}

	if (csiphy->base_clk_mux) {
		val = readl_relaxed(csiphy->base_clk_mux);
		if (cfg->combo_mode && (lane_mask & 0x18) == 0x18) {
			val &= ~0xf0;
			val |= cfg->csid_id << 4;
		} else {
			val &= ~0xf;
			val |= cfg->csid_id;
		}
		writel_relaxed(val, csiphy->base_clk_mux);

		/* Enforce reg write ordering between clk mux & lane enabling */
		wmb();
	}

	csiphy->ops->lanes_enable(csiphy, cfg, link_freq, lane_mask);

	return 0;
}

/*
 * csiphy_stream_off - Disable streaming on CSIPHY module
 * @csiphy: CSIPHY device
 *
 * Helper function to disable streaming on CSIPHY module
 */
static void csiphy_stream_off(struct csiphy_device *csiphy)
{
	csiphy->ops->lanes_disable(csiphy, &csiphy->cfg);
}


/*
 * csiphy_set_stream - Enable/disable streaming on CSIPHY module
 * @sd: CSIPHY V4L2 subdevice
 * @enable: Requested streaming state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csiphy_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (enable)
		ret = csiphy_stream_on(csiphy);
	else
		csiphy_stream_off(csiphy);

	return ret;
}

/*
 * __csiphy_get_format - Get pointer to format structure
 * @csiphy: CSIPHY device
 * @sd_state: V4L2 subdev state
 * @pad: pad from which format is requested
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE format structure
 */
static struct v4l2_mbus_framefmt *
__csiphy_get_format(struct csiphy_device *csiphy,
		    struct v4l2_subdev_state *sd_state,
		    unsigned int pad,
		    enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_state_get_format(sd_state, pad);

	return &csiphy->fmt[pad];
}

/*
 * csiphy_try_format - Handle try format by pad subdev method
 * @csiphy: CSIPHY device
 * @sd_state: V4L2 subdev state
 * @pad: pad on which format is requested
 * @fmt: pointer to v4l2 format structure
 * @which: wanted subdev format
 */
static void csiphy_try_format(struct csiphy_device *csiphy,
			      struct v4l2_subdev_state *sd_state,
			      unsigned int pad,
			      struct v4l2_mbus_framefmt *fmt,
			      enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case MSM_CSIPHY_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < csiphy->nformats; i++)
			if (fmt->code == csiphy->formats[i].code)
				break;

		/* If not found, use UYVY as default */
		if (i >= csiphy->nformats)
			fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;

		fmt->width = clamp_t(u32, fmt->width, 1, 8191);
		fmt->height = clamp_t(u32, fmt->height, 1, 8191);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		break;

	case MSM_CSIPHY_PAD_SRC:
		/* Set and return a format same as sink pad */

		*fmt = *__csiphy_get_format(csiphy, sd_state,
					    MSM_CSID_PAD_SINK,
					    which);

		break;
	}
}

/*
 * csiphy_enum_mbus_code - Handle pixel format enumeration
 * @sd: CSIPHY V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int csiphy_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (code->pad == MSM_CSIPHY_PAD_SINK) {
		if (code->index >= csiphy->nformats)
			return -EINVAL;

		code->code = csiphy->formats[code->index].code;
	} else {
		if (code->index > 0)
			return -EINVAL;

		format = __csiphy_get_format(csiphy, sd_state,
					     MSM_CSIPHY_PAD_SINK,
					     code->which);

		code->code = format->code;
	}

	return 0;
}

/*
 * csiphy_enum_frame_size - Handle frame size enumeration
 * @sd: CSIPHY V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fse: pointer to v4l2_subdev_frame_size_enum structure
 * return -EINVAL or zero on success
 */
static int csiphy_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csiphy_try_format(csiphy, sd_state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csiphy_try_format(csiphy, sd_state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * csiphy_get_format - Handle get format by pads subdev method
 * @sd: CSIPHY V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csiphy_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csiphy_get_format(csiphy, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/*
 * csiphy_set_format - Handle set format by pads subdev method
 * @sd: CSIPHY V4L2 subdevice
 * @sd_state: V4L2 subdev state
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int csiphy_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct csiphy_device *csiphy = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csiphy_get_format(csiphy, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	csiphy_try_format(csiphy, sd_state, fmt->pad, &fmt->format,
			  fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == MSM_CSIPHY_PAD_SINK) {
		format = __csiphy_get_format(csiphy, sd_state,
					     MSM_CSIPHY_PAD_SRC,
					     fmt->which);

		*format = fmt->format;
		csiphy_try_format(csiphy, sd_state, MSM_CSIPHY_PAD_SRC,
				  format,
				  fmt->which);
	}

	return 0;
}

/*
 * csiphy_init_formats - Initialize formats on all pads
 * @sd: CSIPHY V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csiphy_init_formats(struct v4l2_subdev *sd,
			       struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = MSM_CSIPHY_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
			      V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_UYVY8_1X16,
			.width = 1920,
			.height = 1080
		}
	};

	return csiphy_set_format(sd, fh ? fh->state : NULL, &format);
}

static bool csiphy_match_clock_name(const char *clock_name, const char *format,
				    int index)
{
	char name[16]; /* csiphyXXX_timer\0 */

	snprintf(name, sizeof(name), format, index);
	return !strcmp(clock_name, name);
}

/*
 * msm_csiphy_subdev_init - Initialize CSIPHY device structure and resources
 * @csiphy: CSIPHY device
 * @res: CSIPHY module resources table
 * @id: CSIPHY module id
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_csiphy_subdev_init(struct camss *camss,
			   struct csiphy_device *csiphy,
			   const struct camss_subdev_resources *res, u8 id)
{
	struct device *dev = camss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i, j, k;
	int ret;

	csiphy->camss = camss;
	csiphy->id = id;
	csiphy->cfg.combo_mode = 0;
	csiphy->ops = res->ops;

	switch (camss->res->version) {
	case CAMSS_8x16:
		csiphy->formats = csiphy_formats_8x16;
		csiphy->nformats = ARRAY_SIZE(csiphy_formats_8x16);
		break;
	case CAMSS_8x96:
	case CAMSS_660:
		csiphy->formats = csiphy_formats_8x96;
		csiphy->nformats = ARRAY_SIZE(csiphy_formats_8x96);
		break;
	case CAMSS_845:
	case CAMSS_8250:
	case CAMSS_8280XP:
		csiphy->formats = csiphy_formats_sdm845;
		csiphy->nformats = ARRAY_SIZE(csiphy_formats_sdm845);
		break;
	}

	/* Memory */

	csiphy->base = devm_platform_ioremap_resource_byname(pdev, res->reg[0]);
	if (IS_ERR(csiphy->base))
		return PTR_ERR(csiphy->base);

	if (camss->res->version == CAMSS_8x16 ||
	    camss->res->version == CAMSS_8x96) {
		csiphy->base_clk_mux =
			devm_platform_ioremap_resource_byname(pdev, res->reg[1]);
		if (IS_ERR(csiphy->base_clk_mux))
			return PTR_ERR(csiphy->base_clk_mux);
	} else {
		csiphy->base_clk_mux = NULL;
	}

	/* Interrupt */

	ret = platform_get_irq_byname(pdev, res->interrupt[0]);
	if (ret < 0)
		return ret;

	csiphy->irq = ret;
	snprintf(csiphy->irq_name, sizeof(csiphy->irq_name), "%s_%s%d",
		 dev_name(dev), MSM_CSIPHY_NAME, csiphy->id);

	ret = devm_request_irq(dev, csiphy->irq, csiphy->ops->isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN,
			       csiphy->irq_name, csiphy);
	if (ret < 0) {
		dev_err(dev, "request_irq failed: %d\n", ret);
		return ret;
	}

	/* Clocks */

	csiphy->nclocks = 0;
	while (res->clock[csiphy->nclocks])
		csiphy->nclocks++;

	csiphy->clock = devm_kcalloc(dev,
				     csiphy->nclocks, sizeof(*csiphy->clock),
				     GFP_KERNEL);
	if (!csiphy->clock)
		return -ENOMEM;

	csiphy->rate_set = devm_kcalloc(dev,
					csiphy->nclocks,
					sizeof(*csiphy->rate_set),
					GFP_KERNEL);
	if (!csiphy->rate_set)
		return -ENOMEM;

	for (i = 0; i < csiphy->nclocks; i++) {
		struct camss_clock *clock = &csiphy->clock[i];

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

		for (k = 0; k < camss->res->csiphy_num; k++) {
			csiphy->rate_set[i] = csiphy_match_clock_name(clock->name,
								      "csiphy%d_timer", k);
			if (csiphy->rate_set[i])
				break;

			if (camss->res->version == CAMSS_660) {
				csiphy->rate_set[i] = csiphy_match_clock_name(clock->name,
									      "csi%d_phy", k);
				if (csiphy->rate_set[i])
					break;
			}

			csiphy->rate_set[i] = csiphy_match_clock_name(clock->name, "csiphy%d", k);
			if (csiphy->rate_set[i])
				break;
		}
	}

	return 0;
}

/*
 * csiphy_link_setup - Setup CSIPHY connections
 * @entity: Pointer to media entity structure
 * @local: Pointer to local pad
 * @remote: Pointer to remote pad
 * @flags: Link flags
 *
 * Rreturn 0 on success
 */
static int csiphy_link_setup(struct media_entity *entity,
			     const struct media_pad *local,
			     const struct media_pad *remote, u32 flags)
{
	if ((local->flags & MEDIA_PAD_FL_SOURCE) &&
	    (flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct csiphy_device *csiphy;
		struct csid_device *csid;

		if (media_pad_remote_pad_first(local))
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(entity);
		csiphy = v4l2_get_subdevdata(sd);

		sd = media_entity_to_v4l2_subdev(remote->entity);
		csid = v4l2_get_subdevdata(sd);

		csiphy->cfg.csid_id = csid->id;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops csiphy_core_ops = {
	.s_power = csiphy_set_power,
};

static const struct v4l2_subdev_video_ops csiphy_video_ops = {
	.s_stream = csiphy_set_stream,
};

static const struct v4l2_subdev_pad_ops csiphy_pad_ops = {
	.enum_mbus_code = csiphy_enum_mbus_code,
	.enum_frame_size = csiphy_enum_frame_size,
	.get_fmt = csiphy_get_format,
	.set_fmt = csiphy_set_format,
};

static const struct v4l2_subdev_ops csiphy_v4l2_ops = {
	.core = &csiphy_core_ops,
	.video = &csiphy_video_ops,
	.pad = &csiphy_pad_ops,
};

static const struct v4l2_subdev_internal_ops csiphy_v4l2_internal_ops = {
	.open = csiphy_init_formats,
};

static const struct media_entity_operations csiphy_media_ops = {
	.link_setup = csiphy_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * msm_csiphy_register_entity - Register subdev node for CSIPHY module
 * @csiphy: CSIPHY device
 * @v4l2_dev: V4L2 device
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_csiphy_register_entity(struct csiphy_device *csiphy,
			       struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &csiphy->subdev;
	struct media_pad *pads = csiphy->pads;
	struct device *dev = csiphy->camss->dev;
	int ret;

	v4l2_subdev_init(sd, &csiphy_v4l2_ops);
	sd->internal_ops = &csiphy_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		 MSM_CSIPHY_NAME, csiphy->id);
	v4l2_set_subdevdata(sd, csiphy);

	ret = csiphy_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[MSM_CSIPHY_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[MSM_CSIPHY_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &csiphy_media_ops;
	ret = media_entity_pads_init(&sd->entity, MSM_CSIPHY_PADS_NUM, pads);
	if (ret < 0) {
		dev_err(dev, "Failed to init media entity: %d\n", ret);
		return ret;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		media_entity_cleanup(&sd->entity);
	}

	return ret;
}

/*
 * msm_csiphy_unregister_entity - Unregister CSIPHY module subdev node
 * @csiphy: CSIPHY device
 */
void msm_csiphy_unregister_entity(struct csiphy_device *csiphy)
{
	v4l2_device_unregister_subdev(&csiphy->subdev);
	media_entity_cleanup(&csiphy->subdev.entity);
}

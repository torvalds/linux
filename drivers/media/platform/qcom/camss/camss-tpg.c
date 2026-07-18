// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Qualcomm MSM Camera Subsystem - TPG Module
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/mipi-csi2.h>

#include "camss-tpg.h"
#include "camss.h"

static const struct tpg_format_info formats_gen1[] = {
	{
		MEDIA_BUS_FMT_SBGGR8_1X8,
		MIPI_CSI2_DT_RAW8,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
	},
	{
		MEDIA_BUS_FMT_SGBRG8_1X8,
		MIPI_CSI2_DT_RAW8,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
	},
	{
		MEDIA_BUS_FMT_SGRBG8_1X8,
		MIPI_CSI2_DT_RAW8,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
	},
	{
		MEDIA_BUS_FMT_SRGGB8_1X8,
		MIPI_CSI2_DT_RAW8,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
	},
	{
		MEDIA_BUS_FMT_SBGGR10_1X10,
		MIPI_CSI2_DT_RAW10,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
	},
	{
		MEDIA_BUS_FMT_SGBRG10_1X10,
		MIPI_CSI2_DT_RAW10,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
	},
	{
		MEDIA_BUS_FMT_SGRBG10_1X10,
		MIPI_CSI2_DT_RAW10,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
	},
	{
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MIPI_CSI2_DT_RAW10,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
	},
	{
		MEDIA_BUS_FMT_SBGGR12_1X12,
		MIPI_CSI2_DT_RAW12,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
	},
	{
		MEDIA_BUS_FMT_SGBRG12_1X12,
		MIPI_CSI2_DT_RAW12,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
	},
	{
		MEDIA_BUS_FMT_SGRBG12_1X12,
		MIPI_CSI2_DT_RAW12,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
	},
	{
		MEDIA_BUS_FMT_SRGGB12_1X12,
		MIPI_CSI2_DT_RAW12,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
		12,
	},
	{
		MEDIA_BUS_FMT_Y8_1X8,
		MIPI_CSI2_DT_RAW8,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
		8,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
		MIPI_CSI2_DT_RAW10,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
		10,
	},
};

const struct tpg_formats tpg_formats_gen1 = {
	.nformats = ARRAY_SIZE(formats_gen1),
	.formats = formats_gen1
};

const struct tpg_format_info *tpg_get_fmt_entry(const struct tpg_format_info *formats,
						unsigned int nformats,
						u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return &formats[i];

	return ERR_PTR(-EINVAL);
}

static int tpg_set_clock_rates(struct tpg_device *tpg)
{
	struct device *dev = tpg->camss->dev;
	int i, ret;

	for (i = 0; i < tpg->nclocks; i++) {
		struct camss_clock *clock = &tpg->clock[i];
		long round_rate;

		if (clock->freq) {
			round_rate = clk_round_rate(clock->clk, clock->freq[0]);
			if (round_rate < 0) {
				dev_err(dev, "clk round rate failed: %ld\n",
					round_rate);
				return -EINVAL;
			}

			ret = clk_set_rate(clock->clk, round_rate);
			if (ret < 0) {
				dev_err(dev, "clk set rate failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int tpg_set_power(struct v4l2_subdev *sd, int on)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct device *dev = tpg->camss->dev;

	if (on) {
		int ret;

		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			return ret;

		ret = tpg_set_clock_rates(tpg);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = camss_enable_clocks(tpg->nclocks, tpg->clock, dev);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		tpg->res->hw_ops->reset(tpg);

		tpg->res->hw_ops->hw_version(tpg);
	} else {
		camss_disable_clocks(tpg->nclocks, tpg->clock);

		pm_runtime_put_sync(dev);
	}

	return 0;
}

static int tpg_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	int ret;

	if (enable) {
		ret = v4l2_ctrl_handler_setup(&tpg->ctrls);
		if (ret < 0) {
			dev_err(tpg->camss->dev,
				"could not sync v4l2 controls: %d\n", ret);
			return ret;
		}
	}

	return tpg->res->hw_ops->configure_stream(tpg, enable);
}

static struct v4l2_mbus_framefmt *
__tpg_get_format(struct tpg_device *tpg,
		 struct v4l2_subdev_state *sd_state,
		 unsigned int pad,
		 enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_state_get_format(sd_state,
						    pad);

	return &tpg->fmt;
}

static void tpg_try_format(struct tpg_device *tpg,
			   struct v4l2_mbus_framefmt *fmt)
{
	unsigned int i;

	for (i = 0; i < tpg->res->formats->nformats; i++)
		if (tpg->res->formats->formats[i].code == fmt->code)
			break;

	if (i >= tpg->res->formats->nformats)
		fmt->code = MEDIA_BUS_FMT_SBGGR8_1X8;

	fmt->width = clamp_t(u32, fmt->width, TPG_MIN_WIDTH, TPG_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, TPG_MIN_HEIGHT, TPG_MAX_HEIGHT);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int tpg_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);

	if (code->index >= tpg->res->formats->nformats)
		return -EINVAL;

	code->code = tpg->res->formats->formats[code->index].code;

	return 0;
}

static int tpg_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	unsigned int i;

	if (fse->index != 0)
		return -EINVAL;

	for (i = 0; i < tpg->res->formats->nformats; i++)
		if (tpg->res->formats->formats[i].code == fse->code)
			break;

	if (i >= tpg->res->formats->nformats)
		return -EINVAL;

	fse->min_width = TPG_MIN_WIDTH;
	fse->min_height = TPG_MIN_HEIGHT;
	fse->max_width = TPG_MAX_WIDTH;
	fse->max_height = TPG_MAX_HEIGHT;

	return 0;
}

static int tpg_get_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __tpg_get_format(tpg, sd_state, fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int tpg_set_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __tpg_get_format(tpg, sd_state, fmt->pad, fmt->which);
	if (!format)
		return -EINVAL;

	tpg_try_format(tpg, &fmt->format);
	*format = fmt->format;

	return 0;
}

static int tpg_init_formats(struct v4l2_subdev *sd,
			    struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = MSM_TPG_PAD_SRC,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
			      V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_SBGGR8_1X8,
			.width = 1920,
			.height = 1080,
		}
	};

	return tpg_set_format(sd, fh ? fh->state : NULL, &format);
}

static int tpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tpg_device *tpg = container_of(ctrl->handler,
					      struct tpg_device, ctrls);
	int ret = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		ret = tpg->res->hw_ops->configure_testgen_pattern(tpg, ctrl->val);
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops tpg_ctrl_ops = {
	.s_ctrl = tpg_s_ctrl,
};

int msm_tpg_subdev_init(struct camss *camss,
			struct tpg_device *tpg,
			const struct camss_subdev_resources *res, u8 id)
{
	struct platform_device *pdev;
	struct device *dev;
	int i, j;

	dev  = camss->dev;
	pdev = to_platform_device(dev);

	tpg->camss = camss;
	tpg->id = id;
	tpg->res = &res->tpg;
	tpg->res->hw_ops->subdev_init(tpg);

	tpg->base = devm_platform_ioremap_resource_byname(pdev, res->reg[0]);
	if (IS_ERR(tpg->base))
		return PTR_ERR(tpg->base);

	tpg->nclocks = 0;
	while (res->clock[tpg->nclocks])
		tpg->nclocks++;

	if (!tpg->nclocks)
		return 0;

	tpg->clock = devm_kcalloc(dev, tpg->nclocks,
				  sizeof(*tpg->clock), GFP_KERNEL);
	if (!tpg->clock)
		return -ENOMEM;

	for (i = 0; i < tpg->nclocks; i++) {
		struct camss_clock *clock = &tpg->clock[i];

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

		clock->freq = devm_kcalloc(dev, clock->nfreqs,
					   sizeof(*clock->freq), GFP_KERNEL);
		if (!clock->freq)
			return -ENOMEM;

		for (j = 0; j < clock->nfreqs; j++)
			clock->freq[j] = res->clock_rate[i][j];
	}

	return 0;
}

static int tpg_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_pad_remote_pad_first(local))
			return -EBUSY;

	return 0;
}

static const struct v4l2_subdev_core_ops tpg_core_ops = {
	.s_power = tpg_set_power,
};

static const struct v4l2_subdev_video_ops tpg_video_ops = {
	.s_stream = tpg_set_stream,
};

static const struct v4l2_subdev_pad_ops tpg_pad_ops = {
	.enum_mbus_code = tpg_enum_mbus_code,
	.enum_frame_size = tpg_enum_frame_size,
	.get_fmt = tpg_get_format,
	.set_fmt = tpg_set_format,
};

static const struct v4l2_subdev_ops tpg_v4l2_ops = {
	.core = &tpg_core_ops,
	.video = &tpg_video_ops,
	.pad = &tpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops tpg_v4l2_internal_ops = {
	.open = tpg_init_formats,
};

static const struct media_entity_operations tpg_media_ops = {
	.link_setup = tpg_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int msm_tpg_register_entity(struct tpg_device *tpg,
			    struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &tpg->subdev;
	struct device *dev = tpg->camss->dev;
	int ret;

	v4l2_subdev_init(sd, &tpg_v4l2_ops);
	sd->internal_ops = &tpg_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		 "msm_tpg", tpg->id);
	sd->grp_id = TPG_GRP_ID;
	v4l2_set_subdevdata(sd, tpg);

	ret = v4l2_ctrl_handler_init(&tpg->ctrls, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to init ctrl handler: %d\n", ret);
		return ret;
	}

	tpg->testgen_mode = v4l2_ctrl_new_std_menu_items(&tpg->ctrls,
							 &tpg_ctrl_ops, V4L2_CID_TEST_PATTERN,
							 tpg->testgen.nmodes, 0, 0,
							 tpg->testgen.modes);
	if (tpg->ctrls.error) {
		dev_err(dev, "Failed to init ctrl: %d\n", tpg->ctrls.error);
		ret = tpg->ctrls.error;
		goto free_ctrl;
	}

	tpg->subdev.ctrl_handler = &tpg->ctrls;

	ret = tpg_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		goto free_ctrl;
	}

	tpg->pad.flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.ops = &tpg_media_ops;
	ret = media_entity_pads_init(&sd->entity, 1, &tpg->pad);
	if (ret < 0) {
		dev_err(dev, "Failed to init media entity: %d\n", ret);
		goto free_ctrl;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		media_entity_cleanup(&sd->entity);
		goto free_ctrl;
	}

	return 0;

free_ctrl:
	v4l2_ctrl_handler_free(&tpg->ctrls);

	return ret;
}

void msm_tpg_unregister_entity(struct tpg_device *tpg)
{
	v4l2_device_unregister_subdev(&tpg->subdev);
	media_entity_cleanup(&tpg->subdev.entity);
	v4l2_ctrl_handler_free(&tpg->ctrls);
}

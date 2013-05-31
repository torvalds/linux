/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *          Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-device.h>

#include "media-dev.h"
#include "fimc-is-command.h"
#include "fimc-is-param.h"
#include "fimc-is-regs.h"
#include "fimc-is.h"

static int debug;
module_param_named(debug_isp, debug, int, S_IRUGO | S_IWUSR);

static const struct fimc_fmt fimc_isp_formats[FIMC_ISP_NUM_FORMATS] = {
	{
		.name		= "RAW8 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.depth		= { 8 },
		.color		= FIMC_FMT_RAW8,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG8_1X8,
	}, {
		.name		= "RAW10 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.depth		= { 10 },
		.color		= FIMC_FMT_RAW10,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG10_1X10,
	}, {
		.name		= "RAW12 (GRBG)",
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.depth		= { 12 },
		.color		= FIMC_FMT_RAW12,
		.memplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_SGRBG12_1X12,
	},
};

/**
 * fimc_isp_find_format - lookup color format by fourcc or media bus code
 * @pixelformat: fourcc to match, ignored if null
 * @mbus_code: media bus code to match, ignored if null
 * @index: index to the fimc_isp_formats array, ignored if negative
 */
const struct fimc_fmt *fimc_isp_find_format(const u32 *pixelformat,
					const u32 *mbus_code, int index)
{
	const struct fimc_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(fimc_isp_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_isp_formats); ++i) {
		fmt = &fimc_isp_formats[i];
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

void fimc_isp_irq_handler(struct fimc_is *is)
{
	is->i2h_cmd.args[0] = mcuctl_read(is, MCUCTL_REG_ISSR(20));
	is->i2h_cmd.args[1] = mcuctl_read(is, MCUCTL_REG_ISSR(21));

	fimc_is_fw_clear_irq1(is, FIMC_IS_INT_FRAME_DONE_ISP);

	/* TODO: Complete ISP DMA interrupt handler */
	wake_up(&is->irq_queue);
}

/* Capture subdev media entity operations */
static int fimc_is_link_setup(struct media_entity *entity,
				const struct media_pad *local,
				const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations fimc_is_subdev_media_ops = {
	.link_setup = fimc_is_link_setup,
};

static int fimc_is_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_mbus_code_enum *code)
{
	const struct fimc_fmt *fmt;

	fmt = fimc_isp_find_format(NULL, NULL, code->index);
	if (!fmt)
		return -EINVAL;
	code->code = fmt->mbus_code;
	return 0;
}

static int fimc_isp_subdev_get_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_format *fmt)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct v4l2_mbus_framefmt cur_fmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(fh, fmt->pad);
		fmt->format = *mf;
		return 0;
	}

	mf->colorspace = V4L2_COLORSPACE_JPEG;

	mutex_lock(&isp->subdev_lock);
	__is_get_frame_size(is, &cur_fmt);

	if (fmt->pad == FIMC_ISP_SD_PAD_SINK) {
		/* full camera input frame size */
		mf->width = cur_fmt.width + FIMC_ISP_CAC_MARGIN_WIDTH;
		mf->height = cur_fmt.height + FIMC_ISP_CAC_MARGIN_HEIGHT;
		mf->code = V4L2_MBUS_FMT_SGRBG10_1X10;
	} else {
		/* crop size */
		mf->width = cur_fmt.width;
		mf->height = cur_fmt.height;
		mf->code = V4L2_MBUS_FMT_YUV10_1X30;
	}

	mutex_unlock(&isp->subdev_lock);

	v4l2_dbg(1, debug, sd, "%s: pad%d: fmt: 0x%x, %dx%d\n",
		 __func__, fmt->pad, mf->code, mf->width, mf->height);

	return 0;
}

static void __isp_subdev_try_format(struct fimc_isp *isp,
				   struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	if (fmt->pad == FIMC_ISP_SD_PAD_SINK) {
		v4l_bound_align_image(&mf->width, FIMC_ISP_SINK_WIDTH_MIN,
				FIMC_ISP_SINK_WIDTH_MAX, 0,
				&mf->height, FIMC_ISP_SINK_HEIGHT_MIN,
				FIMC_ISP_SINK_HEIGHT_MAX, 0, 0);
		isp->subdev_fmt = *mf;
	} else {
		/* Allow changing format only on sink pad */
		mf->width = isp->subdev_fmt.width - FIMC_ISP_CAC_MARGIN_WIDTH;
		mf->height = isp->subdev_fmt.height - FIMC_ISP_CAC_MARGIN_HEIGHT;
		mf->code = isp->subdev_fmt.code;
	}
}

static int fimc_isp_subdev_set_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_format *fmt)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	int ret = 0;

	v4l2_dbg(1, debug, sd, "%s: pad%d: code: 0x%x, %dx%d\n",
		 __func__, fmt->pad, mf->code, mf->width, mf->height);

	mf->colorspace = V4L2_COLORSPACE_JPEG;

	mutex_lock(&isp->subdev_lock);
	__isp_subdev_try_format(isp, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(fh, fmt->pad);
		*mf = fmt->format;
		mutex_unlock(&isp->subdev_lock);
		return 0;
	}

	if (sd->entity.stream_count == 0)
		__is_set_frame_size(is, mf);
	else
		ret = -EBUSY;
	mutex_unlock(&isp->subdev_lock);

	return ret;
}

static int fimc_isp_subdev_s_stream(struct v4l2_subdev *sd, int on)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	int ret;

	v4l2_dbg(1, debug, sd, "%s: on: %d\n", __func__, on);

	if (!test_bit(IS_ST_INIT_DONE, &is->state))
		return -EBUSY;

	fimc_is_mem_barrier();

	if (on) {
		if (__get_pending_param_count(is)) {
			ret = fimc_is_itf_s_param(is, true);
			if (ret < 0)
				return ret;
		}

		v4l2_dbg(1, debug, sd, "changing mode to %d\n",
						is->config_index);
		ret = fimc_is_itf_mode_change(is);
		if (ret)
			return -EINVAL;

		clear_bit(IS_ST_STREAM_ON, &is->state);
		fimc_is_hw_stream_on(is);
		ret = fimc_is_wait_event(is, IS_ST_STREAM_ON, 1,
					 FIMC_IS_CONFIG_TIMEOUT);
		if (ret < 0) {
			v4l2_err(sd, "stream on timeout\n");
			return ret;
		}
	} else {
		clear_bit(IS_ST_STREAM_OFF, &is->state);
		fimc_is_hw_stream_off(is);
		ret = fimc_is_wait_event(is, IS_ST_STREAM_OFF, 1,
					 FIMC_IS_CONFIG_TIMEOUT);
		if (ret < 0) {
			v4l2_err(sd, "stream off timeout\n");
			return ret;
		}
		is->setfile.sub_index = 0;
	}

	return 0;
}

static int fimc_isp_subdev_s_power(struct v4l2_subdev *sd, int on)
{
	struct fimc_isp *isp = v4l2_get_subdevdata(sd);
	struct fimc_is *is = fimc_isp_to_is(isp);
	int ret = 0;

	pr_debug("on: %d\n", on);

	if (on) {
		ret = pm_runtime_get_sync(&is->pdev->dev);
		if (ret < 0)
			return ret;
		set_bit(IS_ST_PWR_ON, &is->state);

		ret = fimc_is_start_firmware(is);
		if (ret < 0) {
			v4l2_err(sd, "firmware booting failed\n");
			pm_runtime_put(&is->pdev->dev);
			return ret;
		}
		set_bit(IS_ST_PWR_SUBIP_ON, &is->state);

		ret = fimc_is_hw_initialize(is);
	} else {
		/* Close sensor */
		if (!test_bit(IS_ST_PWR_ON, &is->state)) {
			fimc_is_hw_close_sensor(is, 0);

			ret = fimc_is_wait_event(is, IS_ST_OPEN_SENSOR, 0,
						 FIMC_IS_CONFIG_TIMEOUT);
			if (ret < 0) {
				v4l2_err(sd, "sensor close timeout\n");
				return ret;
			}
		}

		/* SUB IP power off */
		if (test_bit(IS_ST_PWR_SUBIP_ON, &is->state)) {
			fimc_is_hw_subip_power_off(is);
			ret = fimc_is_wait_event(is, IS_ST_PWR_SUBIP_ON, 0,
						 FIMC_IS_CONFIG_TIMEOUT);
			if (ret < 0) {
				v4l2_err(sd, "sub-IP power off timeout\n");
				return ret;
			}
		}

		fimc_is_cpu_set_power(is, 0);
		pm_runtime_put_sync(&is->pdev->dev);

		clear_bit(IS_ST_PWR_ON, &is->state);
		clear_bit(IS_ST_INIT_DONE, &is->state);
		is->state = 0;
		is->config[is->config_index].p_region_index[0] = 0;
		is->config[is->config_index].p_region_index[1] = 0;
		set_bit(IS_ST_IDLE, &is->state);
		wmb();
	}

	return ret;
}

static int fimc_isp_subdev_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, FIMC_ISP_SD_PAD_SINK);

	fmt.colorspace = V4L2_COLORSPACE_SRGB;
	fmt.code = fimc_isp_formats[0].mbus_code;
	fmt.width = DEFAULT_PREVIEW_STILL_WIDTH + FIMC_ISP_CAC_MARGIN_WIDTH;
	fmt.height = DEFAULT_PREVIEW_STILL_HEIGHT + FIMC_ISP_CAC_MARGIN_HEIGHT;
	fmt.field = V4L2_FIELD_NONE;
	*format = fmt;

	format = v4l2_subdev_get_try_format(fh, FIMC_ISP_SD_PAD_SRC_FIFO);
	fmt.width = DEFAULT_PREVIEW_STILL_WIDTH;
	fmt.height = DEFAULT_PREVIEW_STILL_HEIGHT;
	*format = fmt;

	format = v4l2_subdev_get_try_format(fh, FIMC_ISP_SD_PAD_SRC_DMA);
	*format = fmt;

	return 0;
}

static const struct v4l2_subdev_internal_ops fimc_is_subdev_internal_ops = {
	.open = fimc_isp_subdev_open,
};

static const struct v4l2_subdev_pad_ops fimc_is_subdev_pad_ops = {
	.enum_mbus_code = fimc_is_subdev_enum_mbus_code,
	.get_fmt = fimc_isp_subdev_get_fmt,
	.set_fmt = fimc_isp_subdev_set_fmt,
};

static const struct v4l2_subdev_video_ops fimc_is_subdev_video_ops = {
	.s_stream = fimc_isp_subdev_s_stream,
};

static const struct v4l2_subdev_core_ops fimc_is_core_ops = {
	.s_power = fimc_isp_subdev_s_power,
};

static struct v4l2_subdev_ops fimc_is_subdev_ops = {
	.core = &fimc_is_core_ops,
	.video = &fimc_is_subdev_video_ops,
	.pad = &fimc_is_subdev_pad_ops,
};

static int __ctrl_set_white_balance(struct fimc_is *is, int value)
{
	switch (value) {
	case V4L2_WHITE_BALANCE_AUTO:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_AUTO, 0);
		break;
	case V4L2_WHITE_BALANCE_DAYLIGHT:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
					ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case V4L2_WHITE_BALANCE_CLOUDY:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
					ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case V4L2_WHITE_BALANCE_INCANDESCENT:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
					ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case V4L2_WHITE_BALANCE_FLUORESCENT:
		__is_set_isp_awb(is, ISP_AWB_COMMAND_ILLUMINATION,
					ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int __ctrl_set_aewb_lock(struct fimc_is *is,
				      struct v4l2_ctrl *ctrl)
{
	bool awb_lock = ctrl->val & V4L2_LOCK_WHITE_BALANCE;
	bool ae_lock = ctrl->val & V4L2_LOCK_EXPOSURE;
	struct isp_param *isp = &is->is_p_region->parameter.isp;
	int cmd, ret;

	cmd = ae_lock ? ISP_AA_COMMAND_STOP : ISP_AA_COMMAND_START;
	isp->aa.cmd = cmd;
	isp->aa.target = ISP_AA_TARGET_AE;
	fimc_is_set_param_bit(is, PARAM_ISP_AA);
	is->af.ae_lock_state = ae_lock;
	wmb();

	ret = fimc_is_itf_s_param(is, false);
	if (ret < 0)
		return ret;

	cmd = awb_lock ? ISP_AA_COMMAND_STOP : ISP_AA_COMMAND_START;
	isp->aa.cmd = cmd;
	isp->aa.target = ISP_AA_TARGET_AE;
	fimc_is_set_param_bit(is, PARAM_ISP_AA);
	is->af.awb_lock_state = awb_lock;
	wmb();

	return fimc_is_itf_s_param(is, false);
}

/* Supported manual ISO values */
static const s64 iso_qmenu[] = {
	50, 100, 200, 400, 800,
};

static int __ctrl_set_iso(struct fimc_is *is, int value)
{
	unsigned int idx, iso;

	if (value == V4L2_ISO_SENSITIVITY_AUTO) {
		__is_set_isp_iso(is, ISP_ISO_COMMAND_AUTO, 0);
		return 0;
	}
	idx = is->isp.ctrls.iso->val;
	if (idx >= ARRAY_SIZE(iso_qmenu))
		return -EINVAL;

	iso = iso_qmenu[idx];
	__is_set_isp_iso(is, ISP_ISO_COMMAND_MANUAL, iso);
	return 0;
}

static int __ctrl_set_metering(struct fimc_is *is, unsigned int value)
{
	unsigned int val;

	switch (value) {
	case V4L2_EXPOSURE_METERING_AVERAGE:
		val = ISP_METERING_COMMAND_AVERAGE;
		break;
	case V4L2_EXPOSURE_METERING_CENTER_WEIGHTED:
		val = ISP_METERING_COMMAND_CENTER;
		break;
	case V4L2_EXPOSURE_METERING_SPOT:
		val = ISP_METERING_COMMAND_SPOT;
		break;
	case V4L2_EXPOSURE_METERING_MATRIX:
		val = ISP_METERING_COMMAND_MATRIX;
		break;
	default:
		return -EINVAL;
	};

	__is_set_isp_metering(is, IS_METERING_CONFIG_CMD, val);
	return 0;
}

static int __ctrl_set_afc(struct fimc_is *is, int value)
{
	switch (value) {
	case V4L2_CID_POWER_LINE_FREQUENCY_DISABLED:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_DISABLE, 0);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_MANUAL, 50);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_MANUAL, 60);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		__is_set_isp_afc(is, ISP_AFC_COMMAND_AUTO, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int __ctrl_set_image_effect(struct fimc_is *is, int value)
{
	static const u8 effects[][2] = {
		{ V4L2_COLORFX_NONE,	 ISP_IMAGE_EFFECT_DISABLE },
		{ V4L2_COLORFX_BW,	 ISP_IMAGE_EFFECT_MONOCHROME },
		{ V4L2_COLORFX_SEPIA,	 ISP_IMAGE_EFFECT_SEPIA },
		{ V4L2_COLORFX_NEGATIVE, ISP_IMAGE_EFFECT_NEGATIVE_MONO },
		{ 16 /* TODO */,	 ISP_IMAGE_EFFECT_NEGATIVE_COLOR },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(effects); i++) {
		if (effects[i][0] != value)
			continue;

		__is_set_isp_effect(is, effects[i][1]);
		return 0;
	}

	return -EINVAL;
}

static int fimc_is_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fimc_isp *isp = ctrl_to_fimc_isp(ctrl);
	struct fimc_is *is = fimc_isp_to_is(isp);
	bool set_param = true;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_CONTRAST,
				    ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_SATURATION,
				    ctrl->val);
		break;

	case V4L2_CID_SHARPNESS:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_SHARPNESS,
				    ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_ABSOLUTE:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_EXPOSURE,
				    ctrl->val);
		break;

	case V4L2_CID_BRIGHTNESS:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS,
				    ctrl->val);
		break;

	case V4L2_CID_HUE:
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_HUE,
				    ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_METERING:
		ret = __ctrl_set_metering(is, ctrl->val);
		break;

	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ret = __ctrl_set_white_balance(is, ctrl->val);
		break;

	case V4L2_CID_3A_LOCK:
		ret = __ctrl_set_aewb_lock(is, ctrl);
		set_param = false;
		break;

	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		ret = __ctrl_set_iso(is, ctrl->val);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = __ctrl_set_afc(is, ctrl->val);
		break;

	case V4L2_CID_COLORFX:
		__ctrl_set_image_effect(is, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		v4l2_err(&isp->subdev, "Failed to set control: %s (%d)\n",
						ctrl->name, ctrl->val);
		return ret;
	}

	if (set_param && test_bit(IS_ST_STREAM_ON, &is->state))
		return fimc_is_itf_s_param(is, true);

	return 0;
}

static const struct v4l2_ctrl_ops fimc_isp_ctrl_ops = {
	.s_ctrl	= fimc_is_s_ctrl,
};

int fimc_isp_subdev_create(struct fimc_isp *isp)
{
	const struct v4l2_ctrl_ops *ops = &fimc_isp_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &isp->ctrls.handler;
	struct v4l2_subdev *sd = &isp->subdev;
	struct fimc_isp_ctrls *ctrls = &isp->ctrls;
	int ret;

	mutex_init(&isp->subdev_lock);

	v4l2_subdev_init(sd, &fimc_is_subdev_ops);
	sd->grp_id = GRP_ID_FIMC_IS;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "FIMC-IS-ISP");

	isp->subdev_pads[FIMC_ISP_SD_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isp->subdev_pads[FIMC_ISP_SD_PAD_SRC_FIFO].flags = MEDIA_PAD_FL_SOURCE;
	isp->subdev_pads[FIMC_ISP_SD_PAD_SRC_DMA].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, FIMC_ISP_SD_PADS_NUM,
				isp->subdev_pads, 0);
	if (ret)
		return ret;

	v4l2_ctrl_handler_init(handler, 20);

	ctrls->saturation = v4l2_ctrl_new_std(handler, ops, V4L2_CID_SATURATION,
						-2, 2, 1, 0);
	ctrls->brightness = v4l2_ctrl_new_std(handler, ops, V4L2_CID_BRIGHTNESS,
						-4, 4, 1, 0);
	ctrls->contrast = v4l2_ctrl_new_std(handler, ops, V4L2_CID_CONTRAST,
						-2, 2, 1, 0);
	ctrls->sharpness = v4l2_ctrl_new_std(handler, ops, V4L2_CID_SHARPNESS,
						-2, 2, 1, 0);
	ctrls->hue = v4l2_ctrl_new_std(handler, ops, V4L2_CID_HUE,
						-2, 2, 1, 0);

	ctrls->auto_wb = v4l2_ctrl_new_std_menu(handler, ops,
					V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
					8, ~0x14e, V4L2_WHITE_BALANCE_AUTO);

	ctrls->exposure = v4l2_ctrl_new_std(handler, ops,
					V4L2_CID_EXPOSURE_ABSOLUTE,
					-4, 4, 1, 0);

	ctrls->exp_metering = v4l2_ctrl_new_std_menu(handler, ops,
					V4L2_CID_EXPOSURE_METERING, 3,
					~0xf, V4L2_EXPOSURE_METERING_AVERAGE);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_POWER_LINE_FREQUENCY,
					V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
					V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
	/* ISO sensitivity */
	ctrls->auto_iso = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_ISO_SENSITIVITY_AUTO, 1, 0,
			V4L2_ISO_SENSITIVITY_AUTO);

	ctrls->iso = v4l2_ctrl_new_int_menu(handler, ops,
			V4L2_CID_ISO_SENSITIVITY, ARRAY_SIZE(iso_qmenu) - 1,
			ARRAY_SIZE(iso_qmenu)/2 - 1, iso_qmenu);

	ctrls->aewb_lock = v4l2_ctrl_new_std(handler, ops,
					V4L2_CID_3A_LOCK, 0, 0x3, 0, 0);

	/* TODO: Add support for NEGATIVE_COLOR option */
	ctrls->colorfx = v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_COLORFX,
			V4L2_COLORFX_SET_CBCR + 1, ~0x1000f, V4L2_COLORFX_NONE);

	if (handler->error) {
		media_entity_cleanup(&sd->entity);
		return handler->error;
	}

	v4l2_ctrl_auto_cluster(2, &ctrls->auto_iso,
			V4L2_ISO_SENSITIVITY_MANUAL, false);

	sd->ctrl_handler = handler;
	sd->internal_ops = &fimc_is_subdev_internal_ops;
	sd->entity.ops = &fimc_is_subdev_media_ops;
	v4l2_set_subdevdata(sd, isp);

	return 0;
}

void fimc_isp_subdev_destroy(struct fimc_isp *isp)
{
	struct v4l2_subdev *sd = &isp->subdev;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&isp->ctrls.handler);
	v4l2_set_subdevdata(sd, NULL);
}

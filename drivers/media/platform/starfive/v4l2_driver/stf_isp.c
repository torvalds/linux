// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <linux/firmware.h>

#define STF_ISP_NAME "stf_isp"

static const struct isp_format isp_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 16},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 16},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 12},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 12},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 12},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 12},
};

int stf_isp_subdev_init(struct stfcamss *stfcamss, int id)
{
	struct stf_isp_dev *isp_dev = &stfcamss->isp_dev[id];

	atomic_set(&isp_dev->ref_count, 0);
	isp_dev->sdev_type = id == 0 ? ISP0_DEV_TYPE : ISP1_DEV_TYPE;
	isp_dev->id = id;
	isp_dev->hw_ops = &isp_ops;
	isp_dev->stfcamss = stfcamss;
	isp_dev->formats = isp_formats_st7110;
	isp_dev->nformats = ARRAY_SIZE(isp_formats_st7110);
	mutex_init(&isp_dev->stream_lock);
	mutex_init(&isp_dev->setfile_lock);
	return 0;
}

/*
 * ISP Controls.
 */

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct stf_isp_dev,
			     ctrls.handler)->subdev;
}

static u64 isp_calc_pixel_rate(struct stf_isp_dev *isp_dev)
{
	u64 rate = 0;

	return rate;
}

static int isp_set_ctrl_hue(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_contrast(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_saturation(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_white_balance(struct stf_isp_dev *isp_dev, int awb)
{
	struct isp_ctrls *ctrls = &isp_dev->ctrls;
	int ret = 0;

	if (!awb && (ctrls->red_balance->is_new
			|| ctrls->blue_balance->is_new)) {
		u16 red = (u16)ctrls->red_balance->val;
		u16 blue = (u16)ctrls->blue_balance->val;

		st_debug(ST_ISP, "red = 0x%x, blue = 0x%x\n", red, blue);
		//isp_dev->hw_ops->isp_set_awb_r_gain(isp_dev, red);
		//if (ret)
		//	return ret;
		//isp_dev->hw_ops->isp_set_awb_b_gain(isp_dev, blue);
	}

	return ret;
}

static int isp_set_ctrl_exposure(struct stf_isp_dev *isp_dev,
				    enum v4l2_exposure_auto_type auto_exposure)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_gain(struct stf_isp_dev *isp_dev, bool auto_gain)
{
	int ret = 0;

	return ret;
}

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Color bars",
	"Color bars w/ rolling bar",
	"Color squares",
	"Color squares w/ rolling bar",
};

#define ISP_TEST_ENABLE			BIT(7)
#define ISP_TEST_ROLLING		BIT(6)	/* rolling horizontal bar */
#define ISP_TEST_TRANSPARENT		BIT(5)
#define ISP_TEST_SQUARE_BW		BIT(4)	/* black & white squares */
#define ISP_TEST_BAR_STANDARD		(0 << 2)
#define ISP_TEST_BAR_VERT_CHANGE_1	(1 << 2)
#define ISP_TEST_BAR_HOR_CHANGE		(2 << 2)
#define ISP_TEST_BAR_VERT_CHANGE_2	(3 << 2)
#define ISP_TEST_BAR			(0 << 0)
#define ISP_TEST_RANDOM			(1 << 0)
#define ISP_TEST_SQUARE			(2 << 0)
#define ISP_TEST_BLACK			(3 << 0)

static const u8 test_pattern_val[] = {
	0,
	ISP_TEST_ENABLE | ISP_TEST_BAR_VERT_CHANGE_1 |
		ISP_TEST_BAR,
	ISP_TEST_ENABLE | ISP_TEST_ROLLING |
		ISP_TEST_BAR_VERT_CHANGE_1 | ISP_TEST_BAR,
	ISP_TEST_ENABLE | ISP_TEST_SQUARE,
	ISP_TEST_ENABLE | ISP_TEST_ROLLING | ISP_TEST_SQUARE,
};

static int isp_set_ctrl_test_pattern(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	// return isp_write_reg(isp_dev, ISP_REG_PRE_ISP_TEST_SET1,
	//			test_pattern_val[value]);
	return ret;
}

static int isp_set_ctrl_light_freq(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_hflip(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_set_ctrl_vflip(struct stf_isp_dev *isp_dev, int value)
{
	int ret = 0;

	return ret;
}

static int isp_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		break;
	}

	return 0;
}

static int isp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int ret;

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	if (!atomic_read(&isp_dev->ref_count))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		ret = isp_set_ctrl_gain(isp_dev, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = isp_set_ctrl_exposure(isp_dev, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = isp_set_ctrl_white_balance(isp_dev, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = isp_set_ctrl_hue(isp_dev, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = isp_set_ctrl_contrast(isp_dev, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = isp_set_ctrl_saturation(isp_dev, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = isp_set_ctrl_test_pattern(isp_dev, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = isp_set_ctrl_light_freq(isp_dev, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = isp_set_ctrl_hflip(isp_dev, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = isp_set_ctrl_vflip(isp_dev, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops isp_ctrl_ops = {
	.g_volatile_ctrl = isp_g_volatile_ctrl,
	.s_ctrl = isp_s_ctrl,
};

static int isp_init_controls(struct stf_isp_dev *isp_dev)
{
	const struct v4l2_ctrl_ops *ops = &isp_ctrl_ops;
	struct isp_ctrls *ctrls = &isp_dev->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);

	/* Clock related controls */
	ctrls->pixel_rate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
					      0, INT_MAX, 1,
					      isp_calc_pixel_rate(isp_dev));

	/* Auto/manual white balance */
	ctrls->auto_wb = v4l2_ctrl_new_std(hdl, ops,
					   V4L2_CID_AUTO_WHITE_BALANCE,
					   0, 1, 1, 1);
	ctrls->blue_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE,
						0, 4095, 1, 0);
	ctrls->red_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE,
					       0, 4095, 1, 0);
	/* Auto/manual exposure */
	ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
						 V4L2_CID_EXPOSURE_AUTO,
						 V4L2_EXPOSURE_MANUAL, 0,
						 V4L2_EXPOSURE_AUTO);
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					    0, 65535, 1, 0);
	/* Auto/manual gain */
	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTOGAIN,
					     0, 1, 1, 1);
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
					0, 1023, 1, 0);

	ctrls->saturation = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION,
					      0, 255, 1, 64);
	ctrls->hue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HUE,
				       0, 359, 1, 0);
	ctrls->contrast = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST,
					    0, 255, 1, 0);
	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(test_pattern_menu) - 1,
					     0, 0, test_pattern_menu);
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP,
					 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP,
					 0, 1, 1, 0);

	ctrls->light_freq =
		v4l2_ctrl_new_std_menu(hdl, ops,
				       V4L2_CID_POWER_LINE_FREQUENCY,
				       V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
				       V4L2_CID_POWER_LINE_FREQUENCY_50HZ);

	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	ctrls->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ctrls->gain->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_auto_cluster(3, &ctrls->auto_wb, 0, false);
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_gain, 0, true);
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_exp, 1, true);

	isp_dev->subdev.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static int isp_set_power(struct v4l2_subdev *sd, int on)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	if (on)
		atomic_inc(&isp_dev->ref_count);
	else
		atomic_dec(&isp_dev->ref_count);

	return 0;
}

static struct v4l2_mbus_framefmt *
__isp_get_format(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which)
{

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&isp_dev->subdev, state, pad);

	return &isp_dev->fmt[pad];
}

static int isp_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int ret = 0;
	struct v4l2_mbus_framefmt *fmt;

	fmt = __isp_get_format(isp_dev, NULL, STF_ISP_PAD_SINK, V4L2_SUBDEV_FORMAT_ACTIVE);
	mutex_lock(&isp_dev->stream_lock);
	if (enable) {
		if (isp_dev->stream_count == 0) {
			isp_dev->hw_ops->isp_clk_enable(isp_dev);
			isp_dev->hw_ops->isp_reset(isp_dev);
			isp_dev->hw_ops->isp_set_format(isp_dev,
					&isp_dev->crop, fmt->code);
				// format->width, format->height);
			isp_dev->hw_ops->isp_config_set(isp_dev);
			isp_dev->hw_ops->isp_stream_set(isp_dev, enable);
		}
		isp_dev->stream_count++;
	} else {
		if (isp_dev->stream_count == 0)
			goto exit;
		if (isp_dev->stream_count == 1) {
			isp_dev->hw_ops->isp_stream_set(isp_dev, enable);
			isp_dev->hw_ops->isp_clk_disable(isp_dev);
		}
		isp_dev->stream_count--;
	}
exit:
	mutex_unlock(&isp_dev->stream_lock);

	if (enable && atomic_read(&isp_dev->ref_count) == 1) {
		/* restore controls */
		ret = v4l2_ctrl_handler_setup(&isp_dev->ctrls.handler);
	}

	return ret;
}

static void isp_try_format(struct stf_isp_dev *isp_dev,
			struct v4l2_subdev_state *state,
			unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case STF_ISP_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < isp_dev->nformats; i++)
			if (fmt->code == isp_dev->formats[i].code)
				break;

		if (i >= isp_dev->nformats)
			fmt->code = MEDIA_BUS_FMT_RGB565_2X8_LE;

		fmt->width = clamp_t(u32,
				fmt->width, 8, STFCAMSS_FRAME_MAX_WIDTH);
		fmt->width &= ~0x7;
		fmt->height = clamp_t(u32,
				fmt->height, 1, STFCAMSS_FRAME_MAX_HEIGHT_PIX);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->flags = 0;

		break;

	case STF_ISP_PAD_SRC:

		*fmt = *__isp_get_format(isp_dev, state, STF_ISP_PAD_SINK, which);

		break;
	}
}

static int isp_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	if (code->index >= isp_dev->nformats)
		return -EINVAL;
	if (code->pad == STF_ISP_PAD_SINK) {
		code->code = isp_dev->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __isp_get_format(isp_dev, state, STF_ISP_PAD_SINK,
					code->which);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;

	return 0;
}

static int isp_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	isp_try_format(isp_dev, state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	isp_try_format(isp_dev, state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int isp_get_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __isp_get_format(isp_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel);

static int isp_set_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __isp_get_format(isp_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	isp_try_format(isp_dev, state, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == STF_ISP_PAD_SINK) {
		struct v4l2_subdev_selection sel = { 0 };
		int ret;

		format = __isp_get_format(isp_dev, state, STF_ISP_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		isp_try_format(isp_dev, state, STF_ISP_PAD_SRC, format,
					fmt->which);

		/* Reset sink pad compose selection */
		sel.which = fmt->which;
		sel.pad = STF_ISP_PAD_SINK;
		sel.target = V4L2_SEL_TGT_COMPOSE;
		sel.r.width = fmt->format.width;
		sel.r.height = fmt->format.height;
		ret = isp_set_selection(sd, state, &sel);
		if (ret < 0)
			return ret;

	}

	return 0;
}

static struct v4l2_rect *
__isp_get_compose(struct stf_isp_dev *isp_dev,
		  struct v4l2_subdev_state *state,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_compose(&isp_dev->subdev, state,
						   STF_ISP_PAD_SINK);

	return &isp_dev->compose;
}

static struct v4l2_rect *
__isp_get_crop(struct stf_isp_dev *isp_dev,
	       struct v4l2_subdev_state *state,
	       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&isp_dev->subdev, state,
						STF_ISP_PAD_SRC);

	return &isp_dev->crop;
}

static void isp_try_compose(struct stf_isp_dev *isp_dev,
			    struct v4l2_subdev_state *state,
			    struct v4l2_rect *rect,
			    enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = __isp_get_format(isp_dev, state, STF_ISP_PAD_SINK, which);

	if (rect->width > fmt->width)
		rect->width = fmt->width;

	if (rect->height > fmt->height)
		rect->height = fmt->height;

	if (fmt->width > rect->width * SCALER_RATIO_MAX)
		rect->width = (fmt->width + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	rect->width &= ~0x7;

	if (fmt->height > rect->height * SCALER_RATIO_MAX)
		rect->height = (fmt->height + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	if (rect->width < 16)
		rect->width = 16;

	if (rect->height < 4)
		rect->height = 4;
}

static void isp_try_crop(struct stf_isp_dev *isp_dev,
			 struct v4l2_subdev_state *state,
			 struct v4l2_rect *rect,
			 enum v4l2_subdev_format_whence which)
{
	struct v4l2_rect *compose;

	compose = __isp_get_compose(isp_dev, state, which);

	if (rect->width > compose->width)
		rect->width = compose->width;

	if (rect->width + rect->left > compose->width)
		rect->left = compose->width - rect->width;

	if (rect->height > compose->height)
		rect->height = compose->height;

	if (rect->height + rect->top > compose->height)
		rect->top = compose->height - rect->height;

	// /* isp in line based mode writes multiple of 16 horizontally */
	rect->left &= ~0x1;
	rect->top &= ~0x1;
	rect->width &= ~0x7;

	if (rect->width < 16) {
		rect->left = 0;
		rect->width = 16;
	}

	if (rect->height < 4) {
		rect->top = 0;
		rect->height = 4;
	}
}

static int isp_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_format fmt = { 0 };
	struct v4l2_rect *rect;
	int ret;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		fmt.pad = sel->pad;
		fmt.which = sel->which;
		ret = isp_get_format(sd, state, &fmt);
		if (ret < 0)
			return ret;

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = fmt.format.width;
		sel->r.height = fmt.format.height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		rect = __isp_get_compose(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		sel->r = *rect;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		rect = __isp_get_compose(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		sel->r.left = rect->left;
		sel->r.top = rect->top;
		sel->r.width = rect->width;
		sel->r.height = rect->height;
		break;
	case V4L2_SEL_TGT_CROP:
		rect = __isp_get_crop(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		sel->r = *rect;
		break;
	default:
		return -EINVAL;
	}

	st_info(ST_ISP, "get left = %d, %d, %d, %d\n",
			sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	return 0;
}

static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_rect *rect;
	int ret;

	st_info(ST_ISP, "left = %d, %d, %d, %d\n",
			sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	if (sel->target == V4L2_SEL_TGT_COMPOSE) {
		struct v4l2_subdev_selection crop = { 0 };

		rect = __isp_get_compose(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		isp_try_compose(isp_dev, state, &sel->r, sel->which);
		*rect = sel->r;

		/* Reset source crop selection */
		crop.which = sel->which;
		crop.pad = STF_ISP_PAD_SRC;
		crop.target = V4L2_SEL_TGT_CROP;
		crop.r = *rect;
		ret = isp_set_selection(sd, state, &crop);
	} else if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_subdev_format fmt = { 0 };

		rect = __isp_get_crop(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		isp_try_crop(isp_dev, state, &sel->r, sel->which);

		*rect = sel->r;

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = STF_ISP_PAD_SRC;
		ret = isp_get_format(sd, state, &fmt);
		if (ret < 0)
			return ret;

		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		ret = isp_set_format(sd, state, &fmt);
	} else {
		ret = -EINVAL;
	}

	st_info(ST_ISP, "out left = %d, %d, %d, %d\n",
			sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	return ret;
}

static int isp_init_formats(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = STF_ISP_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
				V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
			.width = 1920,
			.height = 1080
		}
	};

	return isp_set_format(sd, fh ? fh->state : NULL, &format);
}

static int isp_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_entity_remote_pad(local))
			return -EBUSY;
	return 0;
}

static int stf_isp_load_setfile(struct stf_isp_dev *isp_dev, char *file_name)
{
	struct device *dev = isp_dev->stfcamss->dev;
	const struct firmware *fw;
	u8 *buf = NULL;
	int *regval_num;
	int ret;

	st_debug(ST_ISP, "%s, file_name %s\n", __func__, file_name);
	ret = request_firmware(&fw, file_name, dev);
	if (ret < 0) {
		st_err(ST_ISP, "firmware request failed (%d)\n", ret);
		return ret;
	}
	buf = devm_kzalloc(dev, fw->size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, fw->data, fw->size);

	mutex_lock(&isp_dev->setfile_lock);
	if (isp_dev->setfile.state == 1)
		devm_kfree(dev, isp_dev->setfile.data);
	isp_dev->setfile.data = buf;
	isp_dev->setfile.size = fw->size;
	isp_dev->setfile.state = 1;
	regval_num = (int *)&buf[fw->size - sizeof(unsigned int)];
	isp_dev->setfile.settings.regval_num = *regval_num;
	isp_dev->setfile.settings.regval = (struct regval_t *)buf;
	mutex_unlock(&isp_dev->setfile_lock);

	st_debug(ST_ISP, "stf_isp setfile loaded size: %zu B, reg_nul: %d\n",
			fw->size, isp_dev->setfile.settings.regval_num);

	release_firmware(fw);
	return ret;
}

static long stf_isp_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int ret = -ENOIOCTLCMD;

	switch (cmd) {
	case VIDIOC_STFISP_LOAD_FW: {
		struct stfisp_fw_info *fw_info = arg;

		if (IS_ERR(fw_info)) {
			st_err(ST_ISP, "fw_info failed, params invaild\n");
			return -EINVAL;
		}

		ret = stf_isp_load_setfile(isp_dev, fw_info->filename);
		break;
	}
	default:
		break;
	}
	return ret;
}

static const struct v4l2_subdev_core_ops isp_core_ops = {
	.s_power = isp_set_power,
	.ioctl = stf_isp_ioctl,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops isp_video_ops = {
	.s_stream = isp_set_stream,
};

static const struct v4l2_subdev_pad_ops isp_pad_ops = {
	.enum_mbus_code = isp_enum_mbus_code,
	.enum_frame_size = isp_enum_frame_size,
	.get_fmt = isp_get_format,
	.set_fmt = isp_set_format,
	.get_selection = isp_get_selection,
	.set_selection = isp_set_selection,
};

static const struct v4l2_subdev_ops isp_v4l2_ops = {
	.core = &isp_core_ops,
	.video = &isp_video_ops,
	.pad = &isp_pad_ops,
};

static const struct v4l2_subdev_internal_ops isp_v4l2_internal_ops = {
	.open = isp_init_formats,
};

static const struct media_entity_operations isp_media_ops = {
	.link_setup = isp_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int stf_isp_register(struct stf_isp_dev *isp_dev,
		struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &isp_dev->subdev;
	struct media_pad *pads = isp_dev->pads;
	int ret;

	v4l2_subdev_init(sd, &isp_v4l2_ops);
	sd->internal_ops = &isp_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		STF_ISP_NAME, isp_dev->id);
	v4l2_set_subdevdata(sd, isp_dev);

	ret = isp_init_formats(sd, NULL);
	if (ret < 0) {
		st_err(ST_ISP, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[STF_ISP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_ISP_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &isp_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_ISP_PADS_NUM, pads);
	if (ret < 0) {
		st_err(ST_ISP, "Failed to init media entity: %d\n", ret);
		return ret;
	}

	ret = isp_init_controls(isp_dev);
	if (ret)
		goto err_sreg;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		st_err(ST_ISP, "Failed to register subdev: %d\n", ret);
		goto free_ctrls;
	}

	if (isp_dev->id == 0)
		stf_isp_load_setfile(isp_dev, STF_ISP0_SETFILE);
	else
		stf_isp_load_setfile(isp_dev, STF_ISP1_SETFILE);

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&isp_dev->ctrls.handler);
err_sreg:
	media_entity_cleanup(&sd->entity);
	return ret;
}

int stf_isp_unregister(struct stf_isp_dev *isp_dev)
{
	v4l2_device_unregister_subdev(&isp_dev->subdev);
	media_entity_cleanup(&isp_dev->subdev.entity);
	v4l2_ctrl_handler_free(&isp_dev->ctrls.handler);
	mutex_destroy(&isp_dev->stream_lock);
	mutex_destroy(&isp_dev->setfile_lock);
	return 0;
}

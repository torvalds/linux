// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include "stfcamss.h"
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <linux/firmware.h>
#include <linux/jh7110-isp.h>
#include "stf_isp_ioctl.h"
#include "stf_dmabuf.h"

static int user_config_isp;
static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel);

static struct v4l2_rect *
__isp_get_compose(struct stf_isp_dev *isp_dev,
		  struct v4l2_subdev_state *state,
		  enum v4l2_subdev_format_whence which);

static struct v4l2_rect *
__isp_get_crop(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		enum v4l2_subdev_format_whence which);

static struct v4l2_rect *
__isp_get_scale(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		struct v4l2_subdev_selection *sel);

static struct v4l2_rect *
__isp_get_itiws(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		enum v4l2_subdev_format_whence which);

// sink format and raw format must one by one
static const struct isp_format isp_formats_st7110_sink[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
};

static const struct isp_format isp_formats_st7110_raw[] = {
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12},
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12},
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12},
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12},
};

static const struct isp_format isp_formats_st7110_compat_10bit_raw[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
};

static const struct isp_format isp_formats_st7110_compat_8bit_raw[] = {
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8},
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8},
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8},
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8},
};

static const struct isp_format isp_formats_st7110_uo[] = {
	{ MEDIA_BUS_FMT_Y12_1X12, 8},
};

static const struct isp_format isp_formats_st7110_iti[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12},
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12},
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12},
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12},
	{ MEDIA_BUS_FMT_Y12_1X12, 8},
	{ MEDIA_BUS_FMT_YUV8_1X24, 8},
};

static const struct isp_format_table isp_formats_st7110[] = {
	{ isp_formats_st7110_sink, ARRAY_SIZE(isp_formats_st7110_sink) }, /* pad 0 */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },     /* pad 1 */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },     /* pad 2 */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },     /* pad 3 */
	{ isp_formats_st7110_iti, ARRAY_SIZE(isp_formats_st7110_iti) },   /* pad 4 */
	{ isp_formats_st7110_iti, ARRAY_SIZE(isp_formats_st7110_iti) },   /* pad 5 */
	{ isp_formats_st7110_raw, ARRAY_SIZE(isp_formats_st7110_raw) },   /* pad 6 */
	{ isp_formats_st7110_raw, ARRAY_SIZE(isp_formats_st7110_raw) },   /* pad 7 */
};

int stf_isp_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_isp_dev *isp_dev = stfcamss->isp_dev;

	isp_dev->sdev_type = ISP_DEV_TYPE;
	isp_dev->hw_ops = &isp_ops;
	isp_dev->stfcamss = stfcamss;
	isp_dev->formats = isp_formats_st7110;
	isp_dev->nformats = ARRAY_SIZE(isp_formats_st7110);
	mutex_init(&isp_dev->stream_lock);
	mutex_init(&isp_dev->power_lock);
	mutex_init(&isp_dev->setfile_lock);
	atomic_set(&isp_dev->shadow_count, 0);
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
	int ret = 0;

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	mutex_lock(&isp_dev->power_lock);
	if (isp_dev->power_count == 0) {
		mutex_unlock(&isp_dev->power_lock);
		return 0;
	}
	mutex_unlock(&isp_dev->power_lock);

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
	case V4L2_CID_USER_JH7110_ISP_WB_SETTING:
		break;
	case V4L2_CID_USER_JH7110_ISP_CAR_SETTING:
		break;
	case V4L2_CID_USER_JH7110_ISP_CCM_SETTING:
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

struct v4l2_ctrl_config isp_ctrl[] = {
	[0] = {
		.ops		= &isp_ctrl_ops,
		.type		= V4L2_CTRL_TYPE_U8,
		.def		= 0,
		.min		= 0x00,
		.max		= 0xff,
		.step		= 1,
		.name		= "WB Setting",
		.id		= V4L2_CID_USER_JH7110_ISP_WB_SETTING,
		.dims[0]	= sizeof(struct jh7110_isp_wb_setting),
		.flags		= 0,
	},
	[1] = {
		.ops		= &isp_ctrl_ops,
		.type		= V4L2_CTRL_TYPE_U8,
		.def		= 0,
		.min		= 0x00,
		.max		= 0xff,
		.step		= 1,
		.name		= "Car Setting",
		.id		= V4L2_CID_USER_JH7110_ISP_CAR_SETTING,
		.dims[0]	= sizeof(struct jh7110_isp_car_setting),
		.flags		= 0,
	},
	[2] = {
		.ops		= &isp_ctrl_ops,
		.type		= V4L2_CTRL_TYPE_U8,
		.def		= 0,
		.min		= 0x00,
		.max		= 0xff,
		.step		= 1,
		.name		= "CCM Setting",
		.id		= V4L2_CID_USER_JH7110_ISP_CCM_SETTING,
		.dims[0]	= sizeof(struct jh7110_isp_ccm_setting),
		.flags		= 0,
	},
};

static int isp_init_controls(struct stf_isp_dev *isp_dev)
{
	const struct v4l2_ctrl_ops *ops = &isp_ctrl_ops;
	struct isp_ctrls *ctrls = &isp_dev->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;
	int i;

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

	for (i = 0; i < ARRAY_SIZE(isp_ctrl); i++)
		v4l2_ctrl_new_custom(hdl, &isp_ctrl[i], NULL);


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

	st_debug(ST_ISP, "%s, %d\n", __func__, __LINE__);
	mutex_lock(&isp_dev->power_lock);
	if (on) {
		if (isp_dev->power_count == 0)
			st_debug(ST_ISP, "turn on isp\n");
		isp_dev->power_count++;
	} else {
		if (isp_dev->power_count == 0)
			goto exit;
		isp_dev->power_count--;
	}
exit:
	mutex_unlock(&isp_dev->power_lock);

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

static int isp_get_interface_type(struct media_entity *entity)
{
	struct v4l2_subdev *subdev;
	struct media_pad *pad = &entity->pads[0];

	if (!(pad->flags & MEDIA_PAD_FL_SINK))
		return -EINVAL;

	pad = media_entity_remote_pad(pad);
	if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
		return -EINVAL;

	subdev = media_entity_to_v4l2_subdev(pad->entity);

	st_debug(ST_ISP, "interface subdev name %s\n", subdev->name);
	if (!strncmp(subdev->name, STF_CSI_NAME, strlen(STF_CSI_NAME)))
		return CSI_SENSOR;
	if (!strncmp(subdev->name, STF_DVP_NAME, strlen(STF_DVP_NAME)))
		return DVP_SENSOR;
	return -EINVAL;
}

static int isp_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int ret = 0, interface_type;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_event src_ch = { 0 };

	fmt = __isp_get_format(isp_dev, NULL, STF_ISP_PAD_SINK, V4L2_SUBDEV_FORMAT_ACTIVE);
	mutex_lock(&isp_dev->stream_lock);
	if (enable) {
		if (isp_dev->stream_count == 0) {
			isp_dev->hw_ops->isp_clk_enable(isp_dev);
			if (!user_config_isp)
				isp_dev->hw_ops->isp_config_set(isp_dev);
			interface_type = isp_get_interface_type(&sd->entity);
			if (interface_type < 0) {
				st_err(ST_ISP, "%s, pipeline not config\n", __func__);
				goto exit;
			}
			isp_dev->hw_ops->isp_set_format(isp_dev,
					isp_dev->rect, fmt->code, interface_type);
			isp_dev->hw_ops->isp_reset(isp_dev);
			isp_dev->hw_ops->isp_stream_set(isp_dev, enable);
			user_config_isp = 0;
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
	src_ch.type = V4L2_EVENT_SOURCE_CHANGE,
	src_ch.u.src_change.changes = isp_dev->stream_count,

	v4l2_subdev_notify_event(sd, &src_ch);
exit:
	mutex_unlock(&isp_dev->stream_lock);

	mutex_lock(&isp_dev->power_lock);
	/* restore controls */
	if (enable && isp_dev->power_count == 1) {
		mutex_unlock(&isp_dev->power_lock);
		ret = v4l2_ctrl_handler_setup(&isp_dev->ctrls.handler);
	} else
		mutex_unlock(&isp_dev->power_lock);

	return ret;
}

/*Try to match sensor format with sink, and then get the index as default.*/
static int isp_match_sensor_format_get_index(struct stf_isp_dev *isp_dev)
{
	int ret, idx;
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format fmt;
	const struct isp_format_table *formats;

	if (!isp_dev)
		return -EINVAL;

	sensor = stfcamss_find_sensor(&isp_dev->subdev.entity);
	if (!sensor)
		return -EINVAL;

	subdev = media_entity_to_v4l2_subdev(sensor);
	st_debug(ST_ISP, "Found sensor = %s\n", sensor->name);

	fmt.pad = 0;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret) {
		st_warn(ST_ISP, "Sonser get format failed !!\n");
		return -EINVAL;
	}

	st_debug(ST_ISP, "Got sensor format 0x%x !!\n", fmt.format.code);

	formats = &isp_dev->formats[0];		/* isp sink format */
	for (idx = 0; idx < formats->nfmts; idx++) {
		if (formats->fmts[idx].code == fmt.format.code) {
			st_info(ST_ISP,
				"Match sensor format to isp_formats_st7110_sink index %d !!\n",
				idx);
			return idx;
		}
	}
	return -ERANGE;
}

static int isp_match_format_get_index(const struct isp_format_table *f_table,
			__u32 mbus_code,
			unsigned int pad)
{
	int i;

	for (i = 0; i < f_table->nfmts; i++) {
		if (mbus_code == f_table->fmts[i].code) {
			break;
		} else {
			if (pad == STF_ISP_PAD_SRC_RAW || pad == STF_ISP_PAD_SRC_SCD_Y) {
				if (mbus_code == (isp_formats_st7110_compat_10bit_raw[i].code ||
					isp_formats_st7110_compat_8bit_raw[i].code))
					break;
			}
		}
	}

	return i;
}

static void isp_try_format(struct stf_isp_dev *isp_dev,
			struct v4l2_subdev_state *state,
			unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	const struct isp_format_table *formats;
	unsigned int i;
	u32 code = fmt->code;
	u32 bpp;

	if (pad == STF_ISP_PAD_SINK) {
		/* Set format on sink pad */

		formats = &isp_dev->formats[pad];
		fmt->width = clamp_t(u32,
				fmt->width, STFCAMSS_FRAME_MIN_WIDTH,
				STFCAMSS_FRAME_MAX_WIDTH);
		fmt->height = clamp_t(u32,
				fmt->height, STFCAMSS_FRAME_MIN_HEIGHT,
				STFCAMSS_FRAME_MAX_HEIGHT);
		fmt->height &= ~0x1;

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->flags = 0;
	} else {
		formats = &isp_dev->formats[pad];
	}

	i = isp_match_format_get_index(formats, fmt->code, pad);
	st_debug(ST_ISP, "%s pad=%d, code=%x isp_match_format_get_index = %d\n",
					__func__, pad, code, i);

	if (i >= formats->nfmts &&
		(pad == STF_ISP_PAD_SRC_RAW || pad == STF_ISP_PAD_SRC_SCD_Y)) {
		int sensor_idx;

		sensor_idx = isp_match_sensor_format_get_index(isp_dev);
		if (sensor_idx)
			i = sensor_idx;
	}

	if (pad != STF_ISP_PAD_SINK)
		*fmt = *__isp_get_format(isp_dev, state, STF_ISP_PAD_SINK, which);

	if (i >= formats->nfmts) {
		fmt->code = formats->fmts[0].code;
		bpp = formats->fmts[0].bpp;
		st_info(ST_ISP, "Use default index 0 format = 0x%x\n", fmt->code);
	} else {
		// sink format and raw format must one by one
		if (pad == STF_ISP_PAD_SRC_RAW || pad == STF_ISP_PAD_SRC_SCD_Y) {
			fmt->code = formats->fmts[i].code;
			bpp = formats->fmts[i].bpp;
			st_info(ST_ISP, "Use mapping format from sink index %d = 0x%x\n",
					i, fmt->code);
		} else {
			fmt->code = code;
			bpp = formats->fmts[i].bpp;
			st_info(ST_ISP, "Use input format = 0x%x\n", fmt->code);
		}
	}

	switch (pad) {
	case STF_ISP_PAD_SINK:
		break;
	case STF_ISP_PAD_SRC:
		isp_dev->rect[ISP_COMPOSE].bpp = bpp;
		break;
	case STF_ISP_PAD_SRC_SS0:
		isp_dev->rect[ISP_SCALE_SS0].bpp = bpp;
		break;
	case STF_ISP_PAD_SRC_SS1:
		isp_dev->rect[ISP_SCALE_SS1].bpp = bpp;
		break;
	case STF_ISP_PAD_SRC_ITIW:
	case STF_ISP_PAD_SRC_ITIR:
		isp_dev->rect[ISP_ITIWS].bpp = bpp;
		break;
	case STF_ISP_PAD_SRC_RAW:
		isp_dev->rect[ISP_CROP].bpp = bpp;
		break;
	case STF_ISP_PAD_SRC_SCD_Y:
		break;
	}
}

static int isp_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	const struct isp_format_table *formats;

	if (code->index >= isp_dev->nformats)
		return -EINVAL;
	if (code->pad == STF_ISP_PAD_SINK) {
		formats = &isp_dev->formats[code->pad];
		code->code = formats->fmts[code->index].code;
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

static int isp_set_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_selection sel = { 0 };
	struct v4l2_rect *rect = NULL;
	int ret;

	st_debug(ST_ISP, "%s pad=%d, code=%x, which=%d\n",
			__func__, fmt->reserved[0], fmt->format.code, fmt->which);
	format = __isp_get_format(isp_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	mutex_lock(&isp_dev->stream_lock);
	if (isp_dev->stream_count) {
		fmt->format = *format;
		if (fmt->reserved[0] != 0) {
			sel.which = fmt->which;
			sel.pad = fmt->reserved[0];

			switch (fmt->reserved[0]) {
			case STF_ISP_PAD_SRC:
				rect = __isp_get_compose(isp_dev, state, fmt->which);
				break;
			case STF_ISP_PAD_SRC_SS0:
			case STF_ISP_PAD_SRC_SS1:
				rect = __isp_get_scale(isp_dev, state, &sel);
				break;
			case STF_ISP_PAD_SRC_ITIW:
			case STF_ISP_PAD_SRC_ITIR:
				rect = __isp_get_itiws(isp_dev, state, fmt->which);
				break;
			case STF_ISP_PAD_SRC_RAW:
			case STF_ISP_PAD_SRC_SCD_Y:
				rect = __isp_get_crop(isp_dev, state, fmt->which);
				break;
			default:
				break;
			}
			if (rect != NULL) {
				fmt->format.width = rect->width;
				fmt->format.height = rect->height;
			}
		}
		mutex_unlock(&isp_dev->stream_lock);
		goto out;
	} else {
		isp_try_format(isp_dev, state, fmt->pad, &fmt->format, fmt->which);
		*format = fmt->format;
	}
	mutex_unlock(&isp_dev->stream_lock);

	/* Propagate the format from sink to source */
	if (fmt->pad == STF_ISP_PAD_SINK) {
		/* Reset sink pad compose selection */
		sel.which = fmt->which;
		sel.pad = STF_ISP_PAD_SINK;
		sel.target = V4L2_SEL_TGT_CROP;
		sel.r.width = fmt->format.width;
		sel.r.height = fmt->format.height;
		ret = isp_set_selection(sd, state, &sel);
		if (ret < 0)
			return ret;
	}

out:
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


	return &isp_dev->rect[ISP_COMPOSE].rect;
}

static struct v4l2_rect *
__isp_get_crop(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&isp_dev->subdev, state,
						STF_ISP_PAD_SINK);

	return &isp_dev->rect[ISP_CROP].rect;
}

static struct v4l2_rect *
__isp_get_scale(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		struct v4l2_subdev_selection *sel)
{
	int pad;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_compose(&isp_dev->subdev, state,
						STF_ISP_PAD_SINK);
	if (sel->pad != STF_ISP_PAD_SRC_SS0 && sel->pad != STF_ISP_PAD_SRC_SS1)
		return NULL;

	pad = sel->pad == STF_ISP_PAD_SRC_SS0 ? ISP_SCALE_SS0 : ISP_SCALE_SS1;
	return &isp_dev->rect[pad].rect;
}

static struct v4l2_rect *
__isp_get_itiws(struct stf_isp_dev *isp_dev,
		struct v4l2_subdev_state *state,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&isp_dev->subdev, state, STF_ISP_PAD_SINK);

	return &isp_dev->rect[ISP_ITIWS].rect;
}

static void isp_try_crop(struct stf_isp_dev *isp_dev,
			    struct v4l2_subdev_state *state,
			    struct v4l2_rect *rect,
			    enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = __isp_get_format(isp_dev, state, STF_ISP_PAD_SINK, which);

	if (rect->width > fmt->width)
		rect->width = fmt->width;

	if (rect->width + rect->left > fmt->width)
		rect->left = fmt->width - rect->width;

	if (rect->height > fmt->height)
		rect->height = fmt->height;

	if (rect->height + rect->top > fmt->height)
		rect->top = fmt->height - rect->height;

	if (rect->width < STFCAMSS_FRAME_MIN_WIDTH) {
		rect->left = 0;
		rect->width = STFCAMSS_FRAME_MIN_WIDTH;
	}

	if (rect->height < STFCAMSS_FRAME_MIN_HEIGHT) {
		rect->top = 0;
		rect->height = STFCAMSS_FRAME_MIN_HEIGHT;
	}
	rect->height &= ~0x1;
}

static void isp_try_compose(struct stf_isp_dev *isp_dev,
			 struct v4l2_subdev_state *state,
			 struct v4l2_rect *rect,
			 enum v4l2_subdev_format_whence which)
{
	struct v4l2_rect *crop;

	crop = __isp_get_crop(isp_dev, state, which);

	if (rect->width > crop->width)
		rect->width = crop->width;

	if (rect->height > crop->height)
		rect->height = crop->height;

	if (crop->width > rect->width * SCALER_RATIO_MAX)
		rect->width = (crop->width + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	if (crop->height > rect->height * SCALER_RATIO_MAX)
		rect->height = (crop->height + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	if (rect->width < STFCAMSS_FRAME_MIN_WIDTH)
		rect->width = STFCAMSS_FRAME_MIN_WIDTH;

	if (rect->height < STFCAMSS_FRAME_MIN_HEIGHT)
		rect->height = STFCAMSS_FRAME_MIN_HEIGHT;
	rect->height &= ~0x1;
}

static void isp_try_scale(struct stf_isp_dev *isp_dev,
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

	if (rect->width < STFCAMSS_FRAME_MIN_WIDTH) {
		rect->left = 0;
		rect->width = STFCAMSS_FRAME_MIN_WIDTH;
	}

	if (rect->height < STFCAMSS_FRAME_MIN_HEIGHT) {
		rect->top = 0;
		rect->height = STFCAMSS_FRAME_MIN_HEIGHT;
	}
	rect->height &= ~0x1;
}

static void isp_try_itiws(struct stf_isp_dev *isp_dev,
			    struct v4l2_subdev_state *state,
			    struct v4l2_rect *rect,
			    enum v4l2_subdev_format_whence which)
{
	struct v4l2_rect *crop;

	crop = __isp_get_crop(isp_dev, state, which);

	if (rect->width > crop->width)
		rect->width = crop->width;

	if (rect->width + rect->left > crop->width)
		rect->left = crop->width - rect->width;

	if (rect->height > crop->height)
		rect->height = crop->height;

	if (rect->height + rect->top > crop->height)
		rect->top = crop->height - rect->height;

	if (rect->width < STFCAMSS_FRAME_MIN_WIDTH) {
		rect->left = 0;
		rect->width = STFCAMSS_FRAME_MIN_WIDTH;
	}

	if (rect->height < STFCAMSS_FRAME_MIN_HEIGHT) {
		rect->top = 0;
		rect->height = STFCAMSS_FRAME_MIN_HEIGHT;
	}
	rect->height &= ~0x1;
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
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
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
	case V4L2_SEL_TGT_CROP:
		rect = __isp_get_crop(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		sel->r = *rect;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (sel->pad > STF_ISP_PAD_SRC_ITIR)
			return -EINVAL;
		rect = __isp_get_crop(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		sel->r.left = rect->left;
		sel->r.top = rect->top;
		sel->r.width = rect->width;
		sel->r.height = rect->height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (sel->pad > STF_ISP_PAD_SRC_ITIR)
			return -EINVAL;
		if (sel->pad == STF_ISP_PAD_SRC_SS0
			|| sel->pad == STF_ISP_PAD_SRC_SS1) {
			rect = __isp_get_scale(isp_dev, state, sel);
			if (rect == NULL)
				return -EINVAL;
		} else if (sel->pad == STF_ISP_PAD_SRC_ITIW
			|| sel->pad == STF_ISP_PAD_SRC_ITIR) {
			rect = __isp_get_itiws(isp_dev, state, sel->which);
			if (rect == NULL)
				return -EINVAL;
		} else {
			rect = __isp_get_compose(isp_dev, state, sel->which);
			if (rect == NULL)
				return -EINVAL;
		}
		sel->r = *rect;
		break;
	default:
		return -EINVAL;
	}

	st_info(ST_ISP, "%s pad = %d, left = %d, %d, %d, %d\n",
			__func__, sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	return 0;
}

static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_rect *rect;
	int ret = 0;

	if (sel->target == V4L2_SEL_TGT_COMPOSE &&
			((sel->pad == STF_ISP_PAD_SINK)
			 || (sel->pad == STF_ISP_PAD_SRC))) {
		struct v4l2_subdev_format fmt = { 0 };
		int i;

		rect = __isp_get_compose(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count) {
			sel->r = *rect;
			mutex_unlock(&isp_dev->stream_lock);
			ret = 0;
			goto out;
		} else {
			isp_try_compose(isp_dev, state, &sel->r, sel->which);
			*rect = sel->r;
		}
		mutex_unlock(&isp_dev->stream_lock);

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = STF_ISP_PAD_SRC;
		ret = isp_get_format(sd, state, &fmt);
		if (ret < 0)
			return ret;

		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		ret = isp_set_format(sd, state, &fmt);

		/* Reset scale */
		for (i = STF_ISP_PAD_SRC_SS0; i <= STF_ISP_PAD_SRC_ITIR; i++) {
			struct v4l2_subdev_selection scale = { 0 };

			scale.which = sel->which;
			scale.target = V4L2_SEL_TGT_COMPOSE;
			scale.r = *rect;
			scale.pad = i;
			ret = isp_set_selection(sd, state, &scale);
		}
	} else if (sel->target == V4L2_SEL_TGT_COMPOSE
			&& ((sel->pad == STF_ISP_PAD_SRC_SS0)
				|| (sel->pad == STF_ISP_PAD_SRC_SS1))) {
		struct v4l2_subdev_format fmt = { 0 };

		rect = __isp_get_scale(isp_dev, state, sel);
		if (rect == NULL)
			return -EINVAL;

		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count) {
			sel->r = *rect;
			mutex_unlock(&isp_dev->stream_lock);
			ret = 0;
			goto out;
		} else {
			isp_try_scale(isp_dev, state, &sel->r, sel->which);
			*rect = sel->r;
		}
		mutex_unlock(&isp_dev->stream_lock);

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = sel->pad;
		ret = isp_get_format(sd, state, &fmt);
		if (ret < 0)
			return ret;

		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		ret = isp_set_format(sd, state, &fmt);
	} else if (sel->target == V4L2_SEL_TGT_COMPOSE
			&& ((sel->pad == STF_ISP_PAD_SRC_ITIW)
				|| (sel->pad == STF_ISP_PAD_SRC_ITIR))) {
		struct v4l2_subdev_format fmt = { 0 };

		rect = __isp_get_itiws(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count) {
			sel->r = *rect;
			mutex_unlock(&isp_dev->stream_lock);
			ret = 0;
			goto out;
		} else {
			isp_try_itiws(isp_dev, state, &sel->r, sel->which);
			*rect = sel->r;
		}
		mutex_unlock(&isp_dev->stream_lock);

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = sel->pad;
		ret = isp_get_format(sd, state, &fmt);
		if (ret < 0)
			return ret;

		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		ret = isp_set_format(sd, state, &fmt);
	} else if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_subdev_selection compose = { 0 };
		int i;

		rect = __isp_get_crop(isp_dev, state, sel->which);
		if (rect == NULL)
			return -EINVAL;

		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count) {
			sel->r = *rect;
			mutex_unlock(&isp_dev->stream_lock);
			ret = 0;
			goto out;
		} else {
			isp_try_crop(isp_dev, state, &sel->r, sel->which);
			*rect = sel->r;
		}
		mutex_unlock(&isp_dev->stream_lock);

		/* Reset source compose selection */
		compose.which = sel->which;
		compose.target = V4L2_SEL_TGT_COMPOSE;
		compose.r.width = rect->width;
		compose.r.height = rect->height;
		compose.pad = STF_ISP_PAD_SINK;
		ret = isp_set_selection(sd, state, &compose);

		/* Reset source pad format width and height */
		for (i = STF_ISP_PAD_SRC_RAW; i < STF_ISP_PAD_MAX; i++) {
			struct v4l2_subdev_format fmt = { 0 };

			fmt.which = sel->which;
			fmt.pad = i;
			ret = isp_get_format(sd, state, &fmt);
			if (ret < 0)
				return ret;

			fmt.format.width = rect->width;
			fmt.format.height = rect->height;
			ret = isp_set_format(sd, state, &fmt);
		}
	} else {
		ret = -EINVAL;
	}

	st_info(ST_ISP, "%s pad = %d, left = %d, %d, %d, %d\n",
			__func__, sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);
out:
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
	struct device *dev = isp_dev->stfcamss->dev;
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
	case VIDIOC_STF_DMABUF_ALLOC:
	case VIDIOC_STF_DMABUF_FREE:
		ret = stf_dmabuf_ioctl(dev, cmd, arg);
		break;
	case VIDIOC_STFISP_GET_REG:
		ret = isp_dev->hw_ops->isp_reg_read(isp_dev, arg);
		break;
	case VIDIOC_STFISP_SET_REG:
		ret = isp_dev->hw_ops->isp_reg_write(isp_dev, arg);
		break;
	case VIDIOC_STFISP_SHADOW_LOCK:
		if (atomic_add_unless(&isp_dev->shadow_count, 1, 1))
			ret = 0;
		else
			ret = -EBUSY;
		st_debug(ST_ISP, "%s, %d, ret = %d\n", __func__, __LINE__, ret);
		break;
	case VIDIOC_STFISP_SHADOW_UNLOCK:
		if (atomic_dec_if_positive(&isp_dev->shadow_count) < 0)
			ret = -EINVAL;
		else
			ret = 0;
		st_debug(ST_ISP, "%s, %d, ret = %d\n", __func__, __LINE__, ret);
		break;
	case VIDIOC_STFISP_SHADOW_UNLOCK_N_TRIGGER:
		{
			isp_dev->hw_ops->isp_shadow_trigger(isp_dev);
			if (atomic_dec_if_positive(&isp_dev->shadow_count) < 0)
				ret = -EINVAL;
			else
				ret = 0;
			st_debug(ST_ISP, "%s, %d, ret = %d\n", __func__, __LINE__, ret);
		}
		break;
	case VIDIOC_STFISP_SET_USER_CONFIG_ISP:
		st_debug(ST_ISP, "%s, %d set user_config_isp\n", __func__, __LINE__);
		user_config_isp = 1;
		break;
	default:
		break;
	}
	return ret;
}

int isp_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	st_debug(ST_ISP, "%s, %d\n", __func__, __LINE__);
	while (atomic_dec_if_positive(&isp_dev->shadow_count) > 0)
		st_warn(ST_ISP, "user not unlocked the shadow lock, driver unlock it!\n");

	return 0;
}

static int stf_isp_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		st_debug(ST_ISP, "unspport subscribe_event\n");
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops isp_core_ops = {
	.s_power = isp_set_power,
	.ioctl = stf_isp_ioctl,
	.log_status = v4l2_ctrl_subdev_log_status,
	// .subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.subscribe_event = stf_isp_subscribe_event,
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
	.close = isp_close,
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
		STF_ISP_NAME, 0);
	v4l2_set_subdevdata(sd, isp_dev);

	ret = isp_init_formats(sd, NULL);
	if (ret < 0) {
		st_err(ST_ISP, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[STF_ISP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_ISP_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_SS0].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_SS1].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_ITIW].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_ITIR].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_RAW].flags = MEDIA_PAD_FL_SOURCE;
	pads[STF_ISP_PAD_SRC_SCD_Y].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &isp_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_ISP_PAD_MAX, pads);
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
	mutex_destroy(&isp_dev->power_lock);
	mutex_destroy(&isp_dev->setfile_lock);
	return 0;
}

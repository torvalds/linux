// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Analog Devices ADV748X 8 channel analog front end (AFE) receiver
 * with standard definition processor (SDP)
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>

#include "adv748x.h"

/* -----------------------------------------------------------------------------
 * SDP
 */

#define ADV748X_AFE_STD_AD_PAL_BG_NTSC_J_SECAM		0x0
#define ADV748X_AFE_STD_AD_PAL_BG_NTSC_J_SECAM_PED	0x1
#define ADV748X_AFE_STD_AD_PAL_N_NTSC_J_SECAM		0x2
#define ADV748X_AFE_STD_AD_PAL_N_NTSC_M_SECAM		0x3
#define ADV748X_AFE_STD_NTSC_J				0x4
#define ADV748X_AFE_STD_NTSC_M				0x5
#define ADV748X_AFE_STD_PAL60				0x6
#define ADV748X_AFE_STD_NTSC_443			0x7
#define ADV748X_AFE_STD_PAL_BG				0x8
#define ADV748X_AFE_STD_PAL_N				0x9
#define ADV748X_AFE_STD_PAL_M				0xa
#define ADV748X_AFE_STD_PAL_M_PED			0xb
#define ADV748X_AFE_STD_PAL_COMB_N			0xc
#define ADV748X_AFE_STD_PAL_COMB_N_PED			0xd
#define ADV748X_AFE_STD_PAL_SECAM			0xe
#define ADV748X_AFE_STD_PAL_SECAM_PED			0xf

static int adv748x_afe_read_ro_map(struct adv748x_state *state, u8 reg)
{
	int ret;

	/* Select SDP Read-Only Main Map */
	ret = sdp_write(state, ADV748X_SDP_MAP_SEL,
			ADV748X_SDP_MAP_SEL_RO_MAIN);
	if (ret < 0)
		return ret;

	return sdp_read(state, reg);
}

static int adv748x_afe_status(struct adv748x_afe *afe, u32 *signal,
			      v4l2_std_id *std)
{
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	int info;

	/* Read status from reg 0x10 of SDP RO Map */
	info = adv748x_afe_read_ro_map(state, ADV748X_SDP_RO_10);
	if (info < 0)
		return info;

	if (signal)
		*signal = info & ADV748X_SDP_RO_10_IN_LOCK ?
				0 : V4L2_IN_ST_NO_SIGNAL;

	if (!std)
		return 0;

	/* Standard not valid if there is no signal */
	if (!(info & ADV748X_SDP_RO_10_IN_LOCK)) {
		*std = V4L2_STD_UNKNOWN;
		return 0;
	}

	switch (info & 0x70) {
	case 0x00:
		*std = V4L2_STD_NTSC;
		break;
	case 0x10:
		*std = V4L2_STD_NTSC_443;
		break;
	case 0x20:
		*std = V4L2_STD_PAL_M;
		break;
	case 0x30:
		*std = V4L2_STD_PAL_60;
		break;
	case 0x40:
		*std = V4L2_STD_PAL;
		break;
	case 0x50:
		*std = V4L2_STD_SECAM;
		break;
	case 0x60:
		*std = V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
		break;
	case 0x70:
		*std = V4L2_STD_SECAM;
		break;
	default:
		*std = V4L2_STD_UNKNOWN;
		break;
	}

	return 0;
}

static void adv748x_afe_fill_format(struct adv748x_afe *afe,
				    struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));

	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->field = V4L2_FIELD_ALTERNATE;

	fmt->width = 720;
	fmt->height = afe->curr_norm & V4L2_STD_525_60 ? 480 : 576;

	/* Field height */
	fmt->height /= 2;
}

static int adv748x_afe_std(v4l2_std_id std)
{
	if (std == V4L2_STD_PAL_60)
		return ADV748X_AFE_STD_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV748X_AFE_STD_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV748X_AFE_STD_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV748X_AFE_STD_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV748X_AFE_STD_PAL_COMB_N;
	if (std & V4L2_STD_NTSC)
		return ADV748X_AFE_STD_NTSC_M;
	if (std & V4L2_STD_PAL)
		return ADV748X_AFE_STD_PAL_BG;
	if (std & V4L2_STD_SECAM)
		return ADV748X_AFE_STD_PAL_SECAM;

	return -EINVAL;
}

static void adv748x_afe_set_video_standard(struct adv748x_state *state,
					  int sdpstd)
{
	sdp_clrset(state, ADV748X_SDP_VID_SEL, ADV748X_SDP_VID_SEL_MASK,
		   (sdpstd & 0xf) << ADV748X_SDP_VID_SEL_SHIFT);
}

static int adv748x_afe_s_input(struct adv748x_afe *afe, unsigned int input)
{
	struct adv748x_state *state = adv748x_afe_to_state(afe);

	return sdp_write(state, ADV748X_SDP_INSEL, input);
}

static int adv748x_afe_g_pixelaspect(struct v4l2_subdev *sd,
				     struct v4l2_fract *aspect)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);

	if (afe->curr_norm & V4L2_STD_525_60) {
		aspect->numerator = 11;
		aspect->denominator = 10;
	} else {
		aspect->numerator = 54;
		aspect->denominator = 59;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int adv748x_afe_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);

	*norm = afe->curr_norm;

	return 0;
}

static int adv748x_afe_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	int afe_std = adv748x_afe_std(std);

	if (afe_std < 0)
		return afe_std;

	mutex_lock(&state->mutex);

	adv748x_afe_set_video_standard(state, afe_std);
	afe->curr_norm = std;

	mutex_unlock(&state->mutex);

	return 0;
}

static int adv748x_afe_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	int afe_std;
	int ret;

	mutex_lock(&state->mutex);

	if (afe->streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Set auto detect mode */
	adv748x_afe_set_video_standard(state,
				       ADV748X_AFE_STD_AD_PAL_BG_NTSC_J_SECAM);

	msleep(100);

	/* Read detected standard */
	ret = adv748x_afe_status(afe, NULL, std);

	afe_std = adv748x_afe_std(afe->curr_norm);
	if (afe_std < 0)
		goto unlock;

	/* Restore original state */
	adv748x_afe_set_video_standard(state, afe_std);

unlock:
	mutex_unlock(&state->mutex);

	return ret;
}

static int adv748x_afe_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_ALL;

	return 0;
}

static int adv748x_afe_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	int ret;

	mutex_lock(&state->mutex);

	ret = adv748x_afe_status(afe, status, NULL);

	mutex_unlock(&state->mutex);

	return ret;
}

static int adv748x_afe_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	u32 signal = V4L2_IN_ST_NO_SIGNAL;
	int ret;

	mutex_lock(&state->mutex);

	if (enable) {
		ret = adv748x_afe_s_input(afe, afe->input);
		if (ret)
			goto unlock;
	}

	ret = adv748x_tx_power(&state->txb, enable);
	if (ret)
		goto unlock;

	afe->streaming = enable;

	adv748x_afe_status(afe, &signal, NULL);
	if (signal != V4L2_IN_ST_NO_SIGNAL)
		adv_dbg(state, "Detected SDP signal\n");
	else
		adv_dbg(state, "Couldn't detect SDP video signal\n");

unlock:
	mutex_unlock(&state->mutex);

	return ret;
}

static const struct v4l2_subdev_video_ops adv748x_afe_video_ops = {
	.g_std = adv748x_afe_g_std,
	.s_std = adv748x_afe_s_std,
	.querystd = adv748x_afe_querystd,
	.g_tvnorms = adv748x_afe_g_tvnorms,
	.g_input_status = adv748x_afe_g_input_status,
	.s_stream = adv748x_afe_s_stream,
	.g_pixelaspect = adv748x_afe_g_pixelaspect,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int adv748x_afe_propagate_pixelrate(struct adv748x_afe *afe)
{
	struct v4l2_subdev *tx;

	tx = adv748x_get_remote_sd(&afe->pads[ADV748X_AFE_SOURCE]);
	if (!tx)
		return -ENOLINK;

	/*
	 * The ADV748x ADC sampling frequency is twice the externally supplied
	 * clock whose frequency is required to be 28.63636 MHz. It oversamples
	 * with a factor of 4 resulting in a pixel rate of 14.3180180 MHz.
	 */
	return adv748x_csi2_set_pixelrate(tx, 14318180);
}

static int adv748x_afe_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int adv748x_afe_get_format(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_format *sdformat)
{
	struct adv748x_afe *afe = adv748x_sd_to_afe(sd);
	struct v4l2_mbus_framefmt *mbusformat;

	/* It makes no sense to get the format of the analog sink pads */
	if (sdformat->pad != ADV748X_AFE_SOURCE)
		return -EINVAL;

	if (sdformat->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbusformat = v4l2_subdev_get_try_format(sd, cfg, sdformat->pad);
		sdformat->format = *mbusformat;
	} else {
		adv748x_afe_fill_format(afe, &sdformat->format);
		adv748x_afe_propagate_pixelrate(afe);
	}

	return 0;
}

static int adv748x_afe_set_format(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_format *sdformat)
{
	struct v4l2_mbus_framefmt *mbusformat;

	/* It makes no sense to get the format of the analog sink pads */
	if (sdformat->pad != ADV748X_AFE_SOURCE)
		return -EINVAL;

	if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return adv748x_afe_get_format(sd, cfg, sdformat);

	mbusformat = v4l2_subdev_get_try_format(sd, cfg, sdformat->pad);
	*mbusformat = sdformat->format;

	return 0;
}

static const struct v4l2_subdev_pad_ops adv748x_afe_pad_ops = {
	.enum_mbus_code = adv748x_afe_enum_mbus_code,
	.set_fmt = adv748x_afe_set_format,
	.get_fmt = adv748x_afe_get_format,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops adv748x_afe_ops = {
	.video = &adv748x_afe_video_ops,
	.pad = &adv748x_afe_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Controls
 */

static const char * const afe_ctrl_frp_menu[] = {
	"Disabled",
	"Solid Blue",
	"Color Bars",
	"Grey Ramp",
	"Cb Ramp",
	"Cr Ramp",
	"Boundary"
};

static int adv748x_afe_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct adv748x_afe *afe = adv748x_ctrl_to_afe(ctrl);
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	bool enable;
	int ret;

	ret = sdp_write(state, 0x0e, 0x00);
	if (ret < 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = sdp_write(state, ADV748X_SDP_BRI, ctrl->val);
		break;
	case V4L2_CID_HUE:
		/* Hue is inverted according to HSL chart */
		ret = sdp_write(state, ADV748X_SDP_HUE, -ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = sdp_write(state, ADV748X_SDP_CON, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = sdp_write(state, ADV748X_SDP_SD_SAT_U, ctrl->val);
		if (ret)
			break;
		ret = sdp_write(state, ADV748X_SDP_SD_SAT_V, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		enable = !!ctrl->val;

		/* Enable/Disable Color bar test patterns */
		ret = sdp_clrset(state, ADV748X_SDP_DEF, ADV748X_SDP_DEF_VAL_EN,
				enable);
		if (ret)
			break;
		ret = sdp_clrset(state, ADV748X_SDP_FRP, ADV748X_SDP_FRP_MASK,
				enable ? ctrl->val - 1 : 0);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops adv748x_afe_ctrl_ops = {
	.s_ctrl = adv748x_afe_s_ctrl,
};

static int adv748x_afe_init_controls(struct adv748x_afe *afe)
{
	struct adv748x_state *state = adv748x_afe_to_state(afe);

	v4l2_ctrl_handler_init(&afe->ctrl_hdl, 5);

	/* Use our mutex for the controls */
	afe->ctrl_hdl.lock = &state->mutex;

	v4l2_ctrl_new_std(&afe->ctrl_hdl, &adv748x_afe_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV748X_SDP_BRI_MIN,
			  ADV748X_SDP_BRI_MAX, 1, ADV748X_SDP_BRI_DEF);
	v4l2_ctrl_new_std(&afe->ctrl_hdl, &adv748x_afe_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV748X_SDP_CON_MIN,
			  ADV748X_SDP_CON_MAX, 1, ADV748X_SDP_CON_DEF);
	v4l2_ctrl_new_std(&afe->ctrl_hdl, &adv748x_afe_ctrl_ops,
			  V4L2_CID_SATURATION, ADV748X_SDP_SAT_MIN,
			  ADV748X_SDP_SAT_MAX, 1, ADV748X_SDP_SAT_DEF);
	v4l2_ctrl_new_std(&afe->ctrl_hdl, &adv748x_afe_ctrl_ops,
			  V4L2_CID_HUE, ADV748X_SDP_HUE_MIN,
			  ADV748X_SDP_HUE_MAX, 1, ADV748X_SDP_HUE_DEF);

	v4l2_ctrl_new_std_menu_items(&afe->ctrl_hdl, &adv748x_afe_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(afe_ctrl_frp_menu) - 1,
				     0, 0, afe_ctrl_frp_menu);

	afe->sd.ctrl_handler = &afe->ctrl_hdl;
	if (afe->ctrl_hdl.error) {
		v4l2_ctrl_handler_free(&afe->ctrl_hdl);
		return afe->ctrl_hdl.error;
	}

	return v4l2_ctrl_handler_setup(&afe->ctrl_hdl);
}

int adv748x_afe_init(struct adv748x_afe *afe)
{
	struct adv748x_state *state = adv748x_afe_to_state(afe);
	int ret;
	unsigned int i;

	afe->input = 0;
	afe->streaming = false;
	afe->curr_norm = V4L2_STD_NTSC_M;

	adv748x_subdev_init(&afe->sd, state, &adv748x_afe_ops,
			    MEDIA_ENT_F_ATV_DECODER, "afe");

	/* Identify the first connector found as a default input if set */
	for (i = ADV748X_PORT_AIN0; i <= ADV748X_PORT_AIN7; i++) {
		/* Inputs and ports are 1-indexed to match the data sheet */
		if (state->endpoints[i]) {
			afe->input = i;
			break;
		}
	}

	adv748x_afe_s_input(afe, afe->input);

	adv_dbg(state, "AFE Default input set to %d\n", afe->input);

	/* Entity pads and sinks are 0-indexed to match the pads */
	for (i = ADV748X_AFE_SINK_AIN0; i <= ADV748X_AFE_SINK_AIN7; i++)
		afe->pads[i].flags = MEDIA_PAD_FL_SINK;

	afe->pads[ADV748X_AFE_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&afe->sd.entity, ADV748X_AFE_NR_PADS,
			afe->pads);
	if (ret)
		return ret;

	ret = adv748x_afe_init_controls(afe);
	if (ret)
		goto error;

	return 0;

error:
	media_entity_cleanup(&afe->sd.entity);

	return ret;
}

void adv748x_afe_cleanup(struct adv748x_afe *afe)
{
	v4l2_device_unregister_subdev(&afe->sd);
	media_entity_cleanup(&afe->sd.entity);
	v4l2_ctrl_handler_free(&afe->ctrl_hdl);
}

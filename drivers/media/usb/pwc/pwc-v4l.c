// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux driver for Philips webcam
   USB and Video4Linux interface part.
   (C) 1999-2004 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)
   (C) 2011 Hans de Goede <hdegoede@redhat.com>

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.


*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <asm/io.h>

#include "pwc.h"

#define PWC_CID_CUSTOM(ctrl) ((V4L2_CID_USER_BASE | 0xf000) + custom_ ## ctrl)

static int pwc_g_volatile_ctrl(struct v4l2_ctrl *ctrl);
static int pwc_s_ctrl(struct v4l2_ctrl *ctrl);

static const struct v4l2_ctrl_ops pwc_ctrl_ops = {
	.g_volatile_ctrl = pwc_g_volatile_ctrl,
	.s_ctrl = pwc_s_ctrl,
};

enum { awb_indoor, awb_outdoor, awb_fl, awb_manual, awb_auto };
enum { custom_autocontour, custom_contour, custom_noise_reduction,
	custom_awb_speed, custom_awb_delay,
	custom_save_user, custom_restore_user, custom_restore_factory };

static const char * const pwc_auto_whitebal_qmenu[] = {
	"Indoor (Incandescant Lighting) Mode",
	"Outdoor (Sunlight) Mode",
	"Indoor (Fluorescent Lighting) Mode",
	"Manual Mode",
	"Auto Mode",
	NULL
};

static const struct v4l2_ctrl_config pwc_auto_white_balance_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= V4L2_CID_AUTO_WHITE_BALANCE,
	.type	= V4L2_CTRL_TYPE_MENU,
	.max	= awb_auto,
	.qmenu	= pwc_auto_whitebal_qmenu,
};

static const struct v4l2_ctrl_config pwc_autocontour_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(autocontour),
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.name	= "Auto contour",
	.min	= 0,
	.max	= 1,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_contour_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(contour),
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "Contour",
	.flags  = V4L2_CTRL_FLAG_SLIDER,
	.min	= 0,
	.max	= 63,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_backlight_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= V4L2_CID_BACKLIGHT_COMPENSATION,
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_flicker_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= V4L2_CID_BAND_STOP_FILTER,
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_noise_reduction_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(noise_reduction),
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "Dynamic Noise Reduction",
	.min	= 0,
	.max	= 3,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_save_user_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(save_user),
	.type	= V4L2_CTRL_TYPE_BUTTON,
	.name    = "Save User Settings",
};

static const struct v4l2_ctrl_config pwc_restore_user_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(restore_user),
	.type	= V4L2_CTRL_TYPE_BUTTON,
	.name    = "Restore User Settings",
};

static const struct v4l2_ctrl_config pwc_restore_factory_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(restore_factory),
	.type	= V4L2_CTRL_TYPE_BUTTON,
	.name    = "Restore Factory Settings",
};

static const struct v4l2_ctrl_config pwc_awb_speed_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(awb_speed),
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "Auto White Balance Speed",
	.min	= 1,
	.max	= 32,
	.step	= 1,
};

static const struct v4l2_ctrl_config pwc_awb_delay_cfg = {
	.ops	= &pwc_ctrl_ops,
	.id	= PWC_CID_CUSTOM(awb_delay),
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "Auto White Balance Delay",
	.min	= 0,
	.max	= 63,
	.step	= 1,
};

int pwc_init_controls(struct pwc_device *pdev)
{
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl_config cfg;
	int r, def;

	hdl = &pdev->ctrl_handler;
	r = v4l2_ctrl_handler_init(hdl, 20);
	if (r)
		return r;

	/* Brightness, contrast, saturation, gamma */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, BRIGHTNESS_FORMATTER, &def);
	if (r || def > 127)
		def = 63;
	pdev->brightness = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_BRIGHTNESS, 0, 127, 1, def);

	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, CONTRAST_FORMATTER, &def);
	if (r || def > 63)
		def = 31;
	pdev->contrast = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_CONTRAST, 0, 63, 1, def);

	if (pdev->type >= 675) {
		if (pdev->type < 730)
			pdev->saturation_fmt = SATURATION_MODE_FORMATTER2;
		else
			pdev->saturation_fmt = SATURATION_MODE_FORMATTER1;
		r = pwc_get_s8_ctrl(pdev, GET_CHROM_CTL, pdev->saturation_fmt,
				    &def);
		if (r || def < -100 || def > 100)
			def = 0;
		pdev->saturation = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				      V4L2_CID_SATURATION, -100, 100, 1, def);
	}

	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, GAMMA_FORMATTER, &def);
	if (r || def > 31)
		def = 15;
	pdev->gamma = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_GAMMA, 0, 31, 1, def);

	/* auto white balance, red gain, blue gain */
	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL, WB_MODE_FORMATTER, &def);
	if (r || def > awb_auto)
		def = awb_auto;
	cfg = pwc_auto_white_balance_cfg;
	cfg.name = v4l2_ctrl_get_name(cfg.id);
	cfg.def = def;
	pdev->auto_white_balance = v4l2_ctrl_new_custom(hdl, &cfg, NULL);
	/* check auto controls to avoid NULL deref in v4l2_ctrl_auto_cluster */
	if (!pdev->auto_white_balance)
		return hdl->error;

	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL,
			    PRESET_MANUAL_RED_GAIN_FORMATTER, &def);
	if (r)
		def = 127;
	pdev->red_balance = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_RED_BALANCE, 0, 255, 1, def);

	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL,
			    PRESET_MANUAL_BLUE_GAIN_FORMATTER, &def);
	if (r)
		def = 127;
	pdev->blue_balance = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_BLUE_BALANCE, 0, 255, 1, def);

	v4l2_ctrl_auto_cluster(3, &pdev->auto_white_balance, awb_manual, true);

	/* autogain, gain */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, AGC_MODE_FORMATTER, &def);
	if (r || (def != 0 && def != 0xff))
		def = 0;
	/* Note a register value if 0 means auto gain is on */
	pdev->autogain = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_AUTOGAIN, 0, 1, 1, def == 0);
	if (!pdev->autogain)
		return hdl->error;

	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, PRESET_AGC_FORMATTER, &def);
	if (r || def > 63)
		def = 31;
	pdev->gain = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_GAIN, 0, 63, 1, def);

	/* auto exposure, exposure */
	if (DEVICE_USE_CODEC2(pdev->type)) {
		r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, SHUTTER_MODE_FORMATTER,
				    &def);
		if (r || (def != 0 && def != 0xff))
			def = 0;
		/*
		 * def = 0 auto, def = ff manual
		 * menu idx 0 = auto, idx 1 = manual
		 */
		pdev->exposure_auto = v4l2_ctrl_new_std_menu(hdl,
					&pwc_ctrl_ops,
					V4L2_CID_EXPOSURE_AUTO,
					1, 0, def != 0);
		if (!pdev->exposure_auto)
			return hdl->error;

		/* GET_LUM_CTL, PRESET_SHUTTER_FORMATTER is unreliable */
		r = pwc_get_u16_ctrl(pdev, GET_STATUS_CTL,
				     READ_SHUTTER_FORMATTER, &def);
		if (r || def > 655)
			def = 655;
		pdev->exposure = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
					V4L2_CID_EXPOSURE, 0, 655, 1, def);
		/* CODEC2: separate auto gain & auto exposure */
		v4l2_ctrl_auto_cluster(2, &pdev->autogain, 0, true);
		v4l2_ctrl_auto_cluster(2, &pdev->exposure_auto,
				       V4L2_EXPOSURE_MANUAL, true);
	} else if (DEVICE_USE_CODEC3(pdev->type)) {
		/* GET_LUM_CTL, PRESET_SHUTTER_FORMATTER is unreliable */
		r = pwc_get_u16_ctrl(pdev, GET_STATUS_CTL,
				     READ_SHUTTER_FORMATTER, &def);
		if (r || def > 255)
			def = 255;
		pdev->exposure = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
					V4L2_CID_EXPOSURE, 0, 255, 1, def);
		/* CODEC3: both gain and exposure controlled by autogain */
		pdev->autogain_expo_cluster[0] = pdev->autogain;
		pdev->autogain_expo_cluster[1] = pdev->gain;
		pdev->autogain_expo_cluster[2] = pdev->exposure;
		v4l2_ctrl_auto_cluster(3, pdev->autogain_expo_cluster,
				       0, true);
	}

	/* color / bw setting */
	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL, COLOUR_MODE_FORMATTER,
			 &def);
	if (r || (def != 0 && def != 0xff))
		def = 0xff;
	/* def = 0 bw, def = ff color, menu idx 0 = color, idx 1 = bw */
	pdev->colorfx = v4l2_ctrl_new_std_menu(hdl, &pwc_ctrl_ops,
				V4L2_CID_COLORFX, 1, 0, def == 0);

	/* autocontour, contour */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, AUTO_CONTOUR_FORMATTER, &def);
	if (r || (def != 0 && def != 0xff))
		def = 0;
	cfg = pwc_autocontour_cfg;
	cfg.def = def == 0;
	pdev->autocontour = v4l2_ctrl_new_custom(hdl, &cfg, NULL);
	if (!pdev->autocontour)
		return hdl->error;

	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL, PRESET_CONTOUR_FORMATTER, &def);
	if (r || def > 63)
		def = 31;
	cfg = pwc_contour_cfg;
	cfg.def = def;
	pdev->contour = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	v4l2_ctrl_auto_cluster(2, &pdev->autocontour, 0, false);

	/* backlight */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL,
			    BACK_LIGHT_COMPENSATION_FORMATTER, &def);
	if (r || (def != 0 && def != 0xff))
		def = 0;
	cfg = pwc_backlight_cfg;
	cfg.name = v4l2_ctrl_get_name(cfg.id);
	cfg.def = def == 0;
	pdev->backlight = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	/* flikker rediction */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL,
			    FLICKERLESS_MODE_FORMATTER, &def);
	if (r || (def != 0 && def != 0xff))
		def = 0;
	cfg = pwc_flicker_cfg;
	cfg.name = v4l2_ctrl_get_name(cfg.id);
	cfg.def = def == 0;
	pdev->flicker = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	/* Dynamic noise reduction */
	r = pwc_get_u8_ctrl(pdev, GET_LUM_CTL,
			    DYNAMIC_NOISE_CONTROL_FORMATTER, &def);
	if (r || def > 3)
		def = 2;
	cfg = pwc_noise_reduction_cfg;
	cfg.def = def;
	pdev->noise_reduction = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	/* Save / Restore User / Factory Settings */
	pdev->save_user = v4l2_ctrl_new_custom(hdl, &pwc_save_user_cfg, NULL);
	pdev->restore_user = v4l2_ctrl_new_custom(hdl, &pwc_restore_user_cfg,
						  NULL);
	if (pdev->restore_user)
		pdev->restore_user->flags |= V4L2_CTRL_FLAG_UPDATE;
	pdev->restore_factory = v4l2_ctrl_new_custom(hdl,
						     &pwc_restore_factory_cfg,
						     NULL);
	if (pdev->restore_factory)
		pdev->restore_factory->flags |= V4L2_CTRL_FLAG_UPDATE;

	/* Auto White Balance speed & delay */
	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL,
			    AWB_CONTROL_SPEED_FORMATTER, &def);
	if (r || def < 1 || def > 32)
		def = 1;
	cfg = pwc_awb_speed_cfg;
	cfg.def = def;
	pdev->awb_speed = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	r = pwc_get_u8_ctrl(pdev, GET_CHROM_CTL,
			    AWB_CONTROL_DELAY_FORMATTER, &def);
	if (r || def > 63)
		def = 0;
	cfg = pwc_awb_delay_cfg;
	cfg.def = def;
	pdev->awb_delay = v4l2_ctrl_new_custom(hdl, &cfg, NULL);

	if (!(pdev->features & FEATURE_MOTOR_PANTILT))
		return hdl->error;

	/* Motor pan / tilt / reset */
	pdev->motor_pan = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_PAN_RELATIVE, -4480, 4480, 64, 0);
	if (!pdev->motor_pan)
		return hdl->error;
	pdev->motor_tilt = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_TILT_RELATIVE, -1920, 1920, 64, 0);
	pdev->motor_pan_reset = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_PAN_RESET, 0, 0, 0, 0);
	pdev->motor_tilt_reset = v4l2_ctrl_new_std(hdl, &pwc_ctrl_ops,
				V4L2_CID_TILT_RESET, 0, 0, 0, 0);
	v4l2_ctrl_cluster(4, &pdev->motor_pan);

	return hdl->error;
}

static void pwc_vidioc_fill_fmt(struct v4l2_format *f,
	int width, int height, u32 pixfmt)
{
	memset(&f->fmt.pix, 0, sizeof(struct v4l2_pix_format));
	f->fmt.pix.width        = width;
	f->fmt.pix.height       = height;
	f->fmt.pix.field        = V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat  = pixfmt;
	f->fmt.pix.bytesperline = f->fmt.pix.width;
	f->fmt.pix.sizeimage	= f->fmt.pix.height * f->fmt.pix.width * 3 / 2;
	f->fmt.pix.colorspace	= V4L2_COLORSPACE_SRGB;
	PWC_DEBUG_IOCTL("pwc_vidioc_fill_fmt() width=%d, height=%d, bytesperline=%d, sizeimage=%d, pixelformat=%c%c%c%c\n",
			f->fmt.pix.width,
			f->fmt.pix.height,
			f->fmt.pix.bytesperline,
			f->fmt.pix.sizeimage,
			(f->fmt.pix.pixelformat)&255,
			(f->fmt.pix.pixelformat>>8)&255,
			(f->fmt.pix.pixelformat>>16)&255,
			(f->fmt.pix.pixelformat>>24)&255);
}

/* ioctl(VIDIOC_TRY_FMT) */
static int pwc_vidioc_try_fmt(struct pwc_device *pdev, struct v4l2_format *f)
{
	int size;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		PWC_DEBUG_IOCTL("Bad video type must be V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	switch (f->fmt.pix.pixelformat) {
		case V4L2_PIX_FMT_YUV420:
			break;
		case V4L2_PIX_FMT_PWC1:
			if (DEVICE_USE_CODEC23(pdev->type)) {
				PWC_DEBUG_IOCTL("codec1 is only supported for old pwc webcam\n");
				f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
			}
			break;
		case V4L2_PIX_FMT_PWC2:
			if (DEVICE_USE_CODEC1(pdev->type)) {
				PWC_DEBUG_IOCTL("codec23 is only supported for new pwc webcam\n");
				f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
			}
			break;
		default:
			PWC_DEBUG_IOCTL("Unsupported pixel format\n");
			f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	}

	size = pwc_get_size(pdev, f->fmt.pix.width, f->fmt.pix.height);
	pwc_vidioc_fill_fmt(f,
			    pwc_image_sizes[size][0],
			    pwc_image_sizes[size][1],
			    f->fmt.pix.pixelformat);

	return 0;
}

/* ioctl(VIDIOC_SET_FMT) */

static int pwc_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct pwc_device *pdev = video_drvdata(file);
	int ret, pixelformat, compression = 0;

	ret = pwc_vidioc_try_fmt(pdev, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(&pdev->vb_queue))
		return -EBUSY;

	pixelformat = f->fmt.pix.pixelformat;

	PWC_DEBUG_IOCTL("Trying to set format to: width=%d height=%d fps=%d format=%c%c%c%c\n",
			f->fmt.pix.width, f->fmt.pix.height, pdev->vframes,
			(pixelformat)&255,
			(pixelformat>>8)&255,
			(pixelformat>>16)&255,
			(pixelformat>>24)&255);

	ret = pwc_set_video_mode(pdev, f->fmt.pix.width, f->fmt.pix.height,
				 pixelformat, 30, &compression, 0);

	PWC_DEBUG_IOCTL("pwc_set_video_mode(), return=%d\n", ret);

	pwc_vidioc_fill_fmt(f, pdev->width, pdev->height, pdev->pixfmt);
	return ret;
}

static int pwc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct pwc_device *pdev = video_drvdata(file);

	strscpy(cap->driver, PWC_NAME, sizeof(cap->driver));
	strscpy(cap->card, pdev->vdev.name, sizeof(cap->card));
	usb_make_path(pdev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
					V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int pwc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	if (i->index)	/* Only one INPUT is supported */
		return -EINVAL;

	strscpy(i->name, "Camera", sizeof(i->name));
	i->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int pwc_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int pwc_s_input(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int pwc_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct pwc_device *pdev =
		container_of(ctrl->handler, struct pwc_device, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		if (pdev->color_bal_valid &&
			(pdev->auto_white_balance->val != awb_auto ||
			 time_before(jiffies,
				pdev->last_color_bal_update + HZ / 4))) {
			pdev->red_balance->val  = pdev->last_red_balance;
			pdev->blue_balance->val = pdev->last_blue_balance;
			break;
		}
		ret = pwc_get_u8_ctrl(pdev, GET_STATUS_CTL,
				      READ_RED_GAIN_FORMATTER,
				      &pdev->red_balance->val);
		if (ret)
			break;
		ret = pwc_get_u8_ctrl(pdev, GET_STATUS_CTL,
				      READ_BLUE_GAIN_FORMATTER,
				      &pdev->blue_balance->val);
		if (ret)
			break;
		pdev->last_red_balance  = pdev->red_balance->val;
		pdev->last_blue_balance = pdev->blue_balance->val;
		pdev->last_color_bal_update = jiffies;
		pdev->color_bal_valid = true;
		break;
	case V4L2_CID_AUTOGAIN:
		if (pdev->gain_valid && time_before(jiffies,
				pdev->last_gain_update + HZ / 4)) {
			pdev->gain->val = pdev->last_gain;
			break;
		}
		ret = pwc_get_u8_ctrl(pdev, GET_STATUS_CTL,
				      READ_AGC_FORMATTER, &pdev->gain->val);
		if (ret)
			break;
		pdev->last_gain = pdev->gain->val;
		pdev->last_gain_update = jiffies;
		pdev->gain_valid = true;
		if (!DEVICE_USE_CODEC3(pdev->type))
			break;
		/* For CODEC3 where autogain also controls expo */
		/* fall through */
	case V4L2_CID_EXPOSURE_AUTO:
		if (pdev->exposure_valid && time_before(jiffies,
				pdev->last_exposure_update + HZ / 4)) {
			pdev->exposure->val = pdev->last_exposure;
			break;
		}
		ret = pwc_get_u16_ctrl(pdev, GET_STATUS_CTL,
				       READ_SHUTTER_FORMATTER,
				       &pdev->exposure->val);
		if (ret)
			break;
		pdev->last_exposure = pdev->exposure->val;
		pdev->last_exposure_update = jiffies;
		pdev->exposure_valid = true;
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		PWC_ERROR("g_ctrl %s error %d\n", ctrl->name, ret);

	return ret;
}

static int pwc_set_awb(struct pwc_device *pdev)
{
	int ret;

	if (pdev->auto_white_balance->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      WB_MODE_FORMATTER,
				      pdev->auto_white_balance->val);
		if (ret)
			return ret;

		if (pdev->auto_white_balance->val != awb_manual)
			pdev->color_bal_valid = false; /* Force cache update */

		/*
		 * If this is a preset, update our red / blue balance values
		 * so that events get generated for the new preset values
		 */
		if (pdev->auto_white_balance->val == awb_indoor ||
		    pdev->auto_white_balance->val == awb_outdoor ||
		    pdev->auto_white_balance->val == awb_fl)
			pwc_g_volatile_ctrl(pdev->auto_white_balance);
	}
	if (pdev->auto_white_balance->val != awb_manual)
		return 0;

	if (pdev->red_balance->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      PRESET_MANUAL_RED_GAIN_FORMATTER,
				      pdev->red_balance->val);
		if (ret)
			return ret;
	}

	if (pdev->blue_balance->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      PRESET_MANUAL_BLUE_GAIN_FORMATTER,
				      pdev->blue_balance->val);
		if (ret)
			return ret;
	}
	return 0;
}

/* For CODEC2 models which have separate autogain and auto exposure */
static int pwc_set_autogain(struct pwc_device *pdev)
{
	int ret;

	if (pdev->autogain->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      AGC_MODE_FORMATTER,
				      pdev->autogain->val ? 0 : 0xff);
		if (ret)
			return ret;

		if (pdev->autogain->val)
			pdev->gain_valid = false; /* Force cache update */
	}

	if (pdev->autogain->val)
		return 0;

	if (pdev->gain->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      PRESET_AGC_FORMATTER,
				      pdev->gain->val);
		if (ret)
			return ret;
	}
	return 0;
}

/* For CODEC2 models which have separate autogain and auto exposure */
static int pwc_set_exposure_auto(struct pwc_device *pdev)
{
	int ret;
	int is_auto = pdev->exposure_auto->val == V4L2_EXPOSURE_AUTO;

	if (pdev->exposure_auto->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      SHUTTER_MODE_FORMATTER,
				      is_auto ? 0 : 0xff);
		if (ret)
			return ret;

		if (is_auto)
			pdev->exposure_valid = false; /* Force cache update */
	}

	if (is_auto)
		return 0;

	if (pdev->exposure->is_new) {
		ret = pwc_set_u16_ctrl(pdev, SET_LUM_CTL,
				       PRESET_SHUTTER_FORMATTER,
				       pdev->exposure->val);
		if (ret)
			return ret;
	}
	return 0;
}

/* For CODEC3 models which have autogain controlling both gain and exposure */
static int pwc_set_autogain_expo(struct pwc_device *pdev)
{
	int ret;

	if (pdev->autogain->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      AGC_MODE_FORMATTER,
				      pdev->autogain->val ? 0 : 0xff);
		if (ret)
			return ret;

		if (pdev->autogain->val) {
			pdev->gain_valid     = false; /* Force cache update */
			pdev->exposure_valid = false; /* Force cache update */
		}
	}

	if (pdev->autogain->val)
		return 0;

	if (pdev->gain->is_new) {
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      PRESET_AGC_FORMATTER,
				      pdev->gain->val);
		if (ret)
			return ret;
	}

	if (pdev->exposure->is_new) {
		ret = pwc_set_u16_ctrl(pdev, SET_LUM_CTL,
				       PRESET_SHUTTER_FORMATTER,
				       pdev->exposure->val);
		if (ret)
			return ret;
	}
	return 0;
}

static int pwc_set_motor(struct pwc_device *pdev)
{
	int ret;

	pdev->ctrl_buf[0] = 0;
	if (pdev->motor_pan_reset->is_new)
		pdev->ctrl_buf[0] |= 0x01;
	if (pdev->motor_tilt_reset->is_new)
		pdev->ctrl_buf[0] |= 0x02;
	if (pdev->motor_pan_reset->is_new || pdev->motor_tilt_reset->is_new) {
		ret = send_control_msg(pdev, SET_MPT_CTL,
				       PT_RESET_CONTROL_FORMATTER,
				       pdev->ctrl_buf, 1);
		if (ret < 0)
			return ret;
	}

	memset(pdev->ctrl_buf, 0, 4);
	if (pdev->motor_pan->is_new) {
		pdev->ctrl_buf[0] = pdev->motor_pan->val & 0xFF;
		pdev->ctrl_buf[1] = (pdev->motor_pan->val >> 8);
	}
	if (pdev->motor_tilt->is_new) {
		pdev->ctrl_buf[2] = pdev->motor_tilt->val & 0xFF;
		pdev->ctrl_buf[3] = (pdev->motor_tilt->val >> 8);
	}
	if (pdev->motor_pan->is_new || pdev->motor_tilt->is_new) {
		ret = send_control_msg(pdev, SET_MPT_CTL,
				       PT_RELATIVE_CONTROL_FORMATTER,
				       pdev->ctrl_buf, 4);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int pwc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct pwc_device *pdev =
		container_of(ctrl->handler, struct pwc_device, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      BRIGHTNESS_FORMATTER, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      CONTRAST_FORMATTER, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = pwc_set_s8_ctrl(pdev, SET_CHROM_CTL,
				      pdev->saturation_fmt, ctrl->val);
		break;
	case V4L2_CID_GAMMA:
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      GAMMA_FORMATTER, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = pwc_set_awb(pdev);
		break;
	case V4L2_CID_AUTOGAIN:
		if (DEVICE_USE_CODEC2(pdev->type))
			ret = pwc_set_autogain(pdev);
		else if (DEVICE_USE_CODEC3(pdev->type))
			ret = pwc_set_autogain_expo(pdev);
		else
			ret = -EINVAL;
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		if (DEVICE_USE_CODEC2(pdev->type))
			ret = pwc_set_exposure_auto(pdev);
		else
			ret = -EINVAL;
		break;
	case V4L2_CID_COLORFX:
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      COLOUR_MODE_FORMATTER,
				      ctrl->val ? 0 : 0xff);
		break;
	case PWC_CID_CUSTOM(autocontour):
		if (pdev->autocontour->is_new) {
			ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
					AUTO_CONTOUR_FORMATTER,
					pdev->autocontour->val ? 0 : 0xff);
		}
		if (ret == 0 && pdev->contour->is_new) {
			ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
					      PRESET_CONTOUR_FORMATTER,
					      pdev->contour->val);
		}
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      BACK_LIGHT_COMPENSATION_FORMATTER,
				      ctrl->val ? 0 : 0xff);
		break;
	case V4L2_CID_BAND_STOP_FILTER:
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      FLICKERLESS_MODE_FORMATTER,
				      ctrl->val ? 0 : 0xff);
		break;
	case PWC_CID_CUSTOM(noise_reduction):
		ret = pwc_set_u8_ctrl(pdev, SET_LUM_CTL,
				      DYNAMIC_NOISE_CONTROL_FORMATTER,
				      ctrl->val);
		break;
	case PWC_CID_CUSTOM(save_user):
		ret = pwc_button_ctrl(pdev, SAVE_USER_DEFAULTS_FORMATTER);
		break;
	case PWC_CID_CUSTOM(restore_user):
		ret = pwc_button_ctrl(pdev, RESTORE_USER_DEFAULTS_FORMATTER);
		break;
	case PWC_CID_CUSTOM(restore_factory):
		ret = pwc_button_ctrl(pdev,
				      RESTORE_FACTORY_DEFAULTS_FORMATTER);
		break;
	case PWC_CID_CUSTOM(awb_speed):
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      AWB_CONTROL_SPEED_FORMATTER,
				      ctrl->val);
		break;
	case PWC_CID_CUSTOM(awb_delay):
		ret = pwc_set_u8_ctrl(pdev, SET_CHROM_CTL,
				      AWB_CONTROL_DELAY_FORMATTER,
				      ctrl->val);
		break;
	case V4L2_CID_PAN_RELATIVE:
		ret = pwc_set_motor(pdev);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		PWC_ERROR("s_ctrl %s error %d\n", ctrl->name, ret);

	return ret;
}

static int pwc_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct pwc_device *pdev = video_drvdata(file);

	/* We only support two format: the raw format, and YUV */
	switch (f->index) {
	case 0:
		/* RAW format */
		f->pixelformat = pdev->type <= 646 ? V4L2_PIX_FMT_PWC1 : V4L2_PIX_FMT_PWC2;
		f->flags = V4L2_FMT_FLAG_COMPRESSED;
		strscpy(f->description, "Raw Philips Webcam",
			sizeof(f->description));
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_YUV420;
		strscpy(f->description, "4:2:0, planar, Y-Cb-Cr",
			sizeof(f->description));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int pwc_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	PWC_DEBUG_IOCTL("ioctl(VIDIOC_G_FMT) return size %dx%d\n",
			pdev->width, pdev->height);
	pwc_vidioc_fill_fmt(f, pdev->width, pdev->height, pdev->pixfmt);
	return 0;
}

static int pwc_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct pwc_device *pdev = video_drvdata(file);

	return pwc_vidioc_try_fmt(pdev, f);
}

static int pwc_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{
	struct pwc_device *pdev = video_drvdata(file);
	unsigned int i = 0, index = fsize->index;

	if (fsize->pixel_format == V4L2_PIX_FMT_YUV420 ||
	    (fsize->pixel_format == V4L2_PIX_FMT_PWC1 &&
			DEVICE_USE_CODEC1(pdev->type)) ||
	    (fsize->pixel_format == V4L2_PIX_FMT_PWC2 &&
			DEVICE_USE_CODEC23(pdev->type))) {
		for (i = 0; i < PSZ_MAX; i++) {
			if (!(pdev->image_mask & (1UL << i)))
				continue;
			if (!index--) {
				fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
				fsize->discrete.width = pwc_image_sizes[i][0];
				fsize->discrete.height = pwc_image_sizes[i][1];
				return 0;
			}
		}
	}
	return -EINVAL;
}

static int pwc_enum_frameintervals(struct file *file, void *fh,
					   struct v4l2_frmivalenum *fival)
{
	struct pwc_device *pdev = video_drvdata(file);
	int size = -1;
	unsigned int i;

	for (i = 0; i < PSZ_MAX; i++) {
		if (pwc_image_sizes[i][0] == fival->width &&
				pwc_image_sizes[i][1] == fival->height) {
			size = i;
			break;
		}
	}

	/* TODO: Support raw format */
	if (size < 0 || fival->pixel_format != V4L2_PIX_FMT_YUV420)
		return -EINVAL;

	i = pwc_get_fps(pdev, fival->index, size);
	if (!i)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = i;

	return 0;
}

static int pwc_g_parm(struct file *file, void *fh,
		      struct v4l2_streamparm *parm)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(parm, 0, sizeof(*parm));

	parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.readbuffers = MIN_FRAMES;
	parm->parm.capture.capability |= V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe.denominator = pdev->vframes;
	parm->parm.capture.timeperframe.numerator = 1;

	return 0;
}

static int pwc_s_parm(struct file *file, void *fh,
		      struct v4l2_streamparm *parm)
{
	struct pwc_device *pdev = video_drvdata(file);
	int compression = 0;
	int ret, fps;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* If timeperframe == 0, then reset the framerate to the nominal value.
	   We pick a high framerate here, and let pwc_set_video_mode() figure
	   out the best match. */
	if (parm->parm.capture.timeperframe.numerator == 0 ||
	    parm->parm.capture.timeperframe.denominator == 0)
		fps = 30;
	else
		fps = parm->parm.capture.timeperframe.denominator /
		      parm->parm.capture.timeperframe.numerator;

	if (vb2_is_busy(&pdev->vb_queue))
		return -EBUSY;

	ret = pwc_set_video_mode(pdev, pdev->width, pdev->height, pdev->pixfmt,
				 fps, &compression, 0);

	pwc_g_parm(file, fh, parm);

	return ret;
}

const struct v4l2_ioctl_ops pwc_ioctl_ops = {
	.vidioc_querycap		    = pwc_querycap,
	.vidioc_enum_input		    = pwc_enum_input,
	.vidioc_g_input			    = pwc_g_input,
	.vidioc_s_input			    = pwc_s_input,
	.vidioc_enum_fmt_vid_cap	    = pwc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		    = pwc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		    = pwc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		    = pwc_try_fmt_vid_cap,
	.vidioc_reqbufs			    = vb2_ioctl_reqbufs,
	.vidioc_querybuf		    = vb2_ioctl_querybuf,
	.vidioc_qbuf			    = vb2_ioctl_qbuf,
	.vidioc_dqbuf			    = vb2_ioctl_dqbuf,
	.vidioc_streamon		    = vb2_ioctl_streamon,
	.vidioc_streamoff		    = vb2_ioctl_streamoff,
	.vidioc_log_status		    = v4l2_ctrl_log_status,
	.vidioc_enum_framesizes		    = pwc_enum_framesizes,
	.vidioc_enum_frameintervals	    = pwc_enum_frameintervals,
	.vidioc_g_parm			    = pwc_g_parm,
	.vidioc_s_parm			    = pwc_s_parm,
	.vidioc_subscribe_event		    = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	    = v4l2_event_unsubscribe,
};

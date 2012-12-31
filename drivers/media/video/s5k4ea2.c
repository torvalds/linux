/* linux/drivers/media/video/s5k4ea.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Driver for S5K4EA (SXGA camera) from Samsung Electronics
 * 1/6" 1.3Mp CMOS Image Sensor SoC with an Embedded Image Processor
 * supporting MIPI CSI-2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/s5k4ea_platform.h>

#include <linux/videodev2_samsung.h>

#include "s5k4ea.h"

#define S5K4EA_DRIVER_NAME	"S5K4EA"

/* Default resolution & pixelformat. plz ref s5k4ea_platform.h */
#define DEFAULT_RES		WVGA	/* Index of resoultion */
#define DEFAUT_FPS_INDEX	S5K4EA_15FPS
#define DEFAULT_FMT		V4L2_PIX_FMT_UYVY	/* YUV422 */

/*
 * Specification
 * Parallel : ITU-R. 656/601 YUV422, RGB565, RGB888 (Up to VGA), RAW10
 * Serial : MIPI CSI2 (single lane) YUV422, RGB565, RGB888 (Up to VGA), RAW10
 * Resolution : 1280 (H) x 1024 (V)
 * Image control : Brightness, Contrast, Saturation, Sharpness, Glamour
 * Effect : Mono, Negative, Sepia, Aqua, Sketch
 * FPS : 15fps @full resolution, 30fps @VGA, 24fps @720p
 * Max. pixel clock frequency : 48MHz(upto)
 * Internal PLL (6MHz to 27MHz input frequency)
 */

/* Camera functional setting values configured by user concept */
struct s5k4ea_userset {
	signed int exposure_bias;	/* V4L2_CID_EXPOSURE */
	unsigned int ae_lock;
	unsigned int awb_lock;
	unsigned int auto_wb;	/* V4L2_CID_AUTO_WHITE_BALANCE */
	unsigned int manual_wb;	/* V4L2_CID_WHITE_BALANCE_PRESET */
	unsigned int wb_temp;	/* V4L2_CID_WHITE_BALANCE_TEMPERATURE */
	unsigned int effect;	/* Color FX (AKA Color tone) */
	unsigned int contrast;	/* V4L2_CID_CONTRAST */
	unsigned int saturation;	/* V4L2_CID_SATURATION */
	unsigned int sharpness;		/* V4L2_CID_SHARPNESS */
	unsigned int glamour;
};

struct s5k4ea_state {
	struct s5k4ea_mbus_platform_data *pdata;
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt 	fmt;
	struct v4l2_fract 		timeperframe;
	struct s5k4ea_userset 		userset;
	int 				freq;	/* MCLK in KHz */
	int 				is_mipi;
	int 				isize;
	int 				ver;
	int 				fps;
	int 				fmt_index;
	unsigned short 			devid_mask;
};

static inline struct s5k4ea_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k4ea_state, sd);
}

/*
 * S5K4EA register structure : 2bytes address, 2bytes value
 * retry on write failure up-to 5 times
 */
static inline int s5k4ea_write(struct v4l2_subdev *sd, u16 addr, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[1];
	unsigned char reg[4];
	int err = 0;
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

again:
	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 4;
	msg->buf = reg;

	reg[0] = addr >> 8;
	reg[1] = addr & 0xff;
	reg[2] = val >> 8;
	reg[3] = val & 0xff;

	err = i2c_transfer(client->adapter, msg, 1);
	if (err >= 0)
		return err;	/* Returns here on success */

	/* abnormal case: retry 5 times */
	if (retry < 5) {
		dev_err(&client->dev, "%s: address: 0x%02x%02x, " \
			"value: 0x%02x%02x\n", __func__, \
			reg[0], reg[1], reg[2], reg[3]);
		retry++;
		goto again;
	}

	return err;
}

static int s5k4ea_i2c_write(struct v4l2_subdev *sd, unsigned char i2c_data[],
				unsigned char length)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char buf[length], i;
	struct i2c_msg msg = {client->addr, 0, length, buf};

	for (i = 0; i < length; i++)
		buf[i] = i2c_data[i];

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}

static const char *s5k4ea_querymenu_wb_preset[] = {
	"WB Tungsten", "WB Fluorescent", "WB sunny", "WB cloudy", NULL
};

static const char *s5k4ea_querymenu_effect_mode[] = {
	"Effect Sepia", "Effect Aqua", "Effect Monochrome",
	"Effect Negative", "Effect Sketch", NULL
};

static const char *s5k4ea_querymenu_ev_bias_mode[] = {
	"-3EV",	"-2,1/2EV", "-2EV", "-1,1/2EV",
	"-1EV", "-1/2EV", "0", "1/2EV",
	"1EV", "1,1/2EV", "2EV", "2,1/2EV",
	"3EV", NULL
};

static struct v4l2_queryctrl s5k4ea_controls[] = {
	{
		/*
		 * For now, we just support in preset type
		 * to be close to generic WB system,
		 * we define color temp range for each preset
		 */
		.id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "White balance in kelvin",
		.minimum = 0,
		.maximum = 10000,
		.step = 1,
		.default_value = 0,	/* FIXME */
	},
	{
		.id = V4L2_CID_WHITE_BALANCE_PRESET,
		.type = V4L2_CTRL_TYPE_MENU,
		.name = "White balance preset",
		.minimum = 0,
		.maximum = ARRAY_SIZE(s5k4ea_querymenu_wb_preset) - 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_AUTO_WHITE_BALANCE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Auto white balance",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_EXPOSURE,
		.type = V4L2_CTRL_TYPE_MENU,
		.name = "Exposure bias",
		.minimum = 0,
		.maximum = ARRAY_SIZE(s5k4ea_querymenu_ev_bias_mode) - 2,
		.step = 1,
		.default_value = (ARRAY_SIZE(s5k4ea_querymenu_ev_bias_mode) \
				- 2) / 2,	/* 0 EV */
	},
	{
		.id = V4L2_CID_COLORFX,
		.type = V4L2_CTRL_TYPE_MENU,
		.name = "Image Effect",
		.minimum = 0,
		.maximum = ARRAY_SIZE(s5k4ea_querymenu_effect_mode) - 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Contrast",
		.minimum = 0,
		.maximum = 4,
		.step = 1,
		.default_value = 2,
	},
	{
		.id = V4L2_CID_SATURATION,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Saturation",
		.minimum = 0,
		.maximum = 4,
		.step = 1,
		.default_value = 2,
	},
	{
		.id = V4L2_CID_SHARPNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Sharpness",
		.minimum = 0,
		.maximum = 4,
		.step = 1,
		.default_value = 2,
	},
};

const char * const *s5k4ea_ctrl_get_menu(u32 id)
{
	switch (id) {
	case V4L2_CID_WHITE_BALANCE_PRESET:
		return s5k4ea_querymenu_wb_preset;

	case V4L2_CID_COLORFX:
		return s5k4ea_querymenu_effect_mode;

	case V4L2_CID_EXPOSURE:
		return s5k4ea_querymenu_ev_bias_mode;

	default:
		return v4l2_ctrl_get_menu(id);
	}
}

static inline struct v4l2_queryctrl const *s5k4ea_find_qctrl(int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s5k4ea_controls); i++)
		if (s5k4ea_controls[i].id == id)
			return &s5k4ea_controls[i];

	return NULL;
}

static int s5k4ea_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s5k4ea_controls); i++) {
		if (s5k4ea_controls[i].id == qc->id) {
			memcpy(qc, &s5k4ea_controls[i], \
				sizeof(struct v4l2_queryctrl));
			return 0;
		}
	}

	return -EINVAL;
}

static int s5k4ea_querymenu(struct v4l2_subdev *sd, struct v4l2_querymenu *qm)
{
	struct v4l2_queryctrl qctrl;

	qctrl.id = qm->id;
	s5k4ea_queryctrl(sd, &qctrl);

	return v4l2_ctrl_query_menu(qm, &qctrl, s5k4ea_ctrl_get_menu(qm->id));
}

/*
 * Clock configuration
 * Configure expected MCLK from host and return EINVAL if not supported clock
 * frequency is expected
 *	freq : in Hz
 *	flag : not supported for now
 */
static int s5k4ea_s_crystal_freq(struct v4l2_subdev *sd, u32  freq, u32 flags)
{
	int err = -EINVAL;

	return err;
}

static int s5k4ea_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	struct s5k4ea_state *state = to_state(sd);
	int err = 0;

	*fmt = state->fmt;
	return err;
}

static int s5k4ea_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ea_state *state = to_state(sd);
	int err = 0;

	dev_dbg(&client->dev, "requested res(%d, %d)\n",
		fmt->width, fmt->height);

	if (!state->fmt.width ||
		!state->fmt.height ||
		!state->fmt.code)
		state->fmt = *fmt;
	else
		*fmt = state->fmt;

	return err;
}
static int s5k4ea_enum_framesizes(struct v4l2_subdev *sd,
					struct v4l2_frmsizeenum *fsize)
{
	int err = 0;

	return err;
}

static int s5k4ea_enum_frameintervals(struct v4l2_subdev *sd,
					struct v4l2_frmivalenum *fival)
{
	int err = 0;

	return err;
}

static int s5k4ea_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	int err = 0;

	return err;
}

static int s5k4ea_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	int err = 0;

	return err;
}

static int s5k4ea_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ea_state *state = to_state(sd);
	struct s5k4ea_userset userset = state->userset;
	int err = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ctrl->value = userset.exposure_bias;
		err = 0;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->value = userset.auto_wb;
		err = 0;
		break;
	case V4L2_CID_WHITE_BALANCE_PRESET:
		ctrl->value = userset.manual_wb;
		err = 0;
		break;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		ctrl->value = userset.wb_temp;
		err = 0;
		break;
	case V4L2_CID_COLORFX:
		ctrl->value = userset.effect;
		err = 0;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = userset.contrast;
		err = 0;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = userset.saturation;
		err = 0;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = userset.saturation;
		err = 0;
		break;
	default:
		dev_err(&client->dev, "%s: no such ctrl\n", __func__);
		break;
	}

	return err;
}

static int s5k4ea_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
#ifdef S5K4EA_COMPLETE
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ea_state *state = to_state(sd);
	struct s5k4ea_userset userset = state->userset;
	int err = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "%s: V4L2_CID_EXPOSURE\n", \
			__func__);
		err = s5k4ea_write_regs(sd, s5k4ea_regs_ev_bias[ctrl->value]);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		dev_dbg(&client->dev, "%s: V4L2_CID_AUTO_WHITE_BALANCE\n", \
			__func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_awb_enable[ctrl->value]);
		break;
	case V4L2_CID_WHITE_BALANCE_PRESET:
		dev_dbg(&client->dev, "%s: V4L2_CID_WHITE_BALANCE_PRESET\n", \
			__func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_wb_preset[ctrl->value]);
		break;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		dev_dbg(&client->dev, \
			"%s: V4L2_CID_WHITE_BALANCE_TEMPERATURE\n", __func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_wb_temperature[ctrl->value]);
		break;
	case V4L2_CID_COLORFX:
		dev_dbg(&client->dev, "%s: V4L2_CID_COLORFX\n", __func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_color_effect[ctrl->value]);
		break;
	case V4L2_CID_CONTRAST:
		dev_dbg(&client->dev, "%s: V4L2_CID_CONTRAST\n", __func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_contrast_bias[ctrl->value]);
		break;
	case V4L2_CID_SATURATION:
		dev_dbg(&client->dev, "%s: V4L2_CID_SATURATION\n", __func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_saturation_bias[ctrl->value]);
		break;
	case V4L2_CID_SHARPNESS:
		dev_dbg(&client->dev, "%s: V4L2_CID_SHARPNESS\n", __func__);
		err = s5k4ea_write_regs(sd, \
			s5k4ea_regs_sharpness_bias[ctrl->value]);
		break;
	default:
		dev_err(&client->dev, "%s: no such control\n", __func__);
		break;
	}

	if (err < 0)
		goto out;
	else
		return 0;

out:
	dev_dbg(&client->dev, "%s: vidioc_s_ctrl failed\n", __func__);
	return err;
#else
	return 0;
#endif
}

int
__s5k4ea_init_4bytes(struct v4l2_subdev *sd, unsigned char *reg[], int total)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL, i;
	unsigned char *item;

	for (i = 0; i < total; i++) {
		item = (unsigned char *) &reg[i];
		if (item[0] == REG_DELAY) {
			mdelay(item[1]);
			err = 0;
		} else {
			err = s5k4ea_i2c_write(sd, item, 4);
		}

		if (err < 0)
			v4l_info(client, "%s: register set failed\n", \
			__func__);
	}

	return err;
}

static int
__s5k4ea_init_2bytes(struct v4l2_subdev *sd, unsigned short *reg[], int total)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL, i;
	unsigned short *item;

	for (i = 0; i < total; i++) {
		item = (unsigned short *) &reg[i];
		if (item[0] == REG_DELAY) {
			mdelay(item[1]);
			err = 0;
		} else {
			err = s5k4ea_write(sd, item[0], item[1]);
		}

		if (err < 0)
			v4l_info(client, "%s: register set failed\n", \
			__func__);
	}

	return err;
}

static int s5k4ea_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL;

	v4l_info(client, "%s: camera initialization start\n", __func__);

	err = __s5k4ea_init_4bytes(sd, \
		(unsigned char **) s5k4ea_init_reg1, S5K4EA_INIT_REGS1);

	err = __s5k4ea_init_2bytes(sd, \
		(unsigned short **) s5k4ea_init_reg2, S5K4EA_INIT_REGS2);

	err = __s5k4ea_init_4bytes(sd, \
		(unsigned char **) s5k4ea_init_reg3, S5K4EA_INIT_REGS3);

	err = __s5k4ea_init_2bytes(sd, \
		(unsigned short **) s5k4ea_init_reg4, S5K4EA_INIT_REGS4);

	if (val == 1)
		err = __s5k4ea_init_4bytes(sd, \
			(unsigned char **) s5k4ea_init_jpeg, S5K4EA_INIT_JPEG);
	else
		err = __s5k4ea_init_4bytes(sd, \
			(unsigned char **) s5k4ea_init_reg5, S5K4EA_INIT_REGS5);

	err = __s5k4ea_init_2bytes(sd, \
		(unsigned short **) s5k4ea_init_reg6, S5K4EA_INIT_REGS6);

	err = __s5k4ea_init_4bytes(sd, \
		(unsigned char **) s5k4ea_init_reg7, S5K4EA_INIT_REGS7);

	err = __s5k4ea_init_2bytes(sd, \
		(unsigned short **) s5k4ea_init_reg8, S5K4EA_INIT_REGS8);

	err = __s5k4ea_init_4bytes(sd, \
		(unsigned char **) s5k4ea_init_reg9, S5K4EA_INIT_REGS9);

	err = __s5k4ea_init_2bytes(sd, \
		(unsigned short **) s5k4ea_init_reg10, S5K4EA_INIT_REGS10);

	err = __s5k4ea_init_4bytes(sd, \
		(unsigned char **) s5k4ea_init_reg11, S5K4EA_INIT_REGS11);

	if (err < 0) {
		v4l_err(client, "%s: camera initialization failed\n", \
			__func__);
		return -EIO;	/* FIXME */
	}

	return 0;
}

static int s5k4ea_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ea_state *state = to_state(sd);
	struct s5k4ea_mbus_platform_data *pdata = state->pdata;
	int ret;

	/* bug report */
	BUG_ON(!pdata);
	if(pdata->set_clock) {
		ret = pdata->set_clock(&client->dev, on);
		if(ret)
			return -EIO;
	}

	/* setting power */
	if(pdata->set_power) {
		ret = pdata->set_power(on);
		if(ret)
			return -EIO;
		if(on)
			return s5k4ea_init(sd, 0);
	}

	return 0;
}

static int s5k4ea_sleep(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL, i;

	v4l_info(client, "%s: sleep mode\n", __func__);

	for (i = 0; i < S5K4EA_SLEEP_REGS; i++) {
		if (s5k4ea_sleep_reg[i][0] == REG_DELAY) {
			mdelay(s5k4ea_sleep_reg[i][1]);
			err = 0;
		} else {
			err = s5k4ea_write(sd, s5k4ea_sleep_reg[i][0], \
				s5k4ea_sleep_reg[i][1]);
		}

		if (err < 0)
			v4l_info(client, "%s: register set failed\n", __func__);
	}

	if (err < 0) {
		v4l_err(client, "%s: sleep failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int s5k4ea_wakeup(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL, i;

	v4l_info(client, "%s: wakeup mode\n", __func__);

	for (i = 0; i < S5K4EA_WAKEUP_REGS; i++) {
		if (s5k4ea_wakeup_reg[i][0] == REG_DELAY) {
			mdelay(s5k4ea_wakeup_reg[i][1]);
			err = 0;
		} else {
			err = s5k4ea_write(sd, s5k4ea_wakeup_reg[i][0], \
				s5k4ea_wakeup_reg[i][1]);
		}

		if (err < 0)
			v4l_info(client, "%s: register set failed\n", __func__);
	}

	if (err < 0) {
		v4l_err(client, "%s: wake up failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int s5k4ea_s_stream(struct v4l2_subdev *sd, int enable)
{
	return enable ? s5k4ea_wakeup(sd) : s5k4ea_sleep(sd);
}

static const struct v4l2_subdev_core_ops s5k4ea_core_ops = {
	.init = s5k4ea_init,	/* initializing API */
	.s_power = s5k4ea_s_power,
	.queryctrl = s5k4ea_queryctrl,
	.querymenu = s5k4ea_querymenu,
	.g_ctrl = s5k4ea_g_ctrl,
	.s_ctrl = s5k4ea_s_ctrl,
};

static const struct v4l2_subdev_video_ops s5k4ea_video_ops = {
	.s_crystal_freq = s5k4ea_s_crystal_freq,
	.g_mbus_fmt = s5k4ea_g_fmt,
	.s_mbus_fmt = s5k4ea_s_fmt,
	.enum_framesizes = s5k4ea_enum_framesizes,
	.enum_frameintervals = s5k4ea_enum_frameintervals,
	.g_parm = s5k4ea_g_parm,
	.s_parm = s5k4ea_s_parm,
	.s_stream = s5k4ea_s_stream,
};

static const struct v4l2_subdev_ops s5k4ea_ops = {
	.core = &s5k4ea_core_ops,
	.video = &s5k4ea_video_ops,
};

/*
 * s5k4ea_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int s5k4ea_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct s5k4ea_state *state;
	struct v4l2_subdev *sd;
	struct s5k4ea_mbus_platform_data *pdata = client->dev.platform_data;

	if (!pdata) {
		dev_err( &client->dev, "null platform data");
		return -EIO;
	}

	state = kzalloc(sizeof(struct s5k4ea_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, S5K4EA_DRIVER_NAME);
	state->pdata = client->dev.platform_data;

	/* set default data from sensor specific value */
	state->fmt.width = pdata->fmt.width;
	state->fmt.height = pdata->fmt.height;
	state->fmt.code = pdata->fmt.code;

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &s5k4ea_ops);

	/* needed for acquiring subdevice by this module name */
	snprintf(sd->name, sizeof(sd->name), S5K4EA_DRIVER_NAME);

	dev_info(&client->dev, "id: %d, fmt.code: %d, res: res: %d x %d",
	    pdata->id, pdata->fmt.code,
	    pdata->fmt.width, pdata->fmt.height);
	dev_info(&client->dev, "s5k4ea has been probed\n");

	return 0;
}


static int s5k4ea_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id s5k4ea_id[] = {
	{ S5K4EA_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, s5k4ea_id);

static struct i2c_driver s5k4ea_i2c_driver = {
	.driver = {
		.name	= S5K4EA_DRIVER_NAME,
	},
	.probe		= s5k4ea_probe,
	.remove		= s5k4ea_remove,
	.id_table	= s5k4ea_id,
};

static int __init s5k4ea_mod_init(void)
{
	return i2c_add_driver(&s5k4ea_i2c_driver);
}

static void __exit s5k4ea_mod_exit(void)
{
	i2c_del_driver(&s5k4ea_i2c_driver);
}
module_init(s5k4ea_mod_init);
module_exit(s5k4ea_mod_exit);

MODULE_DESCRIPTION("Samsung Electronics S5K4EA SXGA camera driver");
MODULE_AUTHOR("Dongsoo Nathaniel Kim<dongsoo45.kim@samsung.com>");
MODULE_LICENSE("GPL");


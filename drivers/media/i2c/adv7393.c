/*
 * adv7393 - ADV7393 Video Encoder Driver
 *
 * The encoder hardware does not support SECAM.
 *
 * Copyright (C) 2010-2012 ADVANSEE - http://www.advansee.com/
 * Benoît Thébaudeau <benoit.thebaudeau@advansee.com>
 *
 * Based on ADV7343 driver,
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/uaccess.h>

#include <media/adv7393.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include "adv7393_regs.h"

MODULE_DESCRIPTION("ADV7393 video encoder driver");
MODULE_LICENSE("GPL");

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level 0-1");

struct adv7393_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	u8 reg00;
	u8 reg01;
	u8 reg02;
	u8 reg35;
	u8 reg80;
	u8 reg82;
	u32 output;
	v4l2_std_id std;
};

static inline struct adv7393_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7393_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7393_state, hdl)->sd;
}

static inline int adv7393_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static const u8 adv7393_init_reg_val[] = {
	ADV7393_SOFT_RESET, ADV7393_SOFT_RESET_DEFAULT,
	ADV7393_POWER_MODE_REG, ADV7393_POWER_MODE_REG_DEFAULT,

	ADV7393_HD_MODE_REG1, ADV7393_HD_MODE_REG1_DEFAULT,
	ADV7393_HD_MODE_REG2, ADV7393_HD_MODE_REG2_DEFAULT,
	ADV7393_HD_MODE_REG3, ADV7393_HD_MODE_REG3_DEFAULT,
	ADV7393_HD_MODE_REG4, ADV7393_HD_MODE_REG4_DEFAULT,
	ADV7393_HD_MODE_REG5, ADV7393_HD_MODE_REG5_DEFAULT,
	ADV7393_HD_MODE_REG6, ADV7393_HD_MODE_REG6_DEFAULT,
	ADV7393_HD_MODE_REG7, ADV7393_HD_MODE_REG7_DEFAULT,

	ADV7393_SD_MODE_REG1, ADV7393_SD_MODE_REG1_DEFAULT,
	ADV7393_SD_MODE_REG2, ADV7393_SD_MODE_REG2_DEFAULT,
	ADV7393_SD_MODE_REG3, ADV7393_SD_MODE_REG3_DEFAULT,
	ADV7393_SD_MODE_REG4, ADV7393_SD_MODE_REG4_DEFAULT,
	ADV7393_SD_MODE_REG5, ADV7393_SD_MODE_REG5_DEFAULT,
	ADV7393_SD_MODE_REG6, ADV7393_SD_MODE_REG6_DEFAULT,
	ADV7393_SD_MODE_REG7, ADV7393_SD_MODE_REG7_DEFAULT,
	ADV7393_SD_MODE_REG8, ADV7393_SD_MODE_REG8_DEFAULT,

	ADV7393_SD_TIMING_REG0, ADV7393_SD_TIMING_REG0_DEFAULT,

	ADV7393_SD_HUE_ADJUST, ADV7393_SD_HUE_ADJUST_DEFAULT,
	ADV7393_SD_CGMS_WSS0, ADV7393_SD_CGMS_WSS0_DEFAULT,
	ADV7393_SD_BRIGHTNESS_WSS, ADV7393_SD_BRIGHTNESS_WSS_DEFAULT,
};

/*
 * 			    2^32
 * FSC(reg) =  FSC (HZ) * --------
 *			  27000000
 */
static const struct adv7393_std_info stdinfo[] = {
	{
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_NTSC, 705268427, V4L2_STD_NTSC_443,
	}, {
		/* FSC(Hz) = 3,579,545.45 Hz */
		SD_STD_NTSC, 569408542, V4L2_STD_NTSC,
	}, {
		/* FSC(Hz) = 3,575,611.00 Hz */
		SD_STD_PAL_M, 568782678, V4L2_STD_PAL_M,
	}, {
		/* FSC(Hz) = 3,582,056.00 Hz */
		SD_STD_PAL_N, 569807903, V4L2_STD_PAL_Nc,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_N, 705268427, V4L2_STD_PAL_N,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_M, 705268427, V4L2_STD_PAL_60,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_BDGHI, 705268427, V4L2_STD_PAL,
	},
};

static int adv7393_setstd(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7393_state *state = to_state(sd);
	const struct adv7393_std_info *std_info;
	int num_std;
	u8 reg;
	u32 val;
	int err = 0;
	int i;

	num_std = ARRAY_SIZE(stdinfo);

	for (i = 0; i < num_std; i++) {
		if (stdinfo[i].stdid & std)
			break;
	}

	if (i == num_std) {
		v4l2_dbg(1, debug, sd,
				"Invalid std or std is not supported: %llx\n",
						(unsigned long long)std);
		return -EINVAL;
	}

	std_info = &stdinfo[i];

	/* Set the standard */
	val = state->reg80 & ~SD_STD_MASK;
	val |= std_info->standard_val3;
	err = adv7393_write(sd, ADV7393_SD_MODE_REG1, val);
	if (err < 0)
		goto setstd_exit;

	state->reg80 = val;

	/* Configure the input mode register */
	val = state->reg01 & ~INPUT_MODE_MASK;
	val |= SD_INPUT_MODE;
	err = adv7393_write(sd, ADV7393_MODE_SELECT_REG, val);
	if (err < 0)
		goto setstd_exit;

	state->reg01 = val;

	/* Program the sub carrier frequency registers */
	val = std_info->fsc_val;
	for (reg = ADV7393_FSC_REG0; reg <= ADV7393_FSC_REG3; reg++) {
		err = adv7393_write(sd, reg, val);
		if (err < 0)
			goto setstd_exit;
		val >>= 8;
	}

	val = state->reg82;

	/* Pedestal settings */
	if (std & (V4L2_STD_NTSC | V4L2_STD_NTSC_443))
		val |= SD_PEDESTAL_EN;
	else
		val &= SD_PEDESTAL_DI;

	err = adv7393_write(sd, ADV7393_SD_MODE_REG2, val);
	if (err < 0)
		goto setstd_exit;

	state->reg82 = val;

setstd_exit:
	if (err != 0)
		v4l2_err(sd, "Error setting std, write failed\n");

	return err;
}

static int adv7393_setoutput(struct v4l2_subdev *sd, u32 output_type)
{
	struct adv7393_state *state = to_state(sd);
	u8 val;
	int err = 0;

	if (output_type > ADV7393_SVIDEO_ID) {
		v4l2_dbg(1, debug, sd,
			"Invalid output type or output type not supported:%d\n",
								output_type);
		return -EINVAL;
	}

	/* Enable Appropriate DAC */
	val = state->reg00 & 0x03;

	if (output_type == ADV7393_COMPOSITE_ID)
		val |= ADV7393_COMPOSITE_POWER_VALUE;
	else if (output_type == ADV7393_COMPONENT_ID)
		val |= ADV7393_COMPONENT_POWER_VALUE;
	else
		val |= ADV7393_SVIDEO_POWER_VALUE;

	err = adv7393_write(sd, ADV7393_POWER_MODE_REG, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg00 = val;

	/* Enable YUV output */
	val = state->reg02 | YUV_OUTPUT_SELECT;
	err = adv7393_write(sd, ADV7393_MODE_REG0, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg02 = val;

	/* configure SD DAC Output 1 bit */
	val = state->reg82;
	if (output_type == ADV7393_COMPONENT_ID)
		val &= SD_DAC_OUT1_DI;
	else
		val |= SD_DAC_OUT1_EN;
	err = adv7393_write(sd, ADV7393_SD_MODE_REG2, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg82 = val;

	/* configure ED/HD Color DAC Swap bit to zero */
	val = state->reg35 & HD_DAC_SWAP_DI;
	err = adv7393_write(sd, ADV7393_HD_MODE_REG6, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg35 = val;

setoutput_exit:
	if (err != 0)
		v4l2_err(sd, "Error setting output, write failed\n");

	return err;
}

static int adv7393_log_status(struct v4l2_subdev *sd)
{
	struct adv7393_state *state = to_state(sd);

	v4l2_info(sd, "Standard: %llx\n", (unsigned long long)state->std);
	v4l2_info(sd, "Output: %s\n", (state->output == 0) ? "Composite" :
			((state->output == 1) ? "Component" : "S-Video"));
	return 0;
}

static int adv7393_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return adv7393_write(sd, ADV7393_SD_BRIGHTNESS_WSS,
					ctrl->val & SD_BRIGHTNESS_VALUE_MASK);

	case V4L2_CID_HUE:
		return adv7393_write(sd, ADV7393_SD_HUE_ADJUST,
					ctrl->val - ADV7393_HUE_MIN);

	case V4L2_CID_GAIN:
		return adv7393_write(sd, ADV7393_DAC123_OUTPUT_LEVEL,
					ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops adv7393_ctrl_ops = {
	.s_ctrl = adv7393_s_ctrl,
};

static const struct v4l2_subdev_core_ops adv7393_core_ops = {
	.log_status = adv7393_log_status,
};

static int adv7393_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7393_state *state = to_state(sd);
	int err = 0;

	if (state->std == std)
		return 0;

	err = adv7393_setstd(sd, std);
	if (!err)
		state->std = std;

	return err;
}

static int adv7393_s_routing(struct v4l2_subdev *sd,
		u32 input, u32 output, u32 config)
{
	struct adv7393_state *state = to_state(sd);
	int err = 0;

	if (state->output == output)
		return 0;

	err = adv7393_setoutput(sd, output);
	if (!err)
		state->output = output;

	return err;
}

static const struct v4l2_subdev_video_ops adv7393_video_ops = {
	.s_std_output	= adv7393_s_std_output,
	.s_routing	= adv7393_s_routing,
};

static const struct v4l2_subdev_ops adv7393_ops = {
	.core	= &adv7393_core_ops,
	.video	= &adv7393_video_ops,
};

static int adv7393_initialize(struct v4l2_subdev *sd)
{
	struct adv7393_state *state = to_state(sd);
	int err = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(adv7393_init_reg_val); i += 2) {

		err = adv7393_write(sd, adv7393_init_reg_val[i],
					adv7393_init_reg_val[i+1]);
		if (err) {
			v4l2_err(sd, "Error initializing\n");
			return err;
		}
	}

	/* Configure for default video standard */
	err = adv7393_setoutput(sd, state->output);
	if (err < 0) {
		v4l2_err(sd, "Error setting output during init\n");
		return -EINVAL;
	}

	err = adv7393_setstd(sd, state->std);
	if (err < 0) {
		v4l2_err(sd, "Error setting std during init\n");
		return -EINVAL;
	}

	return err;
}

static int adv7393_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct adv7393_state *state;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	state->reg00	= ADV7393_POWER_MODE_REG_DEFAULT;
	state->reg01	= 0x00;
	state->reg02	= 0x20;
	state->reg35	= ADV7393_HD_MODE_REG6_DEFAULT;
	state->reg80	= ADV7393_SD_MODE_REG1_DEFAULT;
	state->reg82	= ADV7393_SD_MODE_REG2_DEFAULT;

	state->output = ADV7393_COMPOSITE_ID;
	state->std = V4L2_STD_NTSC;

	v4l2_i2c_subdev_init(&state->sd, client, &adv7393_ops);

	v4l2_ctrl_handler_init(&state->hdl, 3);
	v4l2_ctrl_new_std(&state->hdl, &adv7393_ctrl_ops,
			V4L2_CID_BRIGHTNESS, ADV7393_BRIGHTNESS_MIN,
					     ADV7393_BRIGHTNESS_MAX, 1,
					     ADV7393_BRIGHTNESS_DEF);
	v4l2_ctrl_new_std(&state->hdl, &adv7393_ctrl_ops,
			V4L2_CID_HUE, ADV7393_HUE_MIN,
				      ADV7393_HUE_MAX, 1,
				      ADV7393_HUE_DEF);
	v4l2_ctrl_new_std(&state->hdl, &adv7393_ctrl_ops,
			V4L2_CID_GAIN, ADV7393_GAIN_MIN,
				       ADV7393_GAIN_MAX, 1,
				       ADV7393_GAIN_DEF);
	state->sd.ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		int err = state->hdl.error;

		v4l2_ctrl_handler_free(&state->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->hdl);

	err = adv7393_initialize(&state->sd);
	if (err)
		v4l2_ctrl_handler_free(&state->hdl);
	return err;
}

static int adv7393_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7393_state *state = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);

	return 0;
}

static const struct i2c_device_id adv7393_id[] = {
	{"adv7393", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, adv7393_id);

static struct i2c_driver adv7393_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "adv7393",
	},
	.probe		= adv7393_probe,
	.remove		= adv7393_remove,
	.id_table	= adv7393_id,
};
module_i2c_driver(adv7393_driver);

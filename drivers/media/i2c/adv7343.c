/*
 * adv7343 - ADV7343 Video Encoder Driver
 *
 * The encoder hardware does not support SECAM.
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
#include <linux/of.h>
#include <linux/of_graph.h>

#include <media/adv7343.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include "adv7343_regs.h"

MODULE_DESCRIPTION("ADV7343 video encoder driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level 0-1");

struct adv7343_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	const struct adv7343_platform_data *pdata;
	u8 reg00;
	u8 reg01;
	u8 reg02;
	u8 reg35;
	u8 reg80;
	u8 reg82;
	u32 output;
	v4l2_std_id std;
};

static inline struct adv7343_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7343_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7343_state, hdl)->sd;
}

static inline int adv7343_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static const u8 adv7343_init_reg_val[] = {
	ADV7343_SOFT_RESET, ADV7343_SOFT_RESET_DEFAULT,
	ADV7343_POWER_MODE_REG, ADV7343_POWER_MODE_REG_DEFAULT,

	ADV7343_HD_MODE_REG1, ADV7343_HD_MODE_REG1_DEFAULT,
	ADV7343_HD_MODE_REG2, ADV7343_HD_MODE_REG2_DEFAULT,
	ADV7343_HD_MODE_REG3, ADV7343_HD_MODE_REG3_DEFAULT,
	ADV7343_HD_MODE_REG4, ADV7343_HD_MODE_REG4_DEFAULT,
	ADV7343_HD_MODE_REG5, ADV7343_HD_MODE_REG5_DEFAULT,
	ADV7343_HD_MODE_REG6, ADV7343_HD_MODE_REG6_DEFAULT,
	ADV7343_HD_MODE_REG7, ADV7343_HD_MODE_REG7_DEFAULT,

	ADV7343_SD_MODE_REG1, ADV7343_SD_MODE_REG1_DEFAULT,
	ADV7343_SD_MODE_REG2, ADV7343_SD_MODE_REG2_DEFAULT,
	ADV7343_SD_MODE_REG3, ADV7343_SD_MODE_REG3_DEFAULT,
	ADV7343_SD_MODE_REG4, ADV7343_SD_MODE_REG4_DEFAULT,
	ADV7343_SD_MODE_REG5, ADV7343_SD_MODE_REG5_DEFAULT,
	ADV7343_SD_MODE_REG6, ADV7343_SD_MODE_REG6_DEFAULT,
	ADV7343_SD_MODE_REG7, ADV7343_SD_MODE_REG7_DEFAULT,
	ADV7343_SD_MODE_REG8, ADV7343_SD_MODE_REG8_DEFAULT,

	ADV7343_SD_HUE_REG, ADV7343_SD_HUE_REG_DEFAULT,
	ADV7343_SD_CGMS_WSS0, ADV7343_SD_CGMS_WSS0_DEFAULT,
	ADV7343_SD_BRIGHTNESS_WSS, ADV7343_SD_BRIGHTNESS_WSS_DEFAULT,
};

/*
 * 			    2^32
 * FSC(reg) =  FSC (HZ) * --------
 *			  27000000
 */
static const struct adv7343_std_info stdinfo[] = {
	{
		/* FSC(Hz) = 3,579,545.45 Hz */
		SD_STD_NTSC, 569408542, V4L2_STD_NTSC,
	}, {
		/* FSC(Hz) = 3,575,611.00 Hz */
		SD_STD_PAL_M, 568782678, V4L2_STD_PAL_M,
	}, {
		/* FSC(Hz) = 3,582,056.00 */
		SD_STD_PAL_N, 569807903, V4L2_STD_PAL_Nc,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_N, 705268427, V4L2_STD_PAL_N,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_BDGHI, 705268427, V4L2_STD_PAL,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_NTSC, 705268427, V4L2_STD_NTSC_443,
	}, {
		/* FSC(Hz) = 4,433,618.75 Hz */
		SD_STD_PAL_M, 705268427, V4L2_STD_PAL_60,
	},
};

static int adv7343_setstd(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7343_state *state = to_state(sd);
	struct adv7343_std_info *std_info;
	int num_std;
	char *fsc_ptr;
	u8 reg, val;
	int err = 0;
	int i = 0;

	std_info = (struct adv7343_std_info *)stdinfo;
	num_std = ARRAY_SIZE(stdinfo);

	for (i = 0; i < num_std; i++) {
		if (std_info[i].stdid & std)
			break;
	}

	if (i == num_std) {
		v4l2_dbg(1, debug, sd,
				"Invalid std or std is not supported: %llx\n",
						(unsigned long long)std);
		return -EINVAL;
	}

	/* Set the standard */
	val = state->reg80 & (~(SD_STD_MASK));
	val |= std_info[i].standard_val3;
	err = adv7343_write(sd, ADV7343_SD_MODE_REG1, val);
	if (err < 0)
		goto setstd_exit;

	state->reg80 = val;

	/* Configure the input mode register */
	val = state->reg01 & (~((u8) INPUT_MODE_MASK));
	val |= SD_INPUT_MODE;
	err = adv7343_write(sd, ADV7343_MODE_SELECT_REG, val);
	if (err < 0)
		goto setstd_exit;

	state->reg01 = val;

	/* Program the sub carrier frequency registers */
	fsc_ptr = (unsigned char *)&std_info[i].fsc_val;
	reg = ADV7343_FSC_REG0;
	for (i = 0; i < 4; i++, reg++, fsc_ptr++) {
		err = adv7343_write(sd, reg, *fsc_ptr);
		if (err < 0)
			goto setstd_exit;
	}

	val = state->reg80;

	/* Filter settings */
	if (std & (V4L2_STD_NTSC | V4L2_STD_NTSC_443))
		val &= 0x03;
	else if (std & ~V4L2_STD_SECAM)
		val |= 0x04;

	err = adv7343_write(sd, ADV7343_SD_MODE_REG1, val);
	if (err < 0)
		goto setstd_exit;

	state->reg80 = val;

setstd_exit:
	if (err != 0)
		v4l2_err(sd, "Error setting std, write failed\n");

	return err;
}

static int adv7343_setoutput(struct v4l2_subdev *sd, u32 output_type)
{
	struct adv7343_state *state = to_state(sd);
	unsigned char val;
	int err = 0;

	if (output_type > ADV7343_SVIDEO_ID) {
		v4l2_dbg(1, debug, sd,
			"Invalid output type or output type not supported:%d\n",
								output_type);
		return -EINVAL;
	}

	/* Enable Appropriate DAC */
	val = state->reg00 & 0x03;

	/* configure default configuration */
	if (!state->pdata)
		if (output_type == ADV7343_COMPOSITE_ID)
			val |= ADV7343_COMPOSITE_POWER_VALUE;
		else if (output_type == ADV7343_COMPONENT_ID)
			val |= ADV7343_COMPONENT_POWER_VALUE;
		else
			val |= ADV7343_SVIDEO_POWER_VALUE;
	else
		val = state->pdata->mode_config.sleep_mode << 0 |
		      state->pdata->mode_config.pll_control << 1 |
		      state->pdata->mode_config.dac[2] << 2 |
		      state->pdata->mode_config.dac[1] << 3 |
		      state->pdata->mode_config.dac[0] << 4 |
		      state->pdata->mode_config.dac[5] << 5 |
		      state->pdata->mode_config.dac[4] << 6 |
		      state->pdata->mode_config.dac[3] << 7;

	err = adv7343_write(sd, ADV7343_POWER_MODE_REG, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg00 = val;

	/* Enable YUV output */
	val = state->reg02 | YUV_OUTPUT_SELECT;
	err = adv7343_write(sd, ADV7343_MODE_REG0, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg02 = val;

	/* configure SD DAC Output 2 and SD DAC Output 1 bit to zero */
	val = state->reg82 & (SD_DAC_1_DI & SD_DAC_2_DI);

	if (state->pdata && state->pdata->sd_config.sd_dac_out[0])
		val = val | (state->pdata->sd_config.sd_dac_out[0] << 1);
	else if (state->pdata && !state->pdata->sd_config.sd_dac_out[0])
		val = val & ~(state->pdata->sd_config.sd_dac_out[0] << 1);

	if (state->pdata && state->pdata->sd_config.sd_dac_out[1])
		val = val | (state->pdata->sd_config.sd_dac_out[1] << 2);
	else if (state->pdata && !state->pdata->sd_config.sd_dac_out[1])
		val = val & ~(state->pdata->sd_config.sd_dac_out[1] << 2);

	err = adv7343_write(sd, ADV7343_SD_MODE_REG2, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg82 = val;

	/* configure ED/HD Color DAC Swap and ED/HD RGB Input Enable bit to
	 * zero */
	val = state->reg35 & (HD_RGB_INPUT_DI & HD_DAC_SWAP_DI);
	err = adv7343_write(sd, ADV7343_HD_MODE_REG6, val);
	if (err < 0)
		goto setoutput_exit;

	state->reg35 = val;

setoutput_exit:
	if (err != 0)
		v4l2_err(sd, "Error setting output, write failed\n");

	return err;
}

static int adv7343_log_status(struct v4l2_subdev *sd)
{
	struct adv7343_state *state = to_state(sd);

	v4l2_info(sd, "Standard: %llx\n", (unsigned long long)state->std);
	v4l2_info(sd, "Output: %s\n", (state->output == 0) ? "Composite" :
			((state->output == 1) ? "Component" : "S-Video"));
	return 0;
}

static int adv7343_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return adv7343_write(sd, ADV7343_SD_BRIGHTNESS_WSS,
					ctrl->val);

	case V4L2_CID_HUE:
		return adv7343_write(sd, ADV7343_SD_HUE_REG, ctrl->val);

	case V4L2_CID_GAIN:
		return adv7343_write(sd, ADV7343_DAC2_OUTPUT_LEVEL, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops adv7343_ctrl_ops = {
	.s_ctrl = adv7343_s_ctrl,
};

static const struct v4l2_subdev_core_ops adv7343_core_ops = {
	.log_status = adv7343_log_status,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
};

static int adv7343_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7343_state *state = to_state(sd);
	int err = 0;

	if (state->std == std)
		return 0;

	err = adv7343_setstd(sd, std);
	if (!err)
		state->std = std;

	return err;
}

static int adv7343_s_routing(struct v4l2_subdev *sd,
		u32 input, u32 output, u32 config)
{
	struct adv7343_state *state = to_state(sd);
	int err = 0;

	if (state->output == output)
		return 0;

	err = adv7343_setoutput(sd, output);
	if (!err)
		state->output = output;

	return err;
}

static const struct v4l2_subdev_video_ops adv7343_video_ops = {
	.s_std_output	= adv7343_s_std_output,
	.s_routing	= adv7343_s_routing,
};

static const struct v4l2_subdev_ops adv7343_ops = {
	.core	= &adv7343_core_ops,
	.video	= &adv7343_video_ops,
};

static int adv7343_initialize(struct v4l2_subdev *sd)
{
	struct adv7343_state *state = to_state(sd);
	int err = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(adv7343_init_reg_val); i += 2) {

		err = adv7343_write(sd, adv7343_init_reg_val[i],
					adv7343_init_reg_val[i+1]);
		if (err) {
			v4l2_err(sd, "Error initializing\n");
			return err;
		}
	}

	/* Configure for default video standard */
	err = adv7343_setoutput(sd, state->output);
	if (err < 0) {
		v4l2_err(sd, "Error setting output during init\n");
		return -EINVAL;
	}

	err = adv7343_setstd(sd, state->std);
	if (err < 0) {
		v4l2_err(sd, "Error setting std during init\n");
		return -EINVAL;
	}

	return err;
}

static struct adv7343_platform_data *
adv7343_get_pdata(struct i2c_client *client)
{
	struct adv7343_platform_data *pdata;
	struct device_node *np;

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return client->dev.platform_data;

	np = of_graph_get_next_endpoint(client->dev.of_node, NULL);
	if (!np)
		return NULL;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		goto done;

	pdata->mode_config.sleep_mode =
			of_property_read_bool(np, "adi,power-mode-sleep-mode");

	pdata->mode_config.pll_control =
			of_property_read_bool(np, "adi,power-mode-pll-ctrl");

	of_property_read_u32_array(np, "adi,dac-enable",
				   pdata->mode_config.dac, 6);

	of_property_read_u32_array(np, "adi,sd-dac-enable",
				   pdata->sd_config.sd_dac_out, 2);

done:
	of_node_put(np);
	return pdata;
}

static int adv7343_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct adv7343_state *state;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(struct adv7343_state),
			     GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	/* Copy board specific information here */
	state->pdata = adv7343_get_pdata(client);

	state->reg00	= 0x80;
	state->reg01	= 0x00;
	state->reg02	= 0x20;
	state->reg35	= 0x00;
	state->reg80	= ADV7343_SD_MODE_REG1_DEFAULT;
	state->reg82	= ADV7343_SD_MODE_REG2_DEFAULT;

	state->output = ADV7343_COMPOSITE_ID;
	state->std = V4L2_STD_NTSC;

	v4l2_i2c_subdev_init(&state->sd, client, &adv7343_ops);

	v4l2_ctrl_handler_init(&state->hdl, 2);
	v4l2_ctrl_new_std(&state->hdl, &adv7343_ctrl_ops,
			V4L2_CID_BRIGHTNESS, ADV7343_BRIGHTNESS_MIN,
					     ADV7343_BRIGHTNESS_MAX, 1,
					     ADV7343_BRIGHTNESS_DEF);
	v4l2_ctrl_new_std(&state->hdl, &adv7343_ctrl_ops,
			V4L2_CID_HUE, ADV7343_HUE_MIN,
				      ADV7343_HUE_MAX, 1,
				      ADV7343_HUE_DEF);
	v4l2_ctrl_new_std(&state->hdl, &adv7343_ctrl_ops,
			V4L2_CID_GAIN, ADV7343_GAIN_MIN,
				       ADV7343_GAIN_MAX, 1,
				       ADV7343_GAIN_DEF);
	state->sd.ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		err = state->hdl.error;
		goto done;
	}
	v4l2_ctrl_handler_setup(&state->hdl);

	err = adv7343_initialize(&state->sd);
	if (err)
		goto done;

	err = v4l2_async_register_subdev(&state->sd);

done:
	if (err < 0)
		v4l2_ctrl_handler_free(&state->hdl);

	return err;
}

static int adv7343_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7343_state *state = to_state(sd);

	v4l2_async_unregister_subdev(&state->sd);
	v4l2_ctrl_handler_free(&state->hdl);

	return 0;
}

static const struct i2c_device_id adv7343_id[] = {
	{"adv7343", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, adv7343_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id adv7343_of_match[] = {
	{.compatible = "adi,adv7343", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, adv7343_of_match);
#endif

static struct i2c_driver adv7343_driver = {
	.driver = {
		.of_match_table = of_match_ptr(adv7343_of_match),
		.owner	= THIS_MODULE,
		.name	= "adv7343",
	},
	.probe		= adv7343_probe,
	.remove		= adv7343_remove,
	.id_table	= adv7343_id,
};

module_i2c_driver(adv7343_driver);

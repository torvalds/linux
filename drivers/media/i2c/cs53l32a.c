// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cs53l32a (Adaptec AVC-2010 and AVC-2410) i2c ivtv driver.
 * Copyright (C) 2005  Martin Vaughan
 *
 * Audio source switching for Adaptec AVC-2410 added by Trev Jackson
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

MODULE_DESCRIPTION("i2c device driver for cs53l32a Audio ADC");
MODULE_AUTHOR("Martin Vaughan");
MODULE_LICENSE("GPL");

static bool debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages, 0=Off (default), 1=On");


struct cs53l32a_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
};

static inline struct cs53l32a_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cs53l32a_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct cs53l32a_state, hdl)->sd;
}

/* ----------------------------------------------------------------------- */

static int cs53l32a_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static int cs53l32a_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int cs53l32a_s_routing(struct v4l2_subdev *sd,
			      u32 input, u32 output, u32 config)
{
	/* There are 2 physical inputs, but the second input can be
	   placed in two modes, the first mode bypasses the PGA (gain),
	   the second goes through the PGA. Hence there are three
	   possible inputs to choose from. */
	if (input > 2) {
		v4l2_err(sd, "Invalid input %d.\n", input);
		return -EINVAL;
	}
	cs53l32a_write(sd, 0x01, 0x01 + (input << 4));
	return 0;
}

static int cs53l32a_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		cs53l32a_write(sd, 0x03, ctrl->val ? 0xf0 : 0x30);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		cs53l32a_write(sd, 0x04, (u8)ctrl->val);
		cs53l32a_write(sd, 0x05, (u8)ctrl->val);
		return 0;
	}
	return -EINVAL;
}

static int cs53l32a_log_status(struct v4l2_subdev *sd)
{
	struct cs53l32a_state *state = to_state(sd);
	u8 v = cs53l32a_read(sd, 0x01);

	v4l2_info(sd, "Input:  %d\n", (v >> 4) & 3);
	v4l2_ctrl_handler_log_status(&state->hdl, sd->name);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops cs53l32a_ctrl_ops = {
	.s_ctrl = cs53l32a_s_ctrl,
};

static const struct v4l2_subdev_core_ops cs53l32a_core_ops = {
	.log_status = cs53l32a_log_status,
};

static const struct v4l2_subdev_audio_ops cs53l32a_audio_ops = {
	.s_routing = cs53l32a_s_routing,
};

static const struct v4l2_subdev_ops cs53l32a_ops = {
	.core = &cs53l32a_core_ops,
	.audio = &cs53l32a_audio_ops,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static int cs53l32a_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct cs53l32a_state *state;
	struct v4l2_subdev *sd;
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (!id)
		strscpy(client->name, "cs53l32a", sizeof(client->name));

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &cs53l32a_ops);

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(sd, i);

		v4l2_dbg(1, debug, sd, "Read Reg %d %02x\n", i, v);
	}

	v4l2_ctrl_handler_init(&state->hdl, 2);
	v4l2_ctrl_new_std(&state->hdl, &cs53l32a_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, -96, 12, 1, 0);
	v4l2_ctrl_new_std(&state->hdl, &cs53l32a_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	sd->ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		int err = state->hdl.error;

		v4l2_ctrl_handler_free(&state->hdl);
		return err;
	}

	/* Set cs53l32a internal register for Adaptec 2010/2410 setup */

	cs53l32a_write(sd, 0x01, 0x21);
	cs53l32a_write(sd, 0x02, 0x29);
	cs53l32a_write(sd, 0x03, 0x30);
	cs53l32a_write(sd, 0x04, 0x00);
	cs53l32a_write(sd, 0x05, 0x00);
	cs53l32a_write(sd, 0x06, 0x00);
	cs53l32a_write(sd, 0x07, 0x00);

	/* Display results, should be 0x21,0x29,0x30,0x00,0x00,0x00,0x00 */

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(sd, i);

		v4l2_dbg(1, debug, sd, "Read Reg %d %02x\n", i, v);
	}
	return 0;
}

static void cs53l32a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cs53l32a_state *state = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
}

static const struct i2c_device_id cs53l32a_id[] = {
	{ "cs53l32a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs53l32a_id);

static struct i2c_driver cs53l32a_driver = {
	.driver = {
		.name	= "cs53l32a",
	},
	.probe		= cs53l32a_probe,
	.remove		= cs53l32a_remove,
	.id_table	= cs53l32a_id,
};

module_i2c_driver(cs53l32a_driver);

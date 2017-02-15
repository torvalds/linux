/*
 * m52790 i2c ivtv driver.
 * Copyright (C) 2007  Hans Verkuil
 *
 * A/V source switching Mitsubishi M52790SP/FP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/i2c/m52790.h>
#include <media/v4l2-device.h>

MODULE_DESCRIPTION("i2c device driver for m52790 A/V switch");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");


struct m52790_state {
	struct v4l2_subdev sd;
	u16 input;
	u16 output;
};

static inline struct m52790_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct m52790_state, sd);
}

/* ----------------------------------------------------------------------- */

static int m52790_write(struct v4l2_subdev *sd)
{
	struct m52790_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	u8 sw1 = (state->input | state->output) & 0xff;
	u8 sw2 = (state->input | state->output) >> 8;

	return i2c_smbus_write_byte_data(client, sw1, sw2);
}

/* Note: audio and video are linked and cannot be switched separately.
   So audio and video routing commands are identical for this chip.
   In theory the video amplifier and audio modes could be handled
   separately for the output, but that seems to be overkill right now.
   The same holds for implementing an audio mute control, this is now
   part of the audio output routing. The normal case is that another
   chip takes care of the actual muting so making it part of the
   output routing seems to be the right thing to do for now. */
static int m52790_s_routing(struct v4l2_subdev *sd,
			    u32 input, u32 output, u32 config)
{
	struct m52790_state *state = to_state(sd);

	state->input = input;
	state->output = output;
	m52790_write(sd);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int m52790_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct m52790_state *state = to_state(sd);

	if (reg->reg != 0)
		return -EINVAL;
	reg->size = 1;
	reg->val = state->input | state->output;
	return 0;
}

static int m52790_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct m52790_state *state = to_state(sd);

	if (reg->reg != 0)
		return -EINVAL;
	state->input = reg->val & 0x0303;
	state->output = reg->val & ~0x0303;
	m52790_write(sd);
	return 0;
}
#endif

static int m52790_log_status(struct v4l2_subdev *sd)
{
	struct m52790_state *state = to_state(sd);

	v4l2_info(sd, "Switch 1: %02x\n",
			(state->input | state->output) & 0xff);
	v4l2_info(sd, "Switch 2: %02x\n",
			(state->input | state->output) >> 8);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops m52790_core_ops = {
	.log_status = m52790_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = m52790_g_register,
	.s_register = m52790_s_register,
#endif
};

static const struct v4l2_subdev_audio_ops m52790_audio_ops = {
	.s_routing = m52790_s_routing,
};

static const struct v4l2_subdev_video_ops m52790_video_ops = {
	.s_routing = m52790_s_routing,
};

static const struct v4l2_subdev_ops m52790_ops = {
	.core = &m52790_core_ops,
	.audio = &m52790_audio_ops,
	.video = &m52790_video_ops,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */

static int m52790_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct m52790_state *state;
	struct v4l2_subdev *sd;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &m52790_ops);
	state->input = M52790_IN_TUNER;
	state->output = M52790_OUT_STEREO;
	m52790_write(sd);
	return 0;
}

static int m52790_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id m52790_id[] = {
	{ "m52790", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m52790_id);

static struct i2c_driver m52790_driver = {
	.driver = {
		.name	= "m52790",
	},
	.probe		= m52790_probe,
	.remove		= m52790_remove,
	.id_table	= m52790_id,
};

module_i2c_driver(m52790_driver);

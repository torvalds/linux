/*
 * tlv320aic23b - driver version 0.0.1
 *
 * Copyright (C) 2006 Scott Alfter <salfter@ssai.us>
 *
 * Based on wm8775 driver
 *
 * Copyright (C) 2004 Ulf Eklund <ivtv at eklund.to>
 * Copyright (C) 2005 Hans Verkuil <hverkuil@xs4all.nl>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

MODULE_DESCRIPTION("tlv320aic23b driver");
MODULE_AUTHOR("Scott Alfter, Ulf Eklund, Hans Verkuil");
MODULE_LICENSE("GPL");


/* ----------------------------------------------------------------------- */

struct tlv320aic23b_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
};

static inline struct tlv320aic23b_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tlv320aic23b_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct tlv320aic23b_state, hdl)->sd;
}

static int tlv320aic23b_write(struct v4l2_subdev *sd, int reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;

	if ((reg < 0 || reg > 9) && (reg != 15)) {
		v4l2_err(sd, "Invalid register R%d\n", reg);
		return -1;
	}

	for (i = 0; i < 3; i++)
		if (i2c_smbus_write_byte_data(client,
				(reg << 1) | (val >> 8), val & 0xff) == 0)
			return 0;
	v4l2_err(sd, "I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static int tlv320aic23b_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	switch (freq) {
	case 32000: /* set sample rate to 32 kHz */
		tlv320aic23b_write(sd, 8, 0x018);
		break;
	case 44100: /* set sample rate to 44.1 kHz */
		tlv320aic23b_write(sd, 8, 0x022);
		break;
	case 48000: /* set sample rate to 48 kHz */
		tlv320aic23b_write(sd, 8, 0x000);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tlv320aic23b_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		tlv320aic23b_write(sd, 0, 0x180); /* mute both channels */
		/* set gain on both channels to +3.0 dB */
		if (!ctrl->val)
			tlv320aic23b_write(sd, 0, 0x119);
		return 0;
	}
	return -EINVAL;
}

static int tlv320aic23b_log_status(struct v4l2_subdev *sd)
{
	struct tlv320aic23b_state *state = to_state(sd);

	v4l2_ctrl_handler_log_status(&state->hdl, sd->name);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops tlv320aic23b_ctrl_ops = {
	.s_ctrl = tlv320aic23b_s_ctrl,
};

static const struct v4l2_subdev_core_ops tlv320aic23b_core_ops = {
	.log_status = tlv320aic23b_log_status,
};

static const struct v4l2_subdev_audio_ops tlv320aic23b_audio_ops = {
	.s_clock_freq = tlv320aic23b_s_clock_freq,
};

static const struct v4l2_subdev_ops tlv320aic23b_ops = {
	.core = &tlv320aic23b_core_ops,
	.audio = &tlv320aic23b_audio_ops,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static int tlv320aic23b_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct tlv320aic23b_state *state;
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
	v4l2_i2c_subdev_init(sd, client, &tlv320aic23b_ops);

	/* Initialize tlv320aic23b */

	/* RESET */
	tlv320aic23b_write(sd, 15, 0x000);
	/* turn off DAC & mic input */
	tlv320aic23b_write(sd, 6, 0x00A);
	/* left-justified, 24-bit, master mode */
	tlv320aic23b_write(sd, 7, 0x049);
	/* set gain on both channels to +3.0 dB */
	tlv320aic23b_write(sd, 0, 0x119);
	/* set sample rate to 48 kHz */
	tlv320aic23b_write(sd, 8, 0x000);
	/* activate digital interface */
	tlv320aic23b_write(sd, 9, 0x001);

	v4l2_ctrl_handler_init(&state->hdl, 1);
	v4l2_ctrl_new_std(&state->hdl, &tlv320aic23b_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	sd->ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		int err = state->hdl.error;

		v4l2_ctrl_handler_free(&state->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->hdl);
	return 0;
}

static int tlv320aic23b_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tlv320aic23b_state *state = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id tlv320aic23b_id[] = {
	{ "tlv320aic23b", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tlv320aic23b_id);

static struct i2c_driver tlv320aic23b_driver = {
	.driver = {
		.name	= "tlv320aic23b",
	},
	.probe		= tlv320aic23b_probe,
	.remove		= tlv320aic23b_remove,
	.id_table	= tlv320aic23b_id,
};

module_i2c_driver(tlv320aic23b_driver);

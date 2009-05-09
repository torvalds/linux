/*
 *  bt819 - BT819A VideoStream Decoder (Rockwell Part)
 *
 * Copyright (C) 1999 Mike Bernson <mike@mlb.org>
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * Modifications for LML33/DC10plus unified driver
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (9/9/2002)
 *
 * This code was modify/ported from the saa7111 driver written
 * by Dave Perks.
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
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>
#include <media/bt819.h>

MODULE_DESCRIPTION("Brooktree-819 video decoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");


/* ----------------------------------------------------------------------- */

struct bt819 {
	struct v4l2_subdev sd;
	unsigned char reg[32];

	v4l2_std_id norm;
	int ident;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static inline struct bt819 *to_bt819(struct v4l2_subdev *sd)
{
	return container_of(sd, struct bt819, sd);
}

struct timing {
	int hactive;
	int hdelay;
	int vactive;
	int vdelay;
	int hscale;
	int vscale;
};

/* for values, see the bt819 datasheet */
static struct timing timing_data[] = {
	{864 - 24, 20, 625 - 2, 1, 0x0504, 0x0000},
	{858 - 24, 20, 525 - 2, 1, 0x00f8, 0x0000},
};

/* ----------------------------------------------------------------------- */

static inline int bt819_write(struct bt819 *decoder, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(&decoder->sd);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int bt819_setbit(struct bt819 *decoder, u8 reg, u8 bit, u8 value)
{
	return bt819_write(decoder, reg,
		(decoder->reg[reg] & ~(1 << bit)) | (value ? (1 << bit) : 0));
}

static int bt819_write_block(struct bt819 *decoder, const u8 *data, unsigned int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&decoder->sd);
	int ret = -1;
	u8 reg;

	/* the bt819 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] =
				    decoder->reg[reg++] = data[1];
				len -= 2;
				data += 2;
			} while (len >= 2 && data[0] == reg && block_len < 32);
			ret = i2c_master_send(client, block_data, block_len);
			if (ret < 0)
				break;
		}
	} else {
		/* do some slow I2C emulation kind of thing */
		while (len >= 2) {
			reg = *data++;
			ret = bt819_write(decoder, reg, *data++);
			if (ret < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static inline int bt819_read(struct bt819 *decoder, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(&decoder->sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int bt819_init(struct v4l2_subdev *sd)
{
	static unsigned char init[] = {
		/*0x1f, 0x00,*/     /* Reset */
		0x01, 0x59,	/* 0x01 input format */
		0x02, 0x00,	/* 0x02 temporal decimation */
		0x03, 0x12,	/* 0x03 Cropping msb */
		0x04, 0x16,	/* 0x04 Vertical Delay, lsb */
		0x05, 0xe0,	/* 0x05 Vertical Active lsb */
		0x06, 0x80,	/* 0x06 Horizontal Delay lsb */
		0x07, 0xd0,	/* 0x07 Horizontal Active lsb */
		0x08, 0x00,	/* 0x08 Horizontal Scaling msb */
		0x09, 0xf8,	/* 0x09 Horizontal Scaling lsb */
		0x0a, 0x00,	/* 0x0a Brightness control */
		0x0b, 0x30,	/* 0x0b Miscellaneous control */
		0x0c, 0xd8,	/* 0x0c Luma Gain lsb */
		0x0d, 0xfe,	/* 0x0d Chroma Gain (U) lsb */
		0x0e, 0xb4,	/* 0x0e Chroma Gain (V) msb */
		0x0f, 0x00,	/* 0x0f Hue control */
		0x12, 0x04,	/* 0x12 Output Format */
		0x13, 0x20,	/* 0x13 Vertial Scaling msb 0x00
					   chroma comb OFF, line drop scaling, interlace scaling
					   BUG? Why does turning the chroma comb on fuck up color?
					   Bug in the bt819 stepping on my board?
					*/
		0x14, 0x00,	/* 0x14 Vertial Scaling lsb */
		0x16, 0x07,	/* 0x16 Video Timing Polarity
					   ACTIVE=active low
					   FIELD: high=odd,
					   vreset=active high,
					   hreset=active high */
		0x18, 0x68,	/* 0x18 AGC Delay */
		0x19, 0x5d,	/* 0x19 Burst Gate Delay */
		0x1a, 0x80,	/* 0x1a ADC Interface */
	};

	struct bt819 *decoder = to_bt819(sd);
	struct timing *timing = &timing_data[(decoder->norm & V4L2_STD_525_60) ? 1 : 0];

	init[0x03 * 2 - 1] =
	    (((timing->vdelay >> 8) & 0x03) << 6) |
	    (((timing->vactive >> 8) & 0x03) << 4) |
	    (((timing->hdelay >> 8) & 0x03) << 2) |
	    ((timing->hactive >> 8) & 0x03);
	init[0x04 * 2 - 1] = timing->vdelay & 0xff;
	init[0x05 * 2 - 1] = timing->vactive & 0xff;
	init[0x06 * 2 - 1] = timing->hdelay & 0xff;
	init[0x07 * 2 - 1] = timing->hactive & 0xff;
	init[0x08 * 2 - 1] = timing->hscale >> 8;
	init[0x09 * 2 - 1] = timing->hscale & 0xff;
	/* 0x15 in array is address 0x19 */
	init[0x15 * 2 - 1] = (decoder->norm & V4L2_STD_625_50) ? 115 : 93;	/* Chroma burst delay */
	/* reset */
	bt819_write(decoder, 0x1f, 0x00);
	mdelay(1);

	/* init */
	return bt819_write_block(decoder, init, sizeof(init));
}

/* ----------------------------------------------------------------------- */

static int bt819_status(struct v4l2_subdev *sd, u32 *pstatus, v4l2_std_id *pstd)
{
	struct bt819 *decoder = to_bt819(sd);
	int status = bt819_read(decoder, 0x00);
	int res = V4L2_IN_ST_NO_SIGNAL;
	v4l2_std_id std;

	if ((status & 0x80))
		res = 0;

	if ((status & 0x10))
		std = V4L2_STD_PAL;
	else
		std = V4L2_STD_NTSC;
	if (pstd)
		*pstd = std;
	if (pstatus)
		*pstatus = status;

	v4l2_dbg(1, debug, sd, "get status %x\n", status);
	return 0;
}

static int bt819_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	return bt819_status(sd, NULL, std);
}

static int bt819_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	return bt819_status(sd, status, NULL);
}

static int bt819_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct bt819 *decoder = to_bt819(sd);
	struct timing *timing = NULL;

	v4l2_dbg(1, debug, sd, "set norm %llx\n", (unsigned long long)std);

	if (sd->v4l2_dev == NULL || sd->v4l2_dev->notify == NULL)
		v4l2_err(sd, "no notify found!\n");

	if (std & V4L2_STD_NTSC) {
		v4l2_subdev_notify(sd, BT819_FIFO_RESET_LOW, 0);
		bt819_setbit(decoder, 0x01, 0, 1);
		bt819_setbit(decoder, 0x01, 1, 0);
		bt819_setbit(decoder, 0x01, 5, 0);
		bt819_write(decoder, 0x18, 0x68);
		bt819_write(decoder, 0x19, 0x5d);
		/* bt819_setbit(decoder, 0x1a,  5, 1); */
		timing = &timing_data[1];
	} else if (std & V4L2_STD_PAL) {
		v4l2_subdev_notify(sd, BT819_FIFO_RESET_LOW, 0);
		bt819_setbit(decoder, 0x01, 0, 1);
		bt819_setbit(decoder, 0x01, 1, 1);
		bt819_setbit(decoder, 0x01, 5, 1);
		bt819_write(decoder, 0x18, 0x7f);
		bt819_write(decoder, 0x19, 0x72);
		/* bt819_setbit(decoder, 0x1a,  5, 0); */
		timing = &timing_data[0];
	} else {
		v4l2_dbg(1, debug, sd, "unsupported norm %llx\n",
				(unsigned long long)std);
		return -EINVAL;
	}
	bt819_write(decoder, 0x03,
			(((timing->vdelay >> 8) & 0x03) << 6) |
			(((timing->vactive >> 8) & 0x03) << 4) |
			(((timing->hdelay >> 8) & 0x03) << 2) |
			((timing->hactive >> 8) & 0x03));
	bt819_write(decoder, 0x04, timing->vdelay & 0xff);
	bt819_write(decoder, 0x05, timing->vactive & 0xff);
	bt819_write(decoder, 0x06, timing->hdelay & 0xff);
	bt819_write(decoder, 0x07, timing->hactive & 0xff);
	bt819_write(decoder, 0x08, (timing->hscale >> 8) & 0xff);
	bt819_write(decoder, 0x09, timing->hscale & 0xff);
	decoder->norm = std;
	v4l2_subdev_notify(sd, BT819_FIFO_RESET_HIGH, 0);
	return 0;
}

static int bt819_s_routing(struct v4l2_subdev *sd,
			   u32 input, u32 output, u32 config)
{
	struct bt819 *decoder = to_bt819(sd);

	v4l2_dbg(1, debug, sd, "set input %x\n", input);

	if (input < 0 || input > 7)
		return -EINVAL;

	if (sd->v4l2_dev == NULL || sd->v4l2_dev->notify == NULL)
		v4l2_err(sd, "no notify found!\n");

	if (decoder->input != input) {
		v4l2_subdev_notify(sd, BT819_FIFO_RESET_LOW, 0);
		decoder->input = input;
		/* select mode */
		if (decoder->input == 0) {
			bt819_setbit(decoder, 0x0b, 6, 0);
			bt819_setbit(decoder, 0x1a, 1, 1);
		} else {
			bt819_setbit(decoder, 0x0b, 6, 1);
			bt819_setbit(decoder, 0x1a, 1, 0);
		}
		v4l2_subdev_notify(sd, BT819_FIFO_RESET_HIGH, 0);
	}
	return 0;
}

static int bt819_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct bt819 *decoder = to_bt819(sd);

	v4l2_dbg(1, debug, sd, "enable output %x\n", enable);

	if (decoder->enable != enable) {
		decoder->enable = enable;
		bt819_setbit(decoder, 0x16, 7, !enable);
	}
	return 0;
}

static int bt819_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		v4l2_ctrl_query_fill(qc, -128, 127, 1, 0);
		break;

	case V4L2_CID_CONTRAST:
		v4l2_ctrl_query_fill(qc, 0, 511, 1, 256);
		break;

	case V4L2_CID_SATURATION:
		v4l2_ctrl_query_fill(qc, 0, 511, 1, 256);
		break;

	case V4L2_CID_HUE:
		v4l2_ctrl_query_fill(qc, -128, 127, 1, 0);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int bt819_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct bt819 *decoder = to_bt819(sd);
	int temp;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (decoder->bright == ctrl->value)
			break;
		decoder->bright = ctrl->value;
		bt819_write(decoder, 0x0a, decoder->bright);
		break;

	case V4L2_CID_CONTRAST:
		if (decoder->contrast == ctrl->value)
			break;
		decoder->contrast = ctrl->value;
		bt819_write(decoder, 0x0c, decoder->contrast & 0xff);
		bt819_setbit(decoder, 0x0b, 2, ((decoder->contrast >> 8) & 0x01));
		break;

	case V4L2_CID_SATURATION:
		if (decoder->sat == ctrl->value)
			break;
		decoder->sat = ctrl->value;
		bt819_write(decoder, 0x0d, (decoder->sat >> 7) & 0xff);
		bt819_setbit(decoder, 0x0b, 1, ((decoder->sat >> 15) & 0x01));

		/* Ratio between U gain and V gain must stay the same as
		   the ratio between the default U and V gain values. */
		temp = (decoder->sat * 180) / 254;
		bt819_write(decoder, 0x0e, (temp >> 7) & 0xff);
		bt819_setbit(decoder, 0x0b, 0, (temp >> 15) & 0x01);
		break;

	case V4L2_CID_HUE:
		if (decoder->hue == ctrl->value)
			break;
		decoder->hue = ctrl->value;
		bt819_write(decoder, 0x0f, decoder->hue);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int bt819_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct bt819 *decoder = to_bt819(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = decoder->bright;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = decoder->contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = decoder->sat;
		break;
	case V4L2_CID_HUE:
		ctrl->value = decoder->hue;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bt819_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct bt819 *decoder = to_bt819(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, decoder->ident, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops bt819_core_ops = {
	.g_chip_ident = bt819_g_chip_ident,
	.g_ctrl = bt819_g_ctrl,
	.s_ctrl = bt819_s_ctrl,
	.queryctrl = bt819_queryctrl,
	.s_std = bt819_s_std,
};

static const struct v4l2_subdev_video_ops bt819_video_ops = {
	.s_routing = bt819_s_routing,
	.s_stream = bt819_s_stream,
	.querystd = bt819_querystd,
	.g_input_status = bt819_g_input_status,
};

static const struct v4l2_subdev_ops bt819_ops = {
	.core = &bt819_core_ops,
	.video = &bt819_video_ops,
};

/* ----------------------------------------------------------------------- */

static int bt819_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i, ver;
	struct bt819 *decoder;
	struct v4l2_subdev *sd;
	const char *name;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	decoder = kzalloc(sizeof(struct bt819), GFP_KERNEL);
	if (decoder == NULL)
		return -ENOMEM;
	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &bt819_ops);

	ver = bt819_read(decoder, 0x17);
	switch (ver & 0xf0) {
	case 0x70:
		name = "bt819a";
		decoder->ident = V4L2_IDENT_BT819A;
		break;
	case 0x60:
		name = "bt817a";
		decoder->ident = V4L2_IDENT_BT817A;
		break;
	case 0x20:
		name = "bt815a";
		decoder->ident = V4L2_IDENT_BT815A;
		break;
	default:
		v4l2_dbg(1, debug, sd,
			"unknown chip version 0x%02x\n", ver);
		return -ENODEV;
	}

	v4l_info(client, "%s found @ 0x%x (%s)\n", name,
			client->addr << 1, client->adapter->name);

	decoder->norm = V4L2_STD_NTSC;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 0;
	decoder->contrast = 0xd8;	/* 100% of original signal */
	decoder->hue = 0;
	decoder->sat = 0xfe;		/* 100% of original signal */

	i = bt819_init(sd);
	if (i < 0)
		v4l2_dbg(1, debug, sd, "init status %d\n", i);
	return 0;
}

static int bt819_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_bt819(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id bt819_id[] = {
	{ "bt819a", 0 },
	{ "bt817a", 0 },
	{ "bt815a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bt819_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "bt819",
	.probe = bt819_probe,
	.remove = bt819_remove,
	.id_table = bt819_id,
};

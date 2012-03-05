/*
 * adv7170 - adv7170, adv7171 video encoder driver version 0.0.1
 *
 * Copyright (C) 2002 Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Based on adv7176 driver by:
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (1/1/2003)
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
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("Analog Devices ADV7170 video encoder driver");
MODULE_AUTHOR("Maxim Yevtyushkin");
MODULE_LICENSE("GPL");


static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

struct adv7170 {
	struct v4l2_subdev sd;
	unsigned char reg[128];

	v4l2_std_id norm;
	int input;
};

static inline struct adv7170 *to_adv7170(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7170, sd);
}

static char *inputs[] = { "pass_through", "play_back" };

static enum v4l2_mbus_pixelcode adv7170_codes[] = {
	V4L2_MBUS_FMT_UYVY8_2X8,
	V4L2_MBUS_FMT_UYVY8_1X16,
};

/* ----------------------------------------------------------------------- */

static inline int adv7170_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7170 *encoder = to_adv7170(sd);

	encoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int adv7170_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int adv7170_write_block(struct v4l2_subdev *sd,
		     const u8 *data, unsigned int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7170 *encoder = to_adv7170(sd);
	int ret = -1;
	u8 reg;

	/* the adv7170 has an autoincrement function, use it if
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
				    encoder->reg[reg++] = data[1];
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
			ret = adv7170_write(sd, reg, *data++);
			if (ret < 0)
				break;
			len -= 2;
		}
	}
	return ret;
}

/* ----------------------------------------------------------------------- */

#define TR0MODE     0x4c
#define TR0RST	    0x80

#define TR1CAPT	    0x00
#define TR1PLAY	    0x00

static const unsigned char init_NTSC[] = {
	0x00, 0x10,		/* MR0 */
	0x01, 0x20,		/* MR1 */
	0x02, 0x0e,		/* MR2 RTC control: bits 2 and 1 */
	0x03, 0x80,		/* MR3 */
	0x04, 0x30,		/* MR4 */
	0x05, 0x00,		/* Reserved */
	0x06, 0x00,		/* Reserved */
	0x07, TR0MODE,		/* TM0 */
	0x08, TR1CAPT,		/* TM1 */
	0x09, 0x16,		/* Fsc0 */
	0x0a, 0x7c,		/* Fsc1 */
	0x0b, 0xf0,		/* Fsc2 */
	0x0c, 0x21,		/* Fsc3 */
	0x0d, 0x00,		/* Subcarrier Phase */
	0x0e, 0x00,		/* Closed Capt. Ext 0 */
	0x0f, 0x00,		/* Closed Capt. Ext 1 */
	0x10, 0x00,		/* Closed Capt. 0 */
	0x11, 0x00,		/* Closed Capt. 1 */
	0x12, 0x00,		/* Pedestal Ctl 0 */
	0x13, 0x00,		/* Pedestal Ctl 1 */
	0x14, 0x00,		/* Pedestal Ctl 2 */
	0x15, 0x00,		/* Pedestal Ctl 3 */
	0x16, 0x00,		/* CGMS_WSS_0 */
	0x17, 0x00,		/* CGMS_WSS_1 */
	0x18, 0x00,		/* CGMS_WSS_2 */
	0x19, 0x00,		/* Teletext Ctl */
};

static const unsigned char init_PAL[] = {
	0x00, 0x71,		/* MR0 */
	0x01, 0x20,		/* MR1 */
	0x02, 0x0e,		/* MR2 RTC control: bits 2 and 1 */
	0x03, 0x80,		/* MR3 */
	0x04, 0x30,		/* MR4 */
	0x05, 0x00,		/* Reserved */
	0x06, 0x00,		/* Reserved */
	0x07, TR0MODE,		/* TM0 */
	0x08, TR1CAPT,		/* TM1 */
	0x09, 0xcb,		/* Fsc0 */
	0x0a, 0x8a,		/* Fsc1 */
	0x0b, 0x09,		/* Fsc2 */
	0x0c, 0x2a,		/* Fsc3 */
	0x0d, 0x00,		/* Subcarrier Phase */
	0x0e, 0x00,		/* Closed Capt. Ext 0 */
	0x0f, 0x00,		/* Closed Capt. Ext 1 */
	0x10, 0x00,		/* Closed Capt. 0 */
	0x11, 0x00,		/* Closed Capt. 1 */
	0x12, 0x00,		/* Pedestal Ctl 0 */
	0x13, 0x00,		/* Pedestal Ctl 1 */
	0x14, 0x00,		/* Pedestal Ctl 2 */
	0x15, 0x00,		/* Pedestal Ctl 3 */
	0x16, 0x00,		/* CGMS_WSS_0 */
	0x17, 0x00,		/* CGMS_WSS_1 */
	0x18, 0x00,		/* CGMS_WSS_2 */
	0x19, 0x00,		/* Teletext Ctl */
};


static int adv7170_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7170 *encoder = to_adv7170(sd);

	v4l2_dbg(1, debug, sd, "set norm %llx\n", (unsigned long long)std);

	if (std & V4L2_STD_NTSC) {
		adv7170_write_block(sd, init_NTSC, sizeof(init_NTSC));
		if (encoder->input == 0)
			adv7170_write(sd, 0x02, 0x0e);	/* Enable genlock */
		adv7170_write(sd, 0x07, TR0MODE | TR0RST);
		adv7170_write(sd, 0x07, TR0MODE);
	} else if (std & V4L2_STD_PAL) {
		adv7170_write_block(sd, init_PAL, sizeof(init_PAL));
		if (encoder->input == 0)
			adv7170_write(sd, 0x02, 0x0e);	/* Enable genlock */
		adv7170_write(sd, 0x07, TR0MODE | TR0RST);
		adv7170_write(sd, 0x07, TR0MODE);
	} else {
		v4l2_dbg(1, debug, sd, "illegal norm: %llx\n",
				(unsigned long long)std);
		return -EINVAL;
	}
	v4l2_dbg(1, debug, sd, "switched to %llx\n", (unsigned long long)std);
	encoder->norm = std;
	return 0;
}

static int adv7170_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct adv7170 *encoder = to_adv7170(sd);

	/* RJ: input = 0: input is from decoder
	   input = 1: input is from ZR36060
	   input = 2: color bar */

	v4l2_dbg(1, debug, sd, "set input from %s\n",
			input == 0 ? "decoder" : "ZR36060");

	switch (input) {
	case 0:
		adv7170_write(sd, 0x01, 0x20);
		adv7170_write(sd, 0x08, TR1CAPT);	/* TR1 */
		adv7170_write(sd, 0x02, 0x0e);	/* Enable genlock */
		adv7170_write(sd, 0x07, TR0MODE | TR0RST);
		adv7170_write(sd, 0x07, TR0MODE);
		/* udelay(10); */
		break;

	case 1:
		adv7170_write(sd, 0x01, 0x00);
		adv7170_write(sd, 0x08, TR1PLAY);	/* TR1 */
		adv7170_write(sd, 0x02, 0x08);
		adv7170_write(sd, 0x07, TR0MODE | TR0RST);
		adv7170_write(sd, 0x07, TR0MODE);
		/* udelay(10); */
		break;

	default:
		v4l2_dbg(1, debug, sd, "illegal input: %d\n", input);
		return -EINVAL;
	}
	v4l2_dbg(1, debug, sd, "switched to %s\n", inputs[input]);
	encoder->input = input;
	return 0;
}

static int adv7170_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
				enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(adv7170_codes))
		return -EINVAL;

	*code = adv7170_codes[index];
	return 0;
}

static int adv7170_g_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *mf)
{
	u8 val = adv7170_read(sd, 0x7);

	if ((val & 0x40) == (1 << 6))
		mf->code = V4L2_MBUS_FMT_UYVY8_1X16;
	else
		mf->code = V4L2_MBUS_FMT_UYVY8_2X8;

	mf->colorspace  = V4L2_COLORSPACE_SMPTE170M;
	mf->width       = 0;
	mf->height      = 0;
	mf->field       = V4L2_FIELD_ANY;

	return 0;
}

static int adv7170_s_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *mf)
{
	u8 val = adv7170_read(sd, 0x7);
	int ret;

	switch (mf->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
		val &= ~0x40;
		break;

	case V4L2_MBUS_FMT_UYVY8_1X16:
		val |= 0x40;
		break;

	default:
		v4l2_dbg(1, debug, sd,
			"illegal v4l2_mbus_framefmt code: %d\n", mf->code);
		return -EINVAL;
	}

	ret = adv7170_write(sd, 0x7, val);

	return ret;
}

static int adv7170_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_ADV7170, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops adv7170_core_ops = {
	.g_chip_ident = adv7170_g_chip_ident,
};

static const struct v4l2_subdev_video_ops adv7170_video_ops = {
	.s_std_output = adv7170_s_std_output,
	.s_routing = adv7170_s_routing,
	.s_mbus_fmt = adv7170_s_fmt,
	.g_mbus_fmt = adv7170_g_fmt,
	.enum_mbus_fmt  = adv7170_enum_fmt,
};

static const struct v4l2_subdev_ops adv7170_ops = {
	.core = &adv7170_core_ops,
	.video = &adv7170_video_ops,
};

/* ----------------------------------------------------------------------- */

static int adv7170_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct adv7170 *encoder;
	struct v4l2_subdev *sd;
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = kzalloc(sizeof(struct adv7170), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	sd = &encoder->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7170_ops);
	encoder->norm = V4L2_STD_NTSC;
	encoder->input = 0;

	i = adv7170_write_block(sd, init_NTSC, sizeof(init_NTSC));
	if (i >= 0) {
		i = adv7170_write(sd, 0x07, TR0MODE | TR0RST);
		i = adv7170_write(sd, 0x07, TR0MODE);
		i = adv7170_read(sd, 0x12);
		v4l2_dbg(1, debug, sd, "revision %d\n", i & 1);
	}
	if (i < 0)
		v4l2_dbg(1, debug, sd, "init error 0x%x\n", i);
	return 0;
}

static int adv7170_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_adv7170(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id adv7170_id[] = {
	{ "adv7170", 0 },
	{ "adv7171", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7170_id);

static struct i2c_driver adv7170_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "adv7170",
	},
	.probe		= adv7170_probe,
	.remove		= adv7170_remove,
	.id_table	= adv7170_id,
};

static __init int init_adv7170(void)
{
	return i2c_add_driver(&adv7170_driver);
}

static __exit void exit_adv7170(void)
{
	i2c_del_driver(&adv7170_driver);
}

module_init(init_adv7170);
module_exit(exit_adv7170);

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  adv7175 - adv7175a video encoder driver version 0.0.3
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (9/9/2002)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>

MODULE_DESCRIPTION("Analog Devices ADV7175 video encoder driver");
MODULE_AUTHOR("Dave Perks");
MODULE_LICENSE("GPL");

#define   I2C_ADV7175        0xd4
#define   I2C_ADV7176        0x54


static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

struct adv7175 {
	struct v4l2_subdev sd;
	v4l2_std_id norm;
	int input;
};

static inline struct adv7175 *to_adv7175(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7175, sd);
}

static char *inputs[] = { "pass_through", "play_back", "color_bar" };

static u32 adv7175_codes[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_UYVY8_1X16,
};

/* ----------------------------------------------------------------------- */

static inline int adv7175_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int adv7175_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int adv7175_write_block(struct v4l2_subdev *sd,
		     const u8 *data, unsigned int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = -1;
	u8 reg;

	/* the adv7175 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] = data[1];
				reg++;
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
			ret = adv7175_write(sd, reg, *data++);
			if (ret < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static void set_subcarrier_freq(struct v4l2_subdev *sd, int pass_through)
{
	/* for some reason pass_through NTSC needs
	 * a different sub-carrier freq to remain stable. */
	if (pass_through)
		adv7175_write(sd, 0x02, 0x00);
	else
		adv7175_write(sd, 0x02, 0x55);

	adv7175_write(sd, 0x03, 0x55);
	adv7175_write(sd, 0x04, 0x55);
	adv7175_write(sd, 0x05, 0x25);
}

/* ----------------------------------------------------------------------- */
/* Output filter:  S-Video  Composite */

#define MR050       0x11	/* 0x09 */
#define MR060       0x14	/* 0x0c */

/* ----------------------------------------------------------------------- */

#define TR0MODE     0x46
#define TR0RST	    0x80

#define TR1CAPT	    0x80
#define TR1PLAY	    0x00

static const unsigned char init_common[] = {

	0x00, MR050,		/* MR0, PAL enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x0c,		/* subc. freq. */
	0x03, 0x8c,		/* subc. freq. */
	0x04, 0x79,		/* subc. freq. */
	0x05, 0x26,		/* subc. freq. */
	0x06, 0x40,		/* subc. phase */

	0x07, TR0MODE,		/* TR0, 16bit */
	0x08, 0x21,		/*  */
	0x09, 0x00,		/*  */
	0x0a, 0x00,		/*  */
	0x0b, 0x00,		/*  */
	0x0c, TR1CAPT,		/* TR1 */
	0x0d, 0x4f,		/* MR2 */
	0x0e, 0x00,		/*  */
	0x0f, 0x00,		/*  */
	0x10, 0x00,		/*  */
	0x11, 0x00,		/*  */
};

static const unsigned char init_pal[] = {
	0x00, MR050,		/* MR0, PAL enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x0c,		/* subc. freq. */
	0x03, 0x8c,		/* subc. freq. */
	0x04, 0x79,		/* subc. freq. */
	0x05, 0x26,		/* subc. freq. */
	0x06, 0x40,		/* subc. phase */
};

static const unsigned char init_ntsc[] = {
	0x00, MR060,		/* MR0, NTSC enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x55,		/* subc. freq. */
	0x03, 0x55,		/* subc. freq. */
	0x04, 0x55,		/* subc. freq. */
	0x05, 0x25,		/* subc. freq. */
	0x06, 0x1a,		/* subc. phase */
};

static int adv7175_init(struct v4l2_subdev *sd, u32 val)
{
	/* This is just for testing!!! */
	adv7175_write_block(sd, init_common, sizeof(init_common));
	adv7175_write(sd, 0x07, TR0MODE | TR0RST);
	adv7175_write(sd, 0x07, TR0MODE);
	return 0;
}

static int adv7175_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7175 *encoder = to_adv7175(sd);

	if (std & V4L2_STD_NTSC) {
		adv7175_write_block(sd, init_ntsc, sizeof(init_ntsc));
		if (encoder->input == 0)
			adv7175_write(sd, 0x0d, 0x4f);	/* Enable genlock */
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
	} else if (std & V4L2_STD_PAL) {
		adv7175_write_block(sd, init_pal, sizeof(init_pal));
		if (encoder->input == 0)
			adv7175_write(sd, 0x0d, 0x4f);	/* Enable genlock */
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
	} else if (std & V4L2_STD_SECAM) {
		/* This is an attempt to convert
		 * SECAM->PAL (typically it does not work
		 * due to genlock: when decoder is in SECAM
		 * and encoder in in PAL the subcarrier can
		 * not be synchronized with horizontal
		 * quency) */
		adv7175_write_block(sd, init_pal, sizeof(init_pal));
		if (encoder->input == 0)
			adv7175_write(sd, 0x0d, 0x49);	/* Disable genlock */
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
	} else {
		v4l2_dbg(1, debug, sd, "illegal norm: %llx\n",
				(unsigned long long)std);
		return -EINVAL;
	}
	v4l2_dbg(1, debug, sd, "switched to %llx\n", (unsigned long long)std);
	encoder->norm = std;
	return 0;
}

static int adv7175_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct adv7175 *encoder = to_adv7175(sd);

	/* RJ: input = 0: input is from decoder
	   input = 1: input is from ZR36060
	   input = 2: color bar */

	switch (input) {
	case 0:
		adv7175_write(sd, 0x01, 0x00);

		if (encoder->norm & V4L2_STD_NTSC)
			set_subcarrier_freq(sd, 1);

		adv7175_write(sd, 0x0c, TR1CAPT);	/* TR1 */
		if (encoder->norm & V4L2_STD_SECAM)
			adv7175_write(sd, 0x0d, 0x49);	/* Disable genlock */
		else
			adv7175_write(sd, 0x0d, 0x4f);	/* Enable genlock */
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
		/*udelay(10);*/
		break;

	case 1:
		adv7175_write(sd, 0x01, 0x00);

		if (encoder->norm & V4L2_STD_NTSC)
			set_subcarrier_freq(sd, 0);

		adv7175_write(sd, 0x0c, TR1PLAY);	/* TR1 */
		adv7175_write(sd, 0x0d, 0x49);
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
		/* udelay(10); */
		break;

	case 2:
		adv7175_write(sd, 0x01, 0x80);

		if (encoder->norm & V4L2_STD_NTSC)
			set_subcarrier_freq(sd, 0);

		adv7175_write(sd, 0x0d, 0x49);
		adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		adv7175_write(sd, 0x07, TR0MODE);
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

static int adv7175_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(adv7175_codes))
		return -EINVAL;

	code->code = adv7175_codes[code->index];
	return 0;
}

static int adv7175_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	u8 val = adv7175_read(sd, 0x7);

	if (format->pad)
		return -EINVAL;

	if ((val & 0x40) == (1 << 6))
		mf->code = MEDIA_BUS_FMT_UYVY8_1X16;
	else
		mf->code = MEDIA_BUS_FMT_UYVY8_2X8;

	mf->colorspace  = V4L2_COLORSPACE_SMPTE170M;
	mf->width       = 0;
	mf->height      = 0;
	mf->field       = V4L2_FIELD_ANY;

	return 0;
}

static int adv7175_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	u8 val = adv7175_read(sd, 0x7);
	int ret = 0;

	if (format->pad)
		return -EINVAL;

	switch (mf->code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		val &= ~0x40;
		break;

	case MEDIA_BUS_FMT_UYVY8_1X16:
		val |= 0x40;
		break;

	default:
		v4l2_dbg(1, debug, sd,
			"illegal v4l2_mbus_framefmt code: %d\n", mf->code);
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ret = adv7175_write(sd, 0x7, val);

	return ret;
}

static int adv7175_s_power(struct v4l2_subdev *sd, int on)
{
	if (on)
		adv7175_write(sd, 0x01, 0x00);
	else
		adv7175_write(sd, 0x01, 0x78);

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops adv7175_core_ops = {
	.init = adv7175_init,
	.s_power = adv7175_s_power,
};

static const struct v4l2_subdev_video_ops adv7175_video_ops = {
	.s_std_output = adv7175_s_std_output,
	.s_routing = adv7175_s_routing,
};

static const struct v4l2_subdev_pad_ops adv7175_pad_ops = {
	.enum_mbus_code = adv7175_enum_mbus_code,
	.get_fmt = adv7175_get_fmt,
	.set_fmt = adv7175_set_fmt,
};

static const struct v4l2_subdev_ops adv7175_ops = {
	.core = &adv7175_core_ops,
	.video = &adv7175_video_ops,
	.pad = &adv7175_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int adv7175_probe(struct i2c_client *client)
{
	int i;
	struct adv7175 *encoder;
	struct v4l2_subdev *sd;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = devm_kzalloc(&client->dev, sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	sd = &encoder->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7175_ops);
	encoder->norm = V4L2_STD_NTSC;
	encoder->input = 0;

	i = adv7175_write_block(sd, init_common, sizeof(init_common));
	if (i >= 0) {
		i = adv7175_write(sd, 0x07, TR0MODE | TR0RST);
		i = adv7175_write(sd, 0x07, TR0MODE);
		i = adv7175_read(sd, 0x12);
		v4l2_dbg(1, debug, sd, "revision %d\n", i & 1);
	}
	if (i < 0)
		v4l2_dbg(1, debug, sd, "init error 0x%x\n", i);
	return 0;
}

static void adv7175_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id adv7175_id[] = {
	{ "adv7175", 0 },
	{ "adv7176", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7175_id);

static struct i2c_driver adv7175_driver = {
	.driver = {
		.name	= "adv7175",
	},
	.probe		= adv7175_probe,
	.remove		= adv7175_remove,
	.id_table	= adv7175_id,
};

module_i2c_driver(adv7175_driver);

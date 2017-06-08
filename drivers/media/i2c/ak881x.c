/*
 * Driver for AK8813 / AK8814 TV-ecoders from Asahi Kasei Microsystems Co., Ltd. (AKM)
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/i2c/ak881x.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>

#define AK881X_INTERFACE_MODE	0
#define AK881X_VIDEO_PROCESS1	1
#define AK881X_VIDEO_PROCESS2	2
#define AK881X_VIDEO_PROCESS3	3
#define AK881X_DAC_MODE		5
#define AK881X_STATUS		0x24
#define AK881X_DEVICE_ID	0x25
#define AK881X_DEVICE_REVISION	0x26

struct ak881x {
	struct v4l2_subdev subdev;
	struct ak881x_pdata *pdata;
	unsigned int lines;
	char revision;	/* DEVICE_REVISION content */
};

static int reg_read(struct i2c_client *client, const u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int reg_write(struct i2c_client *client, const u8 reg,
		     const u8 data)
{
	return i2c_smbus_write_byte_data(client, reg, data);
}

static int reg_set(struct i2c_client *client, const u8 reg,
		   const u8 data, u8 mask)
{
	int ret = reg_read(client, reg);
	if (ret < 0)
		return ret;
	return reg_write(client, reg, (ret & ~mask) | (data & mask));
}

static struct ak881x *to_ak881x(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ak881x, subdev);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ak881x_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0x26)
		return -EINVAL;

	reg->size = 1;
	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int ak881x_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0x26)
		return -EINVAL;

	if (reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static int ak881x_fill_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ak881x *ak881x = to_ak881x(client);

	if (format->pad)
		return -EINVAL;

	v4l_bound_align_image(&mf->width, 0, 720, 2,
			      &mf->height, 0, ak881x->lines, 1, 0);
	mf->field	= V4L2_FIELD_INTERLACED;
	mf->code	= MEDIA_BUS_FMT_YUYV8_2X8;
	mf->colorspace	= V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int ak881x_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_YUYV8_2X8;
	return 0;
}

static int ak881x_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ak881x *ak881x = to_ak881x(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = 720;
		sel->r.height = ak881x->lines;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ak881x_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ak881x *ak881x = to_ak881x(client);
	u8 vp1;

	if (std == V4L2_STD_NTSC_443) {
		vp1 = 3;
		ak881x->lines = 480;
	} else if (std == V4L2_STD_PAL_M) {
		vp1 = 5;
		ak881x->lines = 480;
	} else if (std == V4L2_STD_PAL_60) {
		vp1 = 7;
		ak881x->lines = 480;
	} else if (std & V4L2_STD_NTSC) {
		vp1 = 0;
		ak881x->lines = 480;
	} else if (std & V4L2_STD_PAL) {
		vp1 = 0xf;
		ak881x->lines = 576;
	} else {
		/* No SECAM or PAL_N/Nc supported */
		return -EINVAL;
	}

	reg_set(client, AK881X_VIDEO_PROCESS1, vp1, 0xf);

	return 0;
}

static int ak881x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ak881x *ak881x = to_ak881x(client);

	if (enable) {
		u8 dac;
		/* For colour-bar testing set bit 6 of AK881X_VIDEO_PROCESS1 */
		/* Default: composite output */
		if (ak881x->pdata->flags & AK881X_COMPONENT)
			dac = 3;
		else
			dac = 4;
		/* Turn on the DAC(s) */
		reg_write(client, AK881X_DAC_MODE, dac);
		dev_dbg(&client->dev, "chip status 0x%x\n",
			reg_read(client, AK881X_STATUS));
	} else {
		/* ...and clear bit 6 of AK881X_VIDEO_PROCESS1 here */
		reg_write(client, AK881X_DAC_MODE, 0);
		dev_dbg(&client->dev, "chip status 0x%x\n",
			reg_read(client, AK881X_STATUS));
	}

	return 0;
}

static const struct v4l2_subdev_core_ops ak881x_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ak881x_g_register,
	.s_register	= ak881x_s_register,
#endif
};

static const struct v4l2_subdev_video_ops ak881x_subdev_video_ops = {
	.s_std_output	= ak881x_s_std_output,
	.s_stream	= ak881x_s_stream,
};

static const struct v4l2_subdev_pad_ops ak881x_subdev_pad_ops = {
	.enum_mbus_code = ak881x_enum_mbus_code,
	.get_selection	= ak881x_get_selection,
	.set_fmt	= ak881x_fill_fmt,
	.get_fmt	= ak881x_fill_fmt,
};

static const struct v4l2_subdev_ops ak881x_subdev_ops = {
	.core	= &ak881x_subdev_core_ops,
	.video	= &ak881x_subdev_video_ops,
	.pad	= &ak881x_subdev_pad_ops,
};

static int ak881x_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct ak881x *ak881x;
	u8 ifmode, data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	ak881x = devm_kzalloc(&client->dev, sizeof(*ak881x), GFP_KERNEL);
	if (!ak881x)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ak881x->subdev, client, &ak881x_subdev_ops);

	data = reg_read(client, AK881X_DEVICE_ID);

	switch (data) {
	case 0x13:
	case 0x14:
		break;
	default:
		dev_err(&client->dev,
			"No ak881x chip detected, register read %x\n", data);
		return -ENODEV;
	}

	ak881x->revision = reg_read(client, AK881X_DEVICE_REVISION);
	ak881x->pdata = client->dev.platform_data;

	if (ak881x->pdata) {
		if (ak881x->pdata->flags & AK881X_FIELD)
			ifmode = 4;
		else
			ifmode = 0;

		switch (ak881x->pdata->flags & AK881X_IF_MODE_MASK) {
		case AK881X_IF_MODE_BT656:
			ifmode |= 1;
			break;
		case AK881X_IF_MODE_MASTER:
			ifmode |= 2;
			break;
		case AK881X_IF_MODE_SLAVE:
		default:
			break;
		}

		dev_dbg(&client->dev, "IF mode %x\n", ifmode);

		/*
		 * "Line Blanking No." seems to be the same as the number of
		 * "black" lines on, e.g., SuperH VOU, whose default value of 20
		 * "incidentally" matches ak881x' default
		 */
		reg_write(client, AK881X_INTERFACE_MODE, ifmode | (20 << 3));
	}

	/* Hardware default: NTSC-M */
	ak881x->lines = 480;

	dev_info(&client->dev, "Detected an ak881x chip ID %x, revision %x\n",
		 data, ak881x->revision);

	return 0;
}

static int ak881x_remove(struct i2c_client *client)
{
	struct ak881x *ak881x = to_ak881x(client);

	v4l2_device_unregister_subdev(&ak881x->subdev);

	return 0;
}

static const struct i2c_device_id ak881x_id[] = {
	{ "ak8813", 0 },
	{ "ak8814", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak881x_id);

static struct i2c_driver ak881x_i2c_driver = {
	.driver = {
		.name = "ak881x",
	},
	.probe		= ak881x_probe,
	.remove		= ak881x_remove,
	.id_table	= ak881x_id,
};

module_i2c_driver(ak881x_i2c_driver);

MODULE_DESCRIPTION("TV-output driver for ak8813/ak8814");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");

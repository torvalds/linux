/***************************************************************************
 * Plug-in for PAS106B image sensor connected to the SN9C1xx PC Camera     *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2004-2007 by Luca Risolia <luca.risolia@studio.unibo.it>  *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#include <linux/delay.h>
#include "sn9c102_sensor.h"


static int pas106b_init(struct sn9c102_device* cam)
{
	int err = 0;

	err = sn9c102_write_const_regs(cam, {0x00, 0x10}, {0x00, 0x11},
				       {0x00, 0x14}, {0x20, 0x17},
				       {0x20, 0x19}, {0x09, 0x18});

	err += sn9c102_i2c_write(cam, 0x02, 0x0c);
	err += sn9c102_i2c_write(cam, 0x05, 0x5a);
	err += sn9c102_i2c_write(cam, 0x06, 0x88);
	err += sn9c102_i2c_write(cam, 0x07, 0x80);
	err += sn9c102_i2c_write(cam, 0x10, 0x06);
	err += sn9c102_i2c_write(cam, 0x11, 0x06);
	err += sn9c102_i2c_write(cam, 0x12, 0x00);
	err += sn9c102_i2c_write(cam, 0x14, 0x02);
	err += sn9c102_i2c_write(cam, 0x13, 0x01);

	msleep(400);

	return err;
}


static int pas106b_get_ctrl(struct sn9c102_device* cam,
			    struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
			int r1 = sn9c102_i2c_read(cam, 0x03),
			    r2 = sn9c102_i2c_read(cam, 0x04);
			if (r1 < 0 || r2 < 0)
				return -EIO;
			ctrl->value = (r1 << 4) | (r2 & 0x0f);
		}
		return 0;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x0c)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		return 0;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x09)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		return 0;
	case V4L2_CID_GAIN:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x0e)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		return 0;
	case V4L2_CID_CONTRAST:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x0f)) < 0)
			return -EIO;
		ctrl->value &= 0x07;
		return 0;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x0a)) < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x1f) << 1;
		return 0;
	case SN9C102_V4L2_CID_DAC_MAGNITUDE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x08)) < 0)
			return -EIO;
		ctrl->value &= 0xf8;
		return 0;
	default:
		return -EINVAL;
	}
}


static int pas106b_set_ctrl(struct sn9c102_device* cam,
			    const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x03, ctrl->value >> 4);
		err += sn9c102_i2c_write(cam, 0x04, ctrl->value & 0x0f);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_write(cam, 0x0c, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x09, ctrl->value);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x0e, ctrl->value);
		break;
	case V4L2_CID_CONTRAST:
		err += sn9c102_i2c_write(cam, 0x0f, ctrl->value);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_i2c_write(cam, 0x0a, ctrl->value >> 1);
		err += sn9c102_i2c_write(cam, 0x0b, ctrl->value >> 1);
		break;
	case SN9C102_V4L2_CID_DAC_MAGNITUDE:
		err += sn9c102_i2c_write(cam, 0x08, ctrl->value << 3);
		break;
	default:
		return -EINVAL;
	}
	err += sn9c102_i2c_write(cam, 0x13, 0x01);

	return err ? -EIO : 0;
}


static int pas106b_set_crop(struct sn9c102_device* cam,
			    const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 4,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 3;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int pas106b_set_pix_format(struct sn9c102_device* cam,
				  const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x2c, 0x17);
	else
		err += sn9c102_write_reg(cam, 0x20, 0x17);

	return err;
}


static const struct sn9c102_sensor pas106b = {
	.name = "PAS106B",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C101 | BRIDGE_SN9C102,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_400KHZ | SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x40,
	.init = &pas106b_init,
	.qctrl = {
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x125,
			.maximum = 0xfff,
			.step = 0x001,
			.default_value = 0x140,
			.flags = 0,
		},
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x0d,
			.flags = 0,
		},
		{
			.id = V4L2_CID_CONTRAST,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "contrast",
			.minimum = 0x00,
			.maximum = 0x07,
			.step = 0x01,
			.default_value = 0x00, /* 0x00~0x03 have same effect */
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x04,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x06,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x3e,
			.step = 0x02,
			.default_value = 0x02,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_DAC_MAGNITUDE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "DAC magnitude",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x01,
			.flags = 0,
		},
	},
	.get_ctrl = &pas106b_get_ctrl,
	.set_ctrl = &pas106b_set_ctrl,
	.cropcap = {
		.bounds = {
			.left = 0,
			.top = 0,
			.width = 352,
			.height = 288,
		},
		.defrect = {
			.left = 0,
			.top = 0,
			.width = 352,
			.height = 288,
		},
	},
	.set_crop = &pas106b_set_crop,
	.pix_format = {
		.width = 352,
		.height = 288,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8, /* we use this field as 'bits per pixel' */
	},
	.set_pix_format = &pas106b_set_pix_format
};


int sn9c102_probe_pas106b(struct sn9c102_device* cam)
{
	int r0 = 0, r1 = 0;
	unsigned int pid = 0;

	/*
	   Minimal initialization to enable the I2C communication
	   NOTE: do NOT change the values!
	*/
	if (sn9c102_write_const_regs(cam,
				     {0x01, 0x01}, /* sensor power down */
				     {0x00, 0x01}, /* sensor power on */
				    {0x28, 0x17})) /* sensor clock at 24 MHz */
		return -EIO;

	r0 = sn9c102_i2c_try_read(cam, &pas106b, 0x00);
	r1 = sn9c102_i2c_try_read(cam, &pas106b, 0x01);
	if (r0 < 0 || r1 < 0)
		return -EIO;

	pid = (r0 << 11) | ((r1 & 0xf0) >> 4);
	if (pid != 0x007)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &pas106b);

	return 0;
}

/***************************************************************************
 * Plug-in for HV7131D image sensor connected to the SN9C1xx PC Camera     *
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

#include "sn9c102_sensor.h"


static struct sn9c102_sensor hv7131d;


static int hv7131d_init(struct sn9c102_device* cam)
{
	int err = 0;

	err += sn9c102_write_reg(cam, 0x00, 0x10);
	err += sn9c102_write_reg(cam, 0x00, 0x11);
	err += sn9c102_write_reg(cam, 0x00, 0x14);
	err += sn9c102_write_reg(cam, 0x60, 0x17);
	err += sn9c102_write_reg(cam, 0x0e, 0x18);
	err += sn9c102_write_reg(cam, 0xf2, 0x19);

	err += sn9c102_i2c_write(cam, 0x01, 0x04);
	err += sn9c102_i2c_write(cam, 0x02, 0x00);
	err += sn9c102_i2c_write(cam, 0x28, 0x00);

	return err;
}


static int hv7131d_get_ctrl(struct sn9c102_device* cam,
			    struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
			int r1 = sn9c102_i2c_read(cam, 0x26),
			    r2 = sn9c102_i2c_read(cam, 0x27);
			if (r1 < 0 || r2 < 0)
				return -EIO;
			ctrl->value = (r1 << 8) | (r2 & 0xff);
		}
		return 0;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x31)) < 0)
			return -EIO;
		ctrl->value = 0x3f - (ctrl->value & 0x3f);
		return 0;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x33)) < 0)
			return -EIO;
		ctrl->value = 0x3f - (ctrl->value & 0x3f);
		return 0;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x32)) < 0)
			return -EIO;
		ctrl->value = 0x3f - (ctrl->value & 0x3f);
		return 0;
	case SN9C102_V4L2_CID_RESET_LEVEL:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x30)) < 0)
			return -EIO;
		ctrl->value &= 0x3f;
		return 0;
	case SN9C102_V4L2_CID_PIXEL_BIAS_VOLTAGE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x34)) < 0)
			return -EIO;
		ctrl->value &= 0x07;
		return 0;
	default:
		return -EINVAL;
	}
}


static int hv7131d_set_ctrl(struct sn9c102_device* cam,
			    const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x26, ctrl->value >> 8);
		err += sn9c102_i2c_write(cam, 0x27, ctrl->value & 0xff);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_write(cam, 0x31, 0x3f - ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x33, 0x3f - ctrl->value);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_i2c_write(cam, 0x32, 0x3f - ctrl->value);
		break;
	case SN9C102_V4L2_CID_RESET_LEVEL:
		err += sn9c102_i2c_write(cam, 0x30, ctrl->value);
		break;
	case SN9C102_V4L2_CID_PIXEL_BIAS_VOLTAGE:
		err += sn9c102_i2c_write(cam, 0x34, ctrl->value);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int hv7131d_set_crop(struct sn9c102_device* cam,
			    const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 2,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 2;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int hv7131d_set_pix_format(struct sn9c102_device* cam,
				  const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x42, 0x19);
	else
		err += sn9c102_write_reg(cam, 0xf2, 0x19);

	return err;
}


static struct sn9c102_sensor hv7131d = {
	.name = "HV7131D",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C101 | BRIDGE_SN9C102 | BRIDGE_SN9C103,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x11,
	.init = &hv7131d_init,
	.qctrl = {
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x0250,
			.maximum = 0xffff,
			.step = 0x0001,
			.default_value = 0x0250,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x20,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x1e,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_RESET_LEVEL,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "reset level",
			.minimum = 0x19,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x30,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_PIXEL_BIAS_VOLTAGE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "pixel bias voltage",
			.minimum = 0x00,
			.maximum = 0x07,
			.step = 0x01,
			.default_value = 0x02,
			.flags = 0,
		},
	},
	.get_ctrl = &hv7131d_get_ctrl,
	.set_ctrl = &hv7131d_set_ctrl,
	.cropcap = {
		.bounds = {
			.left = 0,
			.top = 0,
			.width = 640,
			.height = 480,
		},
		.defrect = {
			.left = 0,
			.top = 0,
			.width = 640,
			.height = 480,
		},
	},
	.set_crop = &hv7131d_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &hv7131d_set_pix_format
};


int sn9c102_probe_hv7131d(struct sn9c102_device* cam)
{
	int r0 = 0, r1 = 0, err = 0;

	err += sn9c102_write_reg(cam, 0x01, 0x01);
	err += sn9c102_write_reg(cam, 0x00, 0x01);
	err += sn9c102_write_reg(cam, 0x28, 0x17);
	if (err)
		return -EIO;

	r0 = sn9c102_i2c_try_read(cam, &hv7131d, 0x00);
	r1 = sn9c102_i2c_try_read(cam, &hv7131d, 0x01);
	if (r0 < 0 || r1 < 0)
		return -EIO;

	if (r0 != 0x00 && r1 != 0x04)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &hv7131d);

	return 0;
}

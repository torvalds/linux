/***************************************************************************
 * Plug-in for PAS202BCA image sensor connected to the SN9C10x PC Camera   *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2006 by Luca Risolia <luca.risolia@studio.unibo.it>       *
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


static struct sn9c102_sensor pas202bca;


static int pas202bca_init(struct sn9c102_device* cam)
{
	int err = 0;

	err += sn9c102_write_reg(cam, 0x00, 0x10);
	err += sn9c102_write_reg(cam, 0x00, 0x11);
	err += sn9c102_write_reg(cam, 0x00, 0x14);
	err += sn9c102_write_reg(cam, 0x20, 0x17);
	err += sn9c102_write_reg(cam, 0x30, 0x19);
	err += sn9c102_write_reg(cam, 0x09, 0x18);

	err += sn9c102_i2c_write(cam, 0x02, 0x14);
	err += sn9c102_i2c_write(cam, 0x03, 0x40);
	err += sn9c102_i2c_write(cam, 0x0d, 0x2c);
	err += sn9c102_i2c_write(cam, 0x0e, 0x01);
	err += sn9c102_i2c_write(cam, 0x0f, 0xa9);
	err += sn9c102_i2c_write(cam, 0x10, 0x08);
	err += sn9c102_i2c_write(cam, 0x13, 0x63);
	err += sn9c102_i2c_write(cam, 0x15, 0x70);
	err += sn9c102_i2c_write(cam, 0x11, 0x01);

	msleep(400);

	return err;
}


static int pas202bca_set_pix_format(struct sn9c102_device* cam,
				    const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x24, 0x17);
	else
		err += sn9c102_write_reg(cam, 0x20, 0x17);

	return err;
}


static int pas202bca_set_ctrl(struct sn9c102_device* cam,
			      const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x04, ctrl->value >> 6);
		err += sn9c102_i2c_write(cam, 0x05, ctrl->value & 0x3f);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_write(cam, 0x09, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x07, ctrl->value);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x10, ctrl->value);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_i2c_write(cam, 0x08, ctrl->value);
		break;
	case SN9C102_V4L2_CID_DAC_MAGNITUDE:
		err += sn9c102_i2c_write(cam, 0x0c, ctrl->value);
		break;
	default:
		return -EINVAL;
	}
	err += sn9c102_i2c_write(cam, 0x11, 0x01);

	return err ? -EIO : 0;
}


static int pas202bca_set_crop(struct sn9c102_device* cam,
			      const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = &pas202bca;
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 3,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 3;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static struct sn9c102_sensor pas202bca = {
	.name = "PAS202BCA",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_400KHZ | SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x40,
	.init = &pas202bca_init,
	.qctrl = {
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x01e5,
			.maximum = 0x3fff,
			.step = 0x0001,
			.default_value = 0x01e5,
			.flags = 0,
		},
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x0c,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x01,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x05,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_DAC_MAGNITUDE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "DAC magnitude",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x04,
			.flags = 0,
		},
	},
	.set_ctrl = &pas202bca_set_ctrl,
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
	.set_crop = &pas202bca_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &pas202bca_set_pix_format
};


int sn9c102_probe_pas202bca(struct sn9c102_device* cam)
{
	const struct usb_device_id pas202bca_id_table[] = {
		{ USB_DEVICE(0x0c45, 0x60af), },
		{ }
	};
	int err = 0;

	if (!sn9c102_match_id(cam,pas202bca_id_table))
		return -ENODEV;

	err += sn9c102_write_reg(cam, 0x01, 0x01);
	err += sn9c102_write_reg(cam, 0x40, 0x01);
	err += sn9c102_write_reg(cam, 0x28, 0x17);
	if (err)
		return -EIO;

	if (sn9c102_i2c_try_write(cam, &pas202bca, 0x10, 0)) /* try to write */
		return -ENODEV;

	sn9c102_attach_sensor(cam, &pas202bca);

	return 0;
}

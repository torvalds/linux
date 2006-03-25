/***************************************************************************
 * Plug-in for OV7630 image sensor connected to the SN9C10x PC Camera      *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2005-2006 by Luca Risolia <luca.risolia@studio.unibo.it>  *
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


static struct sn9c102_sensor ov7630;


static int ov7630_init(struct sn9c102_device* cam)
{
	int err = 0;

	err += sn9c102_write_reg(cam, 0x00, 0x14);
	err += sn9c102_write_reg(cam, 0x60, 0x17);
	err += sn9c102_write_reg(cam, 0x0f, 0x18);
	err += sn9c102_write_reg(cam, 0x50, 0x19);

	err += sn9c102_i2c_write(cam, 0x12, 0x80);
	err += sn9c102_i2c_write(cam, 0x11, 0x01);
	err += sn9c102_i2c_write(cam, 0x15, 0x34);
	err += sn9c102_i2c_write(cam, 0x16, 0x03);
	err += sn9c102_i2c_write(cam, 0x17, 0x1c);
	err += sn9c102_i2c_write(cam, 0x18, 0xbd);
	err += sn9c102_i2c_write(cam, 0x19, 0x06);
	err += sn9c102_i2c_write(cam, 0x1a, 0xf6);
	err += sn9c102_i2c_write(cam, 0x1b, 0x04);
	err += sn9c102_i2c_write(cam, 0x20, 0xf6);
	err += sn9c102_i2c_write(cam, 0x23, 0xee);
	err += sn9c102_i2c_write(cam, 0x26, 0xa0);
	err += sn9c102_i2c_write(cam, 0x27, 0x9a);
	err += sn9c102_i2c_write(cam, 0x28, 0xa0);
	err += sn9c102_i2c_write(cam, 0x29, 0x30);
	err += sn9c102_i2c_write(cam, 0x2a, 0xa0);
	err += sn9c102_i2c_write(cam, 0x2b, 0x1f);
	err += sn9c102_i2c_write(cam, 0x2f, 0x3d);
	err += sn9c102_i2c_write(cam, 0x30, 0x24);
	err += sn9c102_i2c_write(cam, 0x32, 0x86);
	err += sn9c102_i2c_write(cam, 0x60, 0xa9);
	err += sn9c102_i2c_write(cam, 0x61, 0x42);
	err += sn9c102_i2c_write(cam, 0x65, 0x00);
	err += sn9c102_i2c_write(cam, 0x69, 0x38);
	err += sn9c102_i2c_write(cam, 0x6f, 0x88);
	err += sn9c102_i2c_write(cam, 0x70, 0x0b);
	err += sn9c102_i2c_write(cam, 0x71, 0x00);
	err += sn9c102_i2c_write(cam, 0x74, 0x21);
	err += sn9c102_i2c_write(cam, 0x7d, 0xf7);

	return err;
}


static int ov7630_set_ctrl(struct sn9c102_device* cam,
			   const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x10, ctrl->value >> 2);
		err += sn9c102_i2c_write(cam, 0x76, ctrl->value & 0x03);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_write(cam, 0x02, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x01, ctrl->value);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x00, ctrl->value);
		break;
	case V4L2_CID_CONTRAST:
		err += ctrl->value ? sn9c102_i2c_write(cam, 0x05,
						       (ctrl->value-1) | 0x20)
				   : sn9c102_i2c_write(cam, 0x05, 0x00);
		break;
	case V4L2_CID_BRIGHTNESS:
		err += sn9c102_i2c_write(cam, 0x06, ctrl->value);
		break;
	case V4L2_CID_SATURATION:
		err += sn9c102_i2c_write(cam, 0x03, ctrl->value << 4);
		break;
	case V4L2_CID_HUE:
		err += ctrl->value ? sn9c102_i2c_write(cam, 0x04,
						       (ctrl->value-1) | 0x20)
				   : sn9c102_i2c_write(cam, 0x04, 0x00);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x0c, ctrl->value);
		break;
	case V4L2_CID_WHITENESS:
		err += sn9c102_i2c_write(cam, 0x0d, ctrl->value);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x12, (ctrl->value << 2) | 0x78);
		break;
	case V4L2_CID_AUTOGAIN:
		err += sn9c102_i2c_write(cam, 0x13, ctrl->value);
		break;
	case V4L2_CID_VFLIP:
		err += sn9c102_i2c_write(cam, 0x75, 0x0e | (ctrl->value << 7));
		break;
	case V4L2_CID_BLACK_LEVEL:
		err += sn9c102_i2c_write(cam, 0x25, ctrl->value);
		break;
	case SN9C102_V4L2_CID_BRIGHT_LEVEL:
		err += sn9c102_i2c_write(cam, 0x24, ctrl->value);
		break;
	case SN9C102_V4L2_CID_GAMMA:
		err += sn9c102_i2c_write(cam, 0x14, (ctrl->value << 2) | 0x80);
		break;
	case SN9C102_V4L2_CID_BAND_FILTER:
		err += sn9c102_i2c_write(cam, 0x2d, ctrl->value << 2);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int ov7630_set_crop(struct sn9c102_device* cam,
			   const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = &ov7630;
	int err = 0;
	u8 v_start = (u8)(rect->top - s->cropcap.bounds.top) + 1;

	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int ov7630_set_pix_format(struct sn9c102_device* cam,
				 const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x20, 0x19);
	else
		err += sn9c102_write_reg(cam, 0x50, 0x19);

	return err;
}


static struct sn9c102_sensor ov7630 = {
	.name = "OV7630",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.sysfs_ops = SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x21,
	.init = &ov7630_init,
	.qctrl = {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x14,
			.flags = 0,
		},
		{
			.id = V4L2_CID_HUE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "hue",
			.minimum = 0x00,
			.maximum = 0x1f+1,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_SATURATION,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "saturation",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x08,
			.flags = 0,
		},
		{
			.id = V4L2_CID_CONTRAST,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "contrast",
			.minimum = 0x00,
			.maximum = 0x1f+1,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x000,
			.maximum = 0x3ff,
			.step = 0x001,
			.default_value = 0x83<<2,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x3a,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x77,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "brightness",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0xa0,
			.flags = 0,
		},
		{
			.id = V4L2_CID_DO_WHITE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "white balance background: blue",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x20,
			.flags = 0,
		},
		{
			.id = V4L2_CID_WHITENESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "white balance background: red",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x20,
			.flags = 0,
		},
		{
			.id = V4L2_CID_AUTO_WHITE_BALANCE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "auto white balance",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x01,
			.flags = 0,
		},
		{
			.id = V4L2_CID_AUTOGAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "gain & exposure mode",
			.minimum = 0x00,
			.maximum = 0x03,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_VFLIP,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "vertical flip",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x01,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLACK_LEVEL,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "black pixel ratio",
			.minimum = 0x01,
			.maximum = 0x9a,
			.step = 0x01,
			.default_value = 0x8a,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_BRIGHT_LEVEL,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "bright pixel ratio",
			.minimum = 0x01,
			.maximum = 0x9a,
			.step = 0x01,
			.default_value = 0x10,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_BAND_FILTER,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "band filter",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GAMMA,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "rgb gamma",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
	},
	.set_ctrl = &ov7630_set_ctrl,
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
	.set_crop = &ov7630_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &ov7630_set_pix_format
};


int sn9c102_probe_ov7630(struct sn9c102_device* cam)
{
	const struct usb_device_id ov7630_id_table[] = {
		{ USB_DEVICE(0x0c45, 0x602c), },
		{ USB_DEVICE(0x0c45, 0x602d), },
		{ USB_DEVICE(0x0c45, 0x608f), },
		{ USB_DEVICE(0x0c45, 0x60b0), },
		{ }
	};
	int err = 0;

	if (!sn9c102_match_id(cam, ov7630_id_table))
		return -ENODEV;

	err += sn9c102_write_reg(cam, 0x01, 0x01);
	err += sn9c102_write_reg(cam, 0x00, 0x01);
	err += sn9c102_write_reg(cam, 0x28, 0x17);
	if (err)
		return -EIO;

	err += sn9c102_i2c_try_write(cam, &ov7630, 0x0b, 0);
	if (err)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &ov7630);

	return 0;
}

/***************************************************************************
 * Plug-in for HV7131R image sensor connected to the SN9C1xx PC Camera     *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2007 by Luca Risolia <luca.risolia@studio.unibo.it>       *
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


static int hv7131r_init(struct sn9c102_device* cam)
{
	int err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C103:
		err += sn9c102_write_reg(cam, 0x00, 0x03);
		err += sn9c102_write_reg(cam, 0x1a, 0x04);
		err += sn9c102_write_reg(cam, 0x20, 0x05);
		err += sn9c102_write_reg(cam, 0x20, 0x06);
		err += sn9c102_write_reg(cam, 0x03, 0x10);
		err += sn9c102_write_reg(cam, 0x00, 0x14);
		err += sn9c102_write_reg(cam, 0x60, 0x17);
		err += sn9c102_write_reg(cam, 0x0a, 0x18);
		err += sn9c102_write_reg(cam, 0xf0, 0x19);
		err += sn9c102_write_reg(cam, 0x1d, 0x1a);
		err += sn9c102_write_reg(cam, 0x10, 0x1b);
		err += sn9c102_write_reg(cam, 0x02, 0x1c);
		err += sn9c102_write_reg(cam, 0x03, 0x1d);
		err += sn9c102_write_reg(cam, 0x0f, 0x1e);
		err += sn9c102_write_reg(cam, 0x0c, 0x1f);
		err += sn9c102_write_reg(cam, 0x00, 0x20);
		err += sn9c102_write_reg(cam, 0x10, 0x21);
		err += sn9c102_write_reg(cam, 0x20, 0x22);
		err += sn9c102_write_reg(cam, 0x30, 0x23);
		err += sn9c102_write_reg(cam, 0x40, 0x24);
		err += sn9c102_write_reg(cam, 0x50, 0x25);
		err += sn9c102_write_reg(cam, 0x60, 0x26);
		err += sn9c102_write_reg(cam, 0x70, 0x27);
		err += sn9c102_write_reg(cam, 0x80, 0x28);
		err += sn9c102_write_reg(cam, 0x90, 0x29);
		err += sn9c102_write_reg(cam, 0xa0, 0x2a);
		err += sn9c102_write_reg(cam, 0xb0, 0x2b);
		err += sn9c102_write_reg(cam, 0xc0, 0x2c);
		err += sn9c102_write_reg(cam, 0xd0, 0x2d);
		err += sn9c102_write_reg(cam, 0xe0, 0x2e);
		err += sn9c102_write_reg(cam, 0xf0, 0x2f);
		err += sn9c102_write_reg(cam, 0xff, 0x30);
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		err += sn9c102_write_reg(cam, 0x44, 0x01);
		err += sn9c102_write_reg(cam, 0x40, 0x02);
		err += sn9c102_write_reg(cam, 0x00, 0x03);
		err += sn9c102_write_reg(cam, 0x1a, 0x04);
		err += sn9c102_write_reg(cam, 0x44, 0x05);
		err += sn9c102_write_reg(cam, 0x3e, 0x06);
		err += sn9c102_write_reg(cam, 0x1a, 0x07);
		err += sn9c102_write_reg(cam, 0x03, 0x10);
		err += sn9c102_write_reg(cam, 0x08, 0x14);
		err += sn9c102_write_reg(cam, 0xa3, 0x17);
		err += sn9c102_write_reg(cam, 0x4b, 0x18);
		err += sn9c102_write_reg(cam, 0x00, 0x19);
		err += sn9c102_write_reg(cam, 0x1d, 0x1a);
		err += sn9c102_write_reg(cam, 0x10, 0x1b);
		err += sn9c102_write_reg(cam, 0x02, 0x1c);
		err += sn9c102_write_reg(cam, 0x03, 0x1d);
		err += sn9c102_write_reg(cam, 0x0f, 0x1e);
		err += sn9c102_write_reg(cam, 0x0c, 0x1f);
		err += sn9c102_write_reg(cam, 0x00, 0x20);
		err += sn9c102_write_reg(cam, 0x29, 0x21);
		err += sn9c102_write_reg(cam, 0x40, 0x22);
		err += sn9c102_write_reg(cam, 0x54, 0x23);
		err += sn9c102_write_reg(cam, 0x66, 0x24);
		err += sn9c102_write_reg(cam, 0x76, 0x25);
		err += sn9c102_write_reg(cam, 0x85, 0x26);
		err += sn9c102_write_reg(cam, 0x94, 0x27);
		err += sn9c102_write_reg(cam, 0xa1, 0x28);
		err += sn9c102_write_reg(cam, 0xae, 0x29);
		err += sn9c102_write_reg(cam, 0xbb, 0x2a);
		err += sn9c102_write_reg(cam, 0xc7, 0x2b);
		err += sn9c102_write_reg(cam, 0xd3, 0x2c);
		err += sn9c102_write_reg(cam, 0xde, 0x2d);
		err += sn9c102_write_reg(cam, 0xea, 0x2e);
		err += sn9c102_write_reg(cam, 0xf4, 0x2f);
		err += sn9c102_write_reg(cam, 0xff, 0x30);
		err += sn9c102_write_reg(cam, 0x00, 0x3F);
		err += sn9c102_write_reg(cam, 0xC7, 0x40);
		err += sn9c102_write_reg(cam, 0x01, 0x41);
		err += sn9c102_write_reg(cam, 0x44, 0x42);
		err += sn9c102_write_reg(cam, 0x00, 0x43);
		err += sn9c102_write_reg(cam, 0x44, 0x44);
		err += sn9c102_write_reg(cam, 0x00, 0x45);
		err += sn9c102_write_reg(cam, 0x44, 0x46);
		err += sn9c102_write_reg(cam, 0x00, 0x47);
		err += sn9c102_write_reg(cam, 0xC7, 0x48);
		err += sn9c102_write_reg(cam, 0x01, 0x49);
		err += sn9c102_write_reg(cam, 0xC7, 0x4A);
		err += sn9c102_write_reg(cam, 0x01, 0x4B);
		err += sn9c102_write_reg(cam, 0xC7, 0x4C);
		err += sn9c102_write_reg(cam, 0x01, 0x4D);
		err += sn9c102_write_reg(cam, 0x44, 0x4E);
		err += sn9c102_write_reg(cam, 0x00, 0x4F);
		err += sn9c102_write_reg(cam, 0x44, 0x50);
		err += sn9c102_write_reg(cam, 0x00, 0x51);
		err += sn9c102_write_reg(cam, 0x44, 0x52);
		err += sn9c102_write_reg(cam, 0x00, 0x53);
		err += sn9c102_write_reg(cam, 0xC7, 0x54);
		err += sn9c102_write_reg(cam, 0x01, 0x55);
		err += sn9c102_write_reg(cam, 0xC7, 0x56);
		err += sn9c102_write_reg(cam, 0x01, 0x57);
		err += sn9c102_write_reg(cam, 0xC7, 0x58);
		err += sn9c102_write_reg(cam, 0x01, 0x59);
		err += sn9c102_write_reg(cam, 0x44, 0x5A);
		err += sn9c102_write_reg(cam, 0x00, 0x5B);
		err += sn9c102_write_reg(cam, 0x44, 0x5C);
		err += sn9c102_write_reg(cam, 0x00, 0x5D);
		err += sn9c102_write_reg(cam, 0x44, 0x5E);
		err += sn9c102_write_reg(cam, 0x00, 0x5F);
		err += sn9c102_write_reg(cam, 0xC7, 0x60);
		err += sn9c102_write_reg(cam, 0x01, 0x61);
		err += sn9c102_write_reg(cam, 0xC7, 0x62);
		err += sn9c102_write_reg(cam, 0x01, 0x63);
		err += sn9c102_write_reg(cam, 0xC7, 0x64);
		err += sn9c102_write_reg(cam, 0x01, 0x65);
		err += sn9c102_write_reg(cam, 0x44, 0x66);
		err += sn9c102_write_reg(cam, 0x00, 0x67);
		err += sn9c102_write_reg(cam, 0x44, 0x68);
		err += sn9c102_write_reg(cam, 0x00, 0x69);
		err += sn9c102_write_reg(cam, 0x44, 0x6A);
		err += sn9c102_write_reg(cam, 0x00, 0x6B);
		err += sn9c102_write_reg(cam, 0xC7, 0x6C);
		err += sn9c102_write_reg(cam, 0x01, 0x6D);
		err += sn9c102_write_reg(cam, 0xC7, 0x6E);
		err += sn9c102_write_reg(cam, 0x01, 0x6F);
		err += sn9c102_write_reg(cam, 0xC7, 0x70);
		err += sn9c102_write_reg(cam, 0x01, 0x71);
		err += sn9c102_write_reg(cam, 0x44, 0x72);
		err += sn9c102_write_reg(cam, 0x00, 0x73);
		err += sn9c102_write_reg(cam, 0x44, 0x74);
		err += sn9c102_write_reg(cam, 0x00, 0x75);
		err += sn9c102_write_reg(cam, 0x44, 0x76);
		err += sn9c102_write_reg(cam, 0x00, 0x77);
		err += sn9c102_write_reg(cam, 0xC7, 0x78);
		err += sn9c102_write_reg(cam, 0x01, 0x79);
		err += sn9c102_write_reg(cam, 0xC7, 0x7A);
		err += sn9c102_write_reg(cam, 0x01, 0x7B);
		err += sn9c102_write_reg(cam, 0xC7, 0x7C);
		err += sn9c102_write_reg(cam, 0x01, 0x7D);
		err += sn9c102_write_reg(cam, 0x44, 0x7E);
		err += sn9c102_write_reg(cam, 0x00, 0x7F);
		err += sn9c102_write_reg(cam, 0x14, 0x84);
		err += sn9c102_write_reg(cam, 0x00, 0x85);
		err += sn9c102_write_reg(cam, 0x27, 0x86);
		err += sn9c102_write_reg(cam, 0x00, 0x87);
		err += sn9c102_write_reg(cam, 0x07, 0x88);
		err += sn9c102_write_reg(cam, 0x00, 0x89);
		err += sn9c102_write_reg(cam, 0xEC, 0x8A);
		err += sn9c102_write_reg(cam, 0x0f, 0x8B);
		err += sn9c102_write_reg(cam, 0xD8, 0x8C);
		err += sn9c102_write_reg(cam, 0x0f, 0x8D);
		err += sn9c102_write_reg(cam, 0x3D, 0x8E);
		err += sn9c102_write_reg(cam, 0x00, 0x8F);
		err += sn9c102_write_reg(cam, 0x3D, 0x90);
		err += sn9c102_write_reg(cam, 0x00, 0x91);
		err += sn9c102_write_reg(cam, 0xCD, 0x92);
		err += sn9c102_write_reg(cam, 0x0f, 0x93);
		err += sn9c102_write_reg(cam, 0xf7, 0x94);
		err += sn9c102_write_reg(cam, 0x0f, 0x95);
		err += sn9c102_write_reg(cam, 0x0C, 0x96);
		err += sn9c102_write_reg(cam, 0x00, 0x97);
		err += sn9c102_write_reg(cam, 0x00, 0x98);
		err += sn9c102_write_reg(cam, 0x66, 0x99);
		err += sn9c102_write_reg(cam, 0x05, 0x9A);
		err += sn9c102_write_reg(cam, 0x00, 0x9B);
		err += sn9c102_write_reg(cam, 0x04, 0x9C);
		err += sn9c102_write_reg(cam, 0x00, 0x9D);
		err += sn9c102_write_reg(cam, 0x08, 0x9E);
		err += sn9c102_write_reg(cam, 0x00, 0x9F);
		err += sn9c102_write_reg(cam, 0x2D, 0xC0);
		err += sn9c102_write_reg(cam, 0x2D, 0xC1);
		err += sn9c102_write_reg(cam, 0x3A, 0xC2);
		err += sn9c102_write_reg(cam, 0x05, 0xC3);
		err += sn9c102_write_reg(cam, 0x04, 0xC4);
		err += sn9c102_write_reg(cam, 0x3F, 0xC5);
		err += sn9c102_write_reg(cam, 0x00, 0xC6);
		err += sn9c102_write_reg(cam, 0x00, 0xC7);
		err += sn9c102_write_reg(cam, 0x50, 0xC8);
		err += sn9c102_write_reg(cam, 0x3C, 0xC9);
		err += sn9c102_write_reg(cam, 0x28, 0xCA);
		err += sn9c102_write_reg(cam, 0xD8, 0xCB);
		err += sn9c102_write_reg(cam, 0x14, 0xCC);
		err += sn9c102_write_reg(cam, 0xEC, 0xCD);
		err += sn9c102_write_reg(cam, 0x32, 0xCE);
		err += sn9c102_write_reg(cam, 0xDD, 0xCF);
		err += sn9c102_write_reg(cam, 0x32, 0xD0);
		err += sn9c102_write_reg(cam, 0xDD, 0xD1);
		err += sn9c102_write_reg(cam, 0x6A, 0xD2);
		err += sn9c102_write_reg(cam, 0x50, 0xD3);
		err += sn9c102_write_reg(cam, 0x00, 0xD4);
		err += sn9c102_write_reg(cam, 0x00, 0xD5);
		err += sn9c102_write_reg(cam, 0x00, 0xD6);
		break;
	default:
		break;
	}

	err += sn9c102_i2c_write(cam, 0x20, 0x00);
	err += sn9c102_i2c_write(cam, 0x21, 0xd6);
	err += sn9c102_i2c_write(cam, 0x25, 0x06);

	return err;
}


static int hv7131r_get_ctrl(struct sn9c102_device* cam,
			    struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x30)) < 0)
			return -EIO;
		return 0;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x31)) < 0)
			return -EIO;
		ctrl->value = ctrl->value & 0x3f;
		return 0;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x33)) < 0)
			return -EIO;
		ctrl->value = ctrl->value & 0x3f;
		return 0;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x32)) < 0)
			return -EIO;
		ctrl->value = ctrl->value & 0x3f;
		return 0;
	case V4L2_CID_BLACK_LEVEL:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x01)) < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x08) ? 1 : 0;
		return 0;
	default:
		return -EINVAL;
	}
}


static int hv7131r_set_ctrl(struct sn9c102_device* cam,
			    const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x30, ctrl->value);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_write(cam, 0x31, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x33, ctrl->value);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_i2c_write(cam, 0x32, ctrl->value);
		break;
	case V4L2_CID_BLACK_LEVEL:
		{
			int r = sn9c102_i2c_read(cam, 0x01);
			if (r < 0)
				return -EIO;
			err += sn9c102_i2c_write(cam, 0x01,
						 (ctrl->value<<3) | (r&0xf7));
		}
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int hv7131r_set_crop(struct sn9c102_device* cam,
			    const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 1,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 1;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int hv7131r_set_pix_format(struct sn9c102_device* cam,
				  const struct v4l2_pix_format* pix)
{
	int err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C103:
		if (pix->pixelformat == V4L2_PIX_FMT_SBGGR8) {
			err += sn9c102_write_reg(cam, 0xa0, 0x19);
			err += sn9c102_i2c_write(cam, 0x01, 0x04);
		} else {
			err += sn9c102_write_reg(cam, 0x30, 0x19);
			err += sn9c102_i2c_write(cam, 0x01, 0x04);
		}
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (pix->pixelformat == V4L2_PIX_FMT_SBGGR8) {
			err += sn9c102_write_reg(cam, 0xa5, 0x17);
			err += sn9c102_i2c_write(cam, 0x01, 0x24);
		} else {
			err += sn9c102_write_reg(cam, 0xa3, 0x17);
			err += sn9c102_i2c_write(cam, 0x01, 0x04);
		}
		break;
	default:
		break;
	}

	return err;
}


static struct sn9c102_sensor hv7131r = {
	.name = "HV7131R",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C103 | BRIDGE_SN9C105 | BRIDGE_SN9C120,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x11,
	.init = &hv7131r_init,
	.qctrl = {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x40,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x08,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x1a,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x3f,
			.step = 0x01,
			.default_value = 0x2f,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLACK_LEVEL,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "auto black level compensation",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
	},
	.get_ctrl = &hv7131r_get_ctrl,
	.set_ctrl = &hv7131r_set_ctrl,
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
	.set_crop = &hv7131r_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &hv7131r_set_pix_format
};


int sn9c102_probe_hv7131r(struct sn9c102_device* cam)
{
	int devid, err = 0;

	err += sn9c102_write_reg(cam, 0x09, 0x01);
	err += sn9c102_write_reg(cam, 0x44, 0x02);
	err += sn9c102_write_reg(cam, 0x34, 0x01);
	err += sn9c102_write_reg(cam, 0x20, 0x17);
	err += sn9c102_write_reg(cam, 0x34, 0x01);
	err += sn9c102_write_reg(cam, 0x46, 0x01);
	if (err)
		return -EIO;

	devid = sn9c102_i2c_try_read(cam, &hv7131r, 0x00);
	if (devid < 0)
		return -EIO;

	if (devid != 0x02)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &hv7131r);

	return 0;
}

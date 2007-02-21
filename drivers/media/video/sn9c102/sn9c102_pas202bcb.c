/***************************************************************************
 * Plug-in for PAS202BCB image sensor connected to the SN9C1xx PC Camera   *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2004 by Carlos Eduardo Medaglia Dyonisio                  *
 *                       <medaglia@undl.org.br>                            *
 *                       http://cadu.homelinux.com:8080/                   *
 *                                                                         *
 * Support for SN9C103, DAC Magnitude, exposure and green gain controls    *
 * added by Luca Risolia <luca.risolia@studio.unibo.it>                    *
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


static struct sn9c102_sensor pas202bcb;


static int pas202bcb_init(struct sn9c102_device* cam)
{
	int err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	err += sn9c102_write_reg(cam, 0x00, 0x10);
	err += sn9c102_write_reg(cam, 0x00, 0x11);
	err += sn9c102_write_reg(cam, 0x00, 0x14);
	err += sn9c102_write_reg(cam, 0x20, 0x17);
	err += sn9c102_write_reg(cam, 0x30, 0x19);
	err += sn9c102_write_reg(cam, 0x09, 0x18);
		break;
	case BRIDGE_SN9C103:
		err += sn9c102_write_reg(cam, 0x00, 0x02);
		err += sn9c102_write_reg(cam, 0x00, 0x03);
		err += sn9c102_write_reg(cam, 0x1a, 0x04);
		err += sn9c102_write_reg(cam, 0x20, 0x05);
		err += sn9c102_write_reg(cam, 0x20, 0x06);
		err += sn9c102_write_reg(cam, 0x20, 0x07);
		err += sn9c102_write_reg(cam, 0x00, 0x10);
		err += sn9c102_write_reg(cam, 0x00, 0x11);
		err += sn9c102_write_reg(cam, 0x00, 0x14);
		err += sn9c102_write_reg(cam, 0x20, 0x17);
		err += sn9c102_write_reg(cam, 0x30, 0x19);
		err += sn9c102_write_reg(cam, 0x09, 0x18);
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
	default:
		break;
	}

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


static int pas202bcb_get_ctrl(struct sn9c102_device* cam,
			      struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
			int r1 = sn9c102_i2c_read(cam, 0x04),
			    r2 = sn9c102_i2c_read(cam, 0x05);
			if (r1 < 0 || r2 < 0)
				return -EIO;
			ctrl->value = (r1 << 6) | (r2 & 0x3f);
		}
		return 0;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x09)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x07)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case V4L2_CID_GAIN:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x10)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		return 0;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x08)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case SN9C102_V4L2_CID_DAC_MAGNITUDE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x0c)) < 0)
			return -EIO;
		return 0;
	default:
		return -EINVAL;
	}
}


static int pas202bcb_set_pix_format(struct sn9c102_device* cam,
				    const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x28, 0x17);
	else
		err += sn9c102_write_reg(cam, 0x20, 0x17);

	return err;
}


static int pas202bcb_set_ctrl(struct sn9c102_device* cam,
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


static int pas202bcb_set_crop(struct sn9c102_device* cam,
			      const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = 0,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 3;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		h_start = (u8)(rect->left - s->cropcap.bounds.left) + 4;
		break;
	case BRIDGE_SN9C103:
		h_start = (u8)(rect->left - s->cropcap.bounds.left) + 3;
		break;
	default:
		break;
	}

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static struct sn9c102_sensor pas202bcb = {
	.name = "PAS202BCB",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C101 | BRIDGE_SN9C102 | BRIDGE_SN9C103,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_400KHZ | SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x40,
	.init = &pas202bcb_init,
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
			.default_value = 0x0b,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x00,
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
	.get_ctrl = &pas202bcb_get_ctrl,
	.set_ctrl = &pas202bcb_set_ctrl,
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
	.set_crop = &pas202bcb_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &pas202bcb_set_pix_format
};


int sn9c102_probe_pas202bcb(struct sn9c102_device* cam)
{
	int r0 = 0, r1 = 0, err = 0;
	unsigned int pid = 0;

	/*
	 *  Minimal initialization to enable the I2C communication
	 *  NOTE: do NOT change the values!
	 */
	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		err += sn9c102_write_reg(cam, 0x01, 0x01); /* power down */
		err += sn9c102_write_reg(cam, 0x40, 0x01); /* power on */
		err += sn9c102_write_reg(cam, 0x28, 0x17); /* clock 24 MHz */
		break;
	case BRIDGE_SN9C103: /* do _not_ change anything! */
		err += sn9c102_write_reg(cam, 0x09, 0x01);
		err += sn9c102_write_reg(cam, 0x44, 0x01);
		err += sn9c102_write_reg(cam, 0x44, 0x02);
		err += sn9c102_write_reg(cam, 0x29, 0x17);
		break;
	default:
		break;
	}

	r0 = sn9c102_i2c_try_read(cam, &pas202bcb, 0x00);
	r1 = sn9c102_i2c_try_read(cam, &pas202bcb, 0x01);

	if (err || r0 < 0 || r1 < 0)
		return -EIO;

	pid = (r0 << 4) | ((r1 & 0xf0) >> 4);
	if (pid != 0x017)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &pas202bcb);

	return 0;
}

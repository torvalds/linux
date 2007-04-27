/***************************************************************************
 * Plug-in for MI-0343 image sensor connected to the SN9C1xx PC Camera     *
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


static int mi0343_init(struct sn9c102_device* cam)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;

	err = sn9c102_write_const_regs(cam, {0x00, 0x10}, {0x00, 0x11},
				       {0x0a, 0x14}, {0x40, 0x01},
				       {0x20, 0x17}, {0x07, 0x18},
				       {0xa0, 0x19});

	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x0d,
					 0x00, 0x01, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x0d,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x03,
					 0x01, 0xe1, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x04,
					 0x02, 0x81, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x05,
					 0x00, 0x17, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x06,
					 0x00, 0x11, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x62,
					 0x04, 0x9a, 0, 0);

	return err;
}


static int mi0343_get_ctrl(struct sn9c102_device* cam,
			   struct v4l2_control* ctrl)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	u8 data[5+1];

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x09,
					     2+1, data) < 0)
			return -EIO;
		ctrl->value = data[2];
		return 0;
	case V4L2_CID_GAIN:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x35,
					     2+1, data) < 0)
			return -EIO;
		break;
	case V4L2_CID_HFLIP:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x20,
					     2+1, data) < 0)
			return -EIO;
		ctrl->value = data[3] & 0x20 ? 1 : 0;
		return 0;
	case V4L2_CID_VFLIP:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x20,
					     2+1, data) < 0)
			return -EIO;
		ctrl->value = data[3] & 0x80 ? 1 : 0;
		return 0;
	case V4L2_CID_RED_BALANCE:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x2d,
					     2+1, data) < 0)
			return -EIO;
		break;
	case V4L2_CID_BLUE_BALANCE:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x2c,
					     2+1, data) < 0)
			return -EIO;
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x2e,
					     2+1, data) < 0)
			return -EIO;
		break;
	default:
		return -EINVAL;
	}

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		ctrl->value = data[3] | (data[2] << 8);
		if (ctrl->value >= 0x10 && ctrl->value <= 0x3f)
			ctrl->value -= 0x10;
		else if (ctrl->value >= 0x60 && ctrl->value <= 0x7f)
			ctrl->value -= 0x60;
		else if (ctrl->value >= 0xe0 && ctrl->value <= 0xff)
			ctrl->value -= 0xe0;
	}

	return 0;
}


static int mi0343_set_ctrl(struct sn9c102_device* cam,
			   const struct v4l2_control* ctrl)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	u16 reg = 0;
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if (ctrl->value <= (0x3f-0x10))
			reg = 0x10 + ctrl->value;
		else if (ctrl->value <= ((0x3f-0x10) + (0x7f-0x60)))
			reg = 0x60 + (ctrl->value - (0x3f-0x10));
		else
			reg = 0xe0 + (ctrl->value - (0x3f-0x10) - (0x7f-0x60));
		break;
	}

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x09, ctrl->value, 0x00,
						 0, 0);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x35, reg >> 8, reg & 0xff,
						 0, 0);
		break;
	case V4L2_CID_HFLIP:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x20, ctrl->value ? 0x40:0x00,
						 ctrl->value ? 0x20:0x00,
						 0, 0);
		break;
	case V4L2_CID_VFLIP:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x20, ctrl->value ? 0x80:0x00,
						 ctrl->value ? 0x80:0x00,
						 0, 0);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x2d, reg >> 8, reg & 0xff,
						 0, 0);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x2c, reg >> 8, reg & 0xff,
						 0, 0);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x2b, reg >> 8, reg & 0xff,
						 0, 0);
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x2e, reg >> 8, reg & 0xff,
						 0, 0);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int mi0343_set_crop(struct sn9c102_device* cam,
			    const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 0,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 2;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int mi0343_set_pix_format(struct sn9c102_device* cam,
				 const struct v4l2_pix_format* pix)
{
	struct sn9c102_sensor* s = sn9c102_get_sensor(cam);
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X) {
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x0a, 0x00, 0x03, 0, 0);
		err += sn9c102_write_reg(cam, 0x20, 0x19);
	} else {
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x0a, 0x00, 0x05, 0, 0);
		err += sn9c102_write_reg(cam, 0xa0, 0x19);
	}

	return err;
}


static struct sn9c102_sensor mi0343 = {
	.name = "MI-0343",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C101 | BRIDGE_SN9C102,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x5d,
	.init = &mi0343_init,
	.qctrl = {
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x06,
			.flags = 0,
		},
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = (0x3f-0x10)+(0x7f-0x60)+(0xff-0xe0),/*0x6d*/
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_HFLIP,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "horizontal mirror",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
			.flags = 0,
		},
		{
			.id = V4L2_CID_VFLIP,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "vertical mirror",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = (0x3f-0x10)+(0x7f-0x60)+(0xff-0xe0),
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = (0x3f-0x10)+(0x7f-0x60)+(0xff-0xe0),
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = ((0x3f-0x10)+(0x7f-0x60)+(0xff-0xe0)),
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
	},
	.get_ctrl = &mi0343_get_ctrl,
	.set_ctrl = &mi0343_set_ctrl,
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
	.set_crop = &mi0343_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &mi0343_set_pix_format
};


int sn9c102_probe_mi0343(struct sn9c102_device* cam)
{
	u8 data[5+1];
	int err = 0;

	err = sn9c102_write_const_regs(cam, {0x01, 0x01}, {0x00, 0x01},
				       {0x28, 0x17});

	if (err)
		return -EIO;

	if (sn9c102_i2c_try_raw_read(cam, &mi0343, mi0343.i2c_slave_id, 0x00,
				     2, data) < 0)
		return -EIO;

	if (data[4] != 0x32 || data[3] != 0xe3)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &mi0343);

	return 0;
}

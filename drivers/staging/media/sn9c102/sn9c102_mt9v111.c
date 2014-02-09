/***************************************************************************
 * Plug-in for MT9V111 image sensor connected to the SN9C1xx PC Camera     *
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
#include "sn9c102_devtable.h"


static int mt9v111_init(struct sn9c102_device *cam)
{
	struct sn9c102_sensor *s = sn9c102_get_sensor(cam);
	int err = 0;

	err = sn9c102_write_const_regs(cam, {0x44, 0x01}, {0x40, 0x02},
				       {0x00, 0x03}, {0x1a, 0x04},
				       {0x1f, 0x05}, {0x20, 0x06},
				       {0x1f, 0x07}, {0x81, 0x08},
				       {0x5c, 0x09}, {0x00, 0x0a},
				       {0x00, 0x0b}, {0x00, 0x0c},
				       {0x00, 0x0d}, {0x00, 0x0e},
				       {0x00, 0x0f}, {0x03, 0x10},
				       {0x00, 0x11}, {0x00, 0x12},
				       {0x02, 0x13}, {0x14, 0x14},
				       {0x28, 0x15}, {0x1e, 0x16},
				       {0xe2, 0x17}, {0x06, 0x18},
				       {0x00, 0x19}, {0x00, 0x1a},
				       {0x00, 0x1b}, {0x08, 0x20},
				       {0x39, 0x21}, {0x51, 0x22},
				       {0x63, 0x23}, {0x73, 0x24},
				       {0x82, 0x25}, {0x8f, 0x26},
				       {0x9b, 0x27}, {0xa7, 0x28},
				       {0xb1, 0x29}, {0xbc, 0x2a},
				       {0xc6, 0x2b}, {0xcf, 0x2c},
				       {0xd8, 0x2d}, {0xe1, 0x2e},
				       {0xea, 0x2f}, {0xf2, 0x30},
				       {0x13, 0x84}, {0x00, 0x85},
				       {0x25, 0x86}, {0x00, 0x87},
				       {0x07, 0x88}, {0x00, 0x89},
				       {0xee, 0x8a}, {0x0f, 0x8b},
				       {0xe5, 0x8c}, {0x0f, 0x8d},
				       {0x2e, 0x8e}, {0x00, 0x8f},
				       {0x30, 0x90}, {0x00, 0x91},
				       {0xd4, 0x92}, {0x0f, 0x93},
				       {0xfc, 0x94}, {0x0f, 0x95},
				       {0x14, 0x96}, {0x00, 0x97},
				       {0x00, 0x98}, {0x60, 0x99},
				       {0x07, 0x9a}, {0x40, 0x9b},
				       {0x20, 0x9c}, {0x00, 0x9d},
				       {0x00, 0x9e}, {0x00, 0x9f},
				       {0x2d, 0xc0}, {0x2d, 0xc1},
				       {0x3a, 0xc2}, {0x05, 0xc3},
				       {0x04, 0xc4}, {0x3f, 0xc5},
				       {0x00, 0xc6}, {0x00, 0xc7},
				       {0x50, 0xc8}, {0x3c, 0xc9},
				       {0x28, 0xca}, {0xd8, 0xcb},
				       {0x14, 0xcc}, {0xec, 0xcd},
				       {0x32, 0xce}, {0xdd, 0xcf},
				       {0x2d, 0xd0}, {0xdd, 0xd1},
				       {0x6a, 0xd2}, {0x50, 0xd3},
				       {0x60, 0xd4}, {0x00, 0xd5},
				       {0x00, 0xd6});

	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x01,
					 0x00, 0x01, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x0d,
					 0x00, 0x01, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x0d,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x08,
					 0x04, 0x80, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x01,
					 0x00, 0x04, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x08,
					 0x00, 0x08, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x02,
					 0x00, 0x16, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x03,
					 0x01, 0xe7, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x04,
					 0x02, 0x87, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x06,
					 0x00, 0x40, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x05,
					 0x00, 0x09, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x07,
					 0x30, 0x02, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x0c,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x12,
					 0x00, 0xb0, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x13,
					 0x00, 0x7c, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x1e,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x20,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x20,
					 0x00, 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id, 0x01,
					 0x00, 0x04, 0, 0);

	return err;
}

static int mt9v111_get_ctrl(struct sn9c102_device *cam,
			    struct v4l2_control *ctrl)
{
	struct sn9c102_sensor *s = sn9c102_get_sensor(cam);
	u8 data[2];
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (sn9c102_i2c_try_raw_read(cam, s, s->i2c_slave_id, 0x20, 2,
					     data) < 0)
			return -EIO;
		ctrl->value = data[1] & 0x80 ? 1 : 0;
		return 0;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}

static int mt9v111_set_ctrl(struct sn9c102_device *cam,
			    const struct v4l2_control *ctrl)
{
	struct sn9c102_sensor *s = sn9c102_get_sensor(cam);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		err += sn9c102_i2c_try_raw_write(cam, s, 4, s->i2c_slave_id,
						 0x20,
						 ctrl->value ? 0x80 : 0x00,
						 ctrl->value ? 0x80 : 0x00, 0,
						 0);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}

static int mt9v111_set_crop(struct sn9c102_device *cam,
			    const struct v4l2_rect *rect)
{
	struct sn9c102_sensor *s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 v_start = (u8) (rect->top - s->cropcap.bounds.top) + 2;

	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}

static int mt9v111_set_pix_format(struct sn9c102_device *cam,
				  const struct v4l2_pix_format *pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SBGGR8) {
		err += sn9c102_write_reg(cam, 0xb4, 0x17);
	} else {
		err += sn9c102_write_reg(cam, 0xe2, 0x17);
	}

	return err;
}


static const struct sn9c102_sensor mt9v111 = {
	.name = "MT9V111",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C105 | BRIDGE_SN9C120,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x5c,
	.init = &mt9v111_init,
	.qctrl = {
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
	},
	.get_ctrl = &mt9v111_get_ctrl,
	.set_ctrl = &mt9v111_set_ctrl,
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
	.set_crop = &mt9v111_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &mt9v111_set_pix_format
};


int sn9c102_probe_mt9v111(struct sn9c102_device *cam)
{
	u8 data[2];
	int err = 0;

	err += sn9c102_write_const_regs(cam, {0x01, 0xf1}, {0x00, 0xf1},
					{0x29, 0x01}, {0x42, 0x17},
					{0x62, 0x17}, {0x08, 0x01});
	err += sn9c102_i2c_try_raw_write(cam, &mt9v111, 4,
					 mt9v111.i2c_slave_id, 0x01, 0x00,
					 0x04, 0, 0);
	if (err || sn9c102_i2c_try_raw_read(cam, &mt9v111,
					    mt9v111.i2c_slave_id, 0x36, 2,
					    data) < 0)
		return -EIO;

	if (data[0] != 0x82 || data[1] != 0x3a)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &mt9v111);

	return 0;
}

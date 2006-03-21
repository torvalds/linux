/***************************************************************************
 * Plug-in for TAS5130D1B image sensor connected to the ET61X[12]51        *
 * PC Camera Controllers                                                   *
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

#include "et61x251_sensor.h"


static int tas5130d1b_init(struct et61x251_device* cam)
{
	int err = 0;

	err += et61x251_write_reg(cam, 0x14, 0x01);
	err += et61x251_write_reg(cam, 0x1b, 0x02);
	err += et61x251_write_reg(cam, 0x02, 0x12);
	err += et61x251_write_reg(cam, 0x0e, 0x60);
	err += et61x251_write_reg(cam, 0x80, 0x61);
	err += et61x251_write_reg(cam, 0xf0, 0x62);
	err += et61x251_write_reg(cam, 0x03, 0x63);
	err += et61x251_write_reg(cam, 0x14, 0x64);
	err += et61x251_write_reg(cam, 0xf4, 0x65);
	err += et61x251_write_reg(cam, 0x01, 0x66);
	err += et61x251_write_reg(cam, 0x05, 0x67);
	err += et61x251_write_reg(cam, 0x8f, 0x68);
	err += et61x251_write_reg(cam, 0x0f, 0x8d);
	err += et61x251_write_reg(cam, 0x08, 0x8e);

	return err;
}


static int tas5130d1b_set_ctrl(struct et61x251_device* cam,
                               const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		err += et61x251_i2c_raw_write(cam, 2, 0x20,
		                              0xf6-ctrl->value, 0, 0, 0,
		                              0, 0, 0, 0);
		break;
	case V4L2_CID_EXPOSURE:
		err += et61x251_i2c_raw_write(cam, 2, 0x40,
		                              0x47-ctrl->value, 0, 0, 0,
		                              0, 0, 0, 0);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static struct et61x251_sensor tas5130d1b = {
	.name = "TAS5130D1B",
	.interface = ET61X251_I2C_3WIRES,
	.rsta = ET61X251_I2C_RSTA_STOP,
	.active_pixel = {
		.left = 106,
		.top = 13,
	},
	.init = &tas5130d1b_init,
	.qctrl = {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0xf6,
			.step = 0x02,
			.default_value = 0x0d,
			.flags = 0,
		},
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x00,
			.maximum = 0x47,
			.step = 0x01,
			.default_value = 0x23,
			.flags = 0,
		},
	},
	.set_ctrl = &tas5130d1b_set_ctrl,
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
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
};


int et61x251_probe_tas5130d1b(struct et61x251_device* cam)
{
	const struct usb_device_id tas5130d1b_id_table[] = {
		{ USB_DEVICE(0x102c, 0x6251), },
		{ }
	};

	/* Sensor detection is based on USB pid/vid */
	if (!et61x251_match_id(cam, tas5130d1b_id_table))
		return -ENODEV;

	et61x251_attach_sensor(cam, &tas5130d1b);

	return 0;
}

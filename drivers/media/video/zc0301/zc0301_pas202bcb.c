/***************************************************************************
 * Plug-in for PAS202BCB image sensor connected to the ZC030! Image        *
 * Processor and Control Chip                                              *
 *                                                                         *
 * Copyright (C) 2006 by Luca Risolia <luca.risolia@studio.unibo.it>       *
 *                                                                         *
 * Initialization values of the ZC0301 have been taken from the SPCA5XX    *
 * driver maintained by Michel Xhaard <mxhaard@magic.fr>                   *
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

/*
   NOTE: Sensor controls are disabled for now, becouse changing them while
         streaming sometimes results in out-of-sync video frames. We'll use
         the default initialization, until we know how to stop and start video
         in the chip. However, the image quality still looks good under various
         light conditions.
*/

#include <linux/delay.h>
#include "zc0301_sensor.h"


static struct zc0301_sensor pas202bcb;


static int pas202bcb_init(struct zc0301_device* cam)
{
	int err = 0;

	err += zc0301_write_reg(cam, 0x0002, 0x00);
	err += zc0301_write_reg(cam, 0x0003, 0x02);
	err += zc0301_write_reg(cam, 0x0004, 0x80);
	err += zc0301_write_reg(cam, 0x0005, 0x01);
	err += zc0301_write_reg(cam, 0x0006, 0xE0);
	err += zc0301_write_reg(cam, 0x0098, 0x00);
	err += zc0301_write_reg(cam, 0x009A, 0x03);
	err += zc0301_write_reg(cam, 0x011A, 0x00);
	err += zc0301_write_reg(cam, 0x011C, 0x03);
	err += zc0301_write_reg(cam, 0x009B, 0x01);
	err += zc0301_write_reg(cam, 0x009C, 0xE6);
	err += zc0301_write_reg(cam, 0x009D, 0x02);
	err += zc0301_write_reg(cam, 0x009E, 0x86);

	err += zc0301_i2c_write(cam, 0x02, 0x02);
	err += zc0301_i2c_write(cam, 0x0A, 0x01);
	err += zc0301_i2c_write(cam, 0x0B, 0x01);
	err += zc0301_i2c_write(cam, 0x0D, 0x00);
	err += zc0301_i2c_write(cam, 0x12, 0x05);
	err += zc0301_i2c_write(cam, 0x13, 0x63);
	err += zc0301_i2c_write(cam, 0x15, 0x70);

	err += zc0301_write_reg(cam, 0x0101, 0xB7);
	err += zc0301_write_reg(cam, 0x0100, 0x0D);
	err += zc0301_write_reg(cam, 0x0189, 0x06);
	err += zc0301_write_reg(cam, 0x01AD, 0x00);
	err += zc0301_write_reg(cam, 0x01C5, 0x03);
	err += zc0301_write_reg(cam, 0x01CB, 0x13);
	err += zc0301_write_reg(cam, 0x0250, 0x08);
	err += zc0301_write_reg(cam, 0x0301, 0x08);
	err += zc0301_write_reg(cam, 0x018D, 0x70);
	err += zc0301_write_reg(cam, 0x0008, 0x03);
	err += zc0301_write_reg(cam, 0x01C6, 0x04);
	err += zc0301_write_reg(cam, 0x01CB, 0x07);
	err += zc0301_write_reg(cam, 0x0120, 0x11);
	err += zc0301_write_reg(cam, 0x0121, 0x37);
	err += zc0301_write_reg(cam, 0x0122, 0x58);
	err += zc0301_write_reg(cam, 0x0123, 0x79);
	err += zc0301_write_reg(cam, 0x0124, 0x91);
	err += zc0301_write_reg(cam, 0x0125, 0xA6);
	err += zc0301_write_reg(cam, 0x0126, 0xB8);
	err += zc0301_write_reg(cam, 0x0127, 0xC7);
	err += zc0301_write_reg(cam, 0x0128, 0xD3);
	err += zc0301_write_reg(cam, 0x0129, 0xDE);
	err += zc0301_write_reg(cam, 0x012A, 0xE6);
	err += zc0301_write_reg(cam, 0x012B, 0xED);
	err += zc0301_write_reg(cam, 0x012C, 0xF3);
	err += zc0301_write_reg(cam, 0x012D, 0xF8);
	err += zc0301_write_reg(cam, 0x012E, 0xFB);
	err += zc0301_write_reg(cam, 0x012F, 0xFF);
	err += zc0301_write_reg(cam, 0x0130, 0x26);
	err += zc0301_write_reg(cam, 0x0131, 0x23);
	err += zc0301_write_reg(cam, 0x0132, 0x20);
	err += zc0301_write_reg(cam, 0x0133, 0x1C);
	err += zc0301_write_reg(cam, 0x0134, 0x16);
	err += zc0301_write_reg(cam, 0x0135, 0x13);
	err += zc0301_write_reg(cam, 0x0136, 0x10);
	err += zc0301_write_reg(cam, 0x0137, 0x0D);
	err += zc0301_write_reg(cam, 0x0138, 0x0B);
	err += zc0301_write_reg(cam, 0x0139, 0x09);
	err += zc0301_write_reg(cam, 0x013A, 0x07);
	err += zc0301_write_reg(cam, 0x013B, 0x06);
	err += zc0301_write_reg(cam, 0x013C, 0x05);
	err += zc0301_write_reg(cam, 0x013D, 0x04);
	err += zc0301_write_reg(cam, 0x013E, 0x03);
	err += zc0301_write_reg(cam, 0x013F, 0x02);
	err += zc0301_write_reg(cam, 0x010A, 0x4C);
	err += zc0301_write_reg(cam, 0x010B, 0xF5);
	err += zc0301_write_reg(cam, 0x010C, 0xFF);
	err += zc0301_write_reg(cam, 0x010D, 0xF9);
	err += zc0301_write_reg(cam, 0x010E, 0x51);
	err += zc0301_write_reg(cam, 0x010F, 0xF5);
	err += zc0301_write_reg(cam, 0x0110, 0xFB);
	err += zc0301_write_reg(cam, 0x0111, 0xED);
	err += zc0301_write_reg(cam, 0x0112, 0x5F);
	err += zc0301_write_reg(cam, 0x0180, 0x00);
	err += zc0301_write_reg(cam, 0x0019, 0x00);
	err += zc0301_write_reg(cam, 0x0087, 0x20);
	err += zc0301_write_reg(cam, 0x0088, 0x21);

	err += zc0301_i2c_write(cam, 0x20, 0x02);
	err += zc0301_i2c_write(cam, 0x21, 0x1B);
	err += zc0301_i2c_write(cam, 0x03, 0x44);
	err += zc0301_i2c_write(cam, 0x0E, 0x01);
	err += zc0301_i2c_write(cam, 0x0F, 0x00);

	err += zc0301_write_reg(cam, 0x01A9, 0x14);
	err += zc0301_write_reg(cam, 0x01AA, 0x24);
	err += zc0301_write_reg(cam, 0x0190, 0x00);
	err += zc0301_write_reg(cam, 0x0191, 0x02);
	err += zc0301_write_reg(cam, 0x0192, 0x1B);
	err += zc0301_write_reg(cam, 0x0195, 0x00);
	err += zc0301_write_reg(cam, 0x0196, 0x00);
	err += zc0301_write_reg(cam, 0x0197, 0x4D);
	err += zc0301_write_reg(cam, 0x018C, 0x10);
	err += zc0301_write_reg(cam, 0x018F, 0x20);
	err += zc0301_write_reg(cam, 0x001D, 0x44);
	err += zc0301_write_reg(cam, 0x001E, 0x6F);
	err += zc0301_write_reg(cam, 0x001F, 0xAD);
	err += zc0301_write_reg(cam, 0x0020, 0xEB);
	err += zc0301_write_reg(cam, 0x0087, 0x0F);
	err += zc0301_write_reg(cam, 0x0088, 0x0E);
	err += zc0301_write_reg(cam, 0x0180, 0x40);
	err += zc0301_write_reg(cam, 0x0192, 0x1B);
	err += zc0301_write_reg(cam, 0x0191, 0x02);
	err += zc0301_write_reg(cam, 0x0190, 0x00);
	err += zc0301_write_reg(cam, 0x0116, 0x1D);
	err += zc0301_write_reg(cam, 0x0117, 0x40);
	err += zc0301_write_reg(cam, 0x0118, 0x99);
	err += zc0301_write_reg(cam, 0x0180, 0x42);
	err += zc0301_write_reg(cam, 0x0116, 0x1D);
	err += zc0301_write_reg(cam, 0x0117, 0x40);
	err += zc0301_write_reg(cam, 0x0118, 0x99);
	err += zc0301_write_reg(cam, 0x0007, 0x00);

	err += zc0301_i2c_write(cam, 0x11, 0x01);

	msleep(100);

	return err;
}


static int pas202bcb_get_ctrl(struct zc0301_device* cam,
                              struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
			int r1 = zc0301_i2c_read(cam, 0x04, 1),
			    r2 = zc0301_i2c_read(cam, 0x05, 1);
			if (r1 < 0 || r2 < 0)
				return -EIO;
			ctrl->value = (r1 << 6) | (r2 & 0x3f);
		}
		return 0;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = zc0301_i2c_read(cam, 0x09, 1)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = zc0301_i2c_read(cam, 0x07, 1)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case V4L2_CID_GAIN:
		if ((ctrl->value = zc0301_i2c_read(cam, 0x10, 1)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		return 0;
	case ZC0301_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = zc0301_i2c_read(cam, 0x08, 1)) < 0)
			return -EIO;
		ctrl->value &= 0x0f;
		return 0;
	case ZC0301_V4L2_CID_DAC_MAGNITUDE:
		if ((ctrl->value = zc0301_i2c_read(cam, 0x0c, 1)) < 0)
			return -EIO;
		return 0;
	default:
		return -EINVAL;
	}
}


static int pas202bcb_set_ctrl(struct zc0301_device* cam,
                              const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += zc0301_i2c_write(cam, 0x04, ctrl->value >> 6);
		err += zc0301_i2c_write(cam, 0x05, ctrl->value & 0x3f);
		break;
	case V4L2_CID_RED_BALANCE:
		err += zc0301_i2c_write(cam, 0x09, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += zc0301_i2c_write(cam, 0x07, ctrl->value);
		break;
	case V4L2_CID_GAIN:
		err += zc0301_i2c_write(cam, 0x10, ctrl->value);
		break;
	case ZC0301_V4L2_CID_GREEN_BALANCE:
		err += zc0301_i2c_write(cam, 0x08, ctrl->value);
		break;
	case ZC0301_V4L2_CID_DAC_MAGNITUDE:
		err += zc0301_i2c_write(cam, 0x0c, ctrl->value);
		break;
	default:
		return -EINVAL;
	}
	err += zc0301_i2c_write(cam, 0x11, 0x01);

	return err ? -EIO : 0;
}


static struct zc0301_sensor pas202bcb = {
	.name = "PAS202BCB",
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
			.flags = V4L2_CTRL_FLAG_DISABLED,
		},
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x0c,
			.flags = V4L2_CTRL_FLAG_DISABLED,
		},
		{
			.id = ZC0301_V4L2_CID_DAC_MAGNITUDE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "DAC magnitude",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x00,
			.flags = V4L2_CTRL_FLAG_DISABLED,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x01,
			.flags = V4L2_CTRL_FLAG_DISABLED,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x05,
			.flags = V4L2_CTRL_FLAG_DISABLED,
		},
		{
			.id = ZC0301_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x0f,
			.step = 0x01,
			.default_value = 0x00,
			.flags = V4L2_CTRL_FLAG_DISABLED,
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
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_JPEG,
		.priv = 8,
	},
};


int zc0301_probe_pas202bcb(struct zc0301_device* cam)
{
	int r0 = 0, r1 = 0, err = 0;
	unsigned int pid = 0;

	err += zc0301_write_reg(cam, 0x0000, 0x01);
	err += zc0301_write_reg(cam, 0x0010, 0x0e);
	err += zc0301_write_reg(cam, 0x0001, 0x01);
	err += zc0301_write_reg(cam, 0x0012, 0x03);
	err += zc0301_write_reg(cam, 0x0012, 0x01);
	err += zc0301_write_reg(cam, 0x008d, 0x08);

	msleep(10);

	r0 = zc0301_i2c_read(cam, 0x00, 1);
	r1 = zc0301_i2c_read(cam, 0x01, 1);

	if (r0 < 0 || r1 < 0 || err)
		return -EIO;

	pid = (r0 << 4) | ((r1 & 0xf0) >> 4);
	if (pid != 0x017)
		return -ENODEV;

	zc0301_attach_sensor(cam, &pas202bcb);

	return 0;
}

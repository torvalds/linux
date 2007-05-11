/***************************************************************************
 * Plug-in for OV7660 image sensor connected to the SN9C1xx PC Camera      *
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


static int ov7660_init(struct sn9c102_device* cam)
{
	int err = 0;

	err = sn9c102_write_const_regs(cam, {0x40, 0x02}, {0x00, 0x03},
				       {0x1a, 0x04}, {0x03, 0x10},
				       {0x08, 0x14}, {0x20, 0x17},
				       {0x8b, 0x18}, {0x00, 0x19},
				       {0x1d, 0x1a}, {0x10, 0x1b},
				       {0x02, 0x1c}, {0x03, 0x1d},
				       {0x0f, 0x1e}, {0x0c, 0x1f},
				       {0x00, 0x20}, {0x29, 0x21},
				       {0x40, 0x22}, {0x54, 0x23},
				       {0x66, 0x24}, {0x76, 0x25},
				       {0x85, 0x26}, {0x94, 0x27},
				       {0xa1, 0x28}, {0xae, 0x29},
				       {0xbb, 0x2a}, {0xc7, 0x2b},
				       {0xd3, 0x2c}, {0xde, 0x2d},
				       {0xea, 0x2e}, {0xf4, 0x2f},
				       {0xff, 0x30}, {0x00, 0x3F},
				       {0xC7, 0x40}, {0x01, 0x41},
				       {0x44, 0x42}, {0x00, 0x43},
				       {0x44, 0x44}, {0x00, 0x45},
				       {0x44, 0x46}, {0x00, 0x47},
				       {0xC7, 0x48}, {0x01, 0x49},
				       {0xC7, 0x4A}, {0x01, 0x4B},
				       {0xC7, 0x4C}, {0x01, 0x4D},
				       {0x44, 0x4E}, {0x00, 0x4F},
				       {0x44, 0x50}, {0x00, 0x51},
				       {0x44, 0x52}, {0x00, 0x53},
				       {0xC7, 0x54}, {0x01, 0x55},
				       {0xC7, 0x56}, {0x01, 0x57},
				       {0xC7, 0x58}, {0x01, 0x59},
				       {0x44, 0x5A}, {0x00, 0x5B},
				       {0x44, 0x5C}, {0x00, 0x5D},
				       {0x44, 0x5E}, {0x00, 0x5F},
				       {0xC7, 0x60}, {0x01, 0x61},
				       {0xC7, 0x62}, {0x01, 0x63},
				       {0xC7, 0x64}, {0x01, 0x65},
				       {0x44, 0x66}, {0x00, 0x67},
				       {0x44, 0x68}, {0x00, 0x69},
				       {0x44, 0x6A}, {0x00, 0x6B},
				       {0xC7, 0x6C}, {0x01, 0x6D},
				       {0xC7, 0x6E}, {0x01, 0x6F},
				       {0xC7, 0x70}, {0x01, 0x71},
				       {0x44, 0x72}, {0x00, 0x73},
				       {0x44, 0x74}, {0x00, 0x75},
				       {0x44, 0x76}, {0x00, 0x77},
				       {0xC7, 0x78}, {0x01, 0x79},
				       {0xC7, 0x7A}, {0x01, 0x7B},
				       {0xC7, 0x7C}, {0x01, 0x7D},
				       {0x44, 0x7E}, {0x00, 0x7F},
				       {0x14, 0x84}, {0x00, 0x85},
				       {0x27, 0x86}, {0x00, 0x87},
				       {0x07, 0x88}, {0x00, 0x89},
				       {0xEC, 0x8A}, {0x0f, 0x8B},
				       {0xD8, 0x8C}, {0x0f, 0x8D},
				       {0x3D, 0x8E}, {0x00, 0x8F},
				       {0x3D, 0x90}, {0x00, 0x91},
				       {0xCD, 0x92}, {0x0f, 0x93},
				       {0xf7, 0x94}, {0x0f, 0x95},
				       {0x0C, 0x96}, {0x00, 0x97},
				       {0x00, 0x98}, {0x66, 0x99},
				       {0x05, 0x9A}, {0x00, 0x9B},
				       {0x04, 0x9C}, {0x00, 0x9D},
				       {0x08, 0x9E}, {0x00, 0x9F},
				       {0x2D, 0xC0}, {0x2D, 0xC1},
				       {0x3A, 0xC2}, {0x05, 0xC3},
				       {0x04, 0xC4}, {0x3F, 0xC5},
				       {0x00, 0xC6}, {0x00, 0xC7},
				       {0x50, 0xC8}, {0x3C, 0xC9},
				       {0x28, 0xCA}, {0xD8, 0xCB},
				       {0x14, 0xCC}, {0xEC, 0xCD},
				       {0x32, 0xCE}, {0xDD, 0xCF},
				       {0x32, 0xD0}, {0xDD, 0xD1},
				       {0x6A, 0xD2}, {0x50, 0xD3},
				       {0x00, 0xD4}, {0x00, 0xD5},
				       {0x00, 0xD6});

	err += sn9c102_i2c_write(cam, 0x12, 0x80);
	err += sn9c102_i2c_write(cam, 0x11, 0x09);
	err += sn9c102_i2c_write(cam, 0x00, 0x0A);
	err += sn9c102_i2c_write(cam, 0x01, 0x80);
	err += sn9c102_i2c_write(cam, 0x02, 0x80);
	err += sn9c102_i2c_write(cam, 0x03, 0x00);
	err += sn9c102_i2c_write(cam, 0x04, 0x00);
	err += sn9c102_i2c_write(cam, 0x05, 0x08);
	err += sn9c102_i2c_write(cam, 0x06, 0x0B);
	err += sn9c102_i2c_write(cam, 0x07, 0x00);
	err += sn9c102_i2c_write(cam, 0x08, 0x1C);
	err += sn9c102_i2c_write(cam, 0x09, 0x01);
	err += sn9c102_i2c_write(cam, 0x0A, 0x76);
	err += sn9c102_i2c_write(cam, 0x0B, 0x60);
	err += sn9c102_i2c_write(cam, 0x0C, 0x00);
	err += sn9c102_i2c_write(cam, 0x0D, 0x08);
	err += sn9c102_i2c_write(cam, 0x0E, 0x04);
	err += sn9c102_i2c_write(cam, 0x0F, 0x6F);
	err += sn9c102_i2c_write(cam, 0x10, 0x20);
	err += sn9c102_i2c_write(cam, 0x11, 0x03);
	err += sn9c102_i2c_write(cam, 0x12, 0x05);
	err += sn9c102_i2c_write(cam, 0x13, 0xC7);
	err += sn9c102_i2c_write(cam, 0x14, 0x2C);
	err += sn9c102_i2c_write(cam, 0x15, 0x00);
	err += sn9c102_i2c_write(cam, 0x16, 0x02);
	err += sn9c102_i2c_write(cam, 0x17, 0x10);
	err += sn9c102_i2c_write(cam, 0x18, 0x60);
	err += sn9c102_i2c_write(cam, 0x19, 0x02);
	err += sn9c102_i2c_write(cam, 0x1A, 0x7B);
	err += sn9c102_i2c_write(cam, 0x1B, 0x02);
	err += sn9c102_i2c_write(cam, 0x1C, 0x7F);
	err += sn9c102_i2c_write(cam, 0x1D, 0xA2);
	err += sn9c102_i2c_write(cam, 0x1E, 0x01);
	err += sn9c102_i2c_write(cam, 0x1F, 0x0E);
	err += sn9c102_i2c_write(cam, 0x20, 0x05);
	err += sn9c102_i2c_write(cam, 0x21, 0x05);
	err += sn9c102_i2c_write(cam, 0x22, 0x05);
	err += sn9c102_i2c_write(cam, 0x23, 0x05);
	err += sn9c102_i2c_write(cam, 0x24, 0x68);
	err += sn9c102_i2c_write(cam, 0x25, 0x58);
	err += sn9c102_i2c_write(cam, 0x26, 0xD4);
	err += sn9c102_i2c_write(cam, 0x27, 0x80);
	err += sn9c102_i2c_write(cam, 0x28, 0x80);
	err += sn9c102_i2c_write(cam, 0x29, 0x30);
	err += sn9c102_i2c_write(cam, 0x2A, 0x00);
	err += sn9c102_i2c_write(cam, 0x2B, 0x00);
	err += sn9c102_i2c_write(cam, 0x2C, 0x80);
	err += sn9c102_i2c_write(cam, 0x2D, 0x00);
	err += sn9c102_i2c_write(cam, 0x2E, 0x00);
	err += sn9c102_i2c_write(cam, 0x2F, 0x0E);
	err += sn9c102_i2c_write(cam, 0x30, 0x08);
	err += sn9c102_i2c_write(cam, 0x31, 0x30);
	err += sn9c102_i2c_write(cam, 0x32, 0xB4);
	err += sn9c102_i2c_write(cam, 0x33, 0x00);
	err += sn9c102_i2c_write(cam, 0x34, 0x07);
	err += sn9c102_i2c_write(cam, 0x35, 0x84);
	err += sn9c102_i2c_write(cam, 0x36, 0x00);
	err += sn9c102_i2c_write(cam, 0x37, 0x0C);
	err += sn9c102_i2c_write(cam, 0x38, 0x02);
	err += sn9c102_i2c_write(cam, 0x39, 0x43);
	err += sn9c102_i2c_write(cam, 0x3A, 0x00);
	err += sn9c102_i2c_write(cam, 0x3B, 0x0A);
	err += sn9c102_i2c_write(cam, 0x3C, 0x6C);
	err += sn9c102_i2c_write(cam, 0x3D, 0x99);
	err += sn9c102_i2c_write(cam, 0x3E, 0x0E);
	err += sn9c102_i2c_write(cam, 0x3F, 0x41);
	err += sn9c102_i2c_write(cam, 0x40, 0xC1);
	err += sn9c102_i2c_write(cam, 0x41, 0x22);
	err += sn9c102_i2c_write(cam, 0x42, 0x08);
	err += sn9c102_i2c_write(cam, 0x43, 0xF0);
	err += sn9c102_i2c_write(cam, 0x44, 0x10);
	err += sn9c102_i2c_write(cam, 0x45, 0x78);
	err += sn9c102_i2c_write(cam, 0x46, 0xA8);
	err += sn9c102_i2c_write(cam, 0x47, 0x60);
	err += sn9c102_i2c_write(cam, 0x48, 0x80);
	err += sn9c102_i2c_write(cam, 0x49, 0x00);
	err += sn9c102_i2c_write(cam, 0x4A, 0x00);
	err += sn9c102_i2c_write(cam, 0x4B, 0x00);
	err += sn9c102_i2c_write(cam, 0x4C, 0x00);
	err += sn9c102_i2c_write(cam, 0x4D, 0x00);
	err += sn9c102_i2c_write(cam, 0x4E, 0x00);
	err += sn9c102_i2c_write(cam, 0x4F, 0x46);
	err += sn9c102_i2c_write(cam, 0x50, 0x36);
	err += sn9c102_i2c_write(cam, 0x51, 0x0F);
	err += sn9c102_i2c_write(cam, 0x52, 0x17);
	err += sn9c102_i2c_write(cam, 0x53, 0x7F);
	err += sn9c102_i2c_write(cam, 0x54, 0x96);
	err += sn9c102_i2c_write(cam, 0x55, 0x40);
	err += sn9c102_i2c_write(cam, 0x56, 0x40);
	err += sn9c102_i2c_write(cam, 0x57, 0x40);
	err += sn9c102_i2c_write(cam, 0x58, 0x0F);
	err += sn9c102_i2c_write(cam, 0x59, 0xBA);
	err += sn9c102_i2c_write(cam, 0x5A, 0x9A);
	err += sn9c102_i2c_write(cam, 0x5B, 0x22);
	err += sn9c102_i2c_write(cam, 0x5C, 0xB9);
	err += sn9c102_i2c_write(cam, 0x5D, 0x9B);
	err += sn9c102_i2c_write(cam, 0x5E, 0x10);
	err += sn9c102_i2c_write(cam, 0x5F, 0xF0);
	err += sn9c102_i2c_write(cam, 0x60, 0x05);
	err += sn9c102_i2c_write(cam, 0x61, 0x60);
	err += sn9c102_i2c_write(cam, 0x62, 0x00);
	err += sn9c102_i2c_write(cam, 0x63, 0x00);
	err += sn9c102_i2c_write(cam, 0x64, 0x50);
	err += sn9c102_i2c_write(cam, 0x65, 0x30);
	err += sn9c102_i2c_write(cam, 0x66, 0x00);
	err += sn9c102_i2c_write(cam, 0x67, 0x80);
	err += sn9c102_i2c_write(cam, 0x68, 0x7A);
	err += sn9c102_i2c_write(cam, 0x69, 0x90);
	err += sn9c102_i2c_write(cam, 0x6A, 0x80);
	err += sn9c102_i2c_write(cam, 0x6B, 0x0A);
	err += sn9c102_i2c_write(cam, 0x6C, 0x30);
	err += sn9c102_i2c_write(cam, 0x6D, 0x48);
	err += sn9c102_i2c_write(cam, 0x6E, 0x80);
	err += sn9c102_i2c_write(cam, 0x6F, 0x74);
	err += sn9c102_i2c_write(cam, 0x70, 0x64);
	err += sn9c102_i2c_write(cam, 0x71, 0x60);
	err += sn9c102_i2c_write(cam, 0x72, 0x5C);
	err += sn9c102_i2c_write(cam, 0x73, 0x58);
	err += sn9c102_i2c_write(cam, 0x74, 0x54);
	err += sn9c102_i2c_write(cam, 0x75, 0x4C);
	err += sn9c102_i2c_write(cam, 0x76, 0x40);
	err += sn9c102_i2c_write(cam, 0x77, 0x38);
	err += sn9c102_i2c_write(cam, 0x78, 0x34);
	err += sn9c102_i2c_write(cam, 0x79, 0x30);
	err += sn9c102_i2c_write(cam, 0x7A, 0x2F);
	err += sn9c102_i2c_write(cam, 0x7B, 0x2B);
	err += sn9c102_i2c_write(cam, 0x7C, 0x03);
	err += sn9c102_i2c_write(cam, 0x7D, 0x07);
	err += sn9c102_i2c_write(cam, 0x7E, 0x17);
	err += sn9c102_i2c_write(cam, 0x7F, 0x34);
	err += sn9c102_i2c_write(cam, 0x80, 0x41);
	err += sn9c102_i2c_write(cam, 0x81, 0x4D);
	err += sn9c102_i2c_write(cam, 0x82, 0x58);
	err += sn9c102_i2c_write(cam, 0x83, 0x63);
	err += sn9c102_i2c_write(cam, 0x84, 0x6E);
	err += sn9c102_i2c_write(cam, 0x85, 0x77);
	err += sn9c102_i2c_write(cam, 0x86, 0x87);
	err += sn9c102_i2c_write(cam, 0x87, 0x95);
	err += sn9c102_i2c_write(cam, 0x88, 0xAF);
	err += sn9c102_i2c_write(cam, 0x89, 0xC7);
	err += sn9c102_i2c_write(cam, 0x8A, 0xDF);
	err += sn9c102_i2c_write(cam, 0x8B, 0x99);
	err += sn9c102_i2c_write(cam, 0x8C, 0x99);
	err += sn9c102_i2c_write(cam, 0x8D, 0xCF);
	err += sn9c102_i2c_write(cam, 0x8E, 0x20);
	err += sn9c102_i2c_write(cam, 0x8F, 0x26);
	err += sn9c102_i2c_write(cam, 0x90, 0x10);
	err += sn9c102_i2c_write(cam, 0x91, 0x0C);
	err += sn9c102_i2c_write(cam, 0x92, 0x25);
	err += sn9c102_i2c_write(cam, 0x93, 0x00);
	err += sn9c102_i2c_write(cam, 0x94, 0x50);
	err += sn9c102_i2c_write(cam, 0x95, 0x50);
	err += sn9c102_i2c_write(cam, 0x96, 0x00);
	err += sn9c102_i2c_write(cam, 0x97, 0x01);
	err += sn9c102_i2c_write(cam, 0x98, 0x10);
	err += sn9c102_i2c_write(cam, 0x99, 0x40);
	err += sn9c102_i2c_write(cam, 0x9A, 0x40);
	err += sn9c102_i2c_write(cam, 0x9B, 0x20);
	err += sn9c102_i2c_write(cam, 0x9C, 0x00);
	err += sn9c102_i2c_write(cam, 0x9D, 0x99);
	err += sn9c102_i2c_write(cam, 0x9E, 0x7F);
	err += sn9c102_i2c_write(cam, 0x9F, 0x00);
	err += sn9c102_i2c_write(cam, 0xA0, 0x00);
	err += sn9c102_i2c_write(cam, 0xA1, 0x00);

	return err;
}


static int ov7660_get_ctrl(struct sn9c102_device* cam,
			   struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x10)) < 0)
			return -EIO;
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if ((ctrl->value = sn9c102_read_reg(cam, 0x02)) < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x04) ? 1 : 0;
		break;
	case V4L2_CID_RED_BALANCE:
		if ((ctrl->value = sn9c102_read_reg(cam, 0x05)) < 0)
			return -EIO;
		ctrl->value &= 0x7f;
		break;
	case V4L2_CID_BLUE_BALANCE:
		if ((ctrl->value = sn9c102_read_reg(cam, 0x06)) < 0)
			return -EIO;
		ctrl->value &= 0x7f;
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if ((ctrl->value = sn9c102_read_reg(cam, 0x07)) < 0)
			return -EIO;
		ctrl->value &= 0x7f;
		break;
	case SN9C102_V4L2_CID_BAND_FILTER:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x3b)) < 0)
			return -EIO;
		ctrl->value &= 0x08;
		break;
	case V4L2_CID_GAIN:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x00)) < 0)
			return -EIO;
		ctrl->value &= 0x1f;
		break;
	case V4L2_CID_AUTOGAIN:
		if ((ctrl->value = sn9c102_i2c_read(cam, 0x13)) < 0)
			return -EIO;
		ctrl->value &= 0x01;
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int ov7660_set_ctrl(struct sn9c102_device* cam,
			   const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x10, ctrl->value);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		err += sn9c102_write_reg(cam, 0x43 | (ctrl->value << 2), 0x02);
		break;
	case V4L2_CID_RED_BALANCE:
		err += sn9c102_write_reg(cam, ctrl->value, 0x05);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_write_reg(cam, ctrl->value, 0x06);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		err += sn9c102_write_reg(cam, ctrl->value, 0x07);
		break;
	case SN9C102_V4L2_CID_BAND_FILTER:
		err += sn9c102_i2c_write(cam, ctrl->value << 3, 0x3b);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x00, 0x60 + ctrl->value);
		break;
	case V4L2_CID_AUTOGAIN:
		err += sn9c102_i2c_write(cam, 0x13, 0xc0 |
						    (ctrl->value * 0x07));
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int ov7660_set_crop(struct sn9c102_device* cam,
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


static int ov7660_set_pix_format(struct sn9c102_device* cam,
				 const struct v4l2_pix_format* pix)
{
	int r0, err = 0;

	r0 = sn9c102_pread_reg(cam, 0x01);

	if (pix->pixelformat == V4L2_PIX_FMT_JPEG) {
		err += sn9c102_write_reg(cam, r0 | 0x40, 0x01);
		err += sn9c102_write_reg(cam, 0xa2, 0x17);
		err += sn9c102_i2c_write(cam, 0x11, 0x00);
	} else {
		err += sn9c102_write_reg(cam, r0 | 0x40, 0x01);
		err += sn9c102_write_reg(cam, 0xa2, 0x17);
		err += sn9c102_i2c_write(cam, 0x11, 0x0d);
	}

	return err;
}


static const struct sn9c102_sensor ov7660 = {
	.name = "OV7660",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C105 | BRIDGE_SN9C120,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_2WIRES,
	.i2c_slave_id = 0x21,
	.init = &ov7660_init,
	.qctrl = {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0x1f,
			.step = 0x01,
			.default_value = 0x09,
			.flags = 0,
		},
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x27,
			.flags = 0,
		},
		{
			.id = V4L2_CID_DO_WHITE_BALANCE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "night mode",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x00,
			.flags = 0,
		},
		{
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x14,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x14,
			.flags = 0,
		},
		{
			.id = V4L2_CID_AUTOGAIN,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "auto adjust",
			.minimum = 0x00,
			.maximum = 0x01,
			.step = 0x01,
			.default_value = 0x01,
			.flags = 0,
		},
		{
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x14,
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
	},
	.get_ctrl = &ov7660_get_ctrl,
	.set_ctrl = &ov7660_set_ctrl,
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
	.set_crop = &ov7660_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_JPEG,
		.priv = 8,
	},
	.set_pix_format = &ov7660_set_pix_format
};


int sn9c102_probe_ov7660(struct sn9c102_device* cam)
{
	int pid, ver, err;

	err = sn9c102_write_const_regs(cam, {0x01, 0xf1}, {0x00, 0xf1},
				       {0x01, 0x01}, {0x00, 0x01},
				       {0x28, 0x17});

	pid = sn9c102_i2c_try_read(cam, &ov7660, 0x0a);
	ver = sn9c102_i2c_try_read(cam, &ov7660, 0x0b);
	if (err || pid < 0 || ver < 0)
		return -EIO;
	if (pid != 0x76 || ver != 0x60)
		return -ENODEV;

	sn9c102_attach_sensor(cam, &ov7660);

	return 0;
}

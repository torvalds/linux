/***************************************************************************
 * Plug-in for OV7630 image sensor connected to the SN9C1xx PC Camera      *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2006-2007 by Luca Risolia <luca.risolia@studio.unibo.it>  *
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


static int ov7630_init(struct sn9c102_device *cam)
{
	int err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		err = sn9c102_write_const_regs(cam, {0x00, 0x14}, {0x60, 0x17},
					       {0x0f, 0x18}, {0x50, 0x19});

		err += sn9c102_i2c_write(cam, 0x12, 0x8d);
		err += sn9c102_i2c_write(cam, 0x12, 0x0d);
		err += sn9c102_i2c_write(cam, 0x11, 0x00);
		err += sn9c102_i2c_write(cam, 0x15, 0x35);
		err += sn9c102_i2c_write(cam, 0x16, 0x03);
		err += sn9c102_i2c_write(cam, 0x17, 0x1c);
		err += sn9c102_i2c_write(cam, 0x18, 0xbd);
		err += sn9c102_i2c_write(cam, 0x19, 0x06);
		err += sn9c102_i2c_write(cam, 0x1a, 0xf6);
		err += sn9c102_i2c_write(cam, 0x1b, 0x04);
		err += sn9c102_i2c_write(cam, 0x20, 0x44);
		err += sn9c102_i2c_write(cam, 0x23, 0xee);
		err += sn9c102_i2c_write(cam, 0x26, 0xa0);
		err += sn9c102_i2c_write(cam, 0x27, 0x9a);
		err += sn9c102_i2c_write(cam, 0x28, 0x20);
		err += sn9c102_i2c_write(cam, 0x29, 0x30);
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
		break;
	case BRIDGE_SN9C103:
		err = sn9c102_write_const_regs(cam, {0x00, 0x02}, {0x00, 0x03},
					       {0x1a, 0x04}, {0x20, 0x05},
					       {0x20, 0x06}, {0x20, 0x07},
					       {0x03, 0x10}, {0x0a, 0x14},
					       {0x60, 0x17}, {0x0f, 0x18},
					       {0x50, 0x19}, {0x1d, 0x1a},
					       {0x10, 0x1b}, {0x02, 0x1c},
					       {0x03, 0x1d}, {0x0f, 0x1e},
					       {0x0c, 0x1f}, {0x00, 0x20},
					       {0x10, 0x21}, {0x20, 0x22},
					       {0x30, 0x23}, {0x40, 0x24},
					       {0x50, 0x25}, {0x60, 0x26},
					       {0x70, 0x27}, {0x80, 0x28},
					       {0x90, 0x29}, {0xa0, 0x2a},
					       {0xb0, 0x2b}, {0xc0, 0x2c},
					       {0xd0, 0x2d}, {0xe0, 0x2e},
					       {0xf0, 0x2f}, {0xff, 0x30});

		err += sn9c102_i2c_write(cam, 0x12, 0x8d);
		err += sn9c102_i2c_write(cam, 0x12, 0x0d);
		err += sn9c102_i2c_write(cam, 0x15, 0x34);
		err += sn9c102_i2c_write(cam, 0x11, 0x01);
		err += sn9c102_i2c_write(cam, 0x1b, 0x04);
		err += sn9c102_i2c_write(cam, 0x20, 0x44);
		err += sn9c102_i2c_write(cam, 0x23, 0xee);
		err += sn9c102_i2c_write(cam, 0x26, 0xa0);
		err += sn9c102_i2c_write(cam, 0x27, 0x9a);
		err += sn9c102_i2c_write(cam, 0x28, 0x20);
		err += sn9c102_i2c_write(cam, 0x29, 0x30);
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
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
	err = sn9c102_write_const_regs(cam, {0x40, 0x02}, {0x00, 0x03},
				       {0x1a, 0x04}, {0x03, 0x10},
				       {0x0a, 0x14}, {0xe2, 0x17},
				       {0x0b, 0x18}, {0x00, 0x19},
				       {0x1d, 0x1a}, {0x10, 0x1b},
				       {0x02, 0x1c}, {0x03, 0x1d},
				       {0x0f, 0x1e}, {0x0c, 0x1f},
				       {0x00, 0x20}, {0x24, 0x21},
				       {0x3b, 0x22}, {0x47, 0x23},
				       {0x60, 0x24}, {0x71, 0x25},
				       {0x80, 0x26}, {0x8f, 0x27},
				       {0x9d, 0x28}, {0xaa, 0x29},
				       {0xb8, 0x2a}, {0xc4, 0x2b},
				       {0xd1, 0x2c}, {0xdd, 0x2d},
				       {0xe8, 0x2e}, {0xf4, 0x2f},
				       {0xff, 0x30}, {0x00, 0x3f},
				       {0xc7, 0x40}, {0x01, 0x41},
				       {0x44, 0x42}, {0x00, 0x43},
				       {0x44, 0x44}, {0x00, 0x45},
				       {0x44, 0x46}, {0x00, 0x47},
				       {0xc7, 0x48}, {0x01, 0x49},
				       {0xc7, 0x4a}, {0x01, 0x4b},
				       {0xc7, 0x4c}, {0x01, 0x4d},
				       {0x44, 0x4e}, {0x00, 0x4f},
				       {0x44, 0x50}, {0x00, 0x51},
				       {0x44, 0x52}, {0x00, 0x53},
				       {0xc7, 0x54}, {0x01, 0x55},
				       {0xc7, 0x56}, {0x01, 0x57},
				       {0xc7, 0x58}, {0x01, 0x59},
				       {0x44, 0x5a}, {0x00, 0x5b},
				       {0x44, 0x5c}, {0x00, 0x5d},
				       {0x44, 0x5e}, {0x00, 0x5f},
				       {0xc7, 0x60}, {0x01, 0x61},
				       {0xc7, 0x62}, {0x01, 0x63},
				       {0xc7, 0x64}, {0x01, 0x65},
				       {0x44, 0x66}, {0x00, 0x67},
				       {0x44, 0x68}, {0x00, 0x69},
				       {0x44, 0x6a}, {0x00, 0x6b},
				       {0xc7, 0x6c}, {0x01, 0x6d},
				       {0xc7, 0x6e}, {0x01, 0x6f},
				       {0xc7, 0x70}, {0x01, 0x71},
				       {0x44, 0x72}, {0x00, 0x73},
				       {0x44, 0x74}, {0x00, 0x75},
				       {0x44, 0x76}, {0x00, 0x77},
				       {0xc7, 0x78}, {0x01, 0x79},
				       {0xc7, 0x7a}, {0x01, 0x7b},
				       {0xc7, 0x7c}, {0x01, 0x7d},
				       {0x44, 0x7e}, {0x00, 0x7f},
				       {0x17, 0x84}, {0x00, 0x85},
				       {0x2e, 0x86}, {0x00, 0x87},
				       {0x09, 0x88}, {0x00, 0x89},
				       {0xe8, 0x8a}, {0x0f, 0x8b},
				       {0xda, 0x8c}, {0x0f, 0x8d},
				       {0x40, 0x8e}, {0x00, 0x8f},
				       {0x37, 0x90}, {0x00, 0x91},
				       {0xcf, 0x92}, {0x0f, 0x93},
				       {0xfa, 0x94}, {0x0f, 0x95},
				       {0x00, 0x96}, {0x00, 0x97},
				       {0x00, 0x98}, {0x66, 0x99},
				       {0x00, 0x9a}, {0x40, 0x9b},
				       {0x20, 0x9c}, {0x00, 0x9d},
				       {0x00, 0x9e}, {0x00, 0x9f},
				       {0x2d, 0xc0}, {0x2d, 0xc1},
				       {0x3a, 0xc2}, {0x00, 0xc3},
				       {0x04, 0xc4}, {0x3f, 0xc5},
				       {0x00, 0xc6}, {0x00, 0xc7},
				       {0x50, 0xc8}, {0x3c, 0xc9},
				       {0x28, 0xca}, {0xd8, 0xcb},
				       {0x14, 0xcc}, {0xec, 0xcd},
				       {0x32, 0xce}, {0xdd, 0xcf},
				       {0x32, 0xd0}, {0xdd, 0xd1},
				       {0x6a, 0xd2}, {0x50, 0xd3},
				       {0x60, 0xd4}, {0x00, 0xd5},
				       {0x00, 0xd6});

		err += sn9c102_i2c_write(cam, 0x12, 0x80);
		err += sn9c102_i2c_write(cam, 0x12, 0x48);
		err += sn9c102_i2c_write(cam, 0x01, 0x80);
		err += sn9c102_i2c_write(cam, 0x02, 0x80);
		err += sn9c102_i2c_write(cam, 0x03, 0x80);
		err += sn9c102_i2c_write(cam, 0x04, 0x10);
		err += sn9c102_i2c_write(cam, 0x05, 0x20);
		err += sn9c102_i2c_write(cam, 0x06, 0x80);
		err += sn9c102_i2c_write(cam, 0x11, 0x00);
		err += sn9c102_i2c_write(cam, 0x0c, 0x20);
		err += sn9c102_i2c_write(cam, 0x0d, 0x20);
		err += sn9c102_i2c_write(cam, 0x15, 0x80);
		err += sn9c102_i2c_write(cam, 0x16, 0x03);
		err += sn9c102_i2c_write(cam, 0x17, 0x1b);
		err += sn9c102_i2c_write(cam, 0x18, 0xbd);
		err += sn9c102_i2c_write(cam, 0x19, 0x05);
		err += sn9c102_i2c_write(cam, 0x1a, 0xf6);
		err += sn9c102_i2c_write(cam, 0x1b, 0x04);
		err += sn9c102_i2c_write(cam, 0x21, 0x1b);
		err += sn9c102_i2c_write(cam, 0x22, 0x00);
		err += sn9c102_i2c_write(cam, 0x23, 0xde);
		err += sn9c102_i2c_write(cam, 0x24, 0x10);
		err += sn9c102_i2c_write(cam, 0x25, 0x8a);
		err += sn9c102_i2c_write(cam, 0x26, 0xa0);
		err += sn9c102_i2c_write(cam, 0x27, 0xca);
		err += sn9c102_i2c_write(cam, 0x28, 0xa2);
		err += sn9c102_i2c_write(cam, 0x29, 0x74);
		err += sn9c102_i2c_write(cam, 0x2a, 0x88);
		err += sn9c102_i2c_write(cam, 0x2b, 0x34);
		err += sn9c102_i2c_write(cam, 0x2c, 0x88);
		err += sn9c102_i2c_write(cam, 0x2e, 0x00);
		err += sn9c102_i2c_write(cam, 0x2f, 0x00);
		err += sn9c102_i2c_write(cam, 0x30, 0x00);
		err += sn9c102_i2c_write(cam, 0x32, 0xc2);
		err += sn9c102_i2c_write(cam, 0x33, 0x08);
		err += sn9c102_i2c_write(cam, 0x4c, 0x40);
		err += sn9c102_i2c_write(cam, 0x4d, 0xf3);
		err += sn9c102_i2c_write(cam, 0x60, 0x05);
		err += sn9c102_i2c_write(cam, 0x61, 0x40);
		err += sn9c102_i2c_write(cam, 0x62, 0x12);
		err += sn9c102_i2c_write(cam, 0x63, 0x57);
		err += sn9c102_i2c_write(cam, 0x64, 0x73);
		err += sn9c102_i2c_write(cam, 0x65, 0x00);
		err += sn9c102_i2c_write(cam, 0x66, 0x55);
		err += sn9c102_i2c_write(cam, 0x67, 0x01);
		err += sn9c102_i2c_write(cam, 0x68, 0xac);
		err += sn9c102_i2c_write(cam, 0x69, 0x38);
		err += sn9c102_i2c_write(cam, 0x6f, 0x1f);
		err += sn9c102_i2c_write(cam, 0x70, 0x01);
		err += sn9c102_i2c_write(cam, 0x71, 0x00);
		err += sn9c102_i2c_write(cam, 0x72, 0x10);
		err += sn9c102_i2c_write(cam, 0x73, 0x50);
		err += sn9c102_i2c_write(cam, 0x74, 0x20);
		err += sn9c102_i2c_write(cam, 0x76, 0x01);
		err += sn9c102_i2c_write(cam, 0x77, 0xf3);
		err += sn9c102_i2c_write(cam, 0x78, 0x90);
		err += sn9c102_i2c_write(cam, 0x79, 0x98);
		err += sn9c102_i2c_write(cam, 0x7a, 0x98);
		err += sn9c102_i2c_write(cam, 0x7b, 0x00);
		err += sn9c102_i2c_write(cam, 0x7c, 0x38);
		err += sn9c102_i2c_write(cam, 0x7d, 0xff);
		break;
	default:
		break;
	}

	return err;
}


static int ov7630_get_ctrl(struct sn9c102_device *cam,
			   struct v4l2_control *ctrl)
{
	enum sn9c102_bridge bridge = sn9c102_get_bridge(cam);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ctrl->value = sn9c102_i2c_read(cam, 0x10);
		if (ctrl->value < 0)
			return -EIO;
		break;
	case V4L2_CID_RED_BALANCE:
		if (bridge == BRIDGE_SN9C105 || bridge == BRIDGE_SN9C120)
			ctrl->value = sn9c102_pread_reg(cam, 0x05);
		else
			ctrl->value = sn9c102_pread_reg(cam, 0x07);
		break;
	case V4L2_CID_BLUE_BALANCE:
		ctrl->value = sn9c102_pread_reg(cam, 0x06);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if (bridge == BRIDGE_SN9C105 || bridge == BRIDGE_SN9C120)
			ctrl->value = sn9c102_pread_reg(cam, 0x07);
		else
			ctrl->value = sn9c102_pread_reg(cam, 0x05);
		break;
		break;
	case V4L2_CID_GAIN:
		ctrl->value = sn9c102_i2c_read(cam, 0x00);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value &= 0x3f;
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		ctrl->value = sn9c102_i2c_read(cam, 0x0c);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value &= 0x3f;
		break;
	case V4L2_CID_WHITENESS:
		ctrl->value = sn9c102_i2c_read(cam, 0x0d);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value &= 0x3f;
		break;
	case V4L2_CID_AUTOGAIN:
		ctrl->value = sn9c102_i2c_read(cam, 0x13);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value &= 0x01;
		break;
	case V4L2_CID_VFLIP:
		ctrl->value = sn9c102_i2c_read(cam, 0x75);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x80) ? 1 : 0;
		break;
	case SN9C102_V4L2_CID_GAMMA:
		ctrl->value = sn9c102_i2c_read(cam, 0x14);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x02) ? 1 : 0;
		break;
	case SN9C102_V4L2_CID_BAND_FILTER:
		ctrl->value = sn9c102_i2c_read(cam, 0x2d);
		if (ctrl->value < 0)
			return -EIO;
		ctrl->value = (ctrl->value & 0x02) ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int ov7630_set_ctrl(struct sn9c102_device *cam,
			   const struct v4l2_control *ctrl)
{
	enum sn9c102_bridge bridge = sn9c102_get_bridge(cam);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		err += sn9c102_i2c_write(cam, 0x10, ctrl->value);
		break;
	case V4L2_CID_RED_BALANCE:
		if (bridge == BRIDGE_SN9C105 || bridge == BRIDGE_SN9C120)
			err += sn9c102_write_reg(cam, ctrl->value, 0x05);
		else
			err += sn9c102_write_reg(cam, ctrl->value, 0x07);
		break;
	case V4L2_CID_BLUE_BALANCE:
		err += sn9c102_write_reg(cam, ctrl->value, 0x06);
		break;
	case SN9C102_V4L2_CID_GREEN_BALANCE:
		if (bridge == BRIDGE_SN9C105 || bridge == BRIDGE_SN9C120)
			err += sn9c102_write_reg(cam, ctrl->value, 0x07);
		else
			err += sn9c102_write_reg(cam, ctrl->value, 0x05);
		break;
	case V4L2_CID_GAIN:
		err += sn9c102_i2c_write(cam, 0x00, ctrl->value);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		err += sn9c102_i2c_write(cam, 0x0c, ctrl->value);
		break;
	case V4L2_CID_WHITENESS:
		err += sn9c102_i2c_write(cam, 0x0d, ctrl->value);
		break;
	case V4L2_CID_AUTOGAIN:
		err += sn9c102_i2c_write(cam, 0x13, ctrl->value |
						    (ctrl->value << 1));
		break;
	case V4L2_CID_VFLIP:
		err += sn9c102_i2c_write(cam, 0x75, 0x0e | (ctrl->value << 7));
		break;
	case SN9C102_V4L2_CID_GAMMA:
		err += sn9c102_i2c_write(cam, 0x14, ctrl->value << 2);
		break;
	case SN9C102_V4L2_CID_BAND_FILTER:
		err += sn9c102_i2c_write(cam, 0x2d, ctrl->value << 2);
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int ov7630_set_crop(struct sn9c102_device *cam,
			   const struct v4l2_rect *rect)
{
	struct sn9c102_sensor *s = sn9c102_get_sensor(cam);
	int err = 0;
	u8 h_start = 0, v_start = (u8)(rect->top - s->cropcap.bounds.top) + 1;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	case BRIDGE_SN9C103:
		h_start = (u8)(rect->left - s->cropcap.bounds.left) + 1;
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		h_start = (u8)(rect->left - s->cropcap.bounds.left) + 4;
		break;
	default:
		break;
	}

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	return err;
}


static int ov7630_set_pix_format(struct sn9c102_device *cam,
				 const struct v4l2_pix_format *pix)
{
	int err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	case BRIDGE_SN9C103:
		if (pix->pixelformat == V4L2_PIX_FMT_SBGGR8)
			err += sn9c102_write_reg(cam, 0x50, 0x19);
		else
			err += sn9c102_write_reg(cam, 0x20, 0x19);
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (pix->pixelformat == V4L2_PIX_FMT_SBGGR8) {
			err += sn9c102_write_reg(cam, 0xe5, 0x17);
			err += sn9c102_i2c_write(cam, 0x11, 0x04);
		} else {
			err += sn9c102_write_reg(cam, 0xe2, 0x17);
			err += sn9c102_i2c_write(cam, 0x11, 0x02);
		}
		break;
	default:
		break;
	}

	return err;
}


static const struct sn9c102_sensor ov7630 = {
	.name = "OV7630",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.supported_bridge = BRIDGE_SN9C101 | BRIDGE_SN9C102 | BRIDGE_SN9C103 |
			    BRIDGE_SN9C105 | BRIDGE_SN9C120,
	.sysfs_ops = SN9C102_I2C_READ | SN9C102_I2C_WRITE,
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
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "exposure",
			.minimum = 0x00,
			.maximum = 0xff,
			.step = 0x01,
			.default_value = 0x60,
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
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "red balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x20,
			.flags = 0,
		},
		{
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "blue balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x20,
			.flags = 0,
		},
		{
			.id = V4L2_CID_AUTOGAIN,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "auto adjust",
			.minimum = 0x00,
			.maximum = 0x01,
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
			.id = SN9C102_V4L2_CID_GREEN_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "green balance",
			.minimum = 0x00,
			.maximum = 0x7f,
			.step = 0x01,
			.default_value = 0x20,
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
	.get_ctrl = &ov7630_get_ctrl,
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
		.pixelformat = V4L2_PIX_FMT_SN9C10X,
		.priv = 8,
	},
	.set_pix_format = &ov7630_set_pix_format
};


int sn9c102_probe_ov7630(struct sn9c102_device *cam)
{
	int pid, ver, err = 0;

	switch (sn9c102_get_bridge(cam)) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		err = sn9c102_write_const_regs(cam, {0x01, 0x01}, {0x00, 0x01},
					       {0x28, 0x17});
		break;
	case BRIDGE_SN9C103: /* do _not_ change anything! */
		err = sn9c102_write_const_regs(cam, {0x09, 0x01}, {0x42, 0x01},
					       {0x28, 0x17}, {0x44, 0x02});
		pid = sn9c102_i2c_try_read(cam, &ov7630, 0x0a);
		if (err || pid < 0) /* try a different initialization */
			err += sn9c102_write_const_regs(cam, {0x01, 0x01},
							{0x00, 0x01});
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		err = sn9c102_write_const_regs(cam, {0x01, 0xf1}, {0x00, 0xf1},
					       {0x29, 0x01}, {0x74, 0x02},
					       {0x0e, 0x01}, {0x44, 0x01});
		break;
	default:
		break;
	}

	pid = sn9c102_i2c_try_read(cam, &ov7630, 0x0a);
	ver = sn9c102_i2c_try_read(cam, &ov7630, 0x0b);
	if (err || pid < 0 || ver < 0)
		return -EIO;
	if (pid != 0x76 || ver != 0x31)
		return -ENODEV;
	sn9c102_attach_sensor(cam, &ov7630);

	return 0;
}

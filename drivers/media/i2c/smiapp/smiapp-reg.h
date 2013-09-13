/*
 * drivers/media/i2c/smiapp/smiapp-reg.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __SMIAPP_REG_H_
#define __SMIAPP_REG_H_

#include "smiapp-reg-defs.h"

/* Bits for above register */
#define SMIAPP_IMAGE_ORIENTATION_HFLIP		(1 << 0)
#define SMIAPP_IMAGE_ORIENTATION_VFLIP		(1 << 1)

#define SMIAPP_DATA_TRANSFER_IF_1_CTRL_EN		(1 << 0)
#define SMIAPP_DATA_TRANSFER_IF_1_CTRL_RD_EN		(0 << 1)
#define SMIAPP_DATA_TRANSFER_IF_1_CTRL_WR_EN		(1 << 1)
#define SMIAPP_DATA_TRANSFER_IF_1_CTRL_ERR_CLEAR	(1 << 2)
#define SMIAPP_DATA_TRANSFER_IF_1_STATUS_RD_READY	(1 << 0)
#define SMIAPP_DATA_TRANSFER_IF_1_STATUS_WR_READY	(1 << 1)
#define SMIAPP_DATA_TRANSFER_IF_1_STATUS_EDATA		(1 << 2)
#define SMIAPP_DATA_TRANSFER_IF_1_STATUS_EUSAGE		(1 << 3)

#define SMIAPP_SOFTWARE_RESET				(1 << 0)

#define SMIAPP_FLASH_MODE_CAPABILITY_SINGLE_STROBE	(1 << 0)
#define SMIAPP_FLASH_MODE_CAPABILITY_MULTIPLE_STROBE	(1 << 1)

#define SMIAPP_DPHY_CTRL_AUTOMATIC			0
/* DPHY control based on REQUESTED_LINK_BIT_RATE_MBPS */
#define SMIAPP_DPHY_CTRL_UI				1
#define SMIAPP_DPHY_CTRL_REGISTER			2

#define SMIAPP_COMPRESSION_MODE_SIMPLE_PREDICTOR	1
#define SMIAPP_COMPRESSION_MODE_ADVANCED_PREDICTOR	2

#define SMIAPP_MODE_SELECT_SOFTWARE_STANDBY		0
#define SMIAPP_MODE_SELECT_STREAMING			1

#define SMIAPP_SCALING_MODE_NONE			0
#define SMIAPP_SCALING_MODE_HORIZONTAL			1
#define SMIAPP_SCALING_MODE_BOTH			2

#define SMIAPP_SCALING_CAPABILITY_NONE			0
#define SMIAPP_SCALING_CAPABILITY_HORIZONTAL		1
#define SMIAPP_SCALING_CAPABILITY_BOTH			2 /* horizontal/both */

/* digital crop right before scaler */
#define SMIAPP_DIGITAL_CROP_CAPABILITY_NONE		0
#define SMIAPP_DIGITAL_CROP_CAPABILITY_INPUT_CROP	1

#define SMIAPP_BINNING_CAPABILITY_NO			0
#define SMIAPP_BINNING_CAPABILITY_YES			1

/* Maximum number of binning subtypes */
#define SMIAPP_BINNING_SUBTYPES				253

#define SMIAPP_PIXEL_ORDER_GRBG				0
#define SMIAPP_PIXEL_ORDER_RGGB				1
#define SMIAPP_PIXEL_ORDER_BGGR				2
#define SMIAPP_PIXEL_ORDER_GBRG				3

#define SMIAPP_DATA_FORMAT_MODEL_TYPE_NORMAL		1
#define SMIAPP_DATA_FORMAT_MODEL_TYPE_EXTENDED		2
#define SMIAPP_DATA_FORMAT_MODEL_TYPE_NORMAL_N		8
#define SMIAPP_DATA_FORMAT_MODEL_TYPE_EXTENDED_N	16

#define SMIAPP_FRAME_FORMAT_MODEL_TYPE_2BYTE		0x01
#define SMIAPP_FRAME_FORMAT_MODEL_TYPE_4BYTE		0x02
#define SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NROWS_MASK	0x0f
#define SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NCOLS_MASK	0xf0
#define SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NCOLS_SHIFT	4

#define SMIAPP_FRAME_FORMAT_DESC_2_PIXELCODE_MASK	0xf000
#define SMIAPP_FRAME_FORMAT_DESC_2_PIXELCODE_SHIFT	12
#define SMIAPP_FRAME_FORMAT_DESC_2_PIXELS_MASK		0x0fff

#define SMIAPP_FRAME_FORMAT_DESC_4_PIXELCODE_MASK	0xf0000000
#define SMIAPP_FRAME_FORMAT_DESC_4_PIXELCODE_SHIFT	28
#define SMIAPP_FRAME_FORMAT_DESC_4_PIXELS_MASK		0x0000ffff

#define SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_EMBEDDED	1
#define SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_DUMMY	2
#define SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_BLACK	3
#define SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_DARK		4
#define SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_VISIBLE	5

#define SMIAPP_FAST_STANDBY_CTRL_COMPLETE_FRAMES	0
#define SMIAPP_FAST_STANDBY_CTRL_IMMEDIATE		1

/* Scaling N factor */
#define SMIAPP_SCALE_N					16

/* Image statistics registers */
/* Registers 0x2000 to 0x2fff are reserved for future
 * use for statistics features.
 */

/* Manufacturer Specific Registers: 0x3000 to 0x3fff
 * The manufacturer specifies these as a black box.
 */

#endif /* __SMIAPP_REG_H_ */

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <media/v4l2-device.h>
#include "vpu.h"
#include "vpu_helpers.h"

static const u8 colorprimaries[] = {
	V4L2_COLORSPACE_LAST,
	V4L2_COLORSPACE_REC709,         /*Rec. ITU-R BT.709-6*/
	0,
	0,
	V4L2_COLORSPACE_470_SYSTEM_M,   /*Rec. ITU-R BT.470-6 System M*/
	V4L2_COLORSPACE_470_SYSTEM_BG,  /*Rec. ITU-R BT.470-6 System B, G*/
	V4L2_COLORSPACE_SMPTE170M,      /*SMPTE170M*/
	V4L2_COLORSPACE_SMPTE240M,      /*SMPTE240M*/
	0,                              /*Generic film*/
	V4L2_COLORSPACE_BT2020,         /*Rec. ITU-R BT.2020-2*/
	0,                              /*SMPTE ST 428-1*/
};

static const u8 colortransfers[] = {
	V4L2_XFER_FUNC_LAST,
	V4L2_XFER_FUNC_709,             /*Rec. ITU-R BT.709-6*/
	0,
	0,
	0,                              /*Rec. ITU-R BT.470-6 System M*/
	0,                              /*Rec. ITU-R BT.470-6 System B, G*/
	V4L2_XFER_FUNC_709,             /*SMPTE170M*/
	V4L2_XFER_FUNC_SMPTE240M,       /*SMPTE240M*/
	V4L2_XFER_FUNC_NONE,            /*Linear transfer characteristics*/
	0,
	0,
	0,                              /*IEC 61966-2-4*/
	0,                              /*Rec. ITU-R BT.1361-0 extended colour gamut*/
	V4L2_XFER_FUNC_SRGB,            /*IEC 61966-2-1 sRGB or sYCC*/
	V4L2_XFER_FUNC_709,             /*Rec. ITU-R BT.2020-2 (10 bit system)*/
	V4L2_XFER_FUNC_709,             /*Rec. ITU-R BT.2020-2 (12 bit system)*/
	V4L2_XFER_FUNC_SMPTE2084,       /*SMPTE ST 2084*/
	0,                              /*SMPTE ST 428-1*/
	0                               /*Rec. ITU-R BT.2100-0 hybrid log-gamma (HLG)*/
};

static const u8 colormatrixcoefs[] = {
	V4L2_YCBCR_ENC_LAST,
	V4L2_YCBCR_ENC_709,              /*Rec. ITU-R BT.709-6*/
	0,
	0,
	0,                               /*Title 47 Code of Federal Regulations*/
	V4L2_YCBCR_ENC_601,              /*Rec. ITU-R BT.601-7 625*/
	V4L2_YCBCR_ENC_601,              /*Rec. ITU-R BT.601-7 525*/
	V4L2_YCBCR_ENC_SMPTE240M,        /*SMPTE240M*/
	0,
	V4L2_YCBCR_ENC_BT2020,           /*Rec. ITU-R BT.2020-2*/
	V4L2_YCBCR_ENC_BT2020_CONST_LUM  /*Rec. ITU-R BT.2020-2 constant*/
};

u32 vpu_color_cvrt_primaries_v2i(u32 primaries)
{
	return vpu_helper_find_in_array_u8(colorprimaries, ARRAY_SIZE(colorprimaries), primaries);
}

u32 vpu_color_cvrt_primaries_i2v(u32 primaries)
{
	return primaries < ARRAY_SIZE(colorprimaries) ? colorprimaries[primaries] : 0;
}

u32 vpu_color_cvrt_transfers_v2i(u32 transfers)
{
	return vpu_helper_find_in_array_u8(colortransfers, ARRAY_SIZE(colortransfers), transfers);
}

u32 vpu_color_cvrt_transfers_i2v(u32 transfers)
{
	return transfers < ARRAY_SIZE(colortransfers) ? colortransfers[transfers] : 0;
}

u32 vpu_color_cvrt_matrix_v2i(u32 matrix)
{
	return vpu_helper_find_in_array_u8(colormatrixcoefs, ARRAY_SIZE(colormatrixcoefs), matrix);
}

u32 vpu_color_cvrt_matrix_i2v(u32 matrix)
{
	return matrix < ARRAY_SIZE(colormatrixcoefs) ? colormatrixcoefs[matrix] : 0;
}

u32 vpu_color_cvrt_full_range_v2i(u32 full_range)
{
	return (full_range == V4L2_QUANTIZATION_FULL_RANGE);
}

u32 vpu_color_cvrt_full_range_i2v(u32 full_range)
{
	if (full_range)
		return V4L2_QUANTIZATION_FULL_RANGE;

	return V4L2_QUANTIZATION_LIM_RANGE;
}

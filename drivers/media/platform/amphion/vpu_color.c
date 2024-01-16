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

int vpu_color_check_primaries(u32 primaries)
{
	return vpu_color_cvrt_primaries_v2i(primaries) ? 0 : -EINVAL;
}

int vpu_color_check_transfers(u32 transfers)
{
	return vpu_color_cvrt_transfers_v2i(transfers) ? 0 : -EINVAL;
}

int vpu_color_check_matrix(u32 matrix)
{
	return vpu_color_cvrt_matrix_v2i(matrix) ? 0 : -EINVAL;
}

int vpu_color_check_full_range(u32 full_range)
{
	int ret = -EINVAL;

	switch (full_range) {
	case V4L2_QUANTIZATION_FULL_RANGE:
	case V4L2_QUANTIZATION_LIM_RANGE:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

int vpu_color_get_default(u32 primaries, u32 *ptransfers, u32 *pmatrix, u32 *pfull_range)
{
	u32 transfers;
	u32 matrix;
	u32 full_range;

	switch (primaries) {
	case V4L2_COLORSPACE_REC709:
		transfers = V4L2_XFER_FUNC_709;
		matrix = V4L2_YCBCR_ENC_709;
		break;
	case V4L2_COLORSPACE_470_SYSTEM_M:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
	case V4L2_COLORSPACE_SMPTE170M:
		transfers = V4L2_XFER_FUNC_709;
		matrix = V4L2_YCBCR_ENC_601;
		break;
	case V4L2_COLORSPACE_SMPTE240M:
		transfers = V4L2_XFER_FUNC_SMPTE240M;
		matrix = V4L2_YCBCR_ENC_SMPTE240M;
		break;
	case V4L2_COLORSPACE_BT2020:
		transfers = V4L2_XFER_FUNC_709;
		matrix = V4L2_YCBCR_ENC_BT2020;
		break;
	default:
		transfers = V4L2_XFER_FUNC_DEFAULT;
		matrix = V4L2_YCBCR_ENC_DEFAULT;
		break;
	}
	full_range = V4L2_QUANTIZATION_LIM_RANGE;

	if (ptransfers)
		*ptransfers = transfers;
	if (pmatrix)
		*pmatrix = matrix;
	if (pfull_range)
		*pfull_range = full_range;

	return 0;
}

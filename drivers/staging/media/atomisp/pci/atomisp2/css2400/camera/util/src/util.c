/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_util.h"
#include <ia_css_frame.h>
#include <assert_support.h>
#include <math_support.h>

/* for ia_css_binary_max_vf_width() */
#include "ia_css_binary.h"


enum ia_css_err ia_css_convert_errno(
				int in_err)
{
	enum ia_css_err out_err;

	switch (in_err) {
		case 0:
			out_err = IA_CSS_SUCCESS;
			break;
		case EINVAL:
			out_err = IA_CSS_ERR_INVALID_ARGUMENTS;
			break;
		case ENODATA:
			out_err = IA_CSS_ERR_QUEUE_IS_EMPTY;
			break;
		case ENOSYS:
		case ENOTSUP:
			out_err = IA_CSS_ERR_INTERNAL_ERROR;
			break;
		case ENOBUFS:
			out_err = IA_CSS_ERR_QUEUE_IS_FULL;
			break;
		default:
			out_err = IA_CSS_ERR_INTERNAL_ERROR;
			break;
	}
	return out_err;
}

/* MW: Table look-up ??? */
unsigned int ia_css_util_input_format_bpp(
	enum ia_css_stream_format format,
	bool two_ppc)
{
	unsigned int rval = 0;
	switch (format) {
	case IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY:
	case IA_CSS_STREAM_FORMAT_YUV420_8:
	case IA_CSS_STREAM_FORMAT_YUV422_8:
	case IA_CSS_STREAM_FORMAT_RGB_888:
	case IA_CSS_STREAM_FORMAT_RAW_8:
	case IA_CSS_STREAM_FORMAT_BINARY_8:
	case IA_CSS_STREAM_FORMAT_EMBEDDED:
		rval = 8;
		break;
	case IA_CSS_STREAM_FORMAT_YUV420_10:
	case IA_CSS_STREAM_FORMAT_YUV422_10:
	case IA_CSS_STREAM_FORMAT_RAW_10:
		rval = 10;
		break;
	case IA_CSS_STREAM_FORMAT_YUV420_16:
	case IA_CSS_STREAM_FORMAT_YUV422_16:
		rval = 16;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_444:
		rval = 4;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_555:
		rval = 5;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_565:
		rval = 65;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_666:
	case IA_CSS_STREAM_FORMAT_RAW_6:
		rval = 6;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_7:
		rval = 7;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_12:
		rval = 12;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_14:
		if (two_ppc)
			rval = 14;
		else
			rval = 12;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_16:
		if (two_ppc)
			rval = 16;
		else
			rval = 12;
		break;
	default:
		rval = 0;
		break;

	}
return rval;
}

enum ia_css_err ia_css_util_check_vf_info(
	const struct ia_css_frame_info * const info)
{
	enum ia_css_err err;
	unsigned int max_vf_width;
	assert(info != NULL);
	err = ia_css_frame_check_info(info);
	if (err != IA_CSS_SUCCESS)
		return err;
	max_vf_width = ia_css_binary_max_vf_width();
	if (max_vf_width != 0 && info->res.width > max_vf_width*2)
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	return IA_CSS_SUCCESS;
}

enum ia_css_err ia_css_util_check_vf_out_info(
	const struct ia_css_frame_info * const out_info,
	const struct ia_css_frame_info * const vf_info)
{
	enum ia_css_err err;

	assert(out_info != NULL);
	assert(vf_info != NULL);

	err = ia_css_frame_check_info(out_info);
	if (err != IA_CSS_SUCCESS)
		return err;
	err = ia_css_util_check_vf_info(vf_info);
	if (err != IA_CSS_SUCCESS)
		return err;
	return IA_CSS_SUCCESS;
}

enum ia_css_err ia_css_util_check_res(unsigned int width, unsigned int height)
{
	/* height can be odd number for jpeg/embedded data from ISYS2401 */
	if (((width  == 0)   ||
	     (height == 0)   ||
	     IS_ODD(width))) {
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}
	return IA_CSS_SUCCESS;
}

#ifdef ISP2401
bool ia_css_util_res_leq(struct ia_css_resolution a, struct ia_css_resolution b)
{
	return a.width <= b.width && a.height <= b.height;
}

bool ia_css_util_resolution_is_zero(const struct ia_css_resolution resolution)
{
	return (resolution.width == 0) || (resolution.height == 0);
}

bool ia_css_util_resolution_is_even(const struct ia_css_resolution resolution)
{
	return IS_EVEN(resolution.height) && IS_EVEN(resolution.width);
}

#endif
bool ia_css_util_is_input_format_raw(enum ia_css_stream_format format)
{
	return ((format == IA_CSS_STREAM_FORMAT_RAW_6) ||
		(format == IA_CSS_STREAM_FORMAT_RAW_7) ||
		(format == IA_CSS_STREAM_FORMAT_RAW_8) ||
		(format == IA_CSS_STREAM_FORMAT_RAW_10) ||
		(format == IA_CSS_STREAM_FORMAT_RAW_12));
	/* raw_14 and raw_16 are not supported as input formats to the ISP.
	 * They can only be copied to a frame in memory using the
	 * copy binary.
	 */
}

bool ia_css_util_is_input_format_yuv(enum ia_css_stream_format format)
{
	return format == IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY ||
	    format == IA_CSS_STREAM_FORMAT_YUV420_8  ||
	    format == IA_CSS_STREAM_FORMAT_YUV420_10 ||
	    format == IA_CSS_STREAM_FORMAT_YUV420_16 ||
	    format == IA_CSS_STREAM_FORMAT_YUV422_8  ||
	    format == IA_CSS_STREAM_FORMAT_YUV422_10 ||
	    format == IA_CSS_STREAM_FORMAT_YUV422_16;
}

enum ia_css_err ia_css_util_check_input(
	const struct ia_css_stream_config * const stream_config,
	bool must_be_raw,
	bool must_be_yuv)
{
	assert(stream_config != NULL);

	if (stream_config == NULL)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

#ifdef IS_ISP_2400_SYSTEM
	if (stream_config->input_config.effective_res.width == 0 ||
	    stream_config->input_config.effective_res.height == 0)
		return IA_CSS_ERR_INVALID_ARGUMENTS;
#endif
	if (must_be_raw &&
	    !ia_css_util_is_input_format_raw(stream_config->input_config.format))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	if (must_be_yuv &&
	    !ia_css_util_is_input_format_yuv(stream_config->input_config.format))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	return IA_CSS_SUCCESS;
}


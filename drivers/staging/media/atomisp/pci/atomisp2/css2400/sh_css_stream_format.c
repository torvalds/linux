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

#include "sh_css_stream_format.h"
#include <ia_css_stream_format.h>

unsigned int sh_css_stream_format_2_bits_per_subpixel(
		enum ia_css_stream_format format)
{
	unsigned int rval;

	switch (format) {
	case IA_CSS_STREAM_FORMAT_RGB_444:
		rval = 4;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_555:
		rval = 5;
		break;
	case IA_CSS_STREAM_FORMAT_RGB_565:
	case IA_CSS_STREAM_FORMAT_RGB_666:
	case IA_CSS_STREAM_FORMAT_RAW_6:
		rval = 6;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_7:
		rval = 7;
		break;
	case IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY:
	case IA_CSS_STREAM_FORMAT_YUV420_8:
	case IA_CSS_STREAM_FORMAT_YUV422_8:
	case IA_CSS_STREAM_FORMAT_RGB_888:
	case IA_CSS_STREAM_FORMAT_RAW_8:
	case IA_CSS_STREAM_FORMAT_BINARY_8:
	case IA_CSS_STREAM_FORMAT_USER_DEF1:
	case IA_CSS_STREAM_FORMAT_USER_DEF2:
	case IA_CSS_STREAM_FORMAT_USER_DEF3:
	case IA_CSS_STREAM_FORMAT_USER_DEF4:
	case IA_CSS_STREAM_FORMAT_USER_DEF5:
	case IA_CSS_STREAM_FORMAT_USER_DEF6:
	case IA_CSS_STREAM_FORMAT_USER_DEF7:
	case IA_CSS_STREAM_FORMAT_USER_DEF8:
		rval = 8;
		break;
	case IA_CSS_STREAM_FORMAT_YUV420_10:
	case IA_CSS_STREAM_FORMAT_YUV422_10:
	case IA_CSS_STREAM_FORMAT_RAW_10:
		rval = 10;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_12:
		rval = 12;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_14:
		rval = 14;
		break;
	case IA_CSS_STREAM_FORMAT_RAW_16:
	case IA_CSS_STREAM_FORMAT_YUV420_16:
	case IA_CSS_STREAM_FORMAT_YUV422_16:
		rval = 16;
		break;
	default:
		rval = 0;
		break;
	}

	return rval;
}

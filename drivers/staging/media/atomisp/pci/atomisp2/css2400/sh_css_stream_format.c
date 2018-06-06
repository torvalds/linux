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
		enum atomisp_input_format format)
{
	unsigned int rval;

	switch (format) {
	case ATOMISP_INPUT_FORMAT_RGB_444:
		rval = 4;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_555:
		rval = 5;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_565:
	case ATOMISP_INPUT_FORMAT_RGB_666:
	case ATOMISP_INPUT_FORMAT_RAW_6:
		rval = 6;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_7:
		rval = 7;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
	case ATOMISP_INPUT_FORMAT_YUV420_8:
	case ATOMISP_INPUT_FORMAT_YUV422_8:
	case ATOMISP_INPUT_FORMAT_RGB_888:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_BINARY_8:
	case ATOMISP_INPUT_FORMAT_USER_DEF1:
	case ATOMISP_INPUT_FORMAT_USER_DEF2:
	case ATOMISP_INPUT_FORMAT_USER_DEF3:
	case ATOMISP_INPUT_FORMAT_USER_DEF4:
	case ATOMISP_INPUT_FORMAT_USER_DEF5:
	case ATOMISP_INPUT_FORMAT_USER_DEF6:
	case ATOMISP_INPUT_FORMAT_USER_DEF7:
	case ATOMISP_INPUT_FORMAT_USER_DEF8:
		rval = 8;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_10:
	case ATOMISP_INPUT_FORMAT_YUV422_10:
	case ATOMISP_INPUT_FORMAT_RAW_10:
		rval = 10;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_12:
		rval = 12;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_14:
		rval = 14;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_16:
	case ATOMISP_INPUT_FORMAT_YUV420_16:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
		rval = 16;
		break;
	default:
		rval = 0;
		break;
	}

	return rval;
}

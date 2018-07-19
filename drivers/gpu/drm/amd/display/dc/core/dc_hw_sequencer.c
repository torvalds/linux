/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "core_types.h"
#include "timing_generator.h"
#include "hw_sequencer.h"

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

/* used as index in array of black_color_format */
enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,
	BLACK_COLOR_FORMAT_DEBUG,
};

enum dc_color_space_type {
	COLOR_SPACE_RGB_TYPE,
	COLOR_SPACE_RGB_LIMITED_TYPE,
	COLOR_SPACE_YCBCR601_TYPE,
	COLOR_SPACE_YCBCR709_TYPE,
	COLOR_SPACE_YCBCR601_LIMITED_TYPE,
	COLOR_SPACE_YCBCR709_LIMITED_TYPE
};

static const struct tg_color black_color_format[] = {
	/* BlackColorFormat_RGB_FullRange */
	{0, 0, 0},
	/* BlackColorFormat_RGB_Limited */
	{0x40, 0x40, 0x40},
	/* BlackColorFormat_YUV_TV */
	{0x200, 0x40, 0x200},
	/* BlackColorFormat_YUV_CV */
	{0x1f4, 0x40, 0x1f4},
	/* BlackColorFormat_YUV_SuperAA */
	{0x1a2, 0x20, 0x1a2},
	/* visual confirm debug */
	{0xff, 0xff, 0},
};

struct out_csc_color_matrix_type {
	enum dc_color_space_type color_space_type;
	uint16_t regval[12];
};

static const struct out_csc_color_matrix_type output_csc_matrix[] = {
	{ COLOR_SPACE_RGB_TYPE,
		{ 0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
	{ COLOR_SPACE_RGB_LIMITED_TYPE,
		{ 0x1B67, 0, 0, 0x201, 0, 0x1B67, 0, 0x201, 0, 0, 0x1B67, 0x201} },
	{ COLOR_SPACE_YCBCR601_TYPE,
		{ 0xE04, 0xF444, 0xFDB9, 0x1004, 0x831, 0x1016, 0x320, 0x201, 0xFB45,
				0xF6B7, 0xE04, 0x1004} },
	{ COLOR_SPACE_YCBCR709_TYPE,
		{ 0xE04, 0xF345, 0xFEB7, 0x1004, 0x5D3, 0x1399, 0x1FA,
				0x201, 0xFCCA, 0xF533, 0xE04, 0x1004} },

	/* TODO: correct values below */
	{ COLOR_SPACE_YCBCR601_LIMITED_TYPE,
		{ 0xE00, 0xF447, 0xFDB9, 0x1000, 0x991,
				0x12C9, 0x3A6, 0x200, 0xFB47, 0xF6B9, 0xE00, 0x1000} },
	{ COLOR_SPACE_YCBCR709_LIMITED_TYPE,
		{ 0xE00, 0xF349, 0xFEB7, 0x1000, 0x6CE, 0x16E3,
				0x24F, 0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} },
};

static bool is_rgb_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_SRGB			||
		color_space == COLOR_SPACE_XR_RGB		||
		color_space == COLOR_SPACE_MSREF_SCRGB		||
		color_space == COLOR_SPACE_2020_RGB_FULLRANGE	||
		color_space == COLOR_SPACE_ADOBERGB		||
		color_space == COLOR_SPACE_DCIP3	||
		color_space == COLOR_SPACE_DOLBYVISION)
		ret = true;
	return ret;
}

static bool is_rgb_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_SRGB_LIMITED		||
		color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE)
		ret = true;
	return ret;
}

static bool is_ycbcr601_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR601	||
		color_space == COLOR_SPACE_XV_YCC_601)
		ret = true;
	return ret;
}

static bool is_ycbcr601_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR601_LIMITED)
		ret = true;
	return ret;
}

static bool is_ycbcr709_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR709	||
		color_space == COLOR_SPACE_XV_YCC_709)
		ret = true;
	return ret;
}

static bool is_ycbcr709_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR709_LIMITED)
		ret = true;
	return ret;
}
enum dc_color_space_type get_color_space_type(enum dc_color_space color_space)
{
	enum dc_color_space_type type = COLOR_SPACE_RGB_TYPE;

	if (is_rgb_type(color_space))
		type = COLOR_SPACE_RGB_TYPE;
	else if (is_rgb_limited_type(color_space))
		type = COLOR_SPACE_RGB_LIMITED_TYPE;
	else if (is_ycbcr601_type(color_space))
		type = COLOR_SPACE_YCBCR601_TYPE;
	else if (is_ycbcr709_type(color_space))
		type = COLOR_SPACE_YCBCR709_TYPE;
	else if (is_ycbcr601_limited_type(color_space))
		type = COLOR_SPACE_YCBCR601_LIMITED_TYPE;
	else if (is_ycbcr709_limited_type(color_space))
		type = COLOR_SPACE_YCBCR709_LIMITED_TYPE;

	return type;
}

const uint16_t *find_color_matrix(enum dc_color_space color_space,
							uint32_t *array_size)
{
	int i;
	enum dc_color_space_type type;
	const uint16_t *val = NULL;
	int arr_size = NUM_ELEMENTS(output_csc_matrix);

	type = get_color_space_type(color_space);
	for (i = 0; i < arr_size; i++)
		if (output_csc_matrix[i].color_space_type == type) {
			val = output_csc_matrix[i].regval;
			*array_size = 12;
			break;
		}

	return val;
}


void color_space_to_black_color(
	const struct dc *dc,
	enum dc_color_space colorspace,
	struct tg_color *black_color)
{
	switch (colorspace) {
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_2020_YCBCR:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_CV];
		break;

	case COLOR_SPACE_SRGB_LIMITED:
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_LIMITED];
		break;

	/**
	 * Remove default and add case for all color space
	 * so when we forget to add new color space
	 * compiler will give a warning
	 */
	case COLOR_SPACE_UNKNOWN:
	case COLOR_SPACE_SRGB:
	case COLOR_SPACE_XR_RGB:
	case COLOR_SPACE_MSREF_SCRGB:
	case COLOR_SPACE_XV_YCC_709:
	case COLOR_SPACE_XV_YCC_601:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_ADOBERGB:
	case COLOR_SPACE_DCIP3:
	case COLOR_SPACE_DISPLAYNATIVE:
	case COLOR_SPACE_DOLBYVISION:
	case COLOR_SPACE_APPCTRL:
	case COLOR_SPACE_CUSTOMPOINTS:
		/* fefault is sRGB black (full range). */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_FULLRANGE];
		/* default is sRGB black 0. */
		break;
	}
}

bool hwss_wait_for_blank_complete(
		struct timing_generator *tg)
{
	int counter;

	/* Not applicable if the pipe is not primary, save 300ms of boot time */
	if (!tg->funcs->is_blanked)
		return true;
	for (counter = 0; counter < 100; counter++) {
		if (tg->funcs->is_blanked(tg))
			break;

		msleep(1);
	}

	if (counter == 100) {
		dm_error("DC: failed to blank crtc!\n");
		return false;
	}

	return true;
}

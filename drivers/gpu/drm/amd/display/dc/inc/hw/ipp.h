/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef __DAL_IPP_H__
#define __DAL_IPP_H__

#include "hw_shared.h"
#include "dc_hw_types.h"

#define MAXTRIX_COEFFICIENTS_NUMBER 12
#define MAXTRIX_COEFFICIENTS_WRAP_NUMBER (MAXTRIX_COEFFICIENTS_NUMBER + 4)
#define MAX_OVL_MATRIX_COUNT 12

/* IPP RELATED */
struct input_pixel_processor {
	struct  dc_context *ctx;
	unsigned int inst;
	const struct ipp_funcs *funcs;
};

enum ipp_prescale_mode {
	IPP_PRESCALE_MODE_BYPASS,
	IPP_PRESCALE_MODE_FIXED_SIGNED,
	IPP_PRESCALE_MODE_FLOAT_SIGNED,
	IPP_PRESCALE_MODE_FIXED_UNSIGNED,
	IPP_PRESCALE_MODE_FLOAT_UNSIGNED
};

struct ipp_prescale_params {
	enum ipp_prescale_mode mode;
	uint16_t bias;
	uint16_t scale;
};

enum ipp_degamma_mode {
	IPP_DEGAMMA_MODE_BYPASS,
	IPP_DEGAMMA_MODE_HW_sRGB,
	IPP_DEGAMMA_MODE_HW_xvYCC,
	IPP_DEGAMMA_MODE_USER_PWL
};

enum ovl_color_space {
	OVL_COLOR_SPACE_UNKNOWN = 0,
	OVL_COLOR_SPACE_RGB,
	OVL_COLOR_SPACE_YUV601,
	OVL_COLOR_SPACE_YUV709
};

enum expansion_mode {
	EXPANSION_MODE_DYNAMIC,
	EXPANSION_MODE_ZERO
};

enum ipp_output_format {
	IPP_OUTPUT_FORMAT_12_BIT_FIX,
	IPP_OUTPUT_FORMAT_16_BIT_BYPASS,
	IPP_OUTPUT_FORMAT_FLOAT
};

struct ipp_funcs {

	/*** cursor ***/
	void (*ipp_cursor_set_position)(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_position *position,
		const struct dc_cursor_mi_param *param);

	void (*ipp_cursor_set_attributes)(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_attributes *attributes);

	/*** setup input pixel processing ***/

	/* put the entire pixel processor to bypass */
	void (*ipp_full_bypass)(
			struct input_pixel_processor *ipp);

	/* setup ipp to expand/convert input to pixel processor internal format */
	void (*ipp_setup)(
		struct input_pixel_processor *ipp,
		enum surface_pixel_format input_format,
		enum expansion_mode mode,
		enum ipp_output_format output_format);

	/* DCE function to setup IPP.  TODO: see if we can consolidate to setup */
	void (*ipp_program_prescale)(
			struct input_pixel_processor *ipp,
			struct ipp_prescale_params *params);

	void (*ipp_program_input_lut)(
			struct input_pixel_processor *ipp,
			const struct dc_gamma *gamma);

	/*** DEGAMMA RELATED ***/
	void (*ipp_set_degamma)(
		struct input_pixel_processor *ipp,
		enum ipp_degamma_mode mode);

	void (*ipp_program_degamma_pwl)(
		struct input_pixel_processor *ipp,
		const struct pwl_params *params);

};

#endif /* __DAL_IPP_H__ */

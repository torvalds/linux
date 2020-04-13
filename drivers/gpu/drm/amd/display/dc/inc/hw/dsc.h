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
#ifndef __DAL_DSC_H__
#define __DAL_DSC_H__

#include "dc_dsc.h"
#include "dc_hw_types.h"
#include "dc_types.h"
/* do not include any other headers
 * or else it might break Edid Utility functionality.
 */


/* Input parameters for configuring DSC from the outside of DSC */
struct dsc_config {
	uint32_t pic_width;
	uint32_t pic_height;
	enum dc_pixel_encoding pixel_encoding;
	enum dc_color_depth color_depth;  /* Bits per component */
	bool is_odm;
	struct dc_dsc_config dc_dsc_cfg;
};


/* Output parameters for configuring DSC-related part of OPTC */
struct dsc_optc_config {
	uint32_t slice_width; /* Slice width in pixels */
	uint32_t bytes_per_pixel; /* Bytes per pixel in u3.28 format */
	bool is_pixel_format_444; /* 'true' if pixel format is 'RGB 444' or 'Simple YCbCr 4:2:2' (4:2:2 upsampled to 4:4:4)' */
};


struct dcn_dsc_state {
	uint32_t dsc_clock_en;
	uint32_t dsc_slice_width;
	uint32_t dsc_bytes_per_pixel;
};


/* DSC encoder capabilities
 * They differ from the DPCD DSC caps because they are based on AMD DSC encoder caps.
 */
union dsc_enc_slice_caps {
	struct {
		uint8_t NUM_SLICES_1 : 1;
		uint8_t NUM_SLICES_2 : 1;
		uint8_t NUM_SLICES_3 : 1; /* This one is not per DSC spec, but our encoder supports it */
		uint8_t NUM_SLICES_4 : 1;
		uint8_t NUM_SLICES_8 : 1;
	} bits;
	uint8_t raw;
};

struct dsc_enc_caps {
	uint8_t dsc_version;
	union dsc_enc_slice_caps slice_caps;
	int32_t lb_bit_depth;
	bool is_block_pred_supported;
	union dsc_color_formats color_formats;
	union dsc_color_depth color_depth;
	int32_t max_total_throughput_mps; /* Maximum total throughput with all the slices combined */
	int32_t max_slice_width;
	uint32_t bpp_increment_div; /* bpp increment divisor, e.g. if 16, it's 1/16th of a bit */
};

struct dsc_funcs {
	void (*dsc_get_enc_caps)(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz);
	void (*dsc_read_state)(struct display_stream_compressor *dsc, struct dcn_dsc_state *s);
	bool (*dsc_validate_stream)(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg);
	void (*dsc_set_config)(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
			struct dsc_optc_config *dsc_optc_cfg);
	bool (*dsc_get_packed_pps)(struct display_stream_compressor *dsc, const struct dsc_config *dsc_cfg,
			uint8_t *dsc_packed_pps);
	void (*dsc_enable)(struct display_stream_compressor *dsc, int opp_pipe);
	void (*dsc_disable)(struct display_stream_compressor *dsc);
};

#endif

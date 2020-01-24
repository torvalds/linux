#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
#ifndef DC_DSC_H_
#define DC_DSC_H_
/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 * Author: AMD
 */

/* put it here temporarily until linux has the new addresses official defined */
/* DP Extended DSC Capabilities */
#define DP_DSC_BRANCH_OVERALL_THROUGHPUT_0  0x0a0   /* DP 1.4a SCR */
#define DP_DSC_BRANCH_OVERALL_THROUGHPUT_1  0x0a1
#define DP_DSC_BRANCH_MAX_LINE_WIDTH        0x0a2
#include "dc_types.h"

struct dc_dsc_bw_range {
	uint32_t min_kbps; /* Bandwidth if min_target_bpp_x16 is used */
	uint32_t min_target_bpp_x16;
	uint32_t max_kbps; /* Bandwidth if max_target_bpp_x16 is used */
	uint32_t max_target_bpp_x16;
	uint32_t stream_kbps; /* Uncompressed stream bandwidth */
};

struct display_stream_compressor {
	const struct dsc_funcs *funcs;
#ifndef AMD_EDID_UTILITY
	struct dc_context *ctx;
	int inst;
#endif
};

bool dc_dsc_parse_dsc_dpcd(const uint8_t *dpcd_dsc_basic_data,
		const uint8_t *dpcd_dsc_ext_data,
		struct dsc_dec_dpcd_caps *dsc_sink_caps);

bool dc_dsc_compute_bandwidth_range(
		const struct display_stream_compressor *dsc,
		const uint32_t dsc_min_slice_height_override,
		const uint32_t min_kbps,
		const uint32_t max_kbps,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dc_crtc_timing *timing,
		struct dc_dsc_bw_range *range);

bool dc_dsc_compute_config(
		const struct display_stream_compressor *dsc,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const uint32_t dsc_min_slice_height_override,
		uint32_t target_bandwidth_kbps,
		const struct dc_crtc_timing *timing,
		struct dc_dsc_config *dsc_cfg);
#endif
#endif

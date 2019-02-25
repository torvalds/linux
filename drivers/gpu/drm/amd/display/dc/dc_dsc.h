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
struct dc_dsc_bw_range {
	uint32_t min_kbps;
	uint32_t min_target_bpp_x16;
	uint32_t max_kbps;
	uint32_t max_target_bpp_x16;
	uint32_t stream_kbps;
};


bool dc_dsc_parse_dsc_dpcd(const uint8_t *dpcd_dsc_data,
		struct dsc_dec_dpcd_caps *dsc_sink_caps);

bool dc_dsc_compute_bandwidth_range(
		const struct dc *dc,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dc_crtc_timing *timing,
		struct dc_dsc_bw_range *range);
bool dc_dsc_compute_config(
		const struct dc *dc,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		int target_bandwidth,
		const struct dc_crtc_timing *timing,
		struct dc_dsc_config *dsc_cfg);

bool dc_check_and_fit_timing_into_bandwidth_with_dsc_legacy(
		const struct dc *pDC,
		const struct dc_link *link,
		struct dc_crtc_timing *timing);

bool dc_setup_dsc_in_timing_legacy(const struct dc *pDC,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		int available_bandwidth_kbps,
		struct dc_crtc_timing *timing);
#endif
#endif

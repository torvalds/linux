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

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dsc_helper.h>
#include "dc_hw_types.h"
#include "dsc.h"
#include "dc.h"
#include "rc_calc.h"
#include "fixed31_32.h"

#include "clk_mgr.h"
#include "resource.h"

#define DC_LOGGER \
	dsc->ctx->logger

/* This module's internal functions */

/* default DSC policy target bitrate limit is 16bpp */
static uint32_t dsc_policy_max_target_bpp_limit = 16;

/* default DSC policy enables DSC only when needed */
static bool dsc_policy_enable_dsc_when_not_needed;

static bool dsc_policy_disable_dsc_stream_overhead;

static bool disable_128b_132b_stream_overhead;

#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif
#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

/* Need to account for padding due to pixel-to-symbol packing
 * for uncompressed 128b/132b streams.
 */
static uint32_t apply_128b_132b_stream_overhead(
	const struct dc_crtc_timing *timing, const uint32_t kbps)
{
	uint32_t total_kbps = kbps;

	if (disable_128b_132b_stream_overhead)
		return kbps;

	if (!timing->flags.DSC) {
		struct fixed31_32 bpp;
		struct fixed31_32 overhead_factor;

		bpp = dc_fixpt_from_int(kbps);
		bpp = dc_fixpt_div_int(bpp, timing->pix_clk_100hz / 10);

		/* Symbols_per_HActive = HActive * bpp / (4 lanes * 32-bit symbol size)
		 * Overhead_factor = ceil(Symbols_per_HActive) / Symbols_per_HActive
		 */
		overhead_factor = dc_fixpt_from_int(timing->h_addressable);
		overhead_factor = dc_fixpt_mul(overhead_factor, bpp);
		overhead_factor = dc_fixpt_div_int(overhead_factor, 128);
		overhead_factor = dc_fixpt_div(
			dc_fixpt_from_int(dc_fixpt_ceil(overhead_factor)),
			overhead_factor);

		total_kbps = dc_fixpt_ceil(
			dc_fixpt_mul_int(overhead_factor, total_kbps));
	}

	return total_kbps;
}

uint32_t dc_bandwidth_in_kbps_from_timing(
	const struct dc_crtc_timing *timing,
	const enum dc_link_encoding_format link_encoding)
{
	uint32_t bits_per_channel = 0;
	uint32_t kbps;

	if (timing->flags.DSC)
		return dc_dsc_stream_bandwidth_in_kbps(timing,
				timing->dsc_cfg.bits_per_pixel,
				timing->dsc_cfg.num_slices_h,
				timing->dsc_cfg.is_dp);

	switch (timing->display_color_depth) {
	case COLOR_DEPTH_666:
		bits_per_channel = 6;
		break;
	case COLOR_DEPTH_888:
		bits_per_channel = 8;
		break;
	case COLOR_DEPTH_101010:
		bits_per_channel = 10;
		break;
	case COLOR_DEPTH_121212:
		bits_per_channel = 12;
		break;
	case COLOR_DEPTH_141414:
		bits_per_channel = 14;
		break;
	case COLOR_DEPTH_161616:
		bits_per_channel = 16;
		break;
	default:
		ASSERT(bits_per_channel != 0);
		bits_per_channel = 8;
		break;
	}

	kbps = timing->pix_clk_100hz / 10;
	kbps *= bits_per_channel;

	if (timing->flags.Y_ONLY != 1) {
		/*Only YOnly make reduce bandwidth by 1/3 compares to RGB*/
		kbps *= 3;
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
			kbps /= 2;
		else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
			kbps = kbps * 2 / 3;
	}

	if (link_encoding == DC_LINK_ENCODING_DP_128b_132b)
		kbps = apply_128b_132b_stream_overhead(timing, kbps);

	if (link_encoding == DC_LINK_ENCODING_HDMI_FRL &&
			timing->vic == 0 && timing->hdmi_vic == 0 &&
			timing->frl_uncompressed_video_bandwidth_in_kbps != 0)
		kbps = timing->frl_uncompressed_video_bandwidth_in_kbps;

	return kbps;
}

const struct dc_dsc_primary_bpp prim_bpp_444[] = {
	/* VIC/BPP */
	{64,  192}, /* 1920x1080 @ 100 */
	{77,  192}, /* 1920x1080 @ 100 */
	{63,  192}, /* 1920x1080 @ 120 */
	{78,  192}, /* 1920x1080 @ 120 */
	{93,  192}, /* 3840x2160 @ 24 */
	{103, 192}, /* 3840x2160 @ 24 */
	{94,  192}, /* 3840x2160 @ 25 */
	{104, 192}, /* 3840x2160 @ 25 */
	{95,  192}, /* 3840x2160 @ 30 */
	{105, 192}, /* 3840x2160 @ 30 */
	{114, 192}, /* 3840x2160 @ 48 */
	{116, 192}, /* 3840x2160 @ 48 */
	{96,  192}, /* 3840x2160 @ 50 */
	{106, 192}, /* 3840x2160 @ 50 */
	{97,  192}, /* 3840x2160 @ 60 */
	{107, 192}, /* 3840x2160 @ 60 */
	{117, 192}, /* 3840x2160 @ 100 */
	{119, 192}, /* 3840x2160 @ 100 */
	{118, 192}, /* 3840x2160 @ 120 */
	{120, 192}, /* 3840x2160 @ 120 */
	{98,  192}, /* 4096x2160 @ 24 */
	{99,  192}, /* 4096x2160 @ 25 */
	{100, 192}, /* 4096x2160 @ 30 */
	{115, 192}, /* 4096x2160 @ 48 */
	{101, 192}, /* 4096x2160 @ 50 */
	{102, 192}, /* 4096x2160 @ 60 */
	{218, 192}, /* 4096x2160 @ 100 */
	{219, 192}, /* 4096x2160 @ 120 */
	{121, 192}, /* 5120x2160 @ 24 */
	{122, 192}, /* 5120x2160 @ 25 */
	{123, 192}, /* 5120x2160 @ 30 */
	{124, 192}, /* 5120x2160 @ 48 */
	{125, 192}, /* 5120x2160 @ 50 */
	{126, 173}, /* 5120x2160 @ 60 */
	{127, 192}, /* 5120x2160 @ 100 */
	{193, 175}, /* 5120x2160 @ 120 */
	{194, 192}, /* 7680x2160 @ 24 */
	{202, 192}, /* 7680x2160 @ 24 */
	{195, 192}, /* 7680x2160 @ 25 */
	{203, 192}, /* 7680x2160 @ 25 */
	{196, 192}, /* 7680x2160 @ 30 */
	{204, 192}, /* 7680x2160 @ 30 */
	{197, 157}, /* 7680x2160 @ 48 */
	{205, 157}, /* 7680x2160 @ 48 */
	{198, 157}, /* 7680x2160 @ 50 */
	{206, 157}, /* 7680x2160 @ 50 */
	{199, 159}, /* 7680x2160 @ 60 */
	{207, 159}, /* 7680x2160 @ 60 */
	{200, 134}, /* 7680x2160 @ 100 */
	{208, 134}, /* 7680x2160 @ 100 */
	{201, 130}, /* 7680x2160 @ 120 */
	{209, 130}, /* 7680x2160 @ 120 */
	{210, 182}, /* 10240x4320 @ 24 */
	{211, 181}, /* 10240x4320 @ 25 */
	{212, 177}, /* 10240x4320 @ 30 */
	{213, 163}, /* 10240x4320 @ 48 */
	{214, 162}, /* 10240x4320 @ 50 */
	{215, 157}, /* 10240x4320 @ 60 */
};
const struct dc_dsc_primary_bpp prim_bpp_422[] = {
	/* VIC/BPP */
	{114, 192}, /* 3840x2160 @ 48 */
	{116, 192}, /* 3840x2160 @ 48 */
	{96,  192}, /* 3840x2160 @ 50 */
	{106, 192}, /* 3840x2160 @ 50 */
	{97,  192}, /* 3840x2160 @ 60 */
	{107, 192}, /* 3840x2160 @ 60 */
	{117, 137}, /* 3840x2160 @ 100 */
	{119, 137}, /* 3840x2160 @ 100 */
	{118, 113}, /* 3840x2160 @ 120 */
	{120, 113}, /* 3840x2160 @ 120 */
	{115, 192}, /* 4096x2160 @ 48 */
	{101, 192}, /* 4096x2160 @ 50 */
	{102, 192}, /* 4096x2160 @ 60 */
	{218, 192}, /* 4096x2160 @ 100 */
	{219, 192}, /* 4096x2160 @ 120 */
	{121, 192}, /* 5120x2160 @ 24 */
	{122, 192}, /* 5120x2160 @ 25 */
	{123, 192}, /* 5120x2160 @ 30 */
	{124, 192}, /* 5120x2160 @ 48 */
	{125, 192}, /* 5120x2160 @ 50 */
	{126, 173}, /* 5120x2160 @ 60 */
	{127, 192}, /* 5120x2160 @ 100 */
	{193, 175}, /* 5120x2160 @ 120 */
	{194, 123}, /* 7680x2160 @ 24 */
	{202, 123}, /* 7680x2160 @ 24 */
	{195, 123}, /* 7680x2160 @ 25 */
	{203, 123}, /* 7680x2160 @ 25 */
	{196, 118}, /* 7680x2160 @ 30 */
	{204, 118}, /* 7680x2160 @ 30 */
	{197, 123}, /* 7680x2160 @ 48 */
	{205, 123}, /* 7680x2160 @ 48 */
	{198, 123}, /* 7680x2160 @ 50 */
	{206, 123}, /* 7680x2160 @ 50 */
	{199, 119}, /* 7680x2160 @ 60 */
	{207, 119}, /* 7680x2160 @ 60 */
	{200, 134}, /* 7680x2160 @ 100 */
	{208, 134}, /* 7680x2160 @ 100 */
	{201, 130}, /* 7680x2160 @ 120 */
	{209, 130}, /* 7680x2160 @ 120 */
	{210, 182}, /* 10240x4320 @ 24 */
	{211, 181}, /* 10240x4320 @ 25 */
	{212, 177}, /* 10240x4320 @ 30 */
	{213, 126}, /* 10240x4320 @ 48 */
	{214, 125}, /* 10240x4320 @ 50 */
	{215, 117}, /* 10240x4320 @ 60 */
	{216, 125}, /* 10240x4320 @ 100 */
	{217, 117}, /* 10240x4320 @ 120 */
};

const struct dc_dsc_primary_bpp prim_bpp_420[] = {
	/* VIC/BPP */
	{114, 192}, /* 3840x2160 @ 48 */
	{116, 192}, /* 3840x2160 @ 48 */
	{96,  192}, /* 3840x2160 @ 50 */
	{106, 192}, /* 3840x2160 @ 50 */
	{97,  192}, /* 3840x2160 @ 60 */
	{107, 192}, /* 3840x2160 @ 60 */
	{117, 137}, /* 3840x2160 @ 100 */
	{119, 137}, /* 3840x2160 @ 100 */
	{118, 113}, /* 3840x2160 @ 120 */
	{120, 113}, /* 3840x2160 @ 120 */
	{115, 192}, /* 4096x2160 @ 48 */
	{101, 192}, /* 4096x2160 @ 50 */
	{102, 192}, /* 4096x2160 @ 60 */
	{218, 129}, /* 4096x2160 @ 100 */
	{219, 106}, /* 4096x2160 @ 120 */
	{124, 192}, /* 5120x2160 @ 48 */
	{125, 192}, /* 5120x2160 @ 50 */
	{126, 173}, /* 5120x2160 @ 60 */
	{127, 192}, /* 5120x2160 @ 100 */
	{193, 175}, /* 5120x2160 @ 120 */
	{194, 123}, /* 7680x4320 @ 24 */
	{202, 123}, /* 7680x4320 @ 24 */
	{195, 123}, /* 7680x4320 @ 25 */
	{203, 123}, /* 7680x4320 @ 25 */
	{196, 118}, /* 7680x4320 @ 30 */
	{204, 118}, /* 7680x4320 @ 30 */
	{197, 123}, /* 7680x4320 @ 48 */
	{205, 123}, /* 7680x4320 @ 48 */
	{198, 123}, /* 7680x4320 @ 50 */
	{206, 123}, /* 7680x4320 @ 50 */
	{199, 119}, /* 7680x4320 @ 60 */
	{207, 119}, /* 7680x4320 @ 60 */
	{200, 112}, /* 7680x4320 @ 100 */
	{208, 112}, /* 7680x4320 @ 100 */
	{201, 103}, /* 7680x4320 @ 120 */
	{209, 103}, /* 7680x4320 @ 120 */
	{210,  98}, /* 10240x4320 @ 24 */
	{211,  98}, /* 10240x4320 @ 25 */
	{212, 177}, /* 10240x4320 @ 30 */
	{213,  98}, /* 10240x4320 @ 48 */
	{214, 125}, /* 10240x4320 @ 50 */
	{215, 117}, /* 10240x4320 @ 60 */
	{216, 107}, /* 10240x4320 @ 100 */
	{217,  97}, /* 10240x4320 @ 120 */
};

/* Forward Declerations */
static unsigned int get_min_dsc_slice_count_for_odm(
		const struct display_stream_compressor *dsc,
		const struct dsc_enc_caps *dsc_enc_caps,
		const struct dc_crtc_timing *timing);

static bool decide_dsc_bandwidth_range(
		const uint32_t min_bpp_x16,
		const uint32_t max_bpp_x16,
		const uint32_t num_slices_h,
		const struct dsc_enc_caps *dsc_caps,
		const struct dc_crtc_timing *timing,
		const enum dc_link_encoding_format link_encoding,
		struct dc_dsc_bw_range *range);

static uint32_t compute_bpp_x16_from_target_bandwidth(
		const uint32_t bandwidth_in_kbps,
		const struct dc_crtc_timing *timing,
		const uint32_t num_slices_h,
		const uint32_t bpp_increment_div,
		const bool is_dp);

static void get_dsc_enc_caps(
		const struct display_stream_compressor *dsc,
		struct dsc_enc_caps *dsc_enc_caps,
		int pixel_clock_100Hz);

static bool intersect_dsc_caps(
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dsc_enc_caps *dsc_enc_caps,
		enum dc_pixel_encoding pixel_encoding,
		struct dsc_enc_caps *dsc_common_caps);

static bool setup_dsc_config(
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dsc_enc_caps *dsc_enc_caps,
		int target_bandwidth_kbps,
		const struct dc_crtc_timing *timing,
		const struct dc_dsc_config_options *options,
		const enum dc_link_encoding_format link_encoding,
		int min_slice_count,
		struct dc_dsc_config *dsc_cfg);

static bool convert_bandwidth_to_frl_params(
	int bandwidth_kbps,
	int *num_lanes,
	int *frl_rate);

static uint32_t compute_bpp_x16_from_frl_params(
		const struct dc_crtc_timing *timing,
		const uint32_t num_slices_h,
		const struct dsc_enc_caps *dsc_caps);
static bool dsc_buff_block_size_from_dpcd(int dpcd_buff_block_size, int *buff_block_size)
{

	switch (dpcd_buff_block_size) {
	case DP_DSC_RC_BUF_BLK_SIZE_1:
		*buff_block_size = 1024;
		break;
	case DP_DSC_RC_BUF_BLK_SIZE_4:
		*buff_block_size = 4 * 1024;
		break;
	case DP_DSC_RC_BUF_BLK_SIZE_16:
		*buff_block_size = 16 * 1024;
		break;
	case DP_DSC_RC_BUF_BLK_SIZE_64:
		*buff_block_size = 64 * 1024;
		break;
	default: {
			dm_error("%s: DPCD DSC buffer size not recognized.\n", __func__);
			return false;
		}
	}

	return true;
}


static bool dsc_line_buff_depth_from_dpcd(int dpcd_line_buff_bit_depth, int *line_buff_bit_depth)
{
	if (0 <= dpcd_line_buff_bit_depth && dpcd_line_buff_bit_depth <= 7)
		*line_buff_bit_depth = dpcd_line_buff_bit_depth + 9;
	else if (dpcd_line_buff_bit_depth == 8)
		*line_buff_bit_depth = 8;
	else {
		dm_error("%s: DPCD DSC buffer depth not recognized.\n", __func__);
		return false;
	}

	return true;
}


static bool dsc_throughput_from_dpcd(int dpcd_throughput, int *throughput)
{
	switch (dpcd_throughput) {
	case DP_DSC_THROUGHPUT_MODE_0_UNSUPPORTED:
		*throughput = 0;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_170:
		*throughput = 170;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_340:
		*throughput = 340;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_400:
		*throughput = 400;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_450:
		*throughput = 450;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_500:
		*throughput = 500;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_550:
		*throughput = 550;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_600:
		*throughput = 600;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_650:
		*throughput = 650;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_700:
		*throughput = 700;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_750:
		*throughput = 750;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_800:
		*throughput = 800;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_850:
		*throughput = 850;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_900:
		*throughput = 900;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_950:
		*throughput = 950;
		break;
	case DP_DSC_THROUGHPUT_MODE_0_1000:
		*throughput = 1000;
		break;
	default: {
			dm_error("%s: DPCD DSC throughput mode not recognized.\n", __func__);
			return false;
		}
	}

	return true;
}


static bool dsc_bpp_increment_div_from_dpcd(uint8_t bpp_increment_dpcd, uint32_t *bpp_increment_div)
{
	// Mask bpp increment dpcd field to avoid reading other fields
	bpp_increment_dpcd &= 0x7;

	switch (bpp_increment_dpcd) {
	case 0:
		*bpp_increment_div = 16;
		break;
	case 1:
		*bpp_increment_div = 8;
		break;
	case 2:
		*bpp_increment_div = 4;
		break;
	case 3:
		*bpp_increment_div = 2;
		break;
	case 4:
		*bpp_increment_div = 1;
		break;
	default: {
		dm_error("%s: DPCD DSC bits-per-pixel increment not recognized.\n", __func__);
		return false;
	}
	}

	return true;
}



static bool get_vic_preset_bpp(
		const struct dc_crtc_timing *timing,
		int *preset_bpp)
{
	bool preset_found = false;
	uint32_t table_size_444 = ARRAY_SIZE(prim_bpp_444);
	uint32_t table_size_422 = ARRAY_SIZE(prim_bpp_422);
	uint32_t table_size_420 = ARRAY_SIZE(prim_bpp_420);
	uint32_t i;
	uint32_t vid_id;

	if (timing->vic == 0 && timing->hdmi_vic == 0)
		return false;

	vid_id = timing->vic;
	switch (timing->hdmi_vic) {
	case 1:
		vid_id = 95;
		break;
	case 2:
		vid_id = 94;
		break;
	case 3:
		vid_id = 93;
		break;
	case 4:
		vid_id = 98;
		break;
	default:
		break;
	}

	if (timing->pixel_encoding == PIXEL_ENCODING_RGB ||
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
		for (i = 0; i < table_size_444 ; i++) {
			if (prim_bpp_444[i].vic == vid_id) {
				preset_found = true;
				*preset_bpp = prim_bpp_444[i].target_bpp;
				break;
			}
		}
	} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		for (i = 0; i < table_size_422 ; i++) {
			if (prim_bpp_422[i].vic == vid_id) {
				preset_found = true;
				*preset_bpp = prim_bpp_422[i].target_bpp;
				break;
			}
		}
	} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		for (i = 0; i < table_size_420 ; i++) {
			if (prim_bpp_420[i].vic == vid_id) {
				preset_found = true;
				*preset_bpp = prim_bpp_420[i].target_bpp;
				break;
			}
		}
	} else {
		return false;
	}

	return preset_found;
}

static int hdmi_dsc_get_num_slices(const struct dc_crtc_timing *timing)
{
	int k_slice_adjust = 1;
	int adj_pix_clk_mhz;
	int min_slices;
	int slice_target;
	int slice_width = timing->h_addressable;
	int h_ratio_adj_pix_clk_mhz;

	if (timing->pixel_encoding == PIXEL_ENCODING_RGB ||
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444)
		k_slice_adjust = 2;

	adj_pix_clk_mhz = k_slice_adjust * timing->pix_clk_100hz / 10000 / 2;
	h_ratio_adj_pix_clk_mhz = adj_pix_clk_mhz * timing->h_addressable / timing->h_total;
	if (adj_pix_clk_mhz <= 2720) {
		min_slices = adj_pix_clk_mhz / 340;
		if (adj_pix_clk_mhz % 340 != 0)
			min_slices++;
	} else if (adj_pix_clk_mhz <= 4800) {
		min_slices = adj_pix_clk_mhz / 400;
		if (adj_pix_clk_mhz % 400 != 0)
			min_slices++;
	} else if (h_ratio_adj_pix_clk_mhz <= 4800) {
		min_slices = h_ratio_adj_pix_clk_mhz / 600;
		if (h_ratio_adj_pix_clk_mhz % 600 != 0)
			min_slices++;
	} else {
		min_slices = h_ratio_adj_pix_clk_mhz / 900;
		if (h_ratio_adj_pix_clk_mhz % 900 != 0)
			min_slices++;
	}

	do {
		if (min_slices <= 1)
			slice_target = 1;
		else if (min_slices <= 2)
			slice_target = 2;
		else if (min_slices <= 4)
			slice_target = 4;
		else if (min_slices <= 8)
			slice_target = 8;
		else if (min_slices <= 12)
			slice_target = 12;
		else if (min_slices <= 16)
			slice_target = 16;
		else
			return 0;

		slice_width = timing->h_addressable / slice_target;
		min_slices++;
	} while (slice_width > 2720);

	return slice_target;
}

static int hdmi_dsc_get_bpp(const struct dc_crtc_timing *timing,
		const struct dsc_enc_caps *dsc_common_caps)
{
	int max_dsc_bpp, min_dsc_bpp;
	int target_bytes;
	bool bpp_found = false;
	int bpp_decrement_x16;
	int src_fractional_bpp = dsc_common_caps->bpp_increment_div;
	int bpp_target;
	int bpp_target_x16;
	int bpc_factor = 8;
	int slice_width;
	int num_slices;
	bool hdmi_all_bpp = dsc_common_caps->is_vic_all_bpp;
	int hdmi_max_chunk_bytes = dsc_common_caps->total_chunk_kbytes;

	int preset_bpp;
	bool preset_found = false;

	if (timing->display_color_depth == COLOR_DEPTH_101010)
		bpc_factor = 10;
	if (timing->display_color_depth == COLOR_DEPTH_121212)
		bpc_factor = 12;

	/* Assuming: bpc as 8*/
	if (timing->pixel_encoding == PIXEL_ENCODING_RGB ||
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
		min_dsc_bpp = 8;
		max_dsc_bpp = 3 * bpc_factor;
	} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		min_dsc_bpp = 7;
		max_dsc_bpp = 2 * bpc_factor;
	} else {
		min_dsc_bpp = 6;
		max_dsc_bpp = 3 * bpc_factor / 2;
	}

	if (!hdmi_all_bpp)
		max_dsc_bpp = min(max_dsc_bpp, 12);


	num_slices = hdmi_dsc_get_num_slices(timing);
	if (num_slices == 0)
		return 0;

	slice_width = timing->h_addressable / num_slices;

	bpp_target = max_dsc_bpp;
	bpp_decrement_x16 = 16 / src_fractional_bpp;
	bpp_target_x16 = (bpp_target * 16) - bpp_decrement_x16;
	if (!hdmi_all_bpp)
			bpp_target_x16 = (bpp_target * 16);

	/* check if byte target is below allowed Kbytes */
	while (bpp_target_x16 > (min_dsc_bpp * 16)) {
		target_bytes = num_slices * slice_width * bpp_target_x16 / 16 / 8;
		if (target_bytes <= hdmi_max_chunk_bytes) {
			bpp_found = true;
			break;
		}
		bpp_target_x16 -= bpp_decrement_x16;
	}

	if (bpp_found) {
		if (!hdmi_all_bpp) {
			/* Get preset bpp for CTA modes */
			preset_found = get_vic_preset_bpp(timing, &preset_bpp);
			if (preset_found) {
				bpp_target_x16 = preset_bpp;
				target_bytes =
						num_slices * slice_width * bpp_target_x16 / 16 / 8;
				if (target_bytes > hdmi_max_chunk_bytes)
					return 0;
			}
		}
		return bpp_target_x16;
	}

	return 0;
}

bool dc_dsc_parse_dsc_dpcd(const struct dc *dc,
		const uint8_t *dpcd_dsc_basic_data,
		const uint8_t *dpcd_dsc_branch_decoder_caps,
		struct dsc_dec_dpcd_caps *dsc_sink_caps)
{
	if (!dpcd_dsc_basic_data)
		return false;

	dsc_sink_caps->is_dsc_supported =
		(dpcd_dsc_basic_data[DP_DSC_SUPPORT - DP_DSC_SUPPORT] & DP_DSC_DECOMPRESSION_IS_SUPPORTED) != 0;
	if (!dsc_sink_caps->is_dsc_supported)
		return false;

	dsc_sink_caps->dsc_version = dpcd_dsc_basic_data[DP_DSC_REV - DP_DSC_SUPPORT];

	{
		int buff_block_size;
		int buff_size;

		if (!dsc_buff_block_size_from_dpcd(
				dpcd_dsc_basic_data[DP_DSC_RC_BUF_BLK_SIZE - DP_DSC_SUPPORT] & 0x03,
				&buff_block_size))
			return false;

		buff_size = dpcd_dsc_basic_data[DP_DSC_RC_BUF_SIZE - DP_DSC_SUPPORT] + 1;
		dsc_sink_caps->rc_buffer_size = buff_size * buff_block_size;
	}

	dsc_sink_caps->slice_caps1.raw = dpcd_dsc_basic_data[DP_DSC_SLICE_CAP_1 - DP_DSC_SUPPORT];
	if (!dsc_line_buff_depth_from_dpcd(dpcd_dsc_basic_data[DP_DSC_LINE_BUF_BIT_DEPTH - DP_DSC_SUPPORT],
									   &dsc_sink_caps->lb_bit_depth))
		return false;

	dsc_sink_caps->is_block_pred_supported =
		(dpcd_dsc_basic_data[DP_DSC_BLK_PREDICTION_SUPPORT - DP_DSC_SUPPORT] &
		 DP_DSC_BLK_PREDICTION_IS_SUPPORTED) != 0;

	dsc_sink_caps->edp_max_bits_per_pixel =
		dpcd_dsc_basic_data[DP_DSC_MAX_BITS_PER_PIXEL_LOW - DP_DSC_SUPPORT] |
		dpcd_dsc_basic_data[DP_DSC_MAX_BITS_PER_PIXEL_HI - DP_DSC_SUPPORT] << 8;

	dsc_sink_caps->color_formats.raw = dpcd_dsc_basic_data[DP_DSC_DEC_COLOR_FORMAT_CAP - DP_DSC_SUPPORT];
	dsc_sink_caps->color_depth.raw = dpcd_dsc_basic_data[DP_DSC_DEC_COLOR_DEPTH_CAP - DP_DSC_SUPPORT];

	{
		int dpcd_throughput = dpcd_dsc_basic_data[DP_DSC_PEAK_THROUGHPUT - DP_DSC_SUPPORT];
		int dsc_throughput_granular_delta;

		dsc_throughput_granular_delta = dpcd_dsc_basic_data[DP_DSC_RC_BUF_BLK_SIZE - DP_DSC_SUPPORT] >> 3;
		dsc_throughput_granular_delta *= 2;

		if (!dsc_throughput_from_dpcd(dpcd_throughput & DP_DSC_THROUGHPUT_MODE_0_MASK,
									  &dsc_sink_caps->throughput_mode_0_mps))
			return false;
		dsc_sink_caps->throughput_mode_0_mps += dsc_throughput_granular_delta;

		dpcd_throughput = (dpcd_throughput & DP_DSC_THROUGHPUT_MODE_1_MASK) >> DP_DSC_THROUGHPUT_MODE_1_SHIFT;
		if (!dsc_throughput_from_dpcd(dpcd_throughput, &dsc_sink_caps->throughput_mode_1_mps))
			return false;
	}

	dsc_sink_caps->max_slice_width = dpcd_dsc_basic_data[DP_DSC_MAX_SLICE_WIDTH - DP_DSC_SUPPORT] * 320;
	dsc_sink_caps->slice_caps2.raw = dpcd_dsc_basic_data[DP_DSC_SLICE_CAP_2 - DP_DSC_SUPPORT];

	if (!dsc_bpp_increment_div_from_dpcd(dpcd_dsc_basic_data[DP_DSC_BITS_PER_PIXEL_INC - DP_DSC_SUPPORT],
										 &dsc_sink_caps->bpp_increment_div))
		return false;

	if (dc->debug.dsc_bpp_increment_div) {
		/* dsc_bpp_increment_div should onl be 1, 2, 4, 8 or 16, but rather than rejecting invalid values,
		 * we'll accept all and get it into range. This also makes the above check against 0 redundant,
		 * but that one stresses out the override will be only used if it's not 0.
		 */
		if (dc->debug.dsc_bpp_increment_div >= 1)
			dsc_sink_caps->bpp_increment_div = 1;
		if (dc->debug.dsc_bpp_increment_div >= 2)
			dsc_sink_caps->bpp_increment_div = 2;
		if (dc->debug.dsc_bpp_increment_div >= 4)
			dsc_sink_caps->bpp_increment_div = 4;
		if (dc->debug.dsc_bpp_increment_div >= 8)
			dsc_sink_caps->bpp_increment_div = 8;
		if (dc->debug.dsc_bpp_increment_div >= 16)
			dsc_sink_caps->bpp_increment_div = 16;
	}

	/* Extended caps */
	if (dpcd_dsc_branch_decoder_caps == NULL) { // branch decoder DPCD DSC data can be null for non branch device
		dsc_sink_caps->branch_overall_throughput_0_mps = 0;
		dsc_sink_caps->branch_overall_throughput_1_mps = 0;
		dsc_sink_caps->branch_max_line_width = 0;
		return true;
	}

	dsc_sink_caps->branch_overall_throughput_0_mps =
		dpcd_dsc_branch_decoder_caps[DP_DSC_BRANCH_OVERALL_THROUGHPUT_0 - DP_DSC_BRANCH_OVERALL_THROUGHPUT_0];
	if (dsc_sink_caps->branch_overall_throughput_0_mps == 0)
		dsc_sink_caps->branch_overall_throughput_0_mps = 0;
	else if (dsc_sink_caps->branch_overall_throughput_0_mps == 1)
		dsc_sink_caps->branch_overall_throughput_0_mps = 680;
	else {
		dsc_sink_caps->branch_overall_throughput_0_mps *= 50;
		dsc_sink_caps->branch_overall_throughput_0_mps += 600;
	}

	dsc_sink_caps->branch_overall_throughput_1_mps =
		dpcd_dsc_branch_decoder_caps[DP_DSC_BRANCH_OVERALL_THROUGHPUT_1 - DP_DSC_BRANCH_OVERALL_THROUGHPUT_0];
	if (dsc_sink_caps->branch_overall_throughput_1_mps == 0)
		dsc_sink_caps->branch_overall_throughput_1_mps = 0;
	else if (dsc_sink_caps->branch_overall_throughput_1_mps == 1)
		dsc_sink_caps->branch_overall_throughput_1_mps = 680;
	else {
		dsc_sink_caps->branch_overall_throughput_1_mps *= 50;
		dsc_sink_caps->branch_overall_throughput_1_mps += 600;
	}

	dsc_sink_caps->branch_max_line_width =
		dpcd_dsc_branch_decoder_caps[DP_DSC_BRANCH_MAX_LINE_WIDTH - DP_DSC_BRANCH_OVERALL_THROUGHPUT_0] * 320;
	ASSERT(dsc_sink_caps->branch_max_line_width == 0 || dsc_sink_caps->branch_max_line_width >= 5120);

	dsc_sink_caps->is_dp = true;
	return true;
}
bool dc_dsc_parse_dsc_edid(const struct dc *dc, const struct dc_edid_caps *edid_caps,
		struct dsc_dec_dpcd_caps *dsc_sink_caps)
{
	(void)dc;
	dsc_sink_caps->is_dsc_supported = edid_caps->frl_dsc_support;
	if (!edid_caps->frl_dsc_support)
		return false;

	dsc_sink_caps->dsc_version = 0x21;
	dsc_sink_caps->is_frl = true;
	dsc_sink_caps->is_dp = false;

	switch (edid_caps->frl_dsc_max_slices) {
	case 0:
		break;
	case 1:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 340;
		dsc_sink_caps->throughput_mode_1_mps = 680;
		break;
	case 2:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 340;
		dsc_sink_caps->throughput_mode_1_mps = 680;
		break;
	case 3:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 340;
		dsc_sink_caps->throughput_mode_1_mps = 680;
		break;
	case 4:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 340;
		dsc_sink_caps->throughput_mode_1_mps = 680;
		break;
	case 5:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 400;
		dsc_sink_caps->throughput_mode_1_mps = 800;
		break;
	case 6:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_12 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 400;
		dsc_sink_caps->throughput_mode_1_mps = 800;
		break;
	case 7:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_12 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 600;
		dsc_sink_caps->throughput_mode_1_mps = 1200;
		break;
	case 8:
	default:
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 = 1;
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_12 = 1;
		dsc_sink_caps->throughput_mode_0_mps = 900;
		dsc_sink_caps->throughput_mode_1_mps = 1800;
		break;
	}
	dsc_sink_caps->lb_bit_depth = 13; //Table 7-25
	dsc_sink_caps->is_block_pred_supported = true; //Table 7-25
	dsc_sink_caps->color_formats.bits.RGB = 1;
	dsc_sink_caps->color_formats.bits.YCBCR_444 = 1;
	dsc_sink_caps->color_formats.bits.YCBCR_NATIVE_422 = 1;
	dsc_sink_caps->color_formats.bits.YCBCR_NATIVE_420 =
			(edid_caps->frl_dsc_native_420 == true) ? 1 : 0;
	dsc_sink_caps->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dsc_sink_caps->color_depth.bits.COLOR_DEPTH_10_BPC =
			(edid_caps->frl_dsc_10bpc == true) ? 1 : 0;
	dsc_sink_caps->color_depth.bits.COLOR_DEPTH_12_BPC =
			(edid_caps->frl_dsc_12bpc == true) ? 1 : 0;
	dsc_sink_caps->max_slice_width = 2560;
	dsc_sink_caps->bpp_increment_div = 16; /* bpp increment divisor, e.g. if 16, it's 1/16th of a bit */
	dsc_sink_caps->is_vic_all_bpp = edid_caps->frl_dsc_all_bpp;
	dsc_sink_caps->total_chunk_kbytes =
			1024 * (1 + edid_caps->frl_dsc_total_chunk_kbytes);

	return true;
}

/* If DSC is possbile, get DSC bandwidth range based on [min_bpp, max_bpp] target bitrate range and
 * timing's pixel clock and uncompressed bandwidth.
 * If DSC is not possible, leave '*range' untouched.
 */
bool dc_dsc_compute_bandwidth_range(
		const struct display_stream_compressor *dsc,
		uint32_t dsc_min_slice_height_override,
		uint32_t min_bpp_x16,
		uint32_t max_bpp_x16,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dc_crtc_timing *timing,
		const enum dc_link_encoding_format link_encoding,
		struct dc_dsc_bw_range *range)
{
	bool is_dsc_possible = false;
	unsigned int min_dsc_slice_count;
	struct dsc_enc_caps dsc_enc_caps;
	struct dsc_enc_caps dsc_common_caps;
	struct dc_dsc_config config = {0};
	struct dc_dsc_config_options options = {0};

	options.dsc_min_slice_height_override = dsc_min_slice_height_override;
	options.max_target_bpp_limit_override_x16 = max_bpp_x16;
	options.slice_height_granularity = 1;

	get_dsc_enc_caps(dsc, &dsc_enc_caps, timing->pix_clk_100hz);

	min_dsc_slice_count = get_min_dsc_slice_count_for_odm(dsc, &dsc_enc_caps, timing);

	is_dsc_possible = intersect_dsc_caps(dsc_sink_caps, &dsc_enc_caps,
			timing->pixel_encoding, &dsc_common_caps);

	if (is_dsc_possible)
		is_dsc_possible = setup_dsc_config(dsc_sink_caps, &dsc_enc_caps, 0, timing,
				&options, link_encoding, min_dsc_slice_count, &config);

	if (is_dsc_possible)
		is_dsc_possible = decide_dsc_bandwidth_range(min_bpp_x16, max_bpp_x16,
				config.num_slices_h, &dsc_common_caps, timing, link_encoding, range);

	return is_dsc_possible;
}

void dc_dsc_dump_encoder_caps(const struct display_stream_compressor *dsc,
			      const struct dc_crtc_timing *timing)
{
	struct dsc_enc_caps dsc_enc_caps;

	get_dsc_enc_caps(dsc, &dsc_enc_caps, timing->pix_clk_100hz);

	DC_LOG_DSC("dsc encoder caps:");
	DC_LOG_DSC("\tdsc_version 0x%x", dsc_enc_caps.dsc_version);
	DC_LOG_DSC("\tslice_caps 0x%x", dsc_enc_caps.slice_caps.raw);
	DC_LOG_DSC("\tlb_bit_depth %d", dsc_enc_caps.lb_bit_depth);
	DC_LOG_DSC("\tis_block_pred_supported %d", dsc_enc_caps.is_block_pred_supported);
	DC_LOG_DSC("\tcolor_formats 0x%x", dsc_enc_caps.color_formats.raw);
	DC_LOG_DSC("\tcolor_depth 0x%x", dsc_enc_caps.color_depth.raw);
	DC_LOG_DSC("\tmax_total_throughput_mps %d", dsc_enc_caps.max_total_throughput_mps);
	DC_LOG_DSC("\tmax_slice_width %d", dsc_enc_caps.max_slice_width);
	DC_LOG_DSC("\tbpp_increment_div %d", dsc_enc_caps.bpp_increment_div);
}

void dc_dsc_dump_decoder_caps(const struct display_stream_compressor *dsc,
			      const struct dsc_dec_dpcd_caps *dsc_sink_caps)
{
	DC_LOG_DSC("dsc decoder caps:");
	DC_LOG_DSC("\tis_dsc_supported %d", dsc_sink_caps->is_dsc_supported);
	DC_LOG_DSC("\tdsc_version 0x%x", dsc_sink_caps->dsc_version);
	DC_LOG_DSC("\trc_buffer_size %d", dsc_sink_caps->rc_buffer_size);
	DC_LOG_DSC("\tslice_caps1 0x%x", dsc_sink_caps->slice_caps1.raw);
	DC_LOG_DSC("\tslice_caps2 0x%x", dsc_sink_caps->slice_caps2.raw);
	DC_LOG_DSC("\tlb_bit_depth %d", dsc_sink_caps->lb_bit_depth);
	DC_LOG_DSC("\tis_block_pred_supported %d", dsc_sink_caps->is_block_pred_supported);
	DC_LOG_DSC("\tedp_max_bits_per_pixel %d", dsc_sink_caps->edp_max_bits_per_pixel);
	DC_LOG_DSC("\tcolor_formats 0x%x", dsc_sink_caps->color_formats.raw);
	DC_LOG_DSC("\tthroughput_mode_0_mps %d", dsc_sink_caps->throughput_mode_0_mps);
	DC_LOG_DSC("\tthroughput_mode_1_mps %d", dsc_sink_caps->throughput_mode_1_mps);
	DC_LOG_DSC("\tmax_slice_width %d", dsc_sink_caps->max_slice_width);
	DC_LOG_DSC("\tbpp_increment_div %d", dsc_sink_caps->bpp_increment_div);
	DC_LOG_DSC("\tbranch_overall_throughput_0_mps %d", dsc_sink_caps->branch_overall_throughput_0_mps);
	DC_LOG_DSC("\tbranch_overall_throughput_1_mps %d", dsc_sink_caps->branch_overall_throughput_1_mps);
	DC_LOG_DSC("\tbranch_max_line_width %d", dsc_sink_caps->branch_max_line_width);
	DC_LOG_DSC("\tis_dp %d", dsc_sink_caps->is_dp);
}


static void build_dsc_enc_combined_slice_caps(
		const struct dsc_enc_caps *single_dsc_enc_caps,
		struct dsc_enc_caps *dsc_enc_caps,
		unsigned int max_odm_combine_factor)
{
	/* 1-16 slice configurations, single DSC */
	dsc_enc_caps->slice_caps.raw |= single_dsc_enc_caps->slice_caps.raw;

	/* 2x DSC's */
	if (max_odm_combine_factor >= 2) {
		/* 1 + 1 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_2 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_1;

		/* 2 + 2 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_2;

		/* 4 + 4 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_8 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_4;

		/* 8 + 8 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_16 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_8;
	}

	/* 3x DSC's */
	if (max_odm_combine_factor >= 3) {
		/* 4 + 4 + 4 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_12 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_4;
	}

	/* 4x DSC's */
	if (max_odm_combine_factor >= 4) {
		/* 1 + 1 + 1 + 1 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_1;

		/* 2 + 2 + 2 + 2 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_8 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_2;

		/* 3 + 3 + 3 + 3 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_12 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_3;

		/* 4 + 4 + 4 + 4 */
		dsc_enc_caps->slice_caps.bits.NUM_SLICES_16 |= single_dsc_enc_caps->slice_caps.bits.NUM_SLICES_4;
	}
}

static void build_dsc_enc_caps(
		const struct display_stream_compressor *dsc,
		struct dsc_enc_caps *dsc_enc_caps)
{
	unsigned int max_dscclk_khz;
	unsigned int num_dsc;
	unsigned int max_odm_combine_factor;
	struct dsc_enc_caps single_dsc_enc_caps;

	struct dc *dc;

	if (!dsc || !dsc->ctx || !dsc->ctx->dc || !dsc->funcs->dsc_get_single_enc_caps)
		return;

	dc = dsc->ctx->dc;

	if (!dc->clk_mgr || !dc->clk_mgr->funcs->get_max_clock_khz || !dc->res_pool || dc->debug.disable_dsc)
		return;

	/* get max DSCCLK from clk_mgr */
	max_dscclk_khz = dc->clk_mgr->funcs->get_max_clock_khz(dc->clk_mgr, CLK_TYPE_DSCCLK);

	dsc->funcs->dsc_get_single_enc_caps(&single_dsc_enc_caps, max_dscclk_khz);

	/* global capabilities */
	dsc_enc_caps->dsc_version = single_dsc_enc_caps.dsc_version;
	dsc_enc_caps->lb_bit_depth = single_dsc_enc_caps.lb_bit_depth;
	dsc_enc_caps->is_block_pred_supported = single_dsc_enc_caps.is_block_pred_supported;
	dsc_enc_caps->max_slice_width = single_dsc_enc_caps.max_slice_width;
	dsc_enc_caps->bpp_increment_div = single_dsc_enc_caps.bpp_increment_div;
	dsc_enc_caps->color_formats.raw = single_dsc_enc_caps.color_formats.raw;
	dsc_enc_caps->color_depth.raw = single_dsc_enc_caps.color_depth.raw;

	/* expand per DSC capabilities to global */
	max_odm_combine_factor = dc->caps.max_odm_combine_factor;
	num_dsc = dc->res_pool->res_cap->num_dsc;
	max_odm_combine_factor = min(max_odm_combine_factor, num_dsc);
	dsc_enc_caps->max_total_throughput_mps =
			single_dsc_enc_caps.max_total_throughput_mps *
			max_odm_combine_factor;

	/* check slice counts possible for with ODM combine */
	build_dsc_enc_combined_slice_caps(&single_dsc_enc_caps, dsc_enc_caps, max_odm_combine_factor);
}

static inline uint32_t dsc_div_by_10_round_up(uint32_t value)
{
	return (value + 9) / 10;
}

static unsigned int get_min_dsc_slice_count_for_odm(
		const struct display_stream_compressor *dsc,
		const struct dsc_enc_caps *dsc_enc_caps,
		const struct dc_crtc_timing *timing)
{
	unsigned int max_dispclk_khz;

	/* get max pixel rate and combine caps */
	max_dispclk_khz = dsc_enc_caps->max_total_throughput_mps * 1000;
	if (dsc && dsc->ctx->dc) {
		if (dsc->ctx->dc->clk_mgr &&
			dsc->ctx->dc->clk_mgr->funcs->get_max_clock_khz) {
			/* dispclk is available */
			max_dispclk_khz = dsc->ctx->dc->clk_mgr->funcs->get_max_clock_khz(dsc->ctx->dc->clk_mgr, CLK_TYPE_DISPCLK);
		}
	}

	/* validate parameters */
	if (max_dispclk_khz == 0 || dsc_enc_caps->max_slice_width == 0)
		return 1;

	/* consider minimum odm slices required due to
	 * 1) display pipe throughput (dispclk)
	 * 2) max image width per slice
	 */
	return dc_fixpt_ceil(dc_fixpt_max(
			dc_fixpt_div_int(dc_fixpt_from_int(dsc_div_by_10_round_up(timing->pix_clk_100hz)),
			max_dispclk_khz), // throughput
			dc_fixpt_div_int(dc_fixpt_from_int(timing->h_addressable + timing->h_border_left + timing->h_border_right),
			dsc_enc_caps->max_slice_width))); // slice width
}

static void get_dsc_enc_caps(
		const struct display_stream_compressor *dsc,
		struct dsc_enc_caps *dsc_enc_caps,
		int pixel_clock_100Hz)
{
	memset(dsc_enc_caps, 0, sizeof(struct dsc_enc_caps));

	if (!dsc || !dsc->ctx || !dsc->ctx->dc || dsc->ctx->dc->debug.disable_dsc)
		return;

	/* check if reported cap global or only for a single DCN DSC enc */
	if (dsc->funcs->dsc_get_enc_caps) {
		dsc->funcs->dsc_get_enc_caps(dsc_enc_caps, pixel_clock_100Hz);
	} else {
		build_dsc_enc_caps(dsc, dsc_enc_caps);
	}
}

/* Returns 'false' if no intersection was found for at least one capability.
 * It also implicitly validates some sink caps against invalid value of zero.
 */
static bool intersect_dsc_caps(
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dsc_enc_caps *dsc_enc_caps,
		enum dc_pixel_encoding pixel_encoding,
		struct dsc_enc_caps *dsc_common_caps)
{
	int32_t max_slices;
	int32_t total_sink_throughput;

	memset(dsc_common_caps, 0, sizeof(struct dsc_enc_caps));

	dsc_common_caps->dsc_version = min(dsc_sink_caps->dsc_version, dsc_enc_caps->dsc_version);
	if (!dsc_common_caps->dsc_version)
		return false;

	dsc_common_caps->slice_caps.bits.NUM_SLICES_1 =
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_1 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_1;
	dsc_common_caps->slice_caps.bits.NUM_SLICES_2 =
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_2 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_2;
	dsc_common_caps->slice_caps.bits.NUM_SLICES_4 =
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_4 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_4;
	dsc_common_caps->slice_caps.bits.NUM_SLICES_8 =
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_8 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_8;
	dsc_common_caps->slice_caps.bits.NUM_SLICES_12 =
		dsc_sink_caps->slice_caps1.bits.NUM_SLICES_12 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_12;
	dsc_common_caps->slice_caps.bits.NUM_SLICES_16 =
		dsc_sink_caps->slice_caps2.bits.NUM_SLICES_16 && dsc_enc_caps->slice_caps.bits.NUM_SLICES_16;

	if (!dsc_common_caps->slice_caps.raw)
		return false;

	dsc_common_caps->lb_bit_depth = min(dsc_sink_caps->lb_bit_depth, dsc_enc_caps->lb_bit_depth);
	if (!dsc_common_caps->lb_bit_depth)
		return false;

	dsc_common_caps->is_block_pred_supported =
		dsc_sink_caps->is_block_pred_supported && dsc_enc_caps->is_block_pred_supported;

	dsc_common_caps->color_formats.raw = dsc_sink_caps->color_formats.raw & dsc_enc_caps->color_formats.raw;
	if (!dsc_common_caps->color_formats.raw)
		return false;

	dsc_common_caps->color_depth.raw = dsc_sink_caps->color_depth.raw & dsc_enc_caps->color_depth.raw;
	if (!dsc_common_caps->color_depth.raw)
		return false;

	max_slices = 0;
	if (dsc_common_caps->slice_caps.bits.NUM_SLICES_1)
		max_slices = 1;

	if (dsc_common_caps->slice_caps.bits.NUM_SLICES_2)
		max_slices = 2;

	if (dsc_common_caps->slice_caps.bits.NUM_SLICES_4)
		max_slices = 4;

	total_sink_throughput = max_slices * dsc_sink_caps->throughput_mode_0_mps;
	if (pixel_encoding == PIXEL_ENCODING_YCBCR422 || pixel_encoding == PIXEL_ENCODING_YCBCR420)
		total_sink_throughput = max_slices * dsc_sink_caps->throughput_mode_1_mps;

	dsc_common_caps->max_total_throughput_mps = min(total_sink_throughput, dsc_enc_caps->max_total_throughput_mps);

	dsc_common_caps->max_slice_width = min(dsc_sink_caps->max_slice_width, dsc_enc_caps->max_slice_width);
	if (!dsc_common_caps->max_slice_width)
		return false;

	dsc_common_caps->bpp_increment_div = min(dsc_sink_caps->bpp_increment_div, dsc_enc_caps->bpp_increment_div);

	// TODO DSC: Remove this workaround for N422 and 420 once it's fixed, or move it to get_dsc_encoder_caps()
	if (pixel_encoding == PIXEL_ENCODING_YCBCR422 || pixel_encoding == PIXEL_ENCODING_YCBCR420)
		dsc_common_caps->bpp_increment_div = min(dsc_common_caps->bpp_increment_div, (uint32_t)8);

	dsc_common_caps->is_frl = dsc_sink_caps->is_frl;
	dsc_common_caps->is_vic_all_bpp = dsc_sink_caps->is_vic_all_bpp;
	dsc_common_caps->total_chunk_kbytes = dsc_sink_caps->total_chunk_kbytes;
	dsc_common_caps->edp_sink_max_bits_per_pixel = dsc_sink_caps->edp_max_bits_per_pixel;
	dsc_common_caps->is_dp = dsc_sink_caps->is_dp;
	return true;
}

static uint32_t compute_bpp_x16_from_target_bandwidth(
	const uint32_t bandwidth_in_kbps,
	const struct dc_crtc_timing *timing,
	const uint32_t num_slices_h,
	const uint32_t bpp_increment_div,
	const bool is_dp)
{
	uint32_t overhead_in_kbps;
	struct fixed31_32 effective_bandwidth_in_kbps;
	struct fixed31_32 bpp_x16;

	overhead_in_kbps = dc_dsc_stream_bandwidth_overhead_in_kbps(
				timing, num_slices_h, is_dp);
	effective_bandwidth_in_kbps = dc_fixpt_from_int(bandwidth_in_kbps);
	effective_bandwidth_in_kbps = dc_fixpt_sub_int(effective_bandwidth_in_kbps,
			overhead_in_kbps);
	bpp_x16 = dc_fixpt_mul_int(effective_bandwidth_in_kbps, 10);
	bpp_x16 = dc_fixpt_div_int(bpp_x16, timing->pix_clk_100hz);
	bpp_x16 = dc_fixpt_from_int(dc_fixpt_floor(dc_fixpt_mul_int(bpp_x16, bpp_increment_div)));
	bpp_x16 = dc_fixpt_div_int(bpp_x16, bpp_increment_div);
	bpp_x16 = dc_fixpt_mul_int(bpp_x16, 16);
	return dc_fixpt_floor(bpp_x16);
}

static bool convert_bandwidth_to_frl_params(
	int bandwidth_kbps,
	int *num_lanes,
	int *frl_rate)
{
	if (bandwidth_kbps == 0)
		return false;

	switch (bandwidth_kbps) {
	case 9000000:
		*num_lanes = 3;
		*frl_rate = 3000;
		break;
	case 18000000:
		*num_lanes = 3;
		*frl_rate = 6000;
		break;
	case 24000000:
		*num_lanes = 4;
		*frl_rate = 6000;
		break;
	case 32000000:
		*num_lanes = 4;
		*frl_rate = 8000;
		break;
	case 40000000:
		*num_lanes = 4;
		*frl_rate = 10000;
		break;
	case 48000000:
		*num_lanes = 4;
		*frl_rate = 12000;
		break;
	default:
		return false;
	}
	return true;
}

static uint32_t compute_bpp_x16_from_frl_params(
		const struct dc_crtc_timing *timing,
		const uint32_t num_slices_h,
		const struct dsc_enc_caps *dsc_caps)
{
	struct fixed31_32 pixel_clock;
	uint32_t num_lanes = dsc_caps->num_lanes;
	uint32_t frl_rate = dsc_caps->frl_rate;
	uint32_t h_active = timing->h_addressable;
	uint32_t h_blank = timing->h_total - timing->h_addressable;
	uint32_t bpp_target_x16;
	struct fixed31_32 r_bit;
	uint32_t f_audio = 48000;
	struct fixed31_32 pixel_rate_tolerance;
	uint32_t audio_tolerance = 1000;
	uint32_t frl_bit_tolerance = 300;
	uint32_t acr_rate_max = 1500;
	uint32_t c_frl_cb = 510;
	uint32_t c_frl_sb;
	struct fixed31_32 overhead_sb;
	struct fixed31_32 overhead_rs;
	struct fixed31_32 overhead_map;
	struct fixed31_32 overhead_min;
	struct fixed31_32 overhead_m;
	struct fixed31_32 overhead_max;
	struct fixed31_32 pixel_clock_max;
	struct fixed31_32 t_line;
	struct fixed31_32 r_bit_min;
	struct fixed31_32 r_frl_char_min;
	struct fixed31_32 c_frl_line;
	uint32_t c_frl_line_int;
	struct fixed31_32 c_frl_available;
	uint32_t c_frl_av_int;
	struct fixed31_32 c_frl_active_av;
	struct fixed31_32 c_frl_blank_av;
	uint32_t acat_ap = 4;
	struct fixed31_32 r_ap;
	struct fixed31_32 max_audio_tol_rate;
	struct fixed31_32 avg_audio_packets_line;
	uint32_t avg_audio_packets_line_int;
	int hc_blank_audio_min;
	uint32_t bytes_target;
	uint32_t hc_active_target;
	uint32_t hc_blank_target_est1;
	uint32_t hc_blank_target_est2;
	struct fixed31_32 hc_blank_target_bandwidth;
	int hc_blank_target;
	uint32_t bpc_factor = 8;
	uint32_t min_dsc_bpp_x16;
	uint32_t max_dsc_bpp_x16;
	bool hdmi_all_bpp = dsc_caps->is_vic_all_bpp;
	uint32_t slice_width;

	if (timing->display_color_depth == COLOR_DEPTH_101010)
		bpc_factor = 10;
	if (timing->display_color_depth == COLOR_DEPTH_121212)
		bpc_factor = 12;

	/* Assuming: bpc as 8*/
	if (timing->pixel_encoding == PIXEL_ENCODING_RGB ||
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
		min_dsc_bpp_x16 = 8 * 16;
		max_dsc_bpp_x16 = 3 * 16 * bpc_factor;
	} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		min_dsc_bpp_x16 = 7 * 16;
		max_dsc_bpp_x16 = 2 * 16 * bpc_factor;
	} else {
		min_dsc_bpp_x16 = 6 * 16;
		max_dsc_bpp_x16 = 3 * 16 * bpc_factor / 2;
	}

	max_dsc_bpp_x16 = MIN(max_dsc_bpp_x16, 256);
	if (!hdmi_all_bpp)
		max_dsc_bpp_x16 = MIN(max_dsc_bpp_x16, 192);

	c_frl_sb = 4 * c_frl_cb + num_lanes;
	pixel_clock = dc_fixpt_div_int(dc_fixpt_from_int(timing->pix_clk_100hz), 10000);
	r_bit = dc_fixpt_from_int(frl_rate);
	pixel_rate_tolerance = dc_fixpt_div_int(dc_fixpt_from_int(5), 1000);
	overhead_sb = dc_fixpt_div_int(dc_fixpt_from_int(num_lanes), c_frl_sb);
	overhead_rs = dc_fixpt_div_int(dc_fixpt_from_int(32), c_frl_sb);
	overhead_map = dc_fixpt_div_int(dc_fixpt_from_int(25), (c_frl_sb * 10));
	overhead_min = dc_fixpt_add(overhead_sb, overhead_rs);
	overhead_min = dc_fixpt_add(overhead_min, overhead_map);
	overhead_m = dc_fixpt_div_int(dc_fixpt_from_int(3), 1000);
	overhead_max = dc_fixpt_add(overhead_min, overhead_m);
	pixel_rate_tolerance = dc_fixpt_add_int(pixel_rate_tolerance, 1);
	pixel_clock_max = dc_fixpt_mul(pixel_clock, pixel_rate_tolerance);
	t_line = dc_fixpt_div(dc_fixpt_from_int(h_active + h_blank), pixel_clock_max);
	r_bit_min = dc_fixpt_div_int(dc_fixpt_from_int(frl_bit_tolerance), 1000000);
	r_bit_min = dc_fixpt_sub(dc_fixpt_from_int(1), r_bit_min);
	r_bit_min = dc_fixpt_mul(r_bit, r_bit_min);
	r_frl_char_min = dc_fixpt_div_int(r_bit_min, 18);
	c_frl_line = dc_fixpt_mul(t_line, r_frl_char_min);
	c_frl_line = dc_fixpt_mul_int(c_frl_line, num_lanes);
	c_frl_line_int = dc_fixpt_floor(c_frl_line);
	c_frl_available = dc_fixpt_sub(dc_fixpt_from_int(1), overhead_max);
	c_frl_available = dc_fixpt_mul_int(c_frl_available, c_frl_line_int);
	c_frl_av_int = dc_fixpt_floor(c_frl_available);
	c_frl_active_av = dc_fixpt_mul_int(dc_fixpt_from_int(c_frl_av_int), h_active);
	c_frl_active_av = dc_fixpt_div_int(c_frl_active_av, (h_active + h_blank));
	c_frl_blank_av = dc_fixpt_mul_int(dc_fixpt_from_int(c_frl_av_int), h_blank);
	c_frl_blank_av =  dc_fixpt_div_int(c_frl_blank_av, (h_active + h_blank));
	r_ap = dc_fixpt_max(dc_fixpt_from_int(192000),
			dc_fixpt_from_int(f_audio * acat_ap));
	r_ap = dc_fixpt_add(r_ap, dc_fixpt_from_int(2 * acr_rate_max));
	max_audio_tol_rate =  dc_fixpt_div_int(dc_fixpt_from_int(audio_tolerance), 1000000);
	max_audio_tol_rate =  dc_fixpt_add(dc_fixpt_from_int(1), max_audio_tol_rate);
	r_ap = dc_fixpt_mul(r_ap, max_audio_tol_rate);
	avg_audio_packets_line = dc_fixpt_mul(r_ap, t_line);
	avg_audio_packets_line = dc_fixpt_div_int(avg_audio_packets_line, 1000000);
	avg_audio_packets_line_int = dc_fixpt_ceil(avg_audio_packets_line);
	hc_blank_audio_min = 32 + 32 * avg_audio_packets_line_int;
	slice_width = dc_fixpt_ceil(dc_fixpt_div_int(
			dc_fixpt_from_int(h_active), num_slices_h));

	/* Slice width for 420 must be even */
	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 && slice_width % 2 != 0) {
		slice_width++;
	}

	for (uint32_t i = max_dsc_bpp_x16; i >= min_dsc_bpp_x16; i--) {
		bpp_target_x16 = i;
		bytes_target = num_slices_h * dc_fixpt_ceil(dc_fixpt_div_int(
				dc_fixpt_from_int(bpp_target_x16 * slice_width), 8 * 16));
		hc_active_target = dc_fixpt_ceil(dc_fixpt_div_int(
				dc_fixpt_from_int(bytes_target), 3));
		hc_blank_target_est1 = dc_fixpt_ceil(dc_fixpt_div_int(
				dc_fixpt_from_int(hc_active_target * h_blank), h_active));
		hc_blank_target_est2 = dc_fixpt_floor(dc_fixpt_max(
				dc_fixpt_from_int(hc_blank_target_est1),
				dc_fixpt_from_int(hc_blank_audio_min)));
		hc_blank_target_bandwidth = dc_fixpt_div_int(dc_fixpt_from_int(3), 2);
		hc_blank_target_bandwidth = dc_fixpt_mul(hc_blank_target_bandwidth,
				dc_fixpt_from_int(hc_active_target));
		hc_blank_target_bandwidth = dc_fixpt_sub(dc_fixpt_from_int(c_frl_av_int),
				hc_blank_target_bandwidth);
		hc_blank_target_bandwidth = dc_fixpt_min(hc_blank_target_bandwidth,
				dc_fixpt_from_int(hc_blank_target_est2));
		hc_blank_target_bandwidth = dc_fixpt_div_int(hc_blank_target_bandwidth, 4);
		hc_blank_target = dc_fixpt_floor(hc_blank_target_bandwidth) * 4;
		if (hc_blank_target >= hc_blank_audio_min)
			return bpp_target_x16;
	}
	return 0;
}
/* Decide DSC bandwidth range based on signal, timing, specs specific and input min and max
 * requirements.
 * The range output includes decided min/max target bpp, the respective bandwidth requirements
 * and native timing bandwidth requirement when DSC is not used.
 */
static bool decide_dsc_bandwidth_range(
		const uint32_t min_bpp_x16,
		const uint32_t max_bpp_x16,
		const uint32_t num_slices_h,
		const struct dsc_enc_caps *dsc_caps,
		const struct dc_crtc_timing *timing,
		const enum dc_link_encoding_format link_encoding,
		struct dc_dsc_bw_range *range)
{
	uint32_t preferred_bpp_x16 = timing->dsc_fixed_bits_per_pixel_x16;

	memset(range, 0, sizeof(*range));

	/* apply signal, timing, specs and explicitly specified DSC range requirements */
	if (preferred_bpp_x16) {
		if (preferred_bpp_x16 <= max_bpp_x16 &&
				preferred_bpp_x16 >= min_bpp_x16) {
			range->max_target_bpp_x16 = preferred_bpp_x16;
			range->min_target_bpp_x16 = preferred_bpp_x16;
		}
	}
	else if (dsc_caps->is_frl) {
		uint32_t specs_preferred_bpp_x16 = hdmi_dsc_get_bpp(timing, dsc_caps);
		uint32_t specs_calculated_bpp_x16 = 0;

		if (timing->vic) {
			/* For CTA timing, we should strictly follow HDMI spec. */
			range->max_target_bpp_x16 = specs_preferred_bpp_x16;
			if (dsc_caps->is_vic_all_bpp || dsc_caps->is_dp)
				range->min_target_bpp_x16 = min_bpp_x16;
			else
				range->min_target_bpp_x16 = specs_preferred_bpp_x16;
		} else {
			if (timing->vic == 0 && timing->hdmi_vic == 0)
				specs_calculated_bpp_x16 = compute_bpp_x16_from_frl_params(
						timing, num_slices_h, dsc_caps);

			if (specs_calculated_bpp_x16 != 0)
				specs_preferred_bpp_x16 = MIN(specs_calculated_bpp_x16,
						specs_preferred_bpp_x16);

			range->max_target_bpp_x16 = MIN(max_bpp_x16, specs_preferred_bpp_x16);
			range->min_target_bpp_x16 = min_bpp_x16;
		}
	}
	/* TODO - make this value generic to all signal types */
	else if (dsc_caps->edp_sink_max_bits_per_pixel) {
		/* apply max bpp limitation from edp sink */
		range->max_target_bpp_x16 = MIN(dsc_caps->edp_sink_max_bits_per_pixel,
				max_bpp_x16);
		range->min_target_bpp_x16 = min_bpp_x16;
	}
	else {
		range->max_target_bpp_x16 = max_bpp_x16;
		range->min_target_bpp_x16 = min_bpp_x16;
	}

	/* populate output structure */
	if (range->max_target_bpp_x16 >= range->min_target_bpp_x16 && range->min_target_bpp_x16 > 0) {
		/* native stream bandwidth */
		range->stream_kbps = dc_bandwidth_in_kbps_from_timing(timing, link_encoding);

		/* max dsc target bpp */
		range->max_kbps = dc_dsc_stream_bandwidth_in_kbps(timing,
				range->max_target_bpp_x16, num_slices_h, dsc_caps->is_dp);

		/* min dsc target bpp */
		range->min_kbps = dc_dsc_stream_bandwidth_in_kbps(timing,
				range->min_target_bpp_x16, num_slices_h, dsc_caps->is_dp);
	}

	return range->max_kbps >= range->min_kbps && range->min_kbps > 0;
}

/* Decides if DSC should be used and calculates target bpp if it should, applying DSC policy.
 *
 * Returns:
 *     - 'true' if target bpp is decided
 *     - 'false' if target bpp cannot be decided (e.g. cannot fit even with min DSC bpp),
 */
static bool decide_dsc_target_bpp_x16(
		const struct dc_dsc_policy *policy,
		const struct dc_dsc_config_options *options,
		const struct dsc_enc_caps *dsc_common_caps,
		const int target_bandwidth_kbps,
		const struct dc_crtc_timing *timing,
		const int num_slices_h,
		const enum dc_link_encoding_format link_encoding,
		int *target_bpp_x16)
{
	struct dc_dsc_bw_range range;
	uint32_t target_bandwidth_kbps_u = (uint32_t)target_bandwidth_kbps;

	*target_bpp_x16 = 0;

	if (decide_dsc_bandwidth_range(policy->min_target_bpp * 16, policy->max_target_bpp * 16,
			num_slices_h, dsc_common_caps, timing, link_encoding, &range)) {
		if (target_bandwidth_kbps_u >= range.stream_kbps) {
			if (policy->enable_dsc_when_not_needed || options->force_dsc_when_not_needed)
				/* enable max bpp even dsc is not needed */
				*target_bpp_x16 = range.max_target_bpp_x16;
		} else if (target_bandwidth_kbps_u >= range.max_kbps) {
			/* use max target bpp allowed */
			*target_bpp_x16 = range.max_target_bpp_x16;
		} else if (target_bandwidth_kbps_u >= range.min_kbps) {
			/* use target bpp that can take entire target bandwidth */
			*target_bpp_x16 = compute_bpp_x16_from_target_bandwidth(
					target_bandwidth_kbps, timing, num_slices_h,
					dsc_common_caps->bpp_increment_div,
					dsc_common_caps->is_dp);
		}
		/* Assign minimum bpp and validate TB borrow scenario later */
		if (target_bandwidth_kbps < range.min_kbps)
			if (dsc_common_caps->is_frl)
				*target_bpp_x16 = range.min_target_bpp_x16;
	}

	return *target_bpp_x16 != 0;
}

#define MIN_AVAILABLE_SLICES_SIZE  6

static int get_available_dsc_slices(union dsc_enc_slice_caps slice_caps, int *available_slices)
{
	int idx = 0;

	if (slice_caps.bits.NUM_SLICES_1)
		available_slices[idx++] = 1;

	if (slice_caps.bits.NUM_SLICES_2)
		available_slices[idx++] = 2;

	if (slice_caps.bits.NUM_SLICES_4)
		available_slices[idx++] = 4;

	if (slice_caps.bits.NUM_SLICES_8)
		available_slices[idx++] = 8;

	if (slice_caps.bits.NUM_SLICES_12)
		available_slices[idx++] = 12;

	if (slice_caps.bits.NUM_SLICES_16)
		available_slices[idx++] = 16;

	return idx;
}


static int get_max_dsc_slices(union dsc_enc_slice_caps slice_caps)
{
	int max_slices = 0;
	int available_slices[MIN_AVAILABLE_SLICES_SIZE];
	int end_idx = get_available_dsc_slices(slice_caps, &available_slices[0]);

	if (end_idx > 0)
		max_slices = available_slices[end_idx - 1];

	return max_slices;
}


// Increment slice number in available slice numbers stops if possible, or just increment if not
static int inc_num_slices(union dsc_enc_slice_caps slice_caps, int num_slices)
{
	// Get next bigger num slices available in common caps
	int available_slices[MIN_AVAILABLE_SLICES_SIZE];
	int end_idx;
	int i;
	int new_num_slices = num_slices;

	end_idx = get_available_dsc_slices(slice_caps, &available_slices[0]);
	if (end_idx == 0) {
		// No available slices found
		new_num_slices++;
		return new_num_slices;
	}

	// Numbers of slices found - get the next bigger number
	for (i = 0; i < end_idx; i++) {
		if (new_num_slices < available_slices[i]) {
			new_num_slices = available_slices[i];
			break;
		}
	}

	if (new_num_slices == num_slices) // No bigger number of slices found
		new_num_slices++;

	return new_num_slices;
}


// Decrement slice number in available slice numbers stops if possible, or just decrement if not. Stop at zero.
static int dec_num_slices(union dsc_enc_slice_caps slice_caps, int num_slices)
{
	// Get next bigger num slices available in common caps
	int available_slices[MIN_AVAILABLE_SLICES_SIZE];
	int end_idx;
	int i;
	int new_num_slices = num_slices;

	end_idx = get_available_dsc_slices(slice_caps, &available_slices[0]);
	if (end_idx == 0 && new_num_slices > 0) {
		// No numbers of slices found
		new_num_slices++;
		return new_num_slices;
	}

	// Numbers of slices found - get the next smaller number
	for (i = end_idx - 1; i >= 0; i--) {
		if (new_num_slices > available_slices[i]) {
			new_num_slices = available_slices[i];
			break;
		}
	}

	if (new_num_slices == num_slices) {
		// No smaller number of slices found
		new_num_slices--;
		if (new_num_slices < 0)
			new_num_slices = 0;
	}

	return new_num_slices;
}


// Choose next bigger number of slices if the requested number of slices is not available
static int fit_num_slices_up(union dsc_enc_slice_caps slice_caps, int num_slices)
{
	// Get next bigger num slices available in common caps
	int available_slices[MIN_AVAILABLE_SLICES_SIZE];
	int end_idx;
	int i;
	int new_num_slices = num_slices;

	end_idx = get_available_dsc_slices(slice_caps, &available_slices[0]);
	if (end_idx == 0) {
		// No available slices found
		new_num_slices++;
		return new_num_slices;
	}

	// Numbers of slices found - get the equal or next bigger number
	for (i = 0; i < end_idx; i++) {
		if (new_num_slices <= available_slices[i]) {
			new_num_slices = available_slices[i];
			break;
		}
	}

	return new_num_slices;
}


/* Attempts to set DSC configuration for the stream, applying DSC policy.
 * Returns 'true' if successful or 'false' if not.
 *
 * Parameters:
 *
 * dsc_sink_caps       - DSC sink decoder capabilities (from DPCD)
 *
 * dsc_enc_caps        - DSC encoder capabilities
 *
 * target_bandwidth_kbps  - Target bandwidth to fit the stream into.
 *                          If 0, do not calculate target bpp.
 *
 * timing              - The stream timing to fit into 'target_bandwidth_kbps' or apply
 *                       maximum compression to, if 'target_badwidth == 0'
 *
 * dsc_cfg             - DSC configuration to use if it was possible to come up with
 *                       one for the given inputs.
 *                       The target bitrate after DSC can be calculated by multiplying
 *                       dsc_cfg.bits_per_pixel (in U6.4 format) by pixel rate, e.g.
 *
 *                       dsc_stream_bitrate_kbps = (int)ceil(timing->pix_clk_khz * dsc_cfg.bits_per_pixel / 16.0);
 */
static bool setup_dsc_config(
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dsc_enc_caps *dsc_enc_caps,
		int target_bandwidth_kbps,
		const struct dc_crtc_timing *timing,
		const struct dc_dsc_config_options *options,
		const enum dc_link_encoding_format link_encoding,
		int min_slices_h,
		struct dc_dsc_config *dsc_cfg)
{
	struct dsc_enc_caps dsc_common_caps;
	int max_slices_h = 0;
	int num_slices_h = 0;
	int pic_width;
	uint32_t pic_width_u;
	int slice_width;
	int target_bpp;
	int sink_per_slice_throughput_mps;
	uint32_t branch_max_throughput_mps = 0;
	bool is_dsc_possible = false;
	int pic_height;
	int slice_height;
	struct dc_dsc_policy policy;
	int num_lanes;
	int frl_rate;

	memset(dsc_cfg, 0, sizeof(struct dc_dsc_config));

	dc_dsc_get_policy_for_timing(timing, options->max_target_bpp_limit_override_x16, &policy, link_encoding);
	pic_width = timing->h_addressable + timing->h_border_left + timing->h_border_right;
	pic_width_u = (uint32_t)pic_width;
	pic_height = timing->v_addressable + timing->v_border_top + timing->v_border_bottom;

	if (!dsc_sink_caps->is_dsc_supported)
		goto done;

	if (dsc_sink_caps->branch_max_line_width && dsc_sink_caps->branch_max_line_width < pic_width_u)
		goto done;

	// Intersect decoder with encoder DSC caps and validate DSC settings
	is_dsc_possible = intersect_dsc_caps(dsc_sink_caps, dsc_enc_caps, timing->pixel_encoding, &dsc_common_caps);
	if (!is_dsc_possible)
		goto done;
	if (convert_bandwidth_to_frl_params(
					target_bandwidth_kbps, &num_lanes, &frl_rate)) {
		dsc_common_caps.num_lanes = num_lanes;
		dsc_common_caps.frl_rate = frl_rate;
	}

	sink_per_slice_throughput_mps = 0;

	// Validate available DSC settings against the mode timing

	// Validate color format (and pick up the throughput values)
	dsc_cfg->ycbcr422_simple = false;
	switch (timing->pixel_encoding)	{
	case PIXEL_ENCODING_RGB:
		is_dsc_possible = (bool)dsc_common_caps.color_formats.bits.RGB;
		sink_per_slice_throughput_mps = dsc_sink_caps->throughput_mode_0_mps;
		branch_max_throughput_mps = dsc_sink_caps->branch_overall_throughput_0_mps;
		break;
	case PIXEL_ENCODING_YCBCR444:
		is_dsc_possible = (bool)dsc_common_caps.color_formats.bits.YCBCR_444;
		sink_per_slice_throughput_mps = dsc_sink_caps->throughput_mode_0_mps;
		branch_max_throughput_mps = dsc_sink_caps->branch_overall_throughput_0_mps;
		break;
	case PIXEL_ENCODING_YCBCR422:
		if (policy.ycbcr422_simple) {
			is_dsc_possible = (bool)dsc_common_caps.color_formats.bits.YCBCR_SIMPLE_422;
			dsc_cfg->ycbcr422_simple = is_dsc_possible;
			sink_per_slice_throughput_mps = dsc_sink_caps->throughput_mode_0_mps;
		} else {
			is_dsc_possible = (bool)dsc_common_caps.color_formats.bits.YCBCR_NATIVE_422;
			sink_per_slice_throughput_mps = dsc_sink_caps->throughput_mode_1_mps;
			branch_max_throughput_mps = dsc_sink_caps->branch_overall_throughput_1_mps;
		}
		break;
	case PIXEL_ENCODING_YCBCR420:
		is_dsc_possible = (bool)dsc_common_caps.color_formats.bits.YCBCR_NATIVE_420;
		sink_per_slice_throughput_mps = dsc_sink_caps->throughput_mode_1_mps;
		branch_max_throughput_mps = dsc_sink_caps->branch_overall_throughput_1_mps;
		break;
	default:
		is_dsc_possible = false;
	}

	// Validate branch's maximum throughput
	if (branch_max_throughput_mps && dsc_div_by_10_round_up(timing->pix_clk_100hz) > branch_max_throughput_mps * 1000)
		is_dsc_possible = false;

	if (!is_dsc_possible)
		goto done;

	// Color depth
	switch (timing->display_color_depth) {
	case COLOR_DEPTH_888:
		is_dsc_possible = (bool)dsc_common_caps.color_depth.bits.COLOR_DEPTH_8_BPC;
		break;
	case COLOR_DEPTH_101010:
		is_dsc_possible = (bool)dsc_common_caps.color_depth.bits.COLOR_DEPTH_10_BPC;
		break;
	case COLOR_DEPTH_121212:
		is_dsc_possible = (bool)dsc_common_caps.color_depth.bits.COLOR_DEPTH_12_BPC;
		break;
	default:
		is_dsc_possible = false;
	}

	if (!is_dsc_possible)
		goto done;

	// Slice width (i.e. number of slices per line)
	max_slices_h = get_max_dsc_slices(dsc_common_caps.slice_caps);

	while (max_slices_h > 0) {
		if (pic_width % max_slices_h == 0)
			break;

		max_slices_h = dec_num_slices(dsc_common_caps.slice_caps, max_slices_h);
	}

	is_dsc_possible = (dsc_common_caps.max_slice_width > 0);
	if (!is_dsc_possible)
		goto done;

	/* increase minimum slice count to meet sink slice width limitations */
	min_slices_h = dc_fixpt_ceil(dc_fixpt_max(
			dc_fixpt_div_int(dc_fixpt_from_int(pic_width), dsc_common_caps.max_slice_width), // sink min
			dc_fixpt_from_int(min_slices_h))); // source min

	min_slices_h = fit_num_slices_up(dsc_common_caps.slice_caps, min_slices_h);

	/* increase minimum slice count to meet sink throughput limitations */
	while (min_slices_h <= max_slices_h) {
		int pix_clk_per_slice_khz = dsc_div_by_10_round_up(timing->pix_clk_100hz) / min_slices_h;
		if (pix_clk_per_slice_khz <= sink_per_slice_throughput_mps * 1000)
			break;

		min_slices_h = inc_num_slices(dsc_common_caps.slice_caps, min_slices_h);
	}

	/* increase minimum slice count to meet divisibility requirements */
	while (pic_width % min_slices_h != 0 && min_slices_h <= max_slices_h) {
		min_slices_h = inc_num_slices(dsc_common_caps.slice_caps, min_slices_h);
	}

	is_dsc_possible = (min_slices_h <= max_slices_h) && max_slices_h != 0;
	if (!is_dsc_possible)
		goto done;

	if (policy.use_min_slices_h) {
		if (min_slices_h > 0)
			num_slices_h = min_slices_h;
		else if (max_slices_h > 0) { // Fall back to max slices if min slices is not working out
			if (policy.max_slices_h)
				num_slices_h = min(policy.max_slices_h, max_slices_h);
			else
				num_slices_h = max_slices_h;
		} else
			is_dsc_possible = false;
	} else {
		if (max_slices_h > 0) {
			if (policy.max_slices_h)
				num_slices_h = min(policy.max_slices_h, max_slices_h);
			else
				num_slices_h = max_slices_h;
		} else if (min_slices_h > 0) // Fall back to min slices if max slices is not possible
			num_slices_h = min_slices_h;
		else
			is_dsc_possible = false;
	}
	if (dsc_sink_caps->is_frl)
		num_slices_h = hdmi_dsc_get_num_slices(timing);
	// When we force ODM, num dsc h slices must be divisible by num odm h slices
	switch (options->dsc_force_odm_hslice_override) {
	case 0:
	case 1:
		break;
	case 2:
		if (num_slices_h < 2)
			num_slices_h = fit_num_slices_up(dsc_common_caps.slice_caps, 2);
		break;
	case 3:
		if (dsc_common_caps.slice_caps.bits.NUM_SLICES_12)
			num_slices_h = 12;
		else
			num_slices_h = 0;
		break;
	case 4:
		if (num_slices_h < 4)
			num_slices_h = fit_num_slices_up(dsc_common_caps.slice_caps, 4);
		break;
	default:
		break;
	}
	if (num_slices_h == 0)
		is_dsc_possible = false;
	if (!is_dsc_possible)
		goto done;

	dsc_cfg->num_slices_h = num_slices_h;
	slice_width = pic_width / num_slices_h;

	is_dsc_possible = slice_width <= dsc_common_caps.max_slice_width;
	if (!is_dsc_possible)
		goto done;

	// Slice height (i.e. number of slices per column): start with policy and pick the first one that height is divisible by.
	// For 4:2:0 make sure the slice height is divisible by 2 as well.
	if (options->dsc_min_slice_height_override == 0)
		slice_height = min(policy.min_slice_height, pic_height);
	else
		slice_height = min((int)(options->dsc_min_slice_height_override), pic_height);

	while (slice_height < pic_height && (pic_height % slice_height != 0 ||
		slice_height % options->slice_height_granularity != 0 ||
		(timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 && slice_height % 2 != 0)))
		slice_height++;

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) // For the case when pic_height < dsc_policy.min_sice_height
		is_dsc_possible = (slice_height % 2 == 0);

	if (!is_dsc_possible)
		goto done;

	if (slice_height > 0) {
		dsc_cfg->num_slices_v = pic_height / slice_height;
	} else {
		is_dsc_possible = false;
		goto done;
	}

	if (target_bandwidth_kbps > 0) {
		is_dsc_possible = decide_dsc_target_bpp_x16(
				&policy,
				options,
				&dsc_common_caps,
				target_bandwidth_kbps,
				timing,
				num_slices_h,
				link_encoding,
				&target_bpp);
		dsc_cfg->bits_per_pixel = target_bpp;
	}
	if (!is_dsc_possible)
		goto done;

	/* Fill out the rest of DSC settings */
	dsc_cfg->block_pred_enable = dsc_common_caps.is_block_pred_supported;
	dsc_cfg->linebuf_depth = dsc_common_caps.lb_bit_depth;
	dsc_cfg->version_minor = (dsc_common_caps.dsc_version & 0xf0) >> 4;
	dsc_cfg->is_frl = dsc_sink_caps->is_frl;
	if (dsc_cfg->is_frl)
		dsc_cfg->num_slices_h = num_slices_h;
	dsc_cfg->is_vic_all_bpp = dsc_sink_caps->is_vic_all_bpp;
	dsc_cfg->total_chunk_kbytes = dsc_sink_caps->total_chunk_kbytes;
	dsc_cfg->is_dp = dsc_sink_caps->is_dp;

done:
	if (!is_dsc_possible)
		memset(dsc_cfg, 0, sizeof(struct dc_dsc_config));

	return is_dsc_possible;
}

bool dc_dsc_compute_config(
		const struct display_stream_compressor *dsc,
		const struct dsc_dec_dpcd_caps *dsc_sink_caps,
		const struct dc_dsc_config_options *options,
		uint32_t target_bandwidth_kbps,
		const struct dc_crtc_timing *timing,
		const enum dc_link_encoding_format link_encoding,
		struct dc_dsc_config *dsc_cfg)
{
	bool is_dsc_possible = false;
	struct dsc_enc_caps dsc_enc_caps;
	unsigned int min_dsc_slice_count;
	get_dsc_enc_caps(dsc, &dsc_enc_caps, timing->pix_clk_100hz);

	min_dsc_slice_count = get_min_dsc_slice_count_for_odm(dsc, &dsc_enc_caps, timing);

	is_dsc_possible = setup_dsc_config(dsc_sink_caps,
		&dsc_enc_caps,
		target_bandwidth_kbps,
		timing,
		options,
		link_encoding,
		min_dsc_slice_count,
		dsc_cfg);
	return is_dsc_possible;
}

uint32_t dc_dsc_stream_bandwidth_in_kbps(const struct dc_crtc_timing *timing,
	uint32_t bpp_x16, uint32_t num_slices_h, bool is_dp)
{
	uint32_t overhead_in_kbps;
	struct fixed31_32 bpp;
	struct fixed31_32 actual_bandwidth_in_kbps;

	overhead_in_kbps = dc_dsc_stream_bandwidth_overhead_in_kbps(
		timing, num_slices_h, is_dp);
	bpp = dc_fixpt_from_fraction(bpp_x16, 16);
	actual_bandwidth_in_kbps = dc_fixpt_from_fraction(timing->pix_clk_100hz, 10);
	actual_bandwidth_in_kbps = dc_fixpt_mul(actual_bandwidth_in_kbps, bpp);
	actual_bandwidth_in_kbps = dc_fixpt_add_int(actual_bandwidth_in_kbps, overhead_in_kbps);
	return dc_fixpt_ceil(actual_bandwidth_in_kbps);
}

uint32_t dc_dsc_stream_bandwidth_overhead_in_kbps(
		const struct dc_crtc_timing *timing,
		const uint32_t num_slices_h,
		const bool is_dp)
{
	struct fixed31_32 max_dsc_overhead;
	struct fixed31_32 refresh_rate;

	if (dsc_policy_disable_dsc_stream_overhead || !is_dp)
		return 0;

	/* use target bpp that can take entire target bandwidth */
	refresh_rate = dc_fixpt_from_int(timing->pix_clk_100hz);
	refresh_rate = dc_fixpt_div_int(refresh_rate, timing->h_total);
	refresh_rate = dc_fixpt_div_int(refresh_rate, timing->v_total);
	refresh_rate = dc_fixpt_mul_int(refresh_rate, 100);

	max_dsc_overhead = dc_fixpt_from_int(num_slices_h);
	max_dsc_overhead = dc_fixpt_mul_int(max_dsc_overhead, timing->v_total);
	max_dsc_overhead = dc_fixpt_mul_int(max_dsc_overhead, 256);
	max_dsc_overhead = dc_fixpt_div_int(max_dsc_overhead, 1000);
	max_dsc_overhead = dc_fixpt_mul(max_dsc_overhead, refresh_rate);

	return dc_fixpt_ceil(max_dsc_overhead);
}

void dc_dsc_get_policy_for_timing(const struct dc_crtc_timing *timing,
		uint32_t max_target_bpp_limit_override_x16,
		struct dc_dsc_policy *policy,
		const enum dc_link_encoding_format link_encoding)
{
	uint32_t bpc = 0;

	policy->min_target_bpp = 0;
	policy->max_target_bpp = 0;

	/* DSC Policy: Use minimum number of slices that fits the pixel clock */
	policy->use_min_slices_h = true;

	/* DSC Policy: Use max available slices
	 * (in our case 4 for or 8, depending on the mode)
	 */
	policy->max_slices_h = 0;

	/* DSC Policy: Use slice height recommended
	 * by VESA DSC Spreadsheet user guide
	 */
	policy->min_slice_height = 108;

	/* DSC Policy: follow DP specs with an internal upper limit to 16 bpp
	 * for better interoperability
	 */
	switch (timing->display_color_depth) {
	case COLOR_DEPTH_888:
		bpc = 8;
		break;
	case COLOR_DEPTH_101010:
		bpc = 10;
		break;
	case COLOR_DEPTH_121212:
		bpc = 12;
		break;
	default:
		return;
	}
	switch (timing->pixel_encoding) {
	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
	case PIXEL_ENCODING_YCBCR422: /* assume no YCbCr422 native support */
		/* DP specs limits to 8 */
		policy->min_target_bpp = 8;
		/* DP specs limits to 3 x bpc */
		policy->max_target_bpp = 3 * bpc;
		policy->ycbcr422_simple = true;
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 && link_encoding == DC_LINK_ENCODING_HDMI_FRL) {
			/* HDMI FRL YCbCr422 native support */
			policy->min_target_bpp = 7;
			policy->max_target_bpp = 2 * bpc;
			policy->ycbcr422_simple = false;
		}
		break;
	case PIXEL_ENCODING_YCBCR420:
		/* DP specs limits to 6 */
		policy->min_target_bpp = 6;
		/* DP specs limits to 1.5 x bpc assume bpc is an even number */
		policy->max_target_bpp = bpc * 3 / 2;
		break;
	default:
		return;
	}

	/* internal upper limit, default 16 bpp */
	if (policy->max_target_bpp > dsc_policy_max_target_bpp_limit)
		policy->max_target_bpp = dsc_policy_max_target_bpp_limit;

	/* apply override */
	if (max_target_bpp_limit_override_x16 && policy->max_target_bpp > max_target_bpp_limit_override_x16 / 16)
		policy->max_target_bpp = max_target_bpp_limit_override_x16 / 16;

	/* enable DSC when not needed, default false */
	policy->enable_dsc_when_not_needed = dsc_policy_enable_dsc_when_not_needed;
}

void dc_dsc_policy_set_max_target_bpp_limit(uint32_t limit)
{
	dsc_policy_max_target_bpp_limit = limit;
}

void dc_dsc_policy_set_enable_dsc_when_not_needed(bool enable)
{
	dsc_policy_enable_dsc_when_not_needed = enable;
}

void dc_dsc_policy_set_disable_dsc_stream_overhead(bool disable)
{
	dsc_policy_disable_dsc_stream_overhead = disable;
}

void dc_set_disable_128b_132b_stream_overhead(bool disable)
{
	disable_128b_132b_stream_overhead = disable;
}

void dc_dsc_get_default_config_option(const struct dc *dc, struct dc_dsc_config_options *options)
{
	options->dsc_min_slice_height_override = dc->debug.dsc_min_slice_height_override;
	options->dsc_force_odm_hslice_override = dc->debug.force_odm_combine;
	options->max_target_bpp_limit_override_x16 = 0;
	options->slice_height_granularity = 1;
	options->force_dsc_when_not_needed = false;
}

/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT

#include "dc.h"
#include "dsc.h"
#include "dc_hw_types.h"
#include <drm/drm_dp_helper.h>

#define DC_LOGGER \
	dsc->ctx->logger

static bool dsc_buff_block_size_from_dpcd(int dpcd_buff_block_size, int *buff_block_size);
static bool dsc_line_buff_depth_from_dpcd(int dpcd_line_buff_bit_depth, int *line_buff_bit_depth);
static bool dsc_throughput_from_dpcd(int dpcd_throughput, int *throughput);
static bool dsc_bpp_increment_div_from_dpcd(int bpp_increment_dpcd, uint32_t *bpp_increment_div);

void dsc_optc_config_log(struct display_stream_compressor *dsc,
		struct dsc_optc_config *config)
{
	DC_LOG_DSC("Setting optc DSC config at DSC inst %d", dsc->inst);
	DC_LOG_DSC("\n\tbytes_per_pixel %d\n\tis_pixel_format_444 %d\n\tslice_width %d",
			config->bytes_per_pixel,
			config->is_pixel_format_444, config->slice_width);
}

void dsc_config_log(struct display_stream_compressor *dsc,
		const struct dsc_config *config)
{
	DC_LOG_DSC("Setting DSC Config at DSC inst %d", dsc->inst);
	DC_LOG_DSC("\n\tnum_slices_h %d\n\tnum_slices_v %d\n\tbits_per_pixel %d\n\tcolor_depth %d",
		config->dc_dsc_cfg.num_slices_h,
		config->dc_dsc_cfg.num_slices_v,
		config->dc_dsc_cfg.bits_per_pixel,
		config->color_depth);
}


bool dsc_parse_dsc_dpcd(const uint8_t *dpcd_dsc_data, struct dsc_dec_dpcd_caps *dsc_sink_caps)
{
	dsc_sink_caps->is_dsc_supported = (dpcd_dsc_data[DP_DSC_SUPPORT - DP_DSC_SUPPORT] & DP_DSC_DECOMPRESSION_IS_SUPPORTED) != 0;
	if (!dsc_sink_caps->is_dsc_supported)
		return true;

	dsc_sink_caps->dsc_version = dpcd_dsc_data[DP_DSC_REV - DP_DSC_SUPPORT];

	{
		int buff_block_size;
		int buff_size;

		if (!dsc_buff_block_size_from_dpcd(dpcd_dsc_data[DP_DSC_RC_BUF_BLK_SIZE - DP_DSC_SUPPORT], &buff_block_size))
			return false;

		buff_size = dpcd_dsc_data[DP_DSC_RC_BUF_SIZE - DP_DSC_SUPPORT] + 1;
		dsc_sink_caps->rc_buffer_size = buff_size * buff_block_size;
	}

	dsc_sink_caps->slice_caps1.raw = dpcd_dsc_data[DP_DSC_SLICE_CAP_1 - DP_DSC_SUPPORT];
	if (!dsc_line_buff_depth_from_dpcd(dpcd_dsc_data[DP_DSC_LINE_BUF_BIT_DEPTH - DP_DSC_SUPPORT], &dsc_sink_caps->lb_bit_depth))
		return false;

	dsc_sink_caps->is_block_pred_supported =
		(dpcd_dsc_data[DP_DSC_BLK_PREDICTION_SUPPORT - DP_DSC_SUPPORT] & DP_DSC_BLK_PREDICTION_IS_SUPPORTED) != 0;

	dsc_sink_caps->edp_max_bits_per_pixel =
		dpcd_dsc_data[DP_DSC_MAX_BITS_PER_PIXEL_LOW - DP_DSC_SUPPORT] |
		dpcd_dsc_data[DP_DSC_MAX_BITS_PER_PIXEL_HI - DP_DSC_SUPPORT] << 8;

	dsc_sink_caps->color_formats.raw = dpcd_dsc_data[DP_DSC_DEC_COLOR_FORMAT_CAP - DP_DSC_SUPPORT];
	dsc_sink_caps->color_depth.raw = dpcd_dsc_data[DP_DSC_DEC_COLOR_DEPTH_CAP - DP_DSC_SUPPORT];

	{
		int dpcd_throughput = dpcd_dsc_data[DP_DSC_PEAK_THROUGHPUT - DP_DSC_SUPPORT];

		if (!dsc_throughput_from_dpcd(dpcd_throughput & DP_DSC_THROUGHPUT_MODE_0_MASK, &dsc_sink_caps->throughput_mode_0_mps))
			return false;

		dpcd_throughput = (dpcd_throughput & DP_DSC_THROUGHPUT_MODE_1_MASK) >> DP_DSC_THROUGHPUT_MODE_1_SHIFT;
		if (!dsc_throughput_from_dpcd(dpcd_throughput, &dsc_sink_caps->throughput_mode_1_mps))
			return false;
	}

	dsc_sink_caps->max_slice_width = dpcd_dsc_data[DP_DSC_MAX_SLICE_WIDTH - DP_DSC_SUPPORT] * 320;
	dsc_sink_caps->slice_caps2.raw = dpcd_dsc_data[DP_DSC_SLICE_CAP_2 - DP_DSC_SUPPORT];

	if (!dsc_bpp_increment_div_from_dpcd(dpcd_dsc_data[DP_DSC_BITS_PER_PIXEL_INC - DP_DSC_SUPPORT], &dsc_sink_caps->bpp_increment_div))
		return false;

	return true;
}


/* This module's internal functions */

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
			dm_error("%s: DPCD DSC buffer size not recoginzed.\n", __func__);
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
		dm_error("%s: DPCD DSC buffer depth not recoginzed.\n", __func__);
		return false;
	}

	return true;
}


static bool dsc_throughput_from_dpcd(int dpcd_throughput, int *throughput)
{
	switch (dpcd_throughput) {
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
			dm_error("%s: DPCD DSC througput mode not recoginzed.\n", __func__);
			return false;
		}
	}

	return true;
}


static bool dsc_bpp_increment_div_from_dpcd(int bpp_increment_dpcd, uint32_t *bpp_increment_div)
{

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
		dm_error("%s: DPCD DSC bits-per-pixel increment not recoginzed.\n", __func__);
		return false;
	}
	}

	return true;
}


#endif // CONFIG_DRM_AMD_DC_DSC_SUPPORT

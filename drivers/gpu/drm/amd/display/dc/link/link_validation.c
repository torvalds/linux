/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

/* FILE POLICY AND INTENDED USAGE:
 * This file owns timing validation against various link limitations. (ex.
 * link bandwidth, receiver capability or our hardware capability) It also
 * provides helper functions exposing bandwidth formulas used in validation.
 */
#include "link_validation.h"
#include "protocols/link_dp_capability.h"
#include "protocols/link_dp_dpia_bw.h"
#include "protocols/link_hdmi_frl.h"
#include "resource.h"

#define DC_LOGGER_INIT(logger)

static uint32_t get_tmds_output_pixel_clock_100hz(const struct dc_crtc_timing *timing)
{

	uint32_t pxl_clk = timing->pix_clk_100hz;

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pxl_clk /= 2;
	else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
		pxl_clk = pxl_clk * 2 / 3;

	if (timing->display_color_depth == COLOR_DEPTH_101010)
		pxl_clk = pxl_clk * 10 / 8;
	else if (timing->display_color_depth == COLOR_DEPTH_121212)
		pxl_clk = pxl_clk * 12 / 8;

	return pxl_clk;
}

static bool dp_active_dongle_validate_timing(
		const struct dc_crtc_timing *timing,
		const struct dpcd_caps *dpcd_caps)
{
	const struct dc_dongle_caps *dongle_caps = &dpcd_caps->dongle_caps;

	switch (dpcd_caps->dongle_type) {
	case DISPLAY_DONGLE_DP_VGA_CONVERTER:
	case DISPLAY_DONGLE_DP_DVI_CONVERTER:
	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		if (timing->pixel_encoding == PIXEL_ENCODING_RGB)
			return true;
		else
			return false;
	default:
		break;
	}

	if (dpcd_caps->dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER &&
			dongle_caps->extendedCapValid == true) {
		/* Check Pixel Encoding */
		switch (timing->pixel_encoding) {
		case PIXEL_ENCODING_RGB:
		case PIXEL_ENCODING_YCBCR444:
			break;
		case PIXEL_ENCODING_YCBCR422:
			if (!dongle_caps->is_dp_hdmi_ycbcr422_pass_through)
				return false;
			break;
		case PIXEL_ENCODING_YCBCR420:
			if (!dongle_caps->is_dp_hdmi_ycbcr420_pass_through)
				return false;
			break;
		case PIXEL_ENCODING_UNDEFINED:
			/* These color depths are currently not supported */
			ASSERT(false);
			break;
		default:
			/* Invalid Pixel Encoding*/
			return false;
		}

		switch (timing->display_color_depth) {
		case COLOR_DEPTH_666:
		case COLOR_DEPTH_888:
			/*888 and 666 should always be supported*/
			break;
		case COLOR_DEPTH_101010:
			if (dongle_caps->dp_hdmi_max_bpc < 10)
				return false;
			break;
		case COLOR_DEPTH_121212:
			if (dongle_caps->dp_hdmi_max_bpc < 12)
				return false;
			break;
		case COLOR_DEPTH_UNDEFINED:
			/* These color depths are currently not supported */
			ASSERT(false);
			break;
		case COLOR_DEPTH_141414:
		case COLOR_DEPTH_161616:
		default:
			/* These color depths are currently not supported */
			return false;
		}

		/* Check 3D format */
		switch (timing->timing_3d_format) {
		case TIMING_3D_FORMAT_NONE:
		case TIMING_3D_FORMAT_FRAME_ALTERNATE:
			/*Only frame alternate 3D is supported on active dongle*/
			break;
		default:
			/*other 3D formats are not supported due to bad infoframe translation */
			return false;
		}

		if (dongle_caps->dp_hdmi_frl_max_link_bw_in_kbps > 0) { // DP to HDMI FRL converter
			struct dc_crtc_timing outputTiming = *timing;

			if (timing->flags.DSC && !timing->dsc_cfg.is_frl)
				/* DP input has DSC, HDMI FRL output doesn't have DSC, remove DSC from output timing */
				outputTiming.flags.DSC = 0;
			if (dc_bandwidth_in_kbps_from_timing(&outputTiming, DC_LINK_ENCODING_HDMI_FRL) >
					dongle_caps->dp_hdmi_frl_max_link_bw_in_kbps)
				return false;
		} else { // DP to HDMI TMDS converter
			if (get_tmds_output_pixel_clock_100hz(timing) > (dongle_caps->dp_hdmi_max_pixel_clk_in_khz * 10))
				return false;
		}
	}

	if (dpcd_caps->channel_coding_cap.bits.DP_128b_132b_SUPPORTED == 0 &&
			dpcd_caps->dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_PASSTHROUGH_SUPPORT == 0 &&
			dongle_caps->dfp_cap_ext.supported) {

		if (dongle_caps->dfp_cap_ext.max_pixel_rate_in_mps < (timing->pix_clk_100hz / 10000))
			return false;

		if (dongle_caps->dfp_cap_ext.max_video_h_active_width < timing->h_addressable)
			return false;

		if (dongle_caps->dfp_cap_ext.max_video_v_active_height < timing->v_addressable)
			return false;

		if (timing->pixel_encoding == PIXEL_ENCODING_RGB) {
			if (!dongle_caps->dfp_cap_ext.encoding_format_caps.support_rgb)
				return false;
			if (timing->display_color_depth == COLOR_DEPTH_666 &&
					!dongle_caps->dfp_cap_ext.rgb_color_depth_caps.support_6bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_888 &&
					!dongle_caps->dfp_cap_ext.rgb_color_depth_caps.support_8bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_101010 &&
					!dongle_caps->dfp_cap_ext.rgb_color_depth_caps.support_10bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_121212 &&
					!dongle_caps->dfp_cap_ext.rgb_color_depth_caps.support_12bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_161616 &&
					!dongle_caps->dfp_cap_ext.rgb_color_depth_caps.support_16bpc)
				return false;
		} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
			if (!dongle_caps->dfp_cap_ext.encoding_format_caps.support_rgb)
				return false;
			if (timing->display_color_depth == COLOR_DEPTH_888 &&
					!dongle_caps->dfp_cap_ext.ycbcr444_color_depth_caps.support_8bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_101010 &&
					!dongle_caps->dfp_cap_ext.ycbcr444_color_depth_caps.support_10bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_121212 &&
					!dongle_caps->dfp_cap_ext.ycbcr444_color_depth_caps.support_12bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_161616 &&
					!dongle_caps->dfp_cap_ext.ycbcr444_color_depth_caps.support_16bpc)
				return false;
		} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			if (!dongle_caps->dfp_cap_ext.encoding_format_caps.support_rgb)
				return false;
			if (timing->display_color_depth == COLOR_DEPTH_888 &&
					!dongle_caps->dfp_cap_ext.ycbcr422_color_depth_caps.support_8bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_101010 &&
					!dongle_caps->dfp_cap_ext.ycbcr422_color_depth_caps.support_10bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_121212 &&
					!dongle_caps->dfp_cap_ext.ycbcr422_color_depth_caps.support_12bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_161616 &&
					!dongle_caps->dfp_cap_ext.ycbcr422_color_depth_caps.support_16bpc)
				return false;
		} else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) {
			if (!dongle_caps->dfp_cap_ext.encoding_format_caps.support_rgb)
				return false;
			if (timing->display_color_depth == COLOR_DEPTH_888 &&
					!dongle_caps->dfp_cap_ext.ycbcr420_color_depth_caps.support_8bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_101010 &&
					!dongle_caps->dfp_cap_ext.ycbcr420_color_depth_caps.support_10bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_121212 &&
					!dongle_caps->dfp_cap_ext.ycbcr420_color_depth_caps.support_12bpc)
				return false;
			else if (timing->display_color_depth == COLOR_DEPTH_161616 &&
					!dongle_caps->dfp_cap_ext.ycbcr420_color_depth_caps.support_16bpc)
				return false;
		}
	}

	return true;
}

uint32_t dp_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	uint32_t total_data_bw_efficiency_x10000 = 0;
	uint32_t link_rate_per_lane_kbps = 0;

	switch (link_dp_get_encoding_format(link_settings)) {
	case DP_8b_10b_ENCODING:
		/* For 8b/10b encoding:
		 * link rate is defined in the unit of LINK_RATE_REF_FREQ_IN_KHZ per DP byte per lane.
		 * data bandwidth efficiency is 80% with additional 3% overhead if FEC is supported.
		 */
		link_rate_per_lane_kbps = link_settings->link_rate * LINK_RATE_REF_FREQ_IN_KHZ * BITS_PER_DP_BYTE;
		total_data_bw_efficiency_x10000 = DATA_EFFICIENCY_8b_10b_x10000;
		if (dp_should_enable_fec(link)) {
			total_data_bw_efficiency_x10000 /= 100;
			total_data_bw_efficiency_x10000 *= DATA_EFFICIENCY_8b_10b_FEC_EFFICIENCY_x100;
		}
		break;
	case DP_128b_132b_ENCODING:
		/* For 128b/132b encoding:
		 * link rate is defined in the unit of 10mbps per lane.
		 * total data bandwidth efficiency is always 96.71%.
		 */
		link_rate_per_lane_kbps = link_settings->link_rate * 10000;
		total_data_bw_efficiency_x10000 = DATA_EFFICIENCY_128b_132b_x10000;
		break;
	default:
		break;
	}

	/* overall effective link bandwidth = link rate per lane * lane count * total data bandwidth efficiency */
	return link_rate_per_lane_kbps * link_settings->lane_count / 10000 * total_data_bw_efficiency_x10000;
}

uint32_t frl_link_bandwidth_kbps(enum hdmi_frl_link_rate link_rate)
{
	switch (link_rate) {
	case HDMI_FRL_LINK_RATE_3GBPS:
		return 9000000;
	case HDMI_FRL_LINK_RATE_6GBPS:
		return 18000000;
	case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
		return 24000000;
	case HDMI_FRL_LINK_RATE_8GBPS:
		return 32000000;
	case HDMI_FRL_LINK_RATE_10GBPS:
		return 40000000;
	case HDMI_FRL_LINK_RATE_12GBPS:
		return 48000000;
	case HDMI_FRL_LINK_RATE_16GBPS:
		return 64000000;
	case HDMI_FRL_LINK_RATE_20GBPS:
		return 80000000;
	case HDMI_FRL_LINK_RATE_24GBPS:
		return 96000000;
	default:
		return 0;
	}
}

bool frl_capacity_computations_common(struct frl_cap_chk_params_fixed31_32 *params,
	struct frl_cap_chk_intermediates_fixed31_32 *inter)
{
	struct fixed31_32 audio_bw_reserve = dc_fixpt_from_int((params->compressed ? 192000 : 0));
	struct fixed31_32 pixel_rate_tolerance = dc_fixpt_div_int(dc_fixpt_from_int(5), 1000);
	struct fixed31_32 max_audio_tol_rate;
	struct fixed31_32 overhead_m;

	inter->c_frl_sb = 4 * C_FRL_CB + params->lanes;
	inter->overhead_sb = dc_fixpt_div_int(dc_fixpt_from_int(params->lanes), inter->c_frl_sb);
	inter->overhead_rs = dc_fixpt_div_int(dc_fixpt_from_int(32), inter->c_frl_sb);
	inter->overhead_map = dc_fixpt_div_int(dc_fixpt_from_int(25), (inter->c_frl_sb * 10));

	inter->overhead_min = dc_fixpt_add(inter->overhead_sb, inter->overhead_rs);
	inter->overhead_min = dc_fixpt_add(inter->overhead_min, inter->overhead_map);
	overhead_m = dc_fixpt_div_int(dc_fixpt_from_int(3), 1000);
	inter->overhead_max = dc_fixpt_add(inter->overhead_min, overhead_m);

	pixel_rate_tolerance = dc_fixpt_add_int(pixel_rate_tolerance, 1);

	inter->f_pixel_clock_max = dc_fixpt_mul(params->f_pixel_clock_nominal, pixel_rate_tolerance);
	inter->t_line = dc_fixpt_div(dc_fixpt_from_int(params->h_active + params->h_blank), inter->f_pixel_clock_max);
	inter->r_bit_min = dc_fixpt_div_int(dc_fixpt_from_int(TOLERANCE_FRL_BIT), 1000000);
	inter->r_bit_min = dc_fixpt_sub(dc_fixpt_from_int(1), inter->r_bit_min);
	inter->r_bit_min = dc_fixpt_mul(params->r_bit_nominal, inter->r_bit_min);

	inter->r_frl_char_min = dc_fixpt_div_int(inter->r_bit_min, 18);
	inter->c_frl_line = dc_fixpt_mul(inter->t_line, inter->r_frl_char_min);
	inter->c_frl_line = dc_fixpt_mul_int(inter->c_frl_line, params->lanes);

	switch (params->audio_packet_type) {
	case 0x02:
		if (params->layout == 0)
			inter->ap = dc_fixpt_div_int(dc_fixpt_from_int(25), 100);
		else if (params->layout == 1)
			inter->ap = dc_fixpt_from_int(1);
		break;
	case 0x08:
		inter->ap = dc_fixpt_div_int(dc_fixpt_from_int(25), 100);
		break;
	case 0x09:
		inter->ap = dc_fixpt_from_int(1);
		break;
	case 0x07:
	case 0x0e:
	case 0x0f:
	case 0x0b:
	case 0x0c:
		/* Unsupported audio format */
		return false;
	default:
		inter->ap = dc_fixpt_from_int(0);
	}

	inter->r_ap = dc_fixpt_max(audio_bw_reserve, dc_fixpt_mul(params->f_audio, inter->ap));
	inter->r_ap = dc_fixpt_add(inter->r_ap, dc_fixpt_from_int(2 * ACR_RATE_MAX));
	max_audio_tol_rate = dc_fixpt_div_int(dc_fixpt_from_int(TOLERANCE_AUDIO_CLOCK), 1000000);
	max_audio_tol_rate = dc_fixpt_add(dc_fixpt_from_int(1), max_audio_tol_rate);
	inter->r_ap = dc_fixpt_mul(inter->r_ap, max_audio_tol_rate);

	inter->avg_audio_packets_line = dc_fixpt_mul(inter->r_ap, inter->t_line);
	inter->avg_audio_packets_line = dc_fixpt_div_int(inter->avg_audio_packets_line, 1000000);
	inter->audio_packets_line = dc_fixpt_ceil(inter->avg_audio_packets_line);

	inter->blank_audio_min = 32 + 32 * inter->audio_packets_line;

	params->borrow_params.audio_packets_line = inter->audio_packets_line;

	return true;
}

bool frl_capacity_computations_uncompressed_video(struct frl_cap_chk_params_fixed31_32 *params,
	struct frl_cap_chk_intermediates_fixed31_32 *inter)
{
	bool res;
	int k_420;
	struct fixed31_32 k_cd;
	struct fixed31_32 c_frl_free;
	int c_frl_free_int;
	int c_frl_rc_margin;
	struct fixed31_32 c_frl_rc_savings;
	int c_frl_rc_savings_int;
	int bpp;
	struct fixed31_32 bytes_line;
	int tb_active;
	int tb_blank;
	struct fixed31_32 f_tb_average;
	struct fixed31_32 t_active_ref;
	struct fixed31_32 t_blank_ref;
	struct fixed31_32 t_active_min;
	struct fixed31_32 t_blank_min;
	int c_frl_actual_payload;
	struct fixed31_32 utilization;

	res = frl_capacity_computations_common(params, inter);
	if (res != true)
		return res;

	k_420 = params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_420 ? 2 : 1;
	if (params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_422)
		k_cd = dc_fixpt_from_int(1);
	else
		k_cd = dc_fixpt_div_int(dc_fixpt_from_int(params->bpc), 8);

	c_frl_free = dc_fixpt_div_int(dc_fixpt_mul_int(k_cd, params->h_blank), k_420);
	c_frl_free = dc_fixpt_sub_int(c_frl_free, 32 * (1 + inter->audio_packets_line) + 7);
	c_frl_free = dc_fixpt_max(c_frl_free, dc_fixpt_from_int(0));
	c_frl_free_int = dc_fixpt_ceil(c_frl_free);
	c_frl_rc_margin  = 4;
	c_frl_rc_savings = dc_fixpt_mul_int(dc_fixpt_div_int(dc_fixpt_from_int(7), 8), c_frl_free_int);
	c_frl_rc_savings = dc_fixpt_sub_int(c_frl_rc_savings, c_frl_rc_margin);
	c_frl_rc_savings_int = dc_fixpt_floor(dc_fixpt_max(c_frl_rc_savings, dc_fixpt_from_int(0)));

	bpp              = dc_fixpt_ceil(dc_fixpt_mul_int(dc_fixpt_div_int(k_cd, k_420), 24));
	bytes_line       = dc_fixpt_div_int(dc_fixpt_from_int(bpp * params->h_active), 8);
	tb_active        = dc_fixpt_ceil(dc_fixpt_div_int(bytes_line, 3));
	tb_blank         = dc_fixpt_ceil(dc_fixpt_div_int(dc_fixpt_mul_int(k_cd, params->h_blank), k_420));

	if (!(inter->blank_audio_min <= tb_blank)) {
		return false;
	}

	f_tb_average = dc_fixpt_div_int(inter->f_pixel_clock_max, (params->h_active + params->h_blank));
	f_tb_average = dc_fixpt_mul_int(f_tb_average, (tb_active + tb_blank));

	t_active_ref = dc_fixpt_div(dc_fixpt_from_int(params->h_active), dc_fixpt_from_int(params->h_active + params->h_blank));
	t_active_ref = dc_fixpt_mul(inter->t_line, t_active_ref);

	t_blank_ref = dc_fixpt_div(dc_fixpt_from_int(params->h_blank), dc_fixpt_from_int(params->h_active + params->h_blank));
	t_blank_ref = dc_fixpt_mul(inter->t_line, t_blank_ref);

	t_active_min = dc_fixpt_sub(dc_fixpt_from_int(1), inter->overhead_max);
	t_active_min = dc_fixpt_mul(t_active_min, inter->r_frl_char_min);
	t_active_min = dc_fixpt_mul_int(t_active_min, params->lanes);
	t_active_min = dc_fixpt_div_int(t_active_min, 1000);
	t_blank_min = t_active_min;

	t_active_min = dc_fixpt_div(dc_fixpt_mul_int(dc_fixpt_div(dc_fixpt_from_int(3), dc_fixpt_from_int(2)), tb_active), t_active_min);

	t_blank_min = dc_fixpt_div(dc_fixpt_from_int(tb_blank), t_blank_min);

	if (dc_fixpt_le(t_active_min, t_active_ref) && dc_fixpt_le(t_blank_min, t_blank_ref)) {
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_NONE;
	} else if (dc_fixpt_lt(t_active_ref, t_active_min) && dc_fixpt_le(t_blank_min, t_blank_ref)) {
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_FROM_BLANK;
	} else {
		return false;
	}


	c_frl_actual_payload = dc_fixpt_ceil(dc_fixpt_mul_int(dc_fixpt_div(dc_fixpt_from_int(3), dc_fixpt_from_int(2)), tb_active)) + tb_blank - c_frl_rc_savings_int;

	utilization = dc_fixpt_div(dc_fixpt_from_int(c_frl_actual_payload), inter->c_frl_line);
	utilization = dc_fixpt_mul_int(utilization, 1000);

	inter->margin = dc_fixpt_sub(dc_fixpt_from_int(1), dc_fixpt_add(utilization, inter->overhead_max));

	if (dc_fixpt_lt(inter->margin, dc_fixpt_from_int(0)) && dc_fixpt_lt(dc_fixpt_from_fraction(1, 100), dc_fixpt_abs(inter->margin)))
		return false;

	return true;
}

static uint32_t dp_get_timing_bandwidth_kbps(
	const struct dc_crtc_timing *timing,
	const struct dc_link *link)
{
	return dc_bandwidth_in_kbps_from_timing(timing,
			dc_link_get_highest_encoding_format(link));
}

static bool dp_validate_mode_timing(
	struct dc_link *link,
	const struct dc_crtc_timing *timing)
{
	uint32_t req_bw;
	uint32_t max_bw;

	const struct dc_link_settings *link_setting;

	/* According to spec, VSC SDP should be used if pixel format is YCbCr420 */
	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 &&
			!link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED &&
			dal_graphics_object_id_get_connector_id(link->link_id) != CONNECTOR_ID_VIRTUAL)
		return false;

	/*always DP fail safe mode*/
	if ((timing->pix_clk_100hz / 10) == (uint32_t) 25175 &&
		timing->h_addressable == (uint32_t) 640 &&
		timing->v_addressable == (uint32_t) 480)
		return true;

	link_setting = dp_get_verified_link_cap(link);

	/* TODO: DYNAMIC_VALIDATION needs to be implemented */
	/*if (flags.DYNAMIC_VALIDATION == 1 &&
		link->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN)
		link_setting = &link->verified_link_cap;
	*/

	req_bw = dc_bandwidth_in_kbps_from_timing(timing, dc_link_get_highest_encoding_format(link));
	max_bw = dp_link_bandwidth_kbps(link, link_setting);
	uint32_t max_uncompressed_pixel_rate_100hz =
		link->dpcd_caps.max_uncompressed_pixel_rate_cap.bits.max_uncompressed_pixel_rate_cap * 10000U;

	bool is_max_uncompressed_pixel_rate_exceeded = link->dpcd_caps.max_uncompressed_pixel_rate_cap.bits.valid &&
			timing->pix_clk_100hz > max_uncompressed_pixel_rate_100hz;

	if (is_max_uncompressed_pixel_rate_exceeded && !timing->flags.DSC) {
		return false;
	}

	if (req_bw <= max_bw) {
		/* remember the biggest mode here, during
		 * initial link training (to get
		 * verified_link_cap), LS sends event about
		 * cannot train at reported cap to upper
		 * layer and upper layer will re-enumerate modes.
		 * this is not necessary if the lower
		 * verified_link_cap is enough to drive
		 * all the modes */

		/* TODO: DYNAMIC_VALIDATION needs to be implemented */
		/* if (flags.DYNAMIC_VALIDATION == 1)
			dpsst->max_req_bw_for_verified_linkcap = dal_max(
				dpsst->max_req_bw_for_verified_linkcap, req_bw); */
		return true;
	} else
		return false;
}

bool frl_validate_mode_timing(
	struct dc_link *link,
	const struct dc_crtc_timing *timing,
	struct dc_hdmi_frl_link_settings *frl_link_settings)
{
	uint32_t req_bw;
	uint32_t max_bw;
	enum engine_id hpo_eng_id;
	unsigned int i;
	unsigned int hpo_frl_stream_enc_index = 0;
	bool frl_output_valid = false;

	if (!link)
		return false;
	if (!link->local_sink)
		return false;

	req_bw = dc_bandwidth_in_kbps_from_timing(timing, dc_link_get_highest_encoding_format(link));
	max_bw = frl_link_bandwidth_kbps(frl_link_settings->frl_link_rate);

	/* Use Engine ID to determine which hpo stream
	 * encoder should be used.
	 */
	if (link->connector_signal == SIGNAL_TYPE_VIRTUAL)
		frl_output_valid = true;
	else {
		struct audio_check audio_frl_check = {0};
		struct audio_info audio_info = {0};
		hpo_eng_id = ENGINE_ID_HPO_0;

		for (i = 0; i < link->dc->res_pool->hpo_frl_stream_enc_count; i++) {
			if (link->dc->res_pool->hpo_frl_stream_enc[i]->id == hpo_eng_id) {
				hpo_frl_stream_enc_index = i;
				break;
			}
		}
		/*add audio check*/
		for (i = 0; i < (link->local_sink->edid_caps.audio_mode_count); i++) {
			audio_info.modes[i].channel_count = link->local_sink->edid_caps.audio_modes[i].channel_count;
			audio_info.modes[i].format_code = link->local_sink->edid_caps.audio_modes[i].format_code;
			audio_info.modes[i].sample_rates.all = link->local_sink->edid_caps.audio_modes[i].sample_rate;
			audio_info.modes[i].sample_size = link->local_sink->edid_caps.audio_modes[i].sample_size;
		}
		audio_info.mode_count = link->local_sink->edid_caps.audio_mode_count;
		get_audio_check(&audio_info, &audio_frl_check);

		frl_output_valid =
			link->dc->res_pool->hpo_frl_stream_enc[hpo_frl_stream_enc_index]->funcs->validate_hdmi_frl_output(
				link->dc->res_pool->hpo_frl_stream_enc[hpo_frl_stream_enc_index],
				timing, &audio_frl_check,
				frl_link_settings,
				link->local_sink->edid_caps.frl_dsc_max_frl_rate);
	}

	if (req_bw <= max_bw && frl_output_valid) {
		/* remember the biggest mode here, during
		 * initial link training (to get
		 * verified_link_cap), LS sends event about
		 * cannot train at reported cap to upper
		 * layer and upper layer will re-enumerate modes.
		 * this is not necessary if the lower
		 * verified_link_cap is enough to drive
		 * all the modes
		 */

		/* TODO: DYNAMIC_VALIDATION needs to be implemented */
		/* if (flags.DYNAMIC_VALIDATION == 1)
		 * dpsst->max_req_bw_for_verified_linkcap = dal_max(
		 * dpsst->max_req_bw_for_verified_linkcap, req_bw);
		 */
		return true;
	} else if (frl_output_valid && timing->dsc_cfg.is_frl) {
		/* HDMI DSC calculation is validated within frl_output_valid
		 * and req_bw may exceed max_bw
		 */
		return true;
	} else
		return false;
}

enum dc_status link_validate_mode_timing(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		const struct dc_crtc_timing *timing)
{
	uint32_t max_pix_clk = stream->link->dongle_max_pix_clk * 10;
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;

	/* A hack to avoid failing any modes for EDID override feature on
	 * topology change such as lower quality cable for DP or different dongle
	 */
	if (link->remote_sinks[0] && link->remote_sinks[0]->sink_signal == SIGNAL_TYPE_VIRTUAL)
		return DC_OK;

	/* If DSC is supported, but native 422 DSC is not supported,
	 * HDMI 2.1a specification requires that all 422 format be disabled (7.7.1)
	*/
	if (dc_is_hdmi_signal(stream->signal)) {
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 && link->dc->config.no_native422_support) {
			return DC_SURFACE_PIXEL_FORMAT_UNSUPPORTED;
		}
	}

	/* Passive Dongle */
	if (max_pix_clk != 0 && get_tmds_output_pixel_clock_100hz(timing) > max_pix_clk)
		return DC_EXCEED_DONGLE_CAP;

	/* Active Dongle*/
	if (!dp_active_dongle_validate_timing(timing, dpcd_caps))
		return DC_EXCEED_DONGLE_CAP;

	switch (stream->signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		if (!dp_validate_mode_timing(
				link,
				timing))
			return DC_NO_DP_LINK_BANDWIDTH;
		break;

	case SIGNAL_TYPE_HDMI_FRL:
		{
			uint32_t pxl_clk_mhz;
			/* Limit pixel clock to DTBCLK Limit (Base Pix > 4 * DTBCLK) */
			pxl_clk_mhz = (timing->pix_clk_100hz + 10000 - 1) / 10000 ;
			if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
				pxl_clk_mhz /= 2;
			else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
				pxl_clk_mhz = pxl_clk_mhz * 2 / 3;
			if (pxl_clk_mhz > DTBCLK_LIMIT && link->ctx->dce_version < DCN_VERSION_3_1)
				return DC_NO_HDMI_FRL_LINK_BANDWIDTH;
		}

		if (!frl_validate_mode_timing(
				link,
				timing,
				hdmi_frl_get_verified_link_cap(link)))
			return DC_NO_HDMI_FRL_LINK_BANDWIDTH;
		break;
	default:
		break;
	}

	return DC_OK;
}

static const struct dc_tunnel_settings *get_dp_tunnel_settings(const struct dc_state *context,
		const struct dc_stream_state *stream)
{
	int i;
	const struct dc_tunnel_settings *dp_tunnel_settings = NULL;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context->res_ctx.pipe_ctx[i].stream && (context->res_ctx.pipe_ctx[i].stream == stream)) {
			dp_tunnel_settings = &context->res_ctx.pipe_ctx[i].link_config.dp_tunnel_settings;
			break;
		}
	}

	return dp_tunnel_settings;
}

/*
 * Calculates the DP tunneling bandwidth required for the stream timing
 * and aggregates the stream bandwidth for the respective DP tunneling link
 *
 * return: dc_status
 */
enum dc_status link_validate_dp_tunnel_bandwidth(const struct dc *dc, const struct dc_state *new_ctx)
{
	(void)dc;
	struct dc_validation_dpia_set dpia_link_sets[MAX_DPIA_NUM] = { 0 };
	uint8_t link_count = 0;
	enum dc_status result = DC_OK;

	// Iterate through streams in the new context
	for (uint8_t i = 0; (i < MAX_PIPES && i < new_ctx->stream_count); i++) {
		const struct dc_stream_state *stream = new_ctx->streams[i];
		const struct dc_link *link;
		const struct dc_tunnel_settings *dp_tunnel_settings;
		uint32_t timing_bw;

		if (stream == NULL)
			continue;

		link = stream->link;

		if (!(link && (stream->signal == SIGNAL_TYPE_DISPLAY_PORT
				|| stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)))
			continue;

		if ((link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA) && (link->hpd_status == false))
			continue;

		dp_tunnel_settings = get_dp_tunnel_settings(new_ctx, stream);

		if ((dp_tunnel_settings == NULL) || (dp_tunnel_settings->should_use_dp_bw_allocation == false))
			continue;

		timing_bw = dp_get_timing_bandwidth_kbps(&stream->timing, link);

		// Find an existing entry for this 'link' in 'dpia_link_sets'
		for (uint8_t j = 0; j < MAX_DPIA_NUM; j++) {
			bool is_new_slot = false;

			if (dpia_link_sets[j].link == NULL) {
				is_new_slot = true;
				link_count++;
				dpia_link_sets[j].required_bw = 0;
				dpia_link_sets[j].link = link;
			}

			if (is_new_slot || (dpia_link_sets[j].link == link)) {
				dpia_link_sets[j].tunnel_settings = dp_tunnel_settings;
				dpia_link_sets[j].required_bw += timing_bw;
				break;
			}
		}
	}

	if (link_count && link_dpia_validate_dp_tunnel_bandwidth(dpia_link_sets, link_count) == false)
		result = DC_FAIL_DP_TUNNEL_BW_VALIDATE;

	return result;
}

struct dp_audio_layout_config {
	uint8_t layouts_per_sample_denom;
	uint8_t symbols_per_layout;
	uint8_t max_layouts_per_audio_sdp;
};

static void get_audio_layout_config(
	uint32_t channel_count,
	enum dp_link_encoding encoding,
	struct dp_audio_layout_config *output)
{
	memset(output, 0, sizeof(struct dp_audio_layout_config));

	/* Assuming L-PCM audio. Current implementation uses max 1 layout per SDP,
	 * with each layout being the same size (8ch layout).
	 */
	if (encoding == DP_8b_10b_ENCODING) {
		if (channel_count == 2) {
			output->layouts_per_sample_denom = 4;
			output->symbols_per_layout = 40;
			output->max_layouts_per_audio_sdp = 1;
		} else if (channel_count == 8 || channel_count == 6) {
			output->layouts_per_sample_denom = 1;
			output->symbols_per_layout = 40;
			output->max_layouts_per_audio_sdp = 1;
		}
	} else if (encoding == DP_128b_132b_ENCODING) {
		if (channel_count == 2) {
			output->layouts_per_sample_denom = 4;
			output->symbols_per_layout = 10;
			output->max_layouts_per_audio_sdp = 1;
		} else if (channel_count == 8 || channel_count == 6) {
			output->layouts_per_sample_denom = 1;
			output->symbols_per_layout = 10;
			output->max_layouts_per_audio_sdp = 1;
		}
	}
}

static uint32_t get_av_stream_map_lane_count(
	enum dp_link_encoding encoding,
	enum dc_lane_count lane_count,
	bool is_mst)
{
	uint32_t av_stream_map_lane_count = 0;

	if (encoding == DP_8b_10b_ENCODING) {
		if (!is_mst)
			av_stream_map_lane_count = lane_count;
		else
			av_stream_map_lane_count = 4;
	} else if (encoding == DP_128b_132b_ENCODING) {
		av_stream_map_lane_count = 4;
	}

	ASSERT(av_stream_map_lane_count != 0);

	return av_stream_map_lane_count;
}

static uint32_t get_audio_sdp_overhead(
	enum dp_link_encoding encoding,
	enum dc_lane_count lane_count,
	bool is_mst)
{
	uint32_t audio_sdp_overhead = 0;

	if (encoding == DP_8b_10b_ENCODING) {
		if (is_mst)
			audio_sdp_overhead = 16; /* 4 * 2 + 8 */
		else
			audio_sdp_overhead = lane_count * 2 + 8;
	} else if (encoding == DP_128b_132b_ENCODING) {
		audio_sdp_overhead = 10; /* 4 x 2.5 */
	}

	ASSERT(audio_sdp_overhead != 0);

	return audio_sdp_overhead;
}

/* Current calculation only applicable for 8b/10b MST and 128b/132b SST/MST.
 */
static uint32_t calculate_overhead_hblank_bw_in_symbols(
	uint32_t max_slice_h)
{
	uint32_t overhead_hblank_bw = 0; /* in stream symbols */

	overhead_hblank_bw += max_slice_h * 4; /* EOC overhead */
	overhead_hblank_bw += 12; /* Main link overhead (VBID, BS/BE) */

	return overhead_hblank_bw;
}

uint32_t dp_required_hblank_size_bytes(
	const struct dc_link *link,
	struct dp_audio_bandwidth_params *audio_params)
{
	/* Main logic from dce_audio is duplicated here, with the main
	 * difference being:
	 * - Pre-determined lane count of 4
	 * - Assumed 16 dsc slices for worst case
	 * - Assumed SDP split disabled for worst case
	 * TODO: Unify logic from dce_audio to prevent duplicated logic.
	 */

	const struct dc_crtc_timing *timing = audio_params->crtc_timing;
	const uint32_t channel_count = audio_params->channel_count;
	const uint32_t sample_rate_hz = audio_params->sample_rate_hz;
	const enum dp_link_encoding link_encoding = audio_params->link_encoding;

	// 8b/10b MST and 128b/132b are always 4 logical lanes.
	const uint32_t lane_count = 4;
	const bool is_mst = (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT);
	// Maximum slice count is with ODM 4:1, 4 slices per DSC
	const uint32_t max_slices_h = 16;

	const uint32_t av_stream_map_lane_count = get_av_stream_map_lane_count(
			link_encoding, lane_count, is_mst);
	const uint32_t audio_sdp_overhead = get_audio_sdp_overhead(
			link_encoding, lane_count, is_mst);
	struct dp_audio_layout_config layout_config;

	if (link_encoding == DP_8b_10b_ENCODING && link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT)
		return 0;

	get_audio_layout_config(
			channel_count, link_encoding, &layout_config);

	/* DP spec recommends between 1.05 to 1.1 safety margin to prevent sample under-run */
	struct fixed31_32 audio_sdp_margin = dc_fixpt_from_fraction(110, 100);
	struct fixed31_32 horizontal_line_freq_khz = dc_fixpt_from_fraction(
			timing->pix_clk_100hz, (long long)timing->h_total * 10);
	struct fixed31_32 samples_per_line;
	struct fixed31_32 layouts_per_line;
	struct fixed31_32 symbols_per_sdp_max_layout;
	struct fixed31_32 remainder;
	uint32_t num_sdp_with_max_layouts;
	uint32_t required_symbols_per_hblank;
	uint32_t required_bytes_per_hblank = 0;

	samples_per_line = dc_fixpt_from_fraction(sample_rate_hz, 1000);
	samples_per_line = dc_fixpt_div(samples_per_line, horizontal_line_freq_khz);
	layouts_per_line = dc_fixpt_div_int(samples_per_line, layout_config.layouts_per_sample_denom);
	// HBlank expansion usage assumes SDP split disabled to allow for worst case.
	layouts_per_line = dc_fixpt_from_int(dc_fixpt_ceil(layouts_per_line));

	num_sdp_with_max_layouts = dc_fixpt_floor(
			dc_fixpt_div_int(layouts_per_line, layout_config.max_layouts_per_audio_sdp));
	symbols_per_sdp_max_layout = dc_fixpt_from_int(
			layout_config.max_layouts_per_audio_sdp * layout_config.symbols_per_layout);
	symbols_per_sdp_max_layout = dc_fixpt_add_int(symbols_per_sdp_max_layout, audio_sdp_overhead);
	symbols_per_sdp_max_layout = dc_fixpt_mul(symbols_per_sdp_max_layout, audio_sdp_margin);
	required_symbols_per_hblank = num_sdp_with_max_layouts;
	required_symbols_per_hblank *= ((dc_fixpt_ceil(symbols_per_sdp_max_layout) + av_stream_map_lane_count) /
			av_stream_map_lane_count) *	av_stream_map_lane_count;

	if (num_sdp_with_max_layouts !=	dc_fixpt_ceil(
			dc_fixpt_div_int(layouts_per_line, layout_config.max_layouts_per_audio_sdp))) {
		remainder = dc_fixpt_sub_int(layouts_per_line,
				num_sdp_with_max_layouts * layout_config.max_layouts_per_audio_sdp);
		remainder = dc_fixpt_mul_int(remainder, layout_config.symbols_per_layout);
		remainder = dc_fixpt_add_int(remainder, audio_sdp_overhead);
		remainder = dc_fixpt_mul(remainder, audio_sdp_margin);
		required_symbols_per_hblank += ((dc_fixpt_ceil(remainder) + av_stream_map_lane_count) /
				av_stream_map_lane_count) * av_stream_map_lane_count;
	}

	required_symbols_per_hblank += calculate_overhead_hblank_bw_in_symbols(max_slices_h);

	if (link_encoding == DP_8b_10b_ENCODING)
		required_bytes_per_hblank = required_symbols_per_hblank; // 8 bits per 8b/10b symbol
	else if (link_encoding == DP_128b_132b_ENCODING)
		required_bytes_per_hblank = required_symbols_per_hblank * 4; // 32 bits per 128b/132b symbol

	return required_bytes_per_hblank;
}


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

#if defined(CONFIG_DRM_AMD_DC_FP)
			if (timing->flags.DSC && !timing->dsc_cfg.is_frl)
				/* DP input has DSC, HDMI FRL output doesn't have DSC, remove DSC from output timing */
				outputTiming.flags.DSC = 0;
#endif
			if (dc_bandwidth_in_kbps_from_timing(&outputTiming) > dongle_caps->dp_hdmi_frl_max_link_bw_in_kbps)
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

	req_bw = dc_bandwidth_in_kbps_from_timing(timing);
	max_bw = dp_link_bandwidth_kbps(link, link_setting);

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

	default:
		break;
	}

	return DC_OK;
}

bool link_validate_dpia_bandwidth(const struct dc_stream_state *stream, const unsigned int num_streams)
{
	bool ret = true;
	int bw_needed[MAX_DPIA_NUM];
	struct dc_link *link[MAX_DPIA_NUM];

	if (!num_streams || num_streams > MAX_DPIA_NUM)
		return ret;

	for (uint8_t i = 0; i < num_streams; ++i) {

		link[i] = stream[i].link;
		bw_needed[i] = dc_bandwidth_in_kbps_from_timing(&stream[i].timing);
	}
	return ret;
}

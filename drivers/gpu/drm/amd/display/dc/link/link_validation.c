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

	bool is_max_uncompressed_pixel_rate_exceeded = link->dpcd_caps.max_uncompressed_pixel_rate_cap.bits.valid &&
			timing->pix_clk_100hz > link->dpcd_caps.max_uncompressed_pixel_rate_cap.bits.max_uncompressed_pixel_rate_cap * 10000;

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

/*
 * This function calculates the bandwidth required for the stream timing
 * and aggregates the stream bandwidth for the respective dpia link
 *
 * @stream: pointer to the dc_stream_state struct instance
 * @num_streams: number of streams to be validated
 *
 * return: true if validation is succeeded
 */
bool link_validate_dpia_bandwidth(const struct dc_stream_state *stream, const unsigned int num_streams)
{
	int bw_needed[MAX_DPIA_NUM] = {0};
	struct dc_link *dpia_link[MAX_DPIA_NUM] = {0};
	int num_dpias = 0;

	for (unsigned int i = 0; i < num_streams; ++i) {
		if (stream[i].signal == SIGNAL_TYPE_DISPLAY_PORT) {
			/* new dpia sst stream, check whether it exceeds max dpia */
			if (num_dpias >= MAX_DPIA_NUM)
				return false;

			dpia_link[num_dpias] = stream[i].link;
			bw_needed[num_dpias] = dc_bandwidth_in_kbps_from_timing(&stream[i].timing,
					dc_link_get_highest_encoding_format(dpia_link[num_dpias]));
			num_dpias++;
		} else if (stream[i].signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
			uint8_t j = 0;
			/* check whether its a known dpia link */
			for (; j < num_dpias; ++j) {
				if (dpia_link[j] == stream[i].link)
					break;
			}

			if (j == num_dpias) {
				/* new dpia mst stream, check whether it exceeds max dpia */
				if (num_dpias >= MAX_DPIA_NUM)
					return false;
				else {
					dpia_link[j] = stream[i].link;
					num_dpias++;
				}
			}

			bw_needed[j] += dc_bandwidth_in_kbps_from_timing(&stream[i].timing,
				dc_link_get_highest_encoding_format(dpia_link[j]));
		}
	}

	/* Include dp overheads */
	for (uint8_t i = 0; i < num_dpias; ++i) {
		int dp_overhead = 0;

		dp_overhead = link_dp_dpia_get_dp_overhead_in_dp_tunneling(dpia_link[i]);
		bw_needed[i] += dp_overhead;
	}

	return dpia_validate_usb4_bw(dpia_link, bw_needed, num_dpias);
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


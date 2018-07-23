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
 */
/*
 * stream_encoder.h
 *
 */

#ifndef STREAM_ENCODER_H_
#define STREAM_ENCODER_H_

#include "audio_types.h"
#include "hw_shared.h"

struct dc_bios;
struct dc_context;
struct dc_crtc_timing;

enum dp_pixel_encoding_type {
	DP_PIXEL_ENCODING_TYPE_RGB444		= 0x00000000,
	DP_PIXEL_ENCODING_TYPE_YCBCR422		= 0x00000001,
	DP_PIXEL_ENCODING_TYPE_YCBCR444		= 0x00000002,
	DP_PIXEL_ENCODING_TYPE_RGB_WIDE_GAMUT	= 0x00000003,
	DP_PIXEL_ENCODING_TYPE_Y_ONLY		= 0x00000004,
	DP_PIXEL_ENCODING_TYPE_YCBCR420		= 0x00000005
};

enum dp_component_depth {
	DP_COMPONENT_PIXEL_DEPTH_6BPC		= 0x00000000,
	DP_COMPONENT_PIXEL_DEPTH_8BPC		= 0x00000001,
	DP_COMPONENT_PIXEL_DEPTH_10BPC		= 0x00000002,
	DP_COMPONENT_PIXEL_DEPTH_12BPC		= 0x00000003,
	DP_COMPONENT_PIXEL_DEPTH_16BPC		= 0x00000004
};

struct encoder_info_frame {
	/* auxiliary video information */
	struct dc_info_packet avi;
	struct dc_info_packet gamut;
	struct dc_info_packet vendor;
	/* source product description */
	struct dc_info_packet spd;
	/* video stream configuration */
	struct dc_info_packet vsc;
	/* HDR Static MetaData */
	struct dc_info_packet hdrsmd;
};

struct encoder_unblank_param {
	struct dc_link_settings link_settings;
	unsigned int pixel_clk_khz;
};

struct encoder_set_dp_phy_pattern_param {
	enum dp_test_pattern dp_phy_pattern;
	const uint8_t *custom_pattern;
	uint32_t custom_pattern_size;
	enum dp_panel_mode dp_panel_mode;
};

struct stream_encoder {
	const struct stream_encoder_funcs *funcs;
	struct dc_context *ctx;
	struct dc_bios *bp;
	enum engine_id id;
};

struct stream_encoder_funcs {
	void (*dp_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		enum dc_color_space output_color_space);

	void (*hdmi_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		int actual_pix_clk_khz,
		bool enable_audio);

	void (*dvi_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		bool is_dual_link);

	void (*set_mst_bandwidth)(
		struct stream_encoder *enc,
		struct fixed31_32 avg_time_slots_per_mtp);

	void (*update_hdmi_info_packets)(
		struct stream_encoder *enc,
		const struct encoder_info_frame *info_frame);

	void (*stop_hdmi_info_packets)(
		struct stream_encoder *enc);

	void (*update_dp_info_packets)(
		struct stream_encoder *enc,
		const struct encoder_info_frame *info_frame);

	void (*stop_dp_info_packets)(
		struct stream_encoder *enc);

	void (*dp_blank)(
		struct stream_encoder *enc);

	void (*dp_unblank)(
		struct stream_encoder *enc,
		const struct encoder_unblank_param *param);

	void (*audio_mute_control)(
		struct stream_encoder *enc, bool mute);

	void (*dp_audio_setup)(
		struct stream_encoder *enc,
		unsigned int az_inst,
		struct audio_info *info);

	void (*dp_audio_enable) (
			struct stream_encoder *enc);

	void (*dp_audio_disable) (
			struct stream_encoder *enc);

	void (*hdmi_audio_setup)(
		struct stream_encoder *enc,
		unsigned int az_inst,
		struct audio_info *info,
		struct audio_crtc_info *audio_crtc_info);

	void (*hdmi_audio_disable) (
			struct stream_encoder *enc);

	void (*setup_stereo_sync) (
			struct stream_encoder *enc,
			int tg_inst,
			bool enable);

	void (*set_avmute)(
		struct stream_encoder *enc, bool enable);

};

#endif /* STREAM_ENCODER_H_ */

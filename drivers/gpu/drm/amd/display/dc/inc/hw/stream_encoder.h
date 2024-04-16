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
#include "dc_link.h"

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

struct audio_clock_info {
	/* pixel clock frequency*/
	uint32_t pixel_clock_in_10khz;
	/* N - 32KHz audio */
	uint32_t n_32khz;
	/* CTS - 32KHz audio*/
	uint32_t cts_32khz;
	uint32_t n_44khz;
	uint32_t cts_44khz;
	uint32_t n_48khz;
	uint32_t cts_48khz;
};

enum dynamic_metadata_mode {
	dmdata_dp,
	dmdata_hdmi,
	dmdata_dolby_vision
};

struct encoder_info_frame {
	/* auxiliary video information */
	struct dc_info_packet avi;
	struct dc_info_packet gamut;
	struct dc_info_packet vendor;
	struct dc_info_packet hfvsif;
	struct dc_info_packet vtem;
	/* source product description */
	struct dc_info_packet spd;
	/* video stream configuration */
	struct dc_info_packet vsc;
	/* HDR Static MetaData */
	struct dc_info_packet hdrsmd;
};

struct encoder_unblank_param {
	struct dc_link_settings link_settings;
	struct dc_crtc_timing timing;
	int opp_cnt;
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
	uint32_t stream_enc_inst;
	struct vpg *vpg;
	struct afmt *afmt;
};

struct enc_state {
	uint32_t dsc_mode;  // DISABLED  0; 1 or 2 indicate enabled state.
	uint32_t dsc_slice_width;
	uint32_t sec_gsp_pps_line_num;
	uint32_t vbid6_line_reference;
	uint32_t vbid6_line_num;
	uint32_t sec_gsp_pps_enable;
	uint32_t sec_stream_enable;
};

struct stream_encoder_funcs {
	void (*dp_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		enum dc_color_space output_color_space,
		bool use_vsc_sdp_for_colorimetry,
		uint32_t enable_sdp_splitting);

	void (*hdmi_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		int actual_pix_clk_khz,
		bool enable_audio);

	void (*dvi_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		bool is_dual_link);

	void (*lvds_set_stream_attribute)(
		struct stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing);

	void (*set_throttled_vcp_size)(
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

	void (*send_immediate_sdp_message)(
				struct stream_encoder *enc,
				const uint8_t *custom_sdp_message,
				unsigned int sdp_message_size);

	void (*stop_dp_info_packets)(
		struct stream_encoder *enc);

	void (*reset_fifo)(
		struct stream_encoder *enc
	);

	void (*dp_blank)(
		struct dc_link *link,
		struct stream_encoder *enc);

	void (*dp_unblank)(
		struct dc_link *link,
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

	void (*dig_connect_to_otg)(
		struct stream_encoder *enc,
		int tg_inst);

	void (*hdmi_reset_stream_attribute)(
		struct stream_encoder *enc);

	unsigned int (*dig_source_otg)(
		struct stream_encoder *enc);

	bool (*dp_get_pixel_format)(
		struct stream_encoder *enc,
		enum dc_pixel_encoding *encoding,
		enum dc_color_depth *depth);

	void (*enc_read_state)(struct stream_encoder *enc, struct enc_state *s);

	void (*dp_set_dsc_config)(
			struct stream_encoder *enc,
			enum optc_dsc_mode dsc_mode,
			uint32_t dsc_bytes_per_pixel,
			uint32_t dsc_slice_width);

	void (*dp_set_dsc_pps_info_packet)(struct stream_encoder *enc,
				bool enable,
				uint8_t *dsc_packed_pps,
				bool immediate_update);

	void (*set_dynamic_metadata)(struct stream_encoder *enc,
			bool enable,
			uint32_t hubp_requestor_id,
			enum dynamic_metadata_mode dmdata_mode);

	void (*dp_set_odm_combine)(
		struct stream_encoder *enc,
		bool odm_combine);

	uint32_t (*get_fifo_cal_average_level)(
		struct stream_encoder *enc);

	void (*set_input_mode)(
		struct stream_encoder *enc, unsigned int pix_per_container);
	void (*enable_fifo)(struct stream_encoder *enc);
	void (*disable_fifo)(struct stream_encoder *enc);
};

struct hpo_dp_stream_encoder_state {
	uint32_t stream_enc_enabled;
	uint32_t vid_stream_enabled;
	uint32_t otg_inst;
	uint32_t pixel_encoding;
	uint32_t component_depth;
	uint32_t compressed_format;
	uint32_t sdp_enabled;
	uint32_t mapped_to_link_enc;
};

struct hpo_dp_stream_encoder {
	const struct hpo_dp_stream_encoder_funcs *funcs;
	struct dc_context *ctx;
	struct dc_bios *bp;
	uint32_t inst;
	enum engine_id id;
	struct vpg *vpg;
	struct apg *apg;
};

struct hpo_dp_stream_encoder_funcs {
	void (*enable_stream)(
			struct hpo_dp_stream_encoder *enc);

	void (*dp_unblank)(
			struct hpo_dp_stream_encoder *enc,
			uint32_t stream_source);

	void (*dp_blank)(
			struct hpo_dp_stream_encoder *enc);

	void (*disable)(
			struct hpo_dp_stream_encoder *enc);

	void (*set_stream_attribute)(
		struct hpo_dp_stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		enum dc_color_space output_color_space,
		bool use_vsc_sdp_for_colorimetry,
		bool compressed_format,
		bool double_buffer_en);

	void (*update_dp_info_packets)(
		struct hpo_dp_stream_encoder *enc,
		const struct encoder_info_frame *info_frame);

	void (*stop_dp_info_packets)(
		struct hpo_dp_stream_encoder *enc);

	void (*dp_set_dsc_pps_info_packet)(
			struct hpo_dp_stream_encoder *enc,
			bool enable,
			uint8_t *dsc_packed_pps,
			bool immediate_update);

	void (*map_stream_to_link)(
			struct hpo_dp_stream_encoder *enc,
			uint32_t stream_enc_inst,
			uint32_t link_enc_inst);

	void (*audio_mute_control)(
			struct hpo_dp_stream_encoder *enc, bool mute);

	void (*dp_audio_setup)(
			struct hpo_dp_stream_encoder *enc,
			unsigned int az_inst,
			struct audio_info *info);

	void (*dp_audio_enable)(
			struct hpo_dp_stream_encoder *enc);

	void (*dp_audio_disable)(
			struct hpo_dp_stream_encoder *enc);

	void (*read_state)(
			struct hpo_dp_stream_encoder *enc,
			struct hpo_dp_stream_encoder_state *state);

	void (*set_hblank_min_symbol_width)(
			struct hpo_dp_stream_encoder *enc,
			uint16_t width);
};

#endif /* STREAM_ENCODER_H_ */

/*
 * stream_encoder.h
 *
 */

#ifndef STREAM_ENCODER_H_
#define STREAM_ENCODER_H_

#include "include/hw_sequencer_types.h"
#include "audio_types.h"

struct dc_bios;
struct dc_context;
struct dc_crtc_timing;

struct encoder_info_packet {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[32];
};

struct encoder_info_frame {
	/* auxiliary video information */
	struct encoder_info_packet avi;
	struct encoder_info_packet gamut;
	struct encoder_info_packet vendor;
	/* source product description */
	struct encoder_info_packet spd;
	/* video stream configuration */
	struct encoder_info_packet vsc;
	/* HDR Static MetaData */
	struct encoder_info_packet hdrsmd;
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
};

#endif /* STREAM_ENCODER_H_ */

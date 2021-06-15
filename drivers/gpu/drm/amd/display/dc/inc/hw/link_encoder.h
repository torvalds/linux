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
 * link_encoder.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef LINK_ENCODER_H_
#define LINK_ENCODER_H_

#include "grph_object_defs.h"
#include "signal_types.h"
#include "dc_types.h"

struct dc_context;
struct encoder_set_dp_phy_pattern_param;
struct link_mst_stream_allocation_table;
struct dc_link_settings;
struct link_training_settings;
struct pipe_ctx;

struct encoder_init_data {
	enum channel_id channel;
	struct graphics_object_id connector;
	enum hpd_source_id hpd_source;
	/* TODO: in DAL2, here was pointer to EventManagerInterface */
	struct graphics_object_id encoder;
	struct dc_context *ctx;
	enum transmitter transmitter;
};

struct encoder_feature_support {
	union {
		struct {
			uint32_t IS_HBR2_CAPABLE:1;
			uint32_t IS_HBR3_CAPABLE:1;
			uint32_t IS_TPS3_CAPABLE:1;
			uint32_t IS_TPS4_CAPABLE:1;
			uint32_t HDMI_6GB_EN:1;
			uint32_t IS_DP2_CAPABLE:1;
			uint32_t IS_UHBR10_CAPABLE:1;
			uint32_t IS_UHBR13_5_CAPABLE:1;
			uint32_t IS_UHBR20_CAPABLE:1;
			uint32_t DP_IS_USB_C:1;
		} bits;
		uint32_t raw;
	} flags;

	enum dc_color_depth max_hdmi_deep_color;
	unsigned int max_hdmi_pixel_clock;
	bool hdmi_ycbcr420_supported;
	bool dp_ycbcr420_supported;
	bool fec_supported;
};

union dpcd_psr_configuration {
	struct {
		unsigned char ENABLE                    : 1;
		unsigned char TRANSMITTER_ACTIVE_IN_PSR : 1;
		unsigned char CRC_VERIFICATION          : 1;
		unsigned char FRAME_CAPTURE_INDICATION  : 1;
		/* For eDP 1.4, PSR v2*/
		unsigned char LINE_CAPTURE_INDICATION   : 1;
		/* For eDP 1.4, PSR v2*/
		unsigned char IRQ_HPD_WITH_CRC_ERROR    : 1;
		unsigned char RESERVED                  : 2;
	} bits;
	unsigned char raw;
};

union psr_error_status {
	struct {
		unsigned char LINK_CRC_ERROR        :1;
		unsigned char RFB_STORAGE_ERROR     :1;
		unsigned char VSC_SDP_ERROR         :1;
		unsigned char RESERVED              :5;
	} bits;
	unsigned char raw;
};

union psr_sink_psr_status {
	struct {
	unsigned char SINK_SELF_REFRESH_STATUS  :3;
	unsigned char RESERVED                  :5;
	} bits;
	unsigned char raw;
};

struct link_encoder {
	const struct link_encoder_funcs *funcs;
	int32_t aux_channel_offset;
	struct dc_context *ctx;
	struct graphics_object_id id;
	struct graphics_object_id connector;
	uint32_t output_signals;
	enum engine_id preferred_engine;
	struct encoder_feature_support features;
	enum transmitter transmitter;
	enum hpd_source_id hpd_source;
	bool usbc_combo_phy;
};

struct link_enc_state {

		uint32_t dphy_fec_en;
		uint32_t dphy_fec_ready_shadow;
		uint32_t dphy_fec_active_status;
		uint32_t dp_link_training_complete;

};

enum encoder_type_select {
	ENCODER_TYPE_DIG = 0,
	ENCODER_TYPE_HDMI_FRL = 1,
	ENCODER_TYPE_DP_128B132B = 2
};

struct link_encoder_funcs {
	void (*read_state)(
			struct link_encoder *enc, struct link_enc_state *s);
	bool (*validate_output_with_stream)(
		struct link_encoder *enc, const struct dc_stream_state *stream);
	void (*hw_init)(struct link_encoder *enc);
	void (*setup)(struct link_encoder *enc,
		enum signal_type signal);
	void (*enable_tmds_output)(struct link_encoder *enc,
		enum clock_source_id clock_source,
		enum dc_color_depth color_depth,
		enum signal_type signal,
		uint32_t pixel_clock);
	void (*enable_dp_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*enable_dp_mst_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*enable_lvds_output)(struct link_encoder *enc,
		enum clock_source_id clock_source,
		uint32_t pixel_clock);
	void (*disable_output)(struct link_encoder *link_enc,
		enum signal_type signal);
	void (*dp_set_lane_settings)(struct link_encoder *enc,
		const struct link_training_settings *link_settings);
	void (*dp_set_phy_pattern)(struct link_encoder *enc,
		const struct encoder_set_dp_phy_pattern_param *para);
	void (*update_mst_stream_allocation_table)(
		struct link_encoder *enc,
		const struct link_mst_stream_allocation_table *table);
	void (*psr_program_dp_dphy_fast_training)(struct link_encoder *enc,
			bool exit_link_training_required);
	void (*psr_program_secondary_packet)(struct link_encoder *enc,
				unsigned int sdp_transmit_line_num_deadline);
	void (*connect_dig_be_to_fe)(struct link_encoder *enc,
		enum engine_id engine,
		bool connect);
	void (*enable_hpd)(struct link_encoder *enc);
	void (*disable_hpd)(struct link_encoder *enc);
	bool (*is_dig_enabled)(struct link_encoder *enc);
	unsigned int (*get_dig_frontend)(struct link_encoder *enc);
	void (*destroy)(struct link_encoder **enc);

	void (*fec_set_enable)(struct link_encoder *enc,
		bool enable);

	void (*fec_set_ready)(struct link_encoder *enc,
		bool ready);

	bool (*fec_is_active)(struct link_encoder *enc);
	bool (*is_in_alt_mode) (struct link_encoder *enc);

	void (*get_max_link_cap)(struct link_encoder *enc,
		struct dc_link_settings *link_settings);

	enum signal_type (*get_dig_mode)(
		struct link_encoder *enc);
	void (*set_dio_phy_mux)(
		struct link_encoder *enc,
		enum encoder_type_select sel,
		uint32_t hpo_inst);
};

/*
 * Used to track assignments of links (display endpoints) to link encoders.
 *
 * Entry in link_enc_assignments table in struct resource_context.
 * Entries only marked valid once encoder assigned to a link and invalidated once unassigned.
 * Uses engine ID as identifier since PHY ID not relevant for USB4 DPIA endpoint.
 */
struct link_enc_assignment {
	bool valid;
	struct display_endpoint_id ep_id;
	enum engine_id eng_id;
};

#if defined(CONFIG_DRM_AMD_DC_DCN)
enum dp2_link_mode {
	DP2_LINK_TRAINING_TPS1,
	DP2_LINK_TRAINING_TPS2,
	DP2_LINK_ACTIVE,
	DP2_TEST_PATTERN
};

enum dp2_phy_tp_select {
	DP_DPHY_TP_SELECT_TPS1,
	DP_DPHY_TP_SELECT_TPS2,
	DP_DPHY_TP_SELECT_PRBS,
	DP_DPHY_TP_SELECT_CUSTOM,
	DP_DPHY_TP_SELECT_SQUARE
};

enum dp2_phy_tp_prbs {
	DP_DPHY_TP_PRBS7,
	DP_DPHY_TP_PRBS9,
	DP_DPHY_TP_PRBS11,
	DP_DPHY_TP_PRBS15,
	DP_DPHY_TP_PRBS23,
	DP_DPHY_TP_PRBS31
};

struct hpo_dp_link_enc_state {
	uint32_t   link_enc_enabled;
	uint32_t   link_mode;
	uint32_t   lane_count;
	uint32_t   slot_count[4];
	uint32_t   stream_src[4];
	uint32_t   vc_rate_x[4];
	uint32_t   vc_rate_y[4];
};

struct hpo_dp_link_encoder {
	const struct hpo_dp_link_encoder_funcs *funcs;
	struct dc_context *ctx;
	int inst;
	enum engine_id preferred_engine;
	enum transmitter transmitter;
	enum hpd_source_id hpd_source;
};

struct hpo_dp_link_encoder_funcs {

	void (*enable_link_phy)(struct hpo_dp_link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum transmitter transmitter);

	void (*disable_link_phy)(struct hpo_dp_link_encoder *link_enc,
		enum signal_type signal);

	void (*link_enable)(
			struct hpo_dp_link_encoder *enc,
			enum dc_lane_count num_lanes);

	void (*link_disable)(
			struct hpo_dp_link_encoder *enc);

	void (*set_link_test_pattern)(
			struct hpo_dp_link_encoder *enc,
			struct encoder_set_dp_phy_pattern_param *tp_params);

	void (*update_stream_allocation_table)(
			struct hpo_dp_link_encoder *enc,
			const struct link_mst_stream_allocation_table *table);

	void (*set_throttled_vcp_size)(
			struct hpo_dp_link_encoder *enc,
			uint32_t stream_encoder_inst,
			struct fixed31_32 avg_time_slots_per_mtp);

	bool (*is_in_alt_mode) (
			struct hpo_dp_link_encoder *enc);

	void (*read_state)(
			struct hpo_dp_link_encoder *enc,
			struct hpo_dp_link_enc_state *state);

	void (*set_ffe)(
		struct hpo_dp_link_encoder *enc,
		const struct dc_link_settings *link_settings,
		uint8_t ffe_preset);
};
#endif

#endif /* LINK_ENCODER_H_ */

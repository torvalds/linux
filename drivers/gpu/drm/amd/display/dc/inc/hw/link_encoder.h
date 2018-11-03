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
		} bits;
		uint32_t raw;
	} flags;

	enum dc_color_depth max_hdmi_deep_color;
	unsigned int max_hdmi_pixel_clock;
	bool ycbcr420_supported;
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
		unsigned char RESERVED              :6;
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
};

struct link_encoder_funcs {
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
	void (*destroy)(struct link_encoder **enc);
};

#endif /* LINK_ENCODER_H_ */

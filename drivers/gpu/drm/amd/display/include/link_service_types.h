/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_LINK_SERVICE_TYPES_H__
#define __DAL_LINK_SERVICE_TYPES_H__

#include "grph_object_id.h"
#include "dpcd_defs.h"
#include "dal_types.h"
#include "irq_types.h"

/*struct mst_mgr_callback_object;*/
struct ddc;
struct irq_manager;

enum {
	MAX_CONTROLLER_NUM = 6
};

enum link_service_type {
	LINK_SERVICE_TYPE_LEGACY = 0,
	LINK_SERVICE_TYPE_DP_SST,
	LINK_SERVICE_TYPE_DP_MST,
	LINK_SERVICE_TYPE_MAX
};

enum dpcd_value_mask {
	DPCD_VALUE_MASK_MAX_LANE_COUNT_LANE_COUNT = 0x1F,
	DPCD_VALUE_MASK_MAX_LANE_COUNT_TPS3_SUPPORTED = 0x40,
	DPCD_VALUE_MASK_MAX_LANE_COUNT_ENHANCED_FRAME_EN = 0x80,
	DPCD_VALUE_MASK_MAX_DOWNSPREAD = 0x01,
	DPCD_VALUE_MASK_LANE_ALIGN_STATUS_INTERLANE_ALIGN_DONE = 0x01
};

enum dp_power_state {
	DP_POWER_STATE_D0 = 1,
	DP_POWER_STATE_D3
};

enum dpcd_downstream_port_types {
	DPCD_DOWNSTREAM_DP,
	DPCD_DOWNSTREAM_VGA,
	DPCD_DOWNSTREAM_DVI_HDMI,
	/* has no EDID (TV, CV) */
	DPCD_DOWNSTREAM_NON_DDC
};

enum edp_revision {
	/* eDP version 1.1 or lower */
	EDP_REVISION_11 = 0x00,
	/* eDP version 1.2 */
	EDP_REVISION_12 = 0x01,
	/* eDP version 1.3 */
	EDP_REVISION_13 = 0x02
};

enum {
	LINK_RATE_REF_FREQ_IN_KHZ = 27000 /*27MHz*/
};

struct link_training_settings {
	struct dc_link_settings link_settings;
	struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX];
	bool allow_invalid_msa_timing_param;
};

enum hw_dp_training_pattern {
	HW_DP_TRAINING_PATTERN_1 = 0,
	HW_DP_TRAINING_PATTERN_2,
	HW_DP_TRAINING_PATTERN_3,
	HW_DP_TRAINING_PATTERN_4
};

/*TODO: Move this enum test harness*/
/* Test patterns*/
enum dp_test_pattern {
	/* Input data is pass through Scrambler
	 * and 8b10b Encoder straight to output*/
	DP_TEST_PATTERN_VIDEO_MODE = 0,
	/* phy test patterns*/
	DP_TEST_PATTERN_D102,
	DP_TEST_PATTERN_SYMBOL_ERROR,
	DP_TEST_PATTERN_PRBS7,

	DP_TEST_PATTERN_80BIT_CUSTOM,
	DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE,

	/* Link Training Patterns */
	DP_TEST_PATTERN_TRAINING_PATTERN1,
	DP_TEST_PATTERN_TRAINING_PATTERN2,
	DP_TEST_PATTERN_TRAINING_PATTERN3,
	DP_TEST_PATTERN_TRAINING_PATTERN4,

	/* link test patterns*/
	DP_TEST_PATTERN_COLOR_SQUARES,
	DP_TEST_PATTERN_COLOR_SQUARES_CEA,
	DP_TEST_PATTERN_VERTICAL_BARS,
	DP_TEST_PATTERN_HORIZONTAL_BARS,
	DP_TEST_PATTERN_COLOR_RAMP,

	/* audio test patterns*/
	DP_TEST_PATTERN_AUDIO_OPERATOR_DEFINED,
	DP_TEST_PATTERN_AUDIO_SAWTOOTH,

	DP_TEST_PATTERN_UNSUPPORTED
};

enum dp_panel_mode {
	/* not required */
	DP_PANEL_MODE_DEFAULT,
	/* standard mode for eDP */
	DP_PANEL_MODE_EDP,
	/* external chips specific settings */
	DP_PANEL_MODE_SPECIAL
};

/**
 * @brief LinkServiceInitOptions to set certain bits
 */
struct link_service_init_options {
	uint32_t APPLY_MISALIGNMENT_BUG_WORKAROUND:1;
};

/**
 * @brief data required to initialize LinkService
 */
struct link_service_init_data {
	/* number of displays indices which the MST Mgr would manange*/
	uint32_t num_of_displays;
	enum link_service_type link_type;
	/*struct mst_mgr_callback_object*topology_change_callback;*/
	/* native aux access */
	struct ddc_service *dpcd_access_srv;
	/* for calling HWSS to program HW */
	struct hw_sequencer *hwss;
	/* the source which to register IRQ on */
	enum dc_irq_source irq_src_hpd_rx;
	enum dc_irq_source irq_src_dp_sink;
	/* other init options such as SW Workarounds */
	struct link_service_init_options init_options;
	uint32_t connector_enum_id;
	struct graphics_object_id connector_id;
	struct dc_context *ctx;
	struct topology_mgr *tm;
};

/* DPCD_ADDR_TRAINING_LANEx_SET registers value */
union dpcd_training_lane_set {
	struct {
#if defined(LITTLEENDIAN_CPU)
		uint8_t VOLTAGE_SWING_SET:2;
		uint8_t MAX_SWING_REACHED:1;
		uint8_t PRE_EMPHASIS_SET:2;
		uint8_t MAX_PRE_EMPHASIS_REACHED:1;
		/* following is reserved in DP 1.1 */
		uint8_t POST_CURSOR2_SET:2;
#elif defined(BIGENDIAN_CPU)
		uint8_t POST_CURSOR2_SET:2;
		uint8_t MAX_PRE_EMPHASIS_REACHED:1;
		uint8_t PRE_EMPHASIS_SET:2;
		uint8_t MAX_SWING_REACHED:1;
		uint8_t VOLTAGE_SWING_SET:2;
#else
	#error ARCH not defined!
#endif
	} bits;

	uint8_t raw;
};

/**
 * @brief represent the 16 byte
 *  global unique identifier
 */
struct mst_guid {
	uint8_t ids[16];
};

/**
 * @brief represents the relative address used
 * to identify a node in MST topology network
 */
struct mst_rad {
	/* number of links. rad[0] up to
	 * rad [linkCount - 1] are valid. */
	uint32_t rad_link_count;
	/* relative address. rad[0] is the
	 * first device connected to the source.	*/
	uint8_t rad[15];
	/* extra 10 bytes for underscores; for e.g.:2_1_8*/
	int8_t rad_str[25];
};

/* DP MST stream allocation (payload bandwidth number) */
struct dp_mst_stream_allocation {
	uint8_t vcp_id;
	/* number of slots required for the DP stream in
	 * transport packet */
	uint8_t slot_count;
};

/* DP MST stream allocation table */
struct dp_mst_stream_allocation_table {
	/* number of DP video streams */
	int stream_count;
	/* array of stream allocations */
	struct dp_mst_stream_allocation stream_allocations[MAX_CONTROLLER_NUM];
};

#endif /*__DAL_LINK_SERVICE_TYPES_H__*/

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
#include "dal_types.h"
#include "irq_types.h"

/*struct mst_mgr_callback_object;*/
struct ddc;
struct irq_manager;

enum {
	MAX_CONTROLLER_NUM = 6
};

enum dp_power_state {
	DP_POWER_STATE_D0 = 1,
	DP_POWER_STATE_D3
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

enum link_training_result {
	LINK_TRAINING_SUCCESS,
	LINK_TRAINING_CR_FAIL_LANE0,
	LINK_TRAINING_CR_FAIL_LANE1,
	LINK_TRAINING_CR_FAIL_LANE23,
	/* CR DONE bit is cleared during EQ step */
	LINK_TRAINING_EQ_FAIL_CR,
	/* other failure during EQ step */
	LINK_TRAINING_EQ_FAIL_EQ,
	LINK_TRAINING_LQA_FAIL,
};

struct link_training_settings {
	struct dc_link_settings link_settings;
	struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX];

	enum dc_voltage_swing *voltage_swing;
	enum dc_pre_emphasis *pre_emphasis;
	enum dc_post_cursor2 *post_cursor2;

	uint16_t cr_pattern_time;
	uint16_t eq_pattern_time;
	enum dc_dp_training_pattern pattern_for_eq;

	bool enhanced_framing;
	bool allow_invalid_msa_timing_param;
};

/*TODO: Move this enum test harness*/
/* Test patterns*/
enum dp_test_pattern {
	/* Input data is pass through Scrambler
	 * and 8b10b Encoder straight to output*/
	DP_TEST_PATTERN_VIDEO_MODE = 0,

	/* phy test patterns*/
	DP_TEST_PATTERN_PHY_PATTERN_BEGIN,
	DP_TEST_PATTERN_D102 = DP_TEST_PATTERN_PHY_PATTERN_BEGIN,
	DP_TEST_PATTERN_SYMBOL_ERROR,
	DP_TEST_PATTERN_PRBS7,
	DP_TEST_PATTERN_80BIT_CUSTOM,
	DP_TEST_PATTERN_CP2520_1,
	DP_TEST_PATTERN_CP2520_2,
	DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE = DP_TEST_PATTERN_CP2520_2,
	DP_TEST_PATTERN_CP2520_3,

	/* Link Training Patterns */
	DP_TEST_PATTERN_TRAINING_PATTERN1,
	DP_TEST_PATTERN_TRAINING_PATTERN2,
	DP_TEST_PATTERN_TRAINING_PATTERN3,
	DP_TEST_PATTERN_TRAINING_PATTERN4,
	DP_TEST_PATTERN_PHY_PATTERN_END = DP_TEST_PATTERN_TRAINING_PATTERN4,

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

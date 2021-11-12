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

#ifndef __DAL_DPCD_DEFS_H__
#define __DAL_DPCD_DEFS_H__

#include <drm/drm_dp_helper.h>
#ifndef DP_SINK_HW_REVISION_START // can remove this once the define gets into linux drm_dp_helper.h
#define DP_SINK_HW_REVISION_START 0x409
#endif

enum dpcd_revision {
	DPCD_REV_10 = 0x10,
	DPCD_REV_11 = 0x11,
	DPCD_REV_12 = 0x12,
	DPCD_REV_13 = 0x13,
	DPCD_REV_14 = 0x14
};

/* these are the types stored at DOWNSTREAMPORT_PRESENT */
enum dpcd_downstream_port_type {
	DOWNSTREAM_DP = 0,
	DOWNSTREAM_VGA,
	DOWNSTREAM_DVI_HDMI_DP_PLUS_PLUS,/* DVI, HDMI, DP++ */
	DOWNSTREAM_NONDDC /* has no EDID (TV,CV) */
};

enum dpcd_link_test_patterns {
	LINK_TEST_PATTERN_NONE = 0,
	LINK_TEST_PATTERN_COLOR_RAMP,
	LINK_TEST_PATTERN_VERTICAL_BARS,
	LINK_TEST_PATTERN_COLOR_SQUARES
};

enum dpcd_test_color_format {
	TEST_COLOR_FORMAT_RGB = 0,
	TEST_COLOR_FORMAT_YCBCR422,
	TEST_COLOR_FORMAT_YCBCR444
};

enum dpcd_test_bit_depth {
	TEST_BIT_DEPTH_6 = 0,
	TEST_BIT_DEPTH_8,
	TEST_BIT_DEPTH_10,
	TEST_BIT_DEPTH_12,
	TEST_BIT_DEPTH_16
};

/* PHY (encoder) test patterns
The order of test patterns follows DPCD register PHY_TEST_PATTERN (0x248)
*/
enum dpcd_phy_test_patterns {
	PHY_TEST_PATTERN_NONE = 0,
	PHY_TEST_PATTERN_D10_2,
	PHY_TEST_PATTERN_SYMBOL_ERROR,
	PHY_TEST_PATTERN_PRBS7,
	PHY_TEST_PATTERN_80BIT_CUSTOM,/* For DP1.2 only */
	PHY_TEST_PATTERN_CP2520_1,
	PHY_TEST_PATTERN_CP2520_2,
	PHY_TEST_PATTERN_CP2520_3, /* same as TPS4 */
	PHY_TEST_PATTERN_128b_132b_TPS1 = 0x8,
	PHY_TEST_PATTERN_128b_132b_TPS2 = 0x10,
	PHY_TEST_PATTERN_PRBS9 = 0x18,
	PHY_TEST_PATTERN_PRBS11 = 0x20,
	PHY_TEST_PATTERN_PRBS15 = 0x28,
	PHY_TEST_PATTERN_PRBS23 = 0x30,
	PHY_TEST_PATTERN_PRBS31 = 0x38,
	PHY_TEST_PATTERN_264BIT_CUSTOM = 0x40,
	PHY_TEST_PATTERN_SQUARE_PULSE = 0x48,
};

enum dpcd_test_dyn_range {
	TEST_DYN_RANGE_VESA = 0,
	TEST_DYN_RANGE_CEA
};

enum dpcd_audio_test_pattern {
	AUDIO_TEST_PATTERN_OPERATOR_DEFINED = 0,/* direct HW translation */
	AUDIO_TEST_PATTERN_SAWTOOTH
};

enum dpcd_audio_sampling_rate {
	AUDIO_SAMPLING_RATE_32KHZ = 0,/* direct HW translation */
	AUDIO_SAMPLING_RATE_44_1KHZ,
	AUDIO_SAMPLING_RATE_48KHZ,
	AUDIO_SAMPLING_RATE_88_2KHZ,
	AUDIO_SAMPLING_RATE_96KHZ,
	AUDIO_SAMPLING_RATE_176_4KHZ,
	AUDIO_SAMPLING_RATE_192KHZ
};

enum dpcd_audio_channels {
	AUDIO_CHANNELS_1 = 0,/* direct HW translation */
	AUDIO_CHANNELS_2,
	AUDIO_CHANNELS_3,
	AUDIO_CHANNELS_4,
	AUDIO_CHANNELS_5,
	AUDIO_CHANNELS_6,
	AUDIO_CHANNELS_7,
	AUDIO_CHANNELS_8,

	AUDIO_CHANNELS_COUNT
};

enum dpcd_audio_test_pattern_periods {
	DPCD_AUDIO_TEST_PATTERN_PERIOD_NOTUSED = 0,/* direct HW translation */
	DPCD_AUDIO_TEST_PATTERN_PERIOD_3,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_6,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_12,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_24,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_48,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_96,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_192,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_384,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_768,
	DPCD_AUDIO_TEST_PATTERN_PERIOD_1536
};

/* This enum is for programming DPCD TRAINING_PATTERN_SET */
enum dpcd_training_patterns {
	DPCD_TRAINING_PATTERN_VIDEOIDLE = 0,/* direct HW translation! */
	DPCD_TRAINING_PATTERN_1,
	DPCD_TRAINING_PATTERN_2,
	DPCD_TRAINING_PATTERN_3,
#if defined(CONFIG_DRM_AMD_DC_DCN)
	DPCD_TRAINING_PATTERN_4 = 7,
	DPCD_128b_132b_TPS1 = 1,
	DPCD_128b_132b_TPS2 = 2,
	DPCD_128b_132b_TPS2_CDS = 3,
#else
	DPCD_TRAINING_PATTERN_4 = 7
#endif
};

/* This enum is for use with PsrSinkPsrStatus.bits.sinkSelfRefreshStatus
It defines the possible PSR states. */
enum dpcd_psr_sink_states {
	PSR_SINK_STATE_INACTIVE = 0,
	PSR_SINK_STATE_ACTIVE_CAPTURE_DISPLAY_ON_SOURCE_TIMING = 1,
	PSR_SINK_STATE_ACTIVE_DISPLAY_FROM_SINK_RFB = 2,
	PSR_SINK_STATE_ACTIVE_CAPTURE_DISPLAY_ON_SINK_TIMING = 3,
	PSR_SINK_STATE_ACTIVE_CAPTURE_TIMING_RESYNC = 4,
	PSR_SINK_STATE_SINK_INTERNAL_ERROR = 7,
};

#define DP_SOURCE_SEQUENCE    		    0x30c
#define DP_SOURCE_TABLE_REVISION	    0x310
#define DP_SOURCE_PAYLOAD_SIZE		    0x311
#define DP_SOURCE_SINK_CAP		    0x317
#define DP_SOURCE_BACKLIGHT_LEVEL	    0x320
#define DP_SOURCE_BACKLIGHT_CURRENT_PEAK    0x326
#define DP_SOURCE_BACKLIGHT_CONTROL	    0x32E
#define DP_SOURCE_BACKLIGHT_ENABLE	    0x32F
#define DP_SOURCE_MINIMUM_HBLANK_SUPPORTED	0x340

#endif /* __DAL_DPCD_DEFS_H__ */

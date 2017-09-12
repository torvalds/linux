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

enum dpcd_address {
/* addresses marked with 1.2 are only defined since DP 1.2 spec */

	/* Reciever Capability Field */
	DPCD_ADDRESS_DPCD_REV = 0x00000,
	DPCD_ADDRESS_MAX_LINK_RATE = 0x00001,
	DPCD_ADDRESS_MAX_LANE_COUNT = 0x00002,
	DPCD_ADDRESS_MAX_DOWNSPREAD = 0x00003,
	DPCD_ADDRESS_NORP = 0x00004,
	DPCD_ADDRESS_DOWNSTREAM_PORT_PRESENT = 0x00005,
	DPCD_ADDRESS_MAIN_LINK_CHANNEL_CODING = 0x00006,
	DPCD_ADDRESS_DOWNSTREAM_PORT_COUNT = 0x00007,
	DPCD_ADDRESS_RECEIVE_PORT0_CAP0 = 0x00008,
	DPCD_ADDRESS_RECEIVE_PORT0_CAP1 = 0x00009,
	DPCD_ADDRESS_RECEIVE_PORT1_CAP0 = 0x0000A,
	DPCD_ADDRESS_RECEIVE_PORT1_CAP1 = 0x0000B,

	DPCD_ADDRESS_I2C_SPEED_CNTL_CAP = 0x0000C,/*1.2*/
	DPCD_ADDRESS_EDP_CONFIG_CAP = 0x0000D,/*1.2*/
	DPCD_ADDRESS_TRAINING_AUX_RD_INTERVAL = 0x000E,/*1.2*/

	DPCD_ADDRESS_MSTM_CAP = 0x00021,/*1.2*/

	/* Audio Video Sync Data Feild */
	DPCD_ADDRESS_AV_GRANULARITY = 0x0023,
	DPCD_ADDRESS_AUDIO_DECODE_LATENCY1 = 0x0024,
	DPCD_ADDRESS_AUDIO_DECODE_LATENCY2 = 0x0025,
	DPCD_ADDRESS_AUDIO_POSTPROCESSING_LATENCY1 = 0x0026,
	DPCD_ADDRESS_AUDIO_POSTPROCESSING_LATENCY2 = 0x0027,
	DPCD_ADDRESS_VIDEO_INTERLACED_LATENCY = 0x0028,
	DPCD_ADDRESS_VIDEO_PROGRESSIVE_LATENCY = 0x0029,
	DPCD_ADDRESS_AUDIO_DELAY_INSERT1 = 0x0002B,
	DPCD_ADDRESS_AUDIO_DELAY_INSERT2 = 0x0002C,
	DPCD_ADDRESS_AUDIO_DELAY_INSERT3 = 0x0002D,

	/* Audio capability */
	DPCD_ADDRESS_NUM_OF_AUDIO_ENDPOINTS = 0x00022,

	DPCD_ADDRESS_GUID_START = 0x00030,/*1.2*/
	DPCD_ADDRESS_GUID_END = 0x0003f,/*1.2*/

	DPCD_ADDRESS_PSR_SUPPORT_VER = 0x00070,
	DPCD_ADDRESS_PSR_CAPABILITY = 0x00071,

	DPCD_ADDRESS_DWN_STRM_PORT0_CAPS = 0x00080,/*1.2a*/

	/* Link Configuration Field */
	DPCD_ADDRESS_LINK_BW_SET = 0x00100,
	DPCD_ADDRESS_LANE_COUNT_SET = 0x00101,
	DPCD_ADDRESS_TRAINING_PATTERN_SET = 0x00102,
	DPCD_ADDRESS_LANE0_SET = 0x00103,
	DPCD_ADDRESS_LANE1_SET = 0x00104,
	DPCD_ADDRESS_LANE2_SET = 0x00105,
	DPCD_ADDRESS_LANE3_SET = 0x00106,
	DPCD_ADDRESS_DOWNSPREAD_CNTL = 0x00107,
	DPCD_ADDRESS_I2C_SPEED_CNTL = 0x00109,/*1.2*/

	DPCD_ADDRESS_EDP_CONFIG_SET = 0x0010A,
	DPCD_ADDRESS_LINK_QUAL_LANE0_SET = 0x0010B,
	DPCD_ADDRESS_LINK_QUAL_LANE1_SET = 0x0010C,
	DPCD_ADDRESS_LINK_QUAL_LANE2_SET = 0x0010D,
	DPCD_ADDRESS_LINK_QUAL_LANE3_SET = 0x0010E,

	DPCD_ADDRESS_LANE0_SET2 = 0x0010F,/*1.2*/
	DPCD_ADDRESS_LANE2_SET2 = 0x00110,/*1.2*/

	DPCD_ADDRESS_MSTM_CNTL = 0x00111,/*1.2*/

	DPCD_ADDRESS_PSR_ENABLE_CFG = 0x0170,

	/* Payload Table Configuration Field 1.2 */
	DPCD_ADDRESS_PAYLOAD_ALLOCATE_SET = 0x001C0,
	DPCD_ADDRESS_PAYLOAD_ALLOCATE_START_TIMESLOT = 0x001C1,
	DPCD_ADDRESS_PAYLOAD_ALLOCATE_TIMESLOT_COUNT = 0x001C2,

	DPCD_ADDRESS_SINK_COUNT = 0x0200,
	DPCD_ADDRESS_DEVICE_SERVICE_IRQ_VECTOR = 0x0201,

	/* Link / Sink Status Field */
	DPCD_ADDRESS_LANE_01_STATUS = 0x00202,
	DPCD_ADDRESS_LANE_23_STATUS = 0x00203,
	DPCD_ADDRESS_LANE_ALIGN_STATUS_UPDATED = 0x0204,
	DPCD_ADDRESS_SINK_STATUS = 0x0205,

	/* Adjust Request Field */
	DPCD_ADDRESS_ADJUST_REQUEST_LANE0_1 = 0x0206,
	DPCD_ADDRESS_ADJUST_REQUEST_LANE2_3 = 0x0207,
	DPCD_ADDRESS_ADJUST_REQUEST_POST_CURSOR2 = 0x020C,

	/* Test Request Field */
	DPCD_ADDRESS_TEST_REQUEST = 0x0218,
	DPCD_ADDRESS_TEST_LINK_RATE = 0x0219,
	DPCD_ADDRESS_TEST_LANE_COUNT = 0x0220,
	DPCD_ADDRESS_TEST_PATTERN = 0x0221,
	DPCD_ADDRESS_TEST_MISC1 = 0x0232,

	/* Phy Test Pattern Field */
	DPCD_ADDRESS_TEST_PHY_PATTERN = 0x0248,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_7_0 = 0x0250,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_15_8 = 0x0251,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_23_16 = 0x0252,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_31_24 = 0x0253,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_39_32 = 0x0254,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_47_40 = 0x0255,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_55_48 = 0x0256,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_63_56 = 0x0257,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_71_64 = 0x0258,
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_79_72 = 0x0259,

	/* Test Response Field*/
	DPCD_ADDRESS_TEST_RESPONSE = 0x0260,

	/* Audio Test Pattern Field 1.2*/
	DPCD_ADDRESS_TEST_AUDIO_MODE = 0x0271,
	DPCD_ADDRESS_TEST_AUDIO_PATTERN_TYPE = 0x0272,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_1 = 0x0273,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_2 = 0x0274,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_3 = 0x0275,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_4 = 0x0276,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_5 = 0x0277,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_6 = 0x0278,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_7 = 0x0279,
	DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_8 = 0x027A,

	/* Payload Table Status Field */
	DPCD_ADDRESS_PAYLOAD_TABLE_UPDATE_STATUS = 0x002C0,/*1.2*/
	DPCD_ADDRESS_VC_PAYLOAD_ID_SLOT1 = 0x002C1,/*1.2*/
	DPCD_ADDRESS_VC_PAYLOAD_ID_SLOT63 = 0x002FF,/*1.2*/

	/* Source Device Specific Field */
	DPCD_ADDRESS_SOURCE_DEVICE_ID_START = 0x0300,
	DPCD_ADDRESS_SOURCE_DEVICE_ID_END = 0x0301,
	DPCD_ADDRESS_AMD_INTERNAL_DEBUG_START       = 0x030C,
	DPCD_ADDRESS_AMD_INTERNAL_DEBUG_END         = 0x030F,
	DPCD_ADDRESS_SOURCE_SPECIFIC_TABLE_START    = 0x0310,
	DPCD_ADDRESS_SOURCE_SPECIFIC_TABLE_END      = 0x037F,
	DPCD_ADDRESS_SOURCE_RESERVED_START         = 0x0380,
	DPCD_ADDRESS_SOURCE_RESERVED_END           = 0x03FF,

	/* Sink Device Specific Field */
	DPCD_ADDRESS_SINK_DEVICE_ID_START = 0x0400,
	DPCD_ADDRESS_SINK_DEVICE_ID_END = 0x0402,
	DPCD_ADDRESS_SINK_DEVICE_STR_START = 0x0403,
	DPCD_ADDRESS_SINK_DEVICE_STR_END = 0x0408,
	DPCD_ADDRESS_SINK_REVISION_START = 0x409,
	DPCD_ADDRESS_SINK_REVISION_END = 0x40B,

	/* Branch Device Specific Field */
	DPCD_ADDRESS_BRANCH_DEVICE_ID_START = 0x0500,
	DPCD_ADDRESS_BRANCH_DEVICE_ID_END = 0x0502,
	DPCD_ADDRESS_BRANCH_DEVICE_STR_START = 0x0503,
	DPCD_ADDRESS_BRANCH_DEVICE_STR_END = 0x0508,
	DPCD_ADDRESS_BRANCH_REVISION_START = 0x0509,
	DPCD_ADDRESS_BRANCH_REVISION_END = 0x050B,

	DPCD_ADDRESS_POWER_STATE = 0x0600,

	/* EDP related */
	DPCD_ADDRESS_EDP_REV = 0x0700,
	DPCD_ADDRESS_EDP_CAPABILITY = 0x0701,
	DPCD_ADDRESS_EDP_BACKLIGHT_ADJUST_CAP = 0x0702,
	DPCD_ADDRESS_EDP_GENERAL_CAP2 = 0x0703,

	DPCD_ADDRESS_EDP_DISPLAY_CONTROL = 0x0720,
	DPCD_ADDRESS_SUPPORTED_LINK_RATES = 0x00010, /* edp 1.4 */
	DPCD_ADDRESS_EDP_BACKLIGHT_SET = 0x0721,
	DPCD_ADDRESS_EDP_BACKLIGHT_BRIGHTNESS_MSB = 0x0722,
	DPCD_ADDRESS_EDP_BACKLIGHT_BRIGHTNESS_LSB = 0x0723,
	DPCD_ADDRESS_EDP_PWMGEN_BIT_COUNT = 0x0724,
	DPCD_ADDRESS_EDP_PWMGEN_BIT_COUNT_CAP_MIN = 0x0725,
	DPCD_ADDRESS_EDP_PWMGEN_BIT_COUNT_CAP_MAX = 0x0726,
	DPCD_ADDRESS_EDP_BACKLIGHT_CONTROL_STATUS = 0x0727,
	DPCD_ADDRESS_EDP_BACKLIGHT_FREQ_SET = 0x0728,
	DPCD_ADDRESS_EDP_REVERVED = 0x0729,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MIN_MSB = 0x072A,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MIN_MID = 0x072B,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MIN_LSB = 0x072C,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MAX_MSB = 0x072D,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MAX_MID = 0x072E,
	DPCD_ADDRESS_EDP_BACKLIGNT_FREQ_CAP_MAX_LSB = 0x072F,

	DPCD_ADDRESS_EDP_DBC_MINIMUM_BRIGHTNESS_SET = 0x0732,
	DPCD_ADDRESS_EDP_DBC_MAXIMUM_BRIGHTNESS_SET = 0x0733,

	/* Sideband MSG Buffers 1.2 */
	DPCD_ADDRESS_DOWN_REQ_START = 0x01000,
	DPCD_ADDRESS_DOWN_REQ_END = 0x011ff,

	DPCD_ADDRESS_UP_REP_START = 0x01200,
	DPCD_ADDRESS_UP_REP_END = 0x013ff,

	DPCD_ADDRESS_DOWN_REP_START = 0x01400,
	DPCD_ADDRESS_DOWN_REP_END = 0x015ff,

	DPCD_ADDRESS_UP_REQ_START = 0x01600,
	DPCD_ADDRESS_UP_REQ_END = 0x017ff,

	/* ESI (Event Status Indicator) Field 1.2 */
	DPCD_ADDRESS_SINK_COUNT_ESI = 0x02002,
	DPCD_ADDRESS_DEVICE_IRQ_ESI0 = 0x02003,
	DPCD_ADDRESS_DEVICE_IRQ_ESI1 = 0x02004,
	/*@todo move dpcd_address_Lane01Status back here*/

	DPCD_ADDRESS_PSR_ERROR_STATUS = 0x2006,
	DPCD_ADDRESS_PSR_EVENT_STATUS = 0x2007,
	DPCD_ADDRESS_PSR_SINK_STATUS = 0x2008,
	DPCD_ADDRESS_PSR_DBG_REGISTER0 = 0x2009,
	DPCD_ADDRESS_PSR_DBG_REGISTER1 = 0x200A,

	DPCD_ADDRESS_DP13_DPCD_REV = 0x2200,
	DPCD_ADDRESS_DP13_MAX_LINK_RATE = 0x2201,

	/* Travis specific addresses */
	DPCD_ADDRESS_TRAVIS_SINK_DEV_SEL = 0x5f0,
	DPCD_ADDRESS_TRAVIS_SINK_ACCESS_OFFSET	= 0x5f1,
	DPCD_ADDRESS_TRAVIS_SINK_ACCESS_REG = 0x5f2,
};

enum dpcd_revision {
	DPCD_REV_10 = 0x10,
	DPCD_REV_11 = 0x11,
	DPCD_REV_12 = 0x12,
	DPCD_REV_13 = 0x13,
	DPCD_REV_14 = 0x14
};

enum dp_pwr_state {
	DP_PWR_STATE_D0 = 1,/* direct HW translation! */
	DP_PWR_STATE_D3
};

/* these are the types stored at DOWNSTREAMPORT_PRESENT */
enum dpcd_downstream_port_type {
	DOWNSTREAM_DP = 0,
	DOWNSTREAM_VGA,
	DOWNSTREAM_DVI_HDMI,
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
	PHY_TEST_PATTERN_HBR2_COMPLIANCE_EYE/* For DP1.2 only */
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
	DPCD_TRAINING_PATTERN_4 = 7
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

/* This enum defines the Panel's eDP revision at DPCD 700h
 * 00h = eDP v1.1 or lower
 * 01h = eDP v1.2
 * 02h = eDP v1.3 (PSR support starts here)
 * 03h = eDP v1.4
 * If unknown revision, treat as eDP v1.1, meaning least functionality set.
 * This enum has values matched to eDP spec, thus values should not change.
 */
enum dpcd_edp_revision {
	DPCD_EDP_REVISION_EDP_V1_1 = 0,
	DPCD_EDP_REVISION_EDP_V1_2 = 1,
	DPCD_EDP_REVISION_EDP_V1_3 = 2,
	DPCD_EDP_REVISION_EDP_V1_4 = 3,
	DPCD_EDP_REVISION_EDP_UNKNOWN = DPCD_EDP_REVISION_EDP_V1_1,
};

union dpcd_rev {
	struct {
		uint8_t MINOR:4;
		uint8_t MAJOR:4;
	} bits;
	uint8_t raw;
};

union max_lane_count {
	struct {
		uint8_t MAX_LANE_COUNT:5;
		uint8_t POST_LT_ADJ_REQ_SUPPORTED:1;
		uint8_t TPS3_SUPPORTED:1;
		uint8_t ENHANCED_FRAME_CAP:1;
	} bits;
	uint8_t raw;
};

union max_down_spread {
	struct {
		uint8_t MAX_DOWN_SPREAD:1;
		uint8_t RESERVED:5;
		uint8_t NO_AUX_HANDSHAKE_LINK_TRAINING:1;
		uint8_t TPS4_SUPPORTED:1;
	} bits;
	uint8_t raw;
};

union mstm_cap {
	struct {
		uint8_t MST_CAP:1;
		uint8_t RESERVED:7;
	} bits;
	uint8_t raw;
};

union lane_count_set {
	struct {
		uint8_t LANE_COUNT_SET:5;
		uint8_t POST_LT_ADJ_REQ_GRANTED:1;
		uint8_t RESERVED:1;
		uint8_t ENHANCED_FRAMING:1;
	} bits;
	uint8_t raw;
};

union lane_status {
	struct {
		uint8_t CR_DONE_0:1;
		uint8_t CHANNEL_EQ_DONE_0:1;
		uint8_t SYMBOL_LOCKED_0:1;
		uint8_t RESERVED0:1;
		uint8_t CR_DONE_1:1;
		uint8_t CHANNEL_EQ_DONE_1:1;
		uint8_t SYMBOL_LOCKED_1:1;
		uint8_t RESERVED_1:1;
	} bits;
	uint8_t raw;
};

union device_service_irq {
	struct {
		uint8_t REMOTE_CONTROL_CMD_PENDING:1;
		uint8_t AUTOMATED_TEST:1;
		uint8_t CP_IRQ:1;
		uint8_t MCCS_IRQ:1;
		uint8_t DOWN_REP_MSG_RDY:1;
		uint8_t UP_REQ_MSG_RDY:1;
		uint8_t SINK_SPECIFIC:1;
		uint8_t reserved:1;
	} bits;
	uint8_t raw;
};

union sink_count {
	struct {
		uint8_t SINK_COUNT:6;
		uint8_t CPREADY:1;
		uint8_t RESERVED:1;
	} bits;
	uint8_t raw;
};

union lane_align_status_updated {
	struct {
		uint8_t INTERLANE_ALIGN_DONE:1;
		uint8_t POST_LT_ADJ_REQ_IN_PROGRESS:1;
		uint8_t RESERVED:4;
		uint8_t DOWNSTREAM_PORT_STATUS_CHANGED:1;
		uint8_t LINK_STATUS_UPDATED:1;
	} bits;
	uint8_t raw;
};

union lane_adjust {
	struct {
		uint8_t VOLTAGE_SWING_LANE:2;
		uint8_t PRE_EMPHASIS_LANE:2;
		uint8_t RESERVED:4;
	} bits;
	uint8_t raw;
};

union dpcd_training_pattern {
	struct {
		uint8_t TRAINING_PATTERN_SET:4;
		uint8_t RECOVERED_CLOCK_OUT_EN:1;
		uint8_t SCRAMBLING_DISABLE:1;
		uint8_t SYMBOL_ERROR_COUNT_SEL:2;
	} v1_4;
	struct {
		uint8_t TRAINING_PATTERN_SET:2;
		uint8_t LINK_QUAL_PATTERN_SET:2;
		uint8_t RESERVED:4;
	} v1_3;
	uint8_t raw;
};

/* Training Lane is used to configure downstream DP device's voltage swing
and pre-emphasis levels*/
/* The DPCD addresses are from 0x103 to 0x106*/
union dpcd_training_lane {
	struct {
		uint8_t VOLTAGE_SWING_SET:2;
		uint8_t MAX_SWING_REACHED:1;
		uint8_t PRE_EMPHASIS_SET:2;
		uint8_t MAX_PRE_EMPHASIS_REACHED:1;
		uint8_t RESERVED:2;
	} bits;
	uint8_t raw;
};

/* TMDS-converter related */
union dwnstream_port_caps_byte0 {
	struct {
		uint8_t DWN_STRM_PORTX_TYPE:3;
		uint8_t DWN_STRM_PORTX_HPD:1;
		uint8_t RESERVERD:4;
	} bits;
	uint8_t raw;
};

/* these are the detailed types stored at DWN_STRM_PORTX_CAP (00080h)*/
enum dpcd_downstream_port_detailed_type {
	DOWN_STREAM_DETAILED_DP = 0,
	DOWN_STREAM_DETAILED_VGA,
	DOWN_STREAM_DETAILED_DVI,
	DOWN_STREAM_DETAILED_HDMI,
	DOWN_STREAM_DETAILED_NONDDC,/* has no EDID (TV,CV)*/
	DOWN_STREAM_DETAILED_DP_PLUS_PLUS
};

union dwnstream_port_caps_byte2 {
	struct {
		uint8_t MAX_BITS_PER_COLOR_COMPONENT:2;
		uint8_t RESERVED:6;
	} bits;
	uint8_t raw;
};

union dp_downstream_port_present {
	uint8_t byte;
	struct {
		uint8_t PORT_PRESENT:1;
		uint8_t PORT_TYPE:2;
		uint8_t FMT_CONVERSION:1;
		uint8_t DETAILED_CAPS:1;
		uint8_t RESERVED:3;
	} fields;
};

union dwnstream_port_caps_byte3_dvi {
	struct {
		uint8_t RESERVED1:1;
		uint8_t DUAL_LINK:1;
		uint8_t HIGH_COLOR_DEPTH:1;
		uint8_t RESERVED2:5;
	} bits;
	uint8_t raw;
};

union dwnstream_port_caps_byte3_hdmi {
	struct {
		uint8_t FRAME_SEQ_TO_FRAME_PACK:1;
		uint8_t RESERVED:7;
	} bits;
	uint8_t raw;
};

/*4-byte structure for detailed capabilities of a down-stream port
(DP-to-TMDS converter).*/

union sink_status {
	struct {
		uint8_t RX_PORT0_STATUS:1;
		uint8_t RX_PORT1_STATUS:1;
		uint8_t RESERVED:6;
	} bits;
	uint8_t raw;
};

/*6-byte structure corresponding to 6 registers (200h-205h)
read during handling of HPD-IRQ*/
union hpd_irq_data {
	struct {
		union sink_count sink_cnt;/* 200h */
		union device_service_irq device_service_irq;/* 201h */
		union lane_status lane01_status;/* 202h */
		union lane_status lane23_status;/* 203h */
		union lane_align_status_updated lane_status_updated;/* 204h */
		union sink_status sink_status;
	} bytes;
	uint8_t raw[6];
};

union down_stream_port_count {
	struct {
		uint8_t DOWN_STR_PORT_COUNT:4;
		uint8_t RESERVED:2; /*Bits 5:4 = RESERVED. Read all 0s.*/
		/*Bit 6 = MSA_TIMING_PAR_IGNORED
		0 = Sink device requires the MSA timing parameters
		1 = Sink device is capable of rendering incoming video
		 stream without MSA timing parameters*/
		uint8_t IGNORE_MSA_TIMING_PARAM:1;
		/*Bit 7 = OUI Support
		0 = OUI not supported
		1 = OUI supported
		(OUI and Device Identification mandatory for DP 1.2)*/
		uint8_t OUI_SUPPORT:1;
	} bits;
	uint8_t raw;
};

union down_spread_ctrl {
	struct {
		uint8_t RESERVED1:4;/* Bit 3:0 = RESERVED. Read all 0s*/
	/* Bits 4 = SPREAD_AMP. Spreading amplitude
	0 = Main link signal is not downspread
	1 = Main link signal is downspread <= 0.5%
	with frequency in the range of 30kHz ~ 33kHz*/
		uint8_t SPREAD_AMP:1;
		uint8_t RESERVED2:2;/*Bit 6:5 = RESERVED. Read all 0s*/
	/*Bit 7 = MSA_TIMING_PAR_IGNORE_EN
	0 = Source device will send valid data for the MSA Timing Params
	1 = Source device may send invalid data for these MSA Timing Params*/
		uint8_t IGNORE_MSA_TIMING_PARAM:1;
	} bits;
	uint8_t raw;
};

union dpcd_edp_config {
	struct {
		uint8_t PANEL_MODE_EDP:1;
		uint8_t FRAMING_CHANGE_ENABLE:1;
		uint8_t RESERVED:5;
		uint8_t PANEL_SELF_TEST_ENABLE:1;
	} bits;
	uint8_t raw;
};

struct dp_device_vendor_id {
	uint8_t ieee_oui[3];/*24-bit IEEE OUI*/
	uint8_t ieee_device_id[6];/*usually 6-byte ASCII name*/
};

struct dp_sink_hw_fw_revision {
	uint8_t ieee_hw_rev;
	uint8_t ieee_fw_rev[2];
};

/*DPCD register of DP receiver capability field bits-*/
union edp_configuration_cap {
	struct {
		uint8_t ALT_SCRAMBLER_RESET:1;
		uint8_t FRAMING_CHANGE:1;
		uint8_t RESERVED:1;
		uint8_t DPCD_DISPLAY_CONTROL_CAPABLE:1;
		uint8_t RESERVED2:4;
	} bits;
	uint8_t raw;
};

union training_aux_rd_interval {
	struct {
		uint8_t TRAINIG_AUX_RD_INTERVAL:7;
		uint8_t EXT_RECIEVER_CAP_FIELD_PRESENT:1;
	} bits;
	uint8_t raw;
};

/* Automated test structures */
union test_request {
	struct {
	uint8_t LINK_TRAINING         :1;
	uint8_t LINK_TEST_PATTRN      :1;
	uint8_t EDID_REAT             :1;
	uint8_t PHY_TEST_PATTERN      :1;
	uint8_t AUDIO_TEST_PATTERN    :1;
	uint8_t RESERVED              :1;
	uint8_t TEST_STEREO_3D        :1;
	} bits;
	uint8_t raw;
};

union test_response {
	struct {
		uint8_t ACK         :1;
		uint8_t NO_ACK      :1;
		uint8_t RESERVED    :6;
	} bits;
	uint8_t raw;
};

union phy_test_pattern {
	struct {
		/* DpcdPhyTestPatterns. This field is 2 bits for DP1.1
		 * and 3 bits for DP1.2.
		 */
		uint8_t PATTERN     :3;
		/* BY speci, bit7:2 is 0 for DP1.1. */
		uint8_t RESERVED    :5;
	} bits;
	uint8_t raw;
};

/* States of Compliance Test Specification (CTS DP1.2). */
union compliance_test_state {
	struct {
		unsigned char STEREO_3D_RUNNING        : 1;
		unsigned char SET_TEST_PATTERN_PENDING : 1;
		unsigned char RESERVED                 : 6;
	} bits;
	unsigned char raw;
};

union link_test_pattern {
	struct {
		/* dpcd_link_test_patterns */
		unsigned char PATTERN :2;
		unsigned char RESERVED:6;
	} bits;
	unsigned char raw;
};

union test_misc {
	struct dpcd_test_misc_bits {
		unsigned char SYNC_CLOCK :1;
		/* dpcd_test_color_format */
		unsigned char CLR_FORMAT :2;
		/* dpcd_test_dyn_range */
		unsigned char DYN_RANGE  :1;
		unsigned char YCBCR      :1;
		/* dpcd_test_bit_depth */
		unsigned char BPC        :3;
	} bits;
	unsigned char raw;
};

#endif /* __DAL_DPCD_DEFS_H__ */

/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef DC_DP_TYPES_H
#define DC_DP_TYPES_H

#include "os_types.h"

enum dc_lane_count {
	LANE_COUNT_UNKNOWN = 0,
	LANE_COUNT_ONE = 1,
	LANE_COUNT_TWO = 2,
	LANE_COUNT_FOUR = 4,
	LANE_COUNT_EIGHT = 8,
	LANE_COUNT_DP_MAX = LANE_COUNT_FOUR
};

/* This is actually a reference clock (27MHz) multiplier
 * 162MBps bandwidth for 1.62GHz like rate,
 * 270MBps for 2.70GHz,
 * 324MBps for 3.24Ghz,
 * 540MBps for 5.40GHz
 * 810MBps for 8.10GHz
 */
enum dc_link_rate {
	LINK_RATE_UNKNOWN = 0,
	LINK_RATE_LOW = 0x06,		// Rate_1 (RBR)	- 1.62 Gbps/Lane
	LINK_RATE_RATE_2 = 0x08,	// Rate_2		- 2.16 Gbps/Lane
	LINK_RATE_RATE_3 = 0x09,	// Rate_3		- 2.43 Gbps/Lane
	LINK_RATE_HIGH = 0x0A,		// Rate_4 (HBR)	- 2.70 Gbps/Lane
	LINK_RATE_RBR2 = 0x0C,		// Rate_5 (RBR2)- 3.24 Gbps/Lane
	LINK_RATE_RATE_6 = 0x10,	// Rate_6		- 4.32 Gbps/Lane
	LINK_RATE_HIGH2 = 0x14,		// Rate_7 (HBR2)- 5.40 Gbps/Lane
	LINK_RATE_HIGH3 = 0x1E,		// Rate_8 (HBR3)- 8.10 Gbps/Lane
	/* Starting from DP2.0 link rate enum directly represents actual
	 * link rate value in unit of 10 mbps
	 */
	LINK_RATE_UHBR10 = 1000,	// UHBR10 - 10.0 Gbps/Lane
	LINK_RATE_UHBR13_5 = 1350,	// UHBR13.5 - 13.5 Gbps/Lane
	LINK_RATE_UHBR20 = 2000,	// UHBR10 - 20.0 Gbps/Lane
};

enum dc_link_spread {
	LINK_SPREAD_DISABLED = 0x00,
	/* 0.5 % downspread 30 kHz */
	LINK_SPREAD_05_DOWNSPREAD_30KHZ = 0x10,
	/* 0.5 % downspread 33 kHz */
	LINK_SPREAD_05_DOWNSPREAD_33KHZ = 0x11
};

enum dc_voltage_swing {
	VOLTAGE_SWING_LEVEL0 = 0,	/* direct HW translation! */
	VOLTAGE_SWING_LEVEL1,
	VOLTAGE_SWING_LEVEL2,
	VOLTAGE_SWING_LEVEL3,
	VOLTAGE_SWING_MAX_LEVEL = VOLTAGE_SWING_LEVEL3
};

enum dc_pre_emphasis {
	PRE_EMPHASIS_DISABLED = 0,	/* direct HW translation! */
	PRE_EMPHASIS_LEVEL1,
	PRE_EMPHASIS_LEVEL2,
	PRE_EMPHASIS_LEVEL3,
	PRE_EMPHASIS_MAX_LEVEL = PRE_EMPHASIS_LEVEL3
};
/* Post Cursor 2 is optional for transmitter
 * and it applies only to the main link operating at HBR2
 */
enum dc_post_cursor2 {
	POST_CURSOR2_DISABLED = 0,	/* direct HW translation! */
	POST_CURSOR2_LEVEL1,
	POST_CURSOR2_LEVEL2,
	POST_CURSOR2_LEVEL3,
	POST_CURSOR2_MAX_LEVEL = POST_CURSOR2_LEVEL3,
};

enum dc_dp_ffe_preset_level {
	DP_FFE_PRESET_LEVEL0 = 0,
	DP_FFE_PRESET_LEVEL1,
	DP_FFE_PRESET_LEVEL2,
	DP_FFE_PRESET_LEVEL3,
	DP_FFE_PRESET_LEVEL4,
	DP_FFE_PRESET_LEVEL5,
	DP_FFE_PRESET_LEVEL6,
	DP_FFE_PRESET_LEVEL7,
	DP_FFE_PRESET_LEVEL8,
	DP_FFE_PRESET_LEVEL9,
	DP_FFE_PRESET_LEVEL10,
	DP_FFE_PRESET_LEVEL11,
	DP_FFE_PRESET_LEVEL12,
	DP_FFE_PRESET_LEVEL13,
	DP_FFE_PRESET_LEVEL14,
	DP_FFE_PRESET_LEVEL15,
	DP_FFE_PRESET_MAX_LEVEL = DP_FFE_PRESET_LEVEL15,
};

enum dc_dp_training_pattern {
	DP_TRAINING_PATTERN_SEQUENCE_1 = 0,
	DP_TRAINING_PATTERN_SEQUENCE_2,
	DP_TRAINING_PATTERN_SEQUENCE_3,
	DP_TRAINING_PATTERN_SEQUENCE_4,
	DP_TRAINING_PATTERN_VIDEOIDLE,
	DP_128b_132b_TPS1,
	DP_128b_132b_TPS2,
	DP_128b_132b_TPS2_CDS,
};

enum dp_link_encoding {
	DP_UNKNOWN_ENCODING = 0,
	DP_8b_10b_ENCODING = 1,
	DP_128b_132b_ENCODING = 2,
};

enum dp_test_link_rate {
	DP_TEST_LINK_RATE_RBR		= 0x06,
	DP_TEST_LINK_RATE_HBR		= 0x0A,
	DP_TEST_LINK_RATE_HBR2		= 0x14,
	DP_TEST_LINK_RATE_HBR3		= 0x1E,
	DP_TEST_LINK_RATE_UHBR10	= 0x01,
	DP_TEST_LINK_RATE_UHBR20	= 0x02,
	DP_TEST_LINK_RATE_UHBR13_5	= 0x03,
};

struct dc_link_settings {
	enum dc_lane_count lane_count;
	enum dc_link_rate link_rate;
	enum dc_link_spread link_spread;
	bool use_link_rate_set;
	uint8_t link_rate_set;
};

union dc_dp_ffe_preset {
	struct {
		uint8_t level		: 4;
		uint8_t reserved	: 1;
		uint8_t no_preshoot	: 1;
		uint8_t no_deemphasis	: 1;
		uint8_t method2		: 1;
	} settings;
	uint8_t raw;
};

struct dc_lane_settings {
	enum dc_voltage_swing VOLTAGE_SWING;
	enum dc_pre_emphasis PRE_EMPHASIS;
	enum dc_post_cursor2 POST_CURSOR2;
	union dc_dp_ffe_preset FFE_PRESET;
};

struct dc_link_training_overrides {
	enum dc_voltage_swing *voltage_swing;
	enum dc_pre_emphasis *pre_emphasis;
	enum dc_post_cursor2 *post_cursor2;
	union dc_dp_ffe_preset *ffe_preset;

	uint16_t *cr_pattern_time;
	uint16_t *eq_pattern_time;
	enum dc_dp_training_pattern *pattern_for_cr;
	enum dc_dp_training_pattern *pattern_for_eq;

	enum dc_link_spread *downspread;
	bool *alternate_scrambler_reset;
	bool *enhanced_framing;
	bool *mst_enable;
	bool *fec_enable;
};

union payload_table_update_status {
	struct {
		uint8_t  VC_PAYLOAD_TABLE_UPDATED:1;
		uint8_t  ACT_HANDLED:1;
	} bits;
	uint8_t  raw;
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
		uint8_t EQ_INTERLANE_ALIGN_DONE_128b_132b:1;
		uint8_t CDS_INTERLANE_ALIGN_DONE_128b_132b:1;
		uint8_t LT_FAILED_128b_132b:1;
		uint8_t RESERVED:1;
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
	struct {
		uint8_t PRESET_VALUE	:4;
		uint8_t RESERVED	:4;
	} tx_ffe;
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
	struct {
		uint8_t PRESET_VALUE	:4;
		uint8_t RESERVED	:4;
	} tx_ffe;
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
#if defined(CONFIG_DRM_AMD_DC_DCN)
		uint8_t MAX_ENCODED_LINK_BW_SUPPORT:3;
		uint8_t SOURCE_CONTROL_MODE_SUPPORT:1;
		uint8_t CONCURRENT_LINK_BRING_UP_SEQ_SUPPORT:1;
		uint8_t RESERVED:1;
#else
		uint8_t RESERVED:6;
#endif
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
		uint8_t YCrCr422_PASS_THROUGH:1;
		uint8_t YCrCr420_PASS_THROUGH:1;
		uint8_t YCrCr422_CONVERSION:1;
		uint8_t YCrCr420_CONVERSION:1;
		uint8_t RESERVED:3;
	} bits;
	uint8_t raw;
};

#if defined(CONFIG_DRM_AMD_DC_DCN)
union hdmi_sink_encoded_link_bw_support {
	struct {
		uint8_t HDMI_SINK_ENCODED_LINK_BW_SUPPORT:3;
		uint8_t RESERVED:5;
	} bits;
	uint8_t raw;
};

union hdmi_encoded_link_bw {
	struct {
		uint8_t FRL_MODE:1; // Bit 0
		uint8_t BW_9Gbps:1;
		uint8_t BW_18Gbps:1;
		uint8_t BW_24Gbps:1;
		uint8_t BW_32Gbps:1;
		uint8_t BW_40Gbps:1;
		uint8_t BW_48Gbps:1;
		uint8_t RESERVED:1; // Bit 7
	} bits;
	uint8_t raw;
};
#endif

/*4-byte structure for detailed capabilities of a down-stream port
(DP-to-TMDS converter).*/
union dwnstream_portxcaps {
	struct {
		union dwnstream_port_caps_byte0 byte0;
		unsigned char max_TMDS_clock;   //byte1
		union dwnstream_port_caps_byte2 byte2;

		union {
			union dwnstream_port_caps_byte3_dvi byteDVI;
			union dwnstream_port_caps_byte3_hdmi byteHDMI;
		} byte3;
	} bytes;

	unsigned char raw[4];
};

union downstream_port {
	struct {
		unsigned char   present:1;
		unsigned char   type:2;
		unsigned char   format_conv:1;
		unsigned char   detailed_caps:1;
		unsigned char   reserved:3;
	} bits;
	unsigned char raw;
};


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

struct dpcd_vendor_signature {
	bool is_valid;

	union dpcd_ieee_vendor_signature {
		struct {
			uint8_t ieee_oui[3];/*24-bit IEEE OUI*/
			uint8_t ieee_device_id[6];/*usually 6-byte ASCII name*/
			uint8_t ieee_hw_rev;
			uint8_t ieee_fw_rev[2];
		};
		uint8_t raw[12];
	} data;
};

struct dpcd_amd_signature {
	uint8_t AMD_IEEE_TxSignature_byte1;
	uint8_t AMD_IEEE_TxSignature_byte2;
	uint8_t AMD_IEEE_TxSignature_byte3;
};

struct dpcd_amd_device_id {
	uint8_t device_id_byte1;
	uint8_t device_id_byte2;
	uint8_t zero[4];
	uint8_t dce_version;
	uint8_t dal_version_byte1;
	uint8_t dal_version_byte2;
};

struct dpcd_source_backlight_set {
	struct  {
		uint8_t byte0;
		uint8_t byte1;
		uint8_t byte2;
		uint8_t byte3;
	} backlight_level_millinits;

	struct  {
		uint8_t byte0;
		uint8_t byte1;
	} backlight_transition_time_ms;
};

union dpcd_source_backlight_get {
	struct {
		uint32_t backlight_millinits_peak; /* 326h */
		uint32_t backlight_millinits_avg; /* 32Ah */
	} bytes;
	uint8_t raw[8];
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

union dprx_feature {
	struct {
		uint8_t GTC_CAP:1;                             // bit 0: DP 1.3+
		uint8_t SST_SPLIT_SDP_CAP:1;                   // bit 1: DP 1.4
		uint8_t AV_SYNC_CAP:1;                         // bit 2: DP 1.3+
		uint8_t VSC_SDP_COLORIMETRY_SUPPORTED:1;       // bit 3: DP 1.3+
		uint8_t VSC_EXT_VESA_SDP_SUPPORTED:1;          // bit 4: DP 1.4
		uint8_t VSC_EXT_VESA_SDP_CHAINING_SUPPORTED:1; // bit 5: DP 1.4
		uint8_t VSC_EXT_CEA_SDP_SUPPORTED:1;           // bit 6: DP 1.4
		uint8_t VSC_EXT_CEA_SDP_CHAINING_SUPPORTED:1;  // bit 7: DP 1.4
	} bits;
	uint8_t raw;
};

union training_aux_rd_interval {
	struct {
		uint8_t TRAINIG_AUX_RD_INTERVAL:7;
		uint8_t EXT_RECEIVER_CAP_FIELD_PRESENT:1;
	} bits;
	uint8_t raw;
};

/* Automated test structures */
union test_request {
	struct {
	uint8_t LINK_TRAINING                :1;
	uint8_t LINK_TEST_PATTRN             :1;
	uint8_t EDID_READ                    :1;
	uint8_t PHY_TEST_PATTERN             :1;
	uint8_t PHY_TEST_CHANNEL_CODING_TYPE :2;
	uint8_t AUDIO_TEST_PATTERN           :1;
	uint8_t TEST_AUDIO_DISABLED_VIDEO    :1;
	} bits;
	uint8_t raw;
};

union test_response {
	struct {
		uint8_t ACK         :1;
		uint8_t NO_ACK      :1;
		uint8_t EDID_CHECKSUM_WRITE:1;
		uint8_t RESERVED    :5;
	} bits;
	uint8_t raw;
};

union phy_test_pattern {
	struct {
		/* This field is 7 bits for DP2.0 */
		uint8_t PATTERN     :7;
		uint8_t RESERVED    :1;
	} bits;
	uint8_t raw;
};

/* States of Compliance Test Specification (CTS DP1.2). */
union compliance_test_state {
	struct {
		unsigned char STEREO_3D_RUNNING        : 1;
		unsigned char RESERVED                 : 7;
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
		unsigned char SYNC_CLOCK  :1;
		/* dpcd_test_color_format */
		unsigned char CLR_FORMAT  :2;
		/* dpcd_test_dyn_range */
		unsigned char DYN_RANGE   :1;
		unsigned char YCBCR_COEFS :1;
		/* dpcd_test_bit_depth */
		unsigned char BPC         :3;
	} bits;
	unsigned char raw;
};

union audio_test_mode {
	struct {
		unsigned char sampling_rate   :4;
		unsigned char channel_count   :4;
	} bits;
	unsigned char raw;
};

union audio_test_pattern_period {
	struct {
		unsigned char pattern_period   :4;
		unsigned char reserved         :4;
	} bits;
	unsigned char raw;
};

struct audio_test_pattern_type {
	unsigned char value;
};

struct dp_audio_test_data_flags {
	uint8_t test_requested  :1;
	uint8_t disable_video   :1;
};

struct dp_audio_test_data {

	struct dp_audio_test_data_flags flags;
	uint8_t sampling_rate;
	uint8_t channel_count;
	uint8_t pattern_type;
	uint8_t pattern_period[8];
};

/* FEC capability DPCD register field bits-*/
union dpcd_fec_capability {
	struct {
		uint8_t FEC_CAPABLE:1;
		uint8_t UNCORRECTED_BLOCK_ERROR_COUNT_CAPABLE:1;
		uint8_t CORRECTED_BLOCK_ERROR_COUNT_CAPABLE:1;
		uint8_t BIT_ERROR_COUNT_CAPABLE:1;
		uint8_t PARITY_BLOCK_ERROR_COUNT_CAPABLE:1;
		uint8_t ARITY_BIT_ERROR_COUNT_CAPABLE:1;
		uint8_t FEC_RUNNING_INDICATOR_SUPPORTED:1;
		uint8_t FEC_ERROR_REPORTING_POLICY_SUPPORTED:1;
	} bits;
	uint8_t raw;
};

/* DSC capability DPCD register field bits-*/
struct dpcd_dsc_support {
	uint8_t DSC_SUPPORT		:1;
	uint8_t DSC_PASSTHROUGH_SUPPORT	:1;
	uint8_t RESERVED		:6;
};

struct dpcd_dsc_algorithm_revision {
	uint8_t DSC_VERSION_MAJOR	:4;
	uint8_t DSC_VERSION_MINOR	:4;
};

struct dpcd_dsc_rc_buffer_block_size {
	uint8_t RC_BLOCK_BUFFER_SIZE	:2;
	uint8_t RESERVED		:6;
};

struct dpcd_dsc_slice_capability1 {
	uint8_t ONE_SLICE_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t TWO_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t RESERVED				:1;
	uint8_t FOUR_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t SIX_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t EIGHT_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t TEN_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
	uint8_t TWELVE_SLICES_PER_DP_DSC_SINK_DEVICE	:1;
};

struct dpcd_dsc_line_buffer_bit_depth {
	uint8_t LINE_BUFFER_BIT_DEPTH	:4;
	uint8_t RESERVED		:4;
};

struct dpcd_dsc_block_prediction_support {
	uint8_t BLOCK_PREDICTION_SUPPORT:1;
	uint8_t RESERVED		:7;
};

struct dpcd_maximum_bits_per_pixel_supported_by_the_decompressor {
	uint8_t MAXIMUM_BITS_PER_PIXEL_SUPPORTED_BY_THE_DECOMPRESSOR_LOW	:7;
	uint8_t MAXIMUM_BITS_PER_PIXEL_SUPPORTED_BY_THE_DECOMPRESSOR_HIGH	:7;
	uint8_t RESERVED							:2;
};

struct dpcd_dsc_decoder_color_format_capabilities {
	uint8_t RGB_SUPPORT			:1;
	uint8_t Y_CB_CR_444_SUPPORT		:1;
	uint8_t Y_CB_CR_SIMPLE_422_SUPPORT	:1;
	uint8_t Y_CB_CR_NATIVE_422_SUPPORT	:1;
	uint8_t Y_CB_CR_NATIVE_420_SUPPORT	:1;
	uint8_t RESERVED			:3;
};

struct dpcd_dsc_decoder_color_depth_capabilities {
	uint8_t RESERVED0			:1;
	uint8_t EIGHT_BITS_PER_COLOR_SUPPORT	:1;
	uint8_t TEN_BITS_PER_COLOR_SUPPORT	:1;
	uint8_t TWELVE_BITS_PER_COLOR_SUPPORT	:1;
	uint8_t RESERVED1			:4;
};

struct dpcd_peak_dsc_throughput_dsc_sink {
	uint8_t THROUGHPUT_MODE_0:4;
	uint8_t THROUGHPUT_MODE_1:4;
};

struct dpcd_dsc_slice_capabilities_2 {
	uint8_t SIXTEEN_SLICES_PER_DSC_SINK_DEVICE	:1;
	uint8_t TWENTY_SLICES_PER_DSC_SINK_DEVICE	:1;
	uint8_t TWENTYFOUR_SLICES_PER_DSC_SINK_DEVICE	:1;
	uint8_t RESERVED				:5;
};

struct dpcd_bits_per_pixel_increment{
	uint8_t INCREMENT_OF_BITS_PER_PIXEL_SUPPORTED	:3;
	uint8_t RESERVED				:5;
};
union dpcd_dsc_basic_capabilities {
	struct {
		struct dpcd_dsc_support dsc_support;
		struct dpcd_dsc_algorithm_revision dsc_algorithm_revision;
		struct dpcd_dsc_rc_buffer_block_size dsc_rc_buffer_block_size;
		uint8_t dsc_rc_buffer_size;
		struct dpcd_dsc_slice_capability1 dsc_slice_capabilities_1;
		struct dpcd_dsc_line_buffer_bit_depth dsc_line_buffer_bit_depth;
		struct dpcd_dsc_block_prediction_support dsc_block_prediction_support;
		struct dpcd_maximum_bits_per_pixel_supported_by_the_decompressor maximum_bits_per_pixel_supported_by_the_decompressor;
		struct dpcd_dsc_decoder_color_format_capabilities dsc_decoder_color_format_capabilities;
		struct dpcd_dsc_decoder_color_depth_capabilities dsc_decoder_color_depth_capabilities;
		struct dpcd_peak_dsc_throughput_dsc_sink peak_dsc_throughput_dsc_sink;
		uint8_t dsc_maximum_slice_width;
		struct dpcd_dsc_slice_capabilities_2 dsc_slice_capabilities_2;
		uint8_t reserved;
		struct dpcd_bits_per_pixel_increment bits_per_pixel_increment;
	} fields;
	uint8_t raw[16];
};

union dpcd_dsc_branch_decoder_capabilities {
	struct {
		uint8_t BRANCH_OVERALL_THROUGHPUT_0;
		uint8_t BRANCH_OVERALL_THROUGHPUT_1;
		uint8_t BRANCH_MAX_LINE_WIDTH;
	} fields;
	uint8_t raw[3];
};

struct dpcd_dsc_capabilities {
	union dpcd_dsc_basic_capabilities dsc_basic_caps;
	union dpcd_dsc_branch_decoder_capabilities dsc_branch_decoder_caps;
};

/* These parameters are from PSR capabilities reported by Sink DPCD */
struct psr_caps {
	unsigned char psr_version;
	unsigned int psr_rfb_setup_time;
	bool psr_exit_link_training_required;
	unsigned char edp_revision;
	unsigned char support_ver;
	bool su_granularity_required;
	bool y_coordinate_required;
	uint8_t su_y_granularity;
	bool alpm_cap;
	bool standby_support;
	uint8_t rate_control_caps;
	unsigned int psr_power_opt_flag;
};

/* Length of router topology ID read from DPCD in bytes. */
#define DPCD_USB4_TOPOLOGY_ID_LEN 5

/* DPCD[0xE000D] DP_TUNNELING_CAPABILITIES SUPPORT register. */
union dp_tun_cap_support {
	struct {
		uint8_t dp_tunneling :1;
		uint8_t rsvd :5;
		uint8_t panel_replay_tun_opt :1;
		uint8_t dpia_bw_alloc :1;
	} bits;
	uint8_t raw;
};

/* DPCD[0xE000E] DP_IN_ADAPTER_INFO register. */
union dpia_info {
	struct {
		uint8_t dpia_num :5;
		uint8_t rsvd :3;
	} bits;
	uint8_t raw;
};

/* DP Tunneling over USB4 */
struct dpcd_usb4_dp_tunneling_info {
	union dp_tun_cap_support dp_tun_cap;
	union dpia_info dpia_info;
	uint8_t usb4_driver_id;
	uint8_t usb4_topology_id[DPCD_USB4_TOPOLOGY_ID_LEN];
};

#ifndef DP_MAIN_LINK_CHANNEL_CODING_CAP
#define DP_MAIN_LINK_CHANNEL_CODING_CAP			0x006
#endif
#ifndef DP_SINK_VIDEO_FALLBACK_FORMATS
#define DP_SINK_VIDEO_FALLBACK_FORMATS			0x020
#endif
#ifndef DP_FEC_CAPABILITY_1
#define DP_FEC_CAPABILITY_1				0x091
#endif
#ifndef DP_DFP_CAPABILITY_EXTENSION_SUPPORT
#define DP_DFP_CAPABILITY_EXTENSION_SUPPORT		0x0A3
#endif
#ifndef DP_LINK_SQUARE_PATTERN
#define DP_LINK_SQUARE_PATTERN				0x10F
#endif
#ifndef DP_CABLE_ATTRIBUTES_UPDATED_BY_DPTX
#define DP_CABLE_ATTRIBUTES_UPDATED_BY_DPTX		0x110
#endif
#ifndef DP_DSC_CONFIGURATION
#define DP_DSC_CONFIGURATION				0x161
#endif
#ifndef DP_PHY_SQUARE_PATTERN
#define DP_PHY_SQUARE_PATTERN				0x249
#endif
#ifndef DP_128b_132b_SUPPORTED_LINK_RATES
#define DP_128b_132b_SUPPORTED_LINK_RATES		0x2215
#endif
#ifndef DP_128b_132b_TRAINING_AUX_RD_INTERVAL
#define DP_128b_132b_TRAINING_AUX_RD_INTERVAL		0x2216
#endif
#ifndef DP_CABLE_ATTRIBUTES_UPDATED_BY_DPRX
#define DP_CABLE_ATTRIBUTES_UPDATED_BY_DPRX		0x2217
#endif
#ifndef DP_TEST_264BIT_CUSTOM_PATTERN_7_0
#define DP_TEST_264BIT_CUSTOM_PATTERN_7_0		0X2230
#endif
#ifndef DP_TEST_264BIT_CUSTOM_PATTERN_263_256
#define DP_TEST_264BIT_CUSTOM_PATTERN_263_256		0X2250
#endif
#ifndef DP_DSC_SUPPORT_AND_DECODER_COUNT
#define DP_DSC_SUPPORT_AND_DECODER_COUNT		0x2260
#endif
#ifndef DP_DSC_MAX_SLICE_COUNT_AND_AGGREGATION_0
#define DP_DSC_MAX_SLICE_COUNT_AND_AGGREGATION_0	0x2270
#endif
#ifndef DP_DSC_DECODER_0_MAXIMUM_SLICE_COUNT_MASK
#define DP_DSC_DECODER_0_MAXIMUM_SLICE_COUNT_MASK	(1 << 0)
#endif
#ifndef DP_DSC_DECODER_0_AGGREGATION_SUPPORT_MASK
#define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_MASK	(0b111 << 1)
#endif
#ifndef DP_DSC_DECODER_0_AGGREGATION_SUPPORT_SHIFT
#define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_SHIFT	1
#endif
#ifndef DP_DSC_DECODER_COUNT_MASK
#define DP_DSC_DECODER_COUNT_MASK			(0b111 << 5)
#endif
#ifndef DP_DSC_DECODER_COUNT_SHIFT
#define DP_DSC_DECODER_COUNT_SHIFT			5
#endif
#ifndef DP_MAIN_LINK_CHANNEL_CODING_SET
#define DP_MAIN_LINK_CHANNEL_CODING_SET			0x108
#endif
#ifndef DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER
#define DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER	0xF0006
#endif
#ifndef DP_PHY_REPEATER_128b_132b_RATES
#define DP_PHY_REPEATER_128b_132b_RATES			0xF0007
#endif
#ifndef DP_128b_132b_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1
#define DP_128b_132b_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1	0xF0022
#endif
#ifndef DP_INTRA_HOP_AUX_REPLY_INDICATION
#define DP_INTRA_HOP_AUX_REPLY_INDICATION		(1 << 3)
/* TODO - Use DRM header to replace above once available */
#endif // DP_INTRA_HOP_AUX_REPLY_INDICATION

union dp_main_line_channel_coding_cap {
	struct {
		uint8_t DP_8b_10b_SUPPORTED	:1;
		uint8_t DP_128b_132b_SUPPORTED	:1;
		uint8_t RESERVED		:6;
	} bits;
	uint8_t raw;
};

union dp_main_link_channel_coding_lttpr_cap {
	struct {
		uint8_t DP_128b_132b_SUPPORTED	:1;
		uint8_t RESERVED		:7;
	} bits;
	uint8_t raw;
};

union dp_128b_132b_supported_link_rates {
	struct {
		uint8_t UHBR10	:1;
		uint8_t UHBR20	:1;
		uint8_t UHBR13_5:1;
		uint8_t RESERVED:5;
	} bits;
	uint8_t raw;
};

union dp_128b_132b_supported_lttpr_link_rates {
	struct {
		uint8_t UHBR10	:1;
		uint8_t UHBR20	:1;
		uint8_t UHBR13_5:1;
		uint8_t RESERVED:5;
	} bits;
	uint8_t raw;
};

union dp_sink_video_fallback_formats {
	struct {
		uint8_t dp_1024x768_60Hz_24bpp_support	:1;
		uint8_t dp_1280x720_60Hz_24bpp_support	:1;
		uint8_t dp_1920x1080_60Hz_24bpp_support	:1;
		uint8_t RESERVED			:5;
	} bits;
	uint8_t raw;
};

union dp_fec_capability1 {
	struct {
		uint8_t AGGREGATED_ERROR_COUNTERS_CAPABLE	:1;
		uint8_t RESERVED				:7;
	} bits;
	uint8_t raw;
};

union dp_cable_id {
	struct {
		uint8_t UHBR10_20_CAPABILITY	:2;
		uint8_t UHBR13_5_CAPABILITY	:1;
		uint8_t CABLE_TYPE		:3;
		uint8_t RESERVED		:2;
	} bits;
	uint8_t raw;
};

struct dp_color_depth_caps {
	uint8_t support_6bpc	:1;
	uint8_t support_8bpc	:1;
	uint8_t support_10bpc	:1;
	uint8_t support_12bpc	:1;
	uint8_t support_16bpc	:1;
	uint8_t RESERVED	:3;
};

struct dp_encoding_format_caps {
	uint8_t support_rgb	:1;
	uint8_t support_ycbcr444:1;
	uint8_t support_ycbcr422:1;
	uint8_t support_ycbcr420:1;
	uint8_t RESERVED	:4;
};

union dp_dfp_cap_ext {
	struct {
		uint8_t supported;
		uint8_t max_pixel_rate_in_mps[2];
		uint8_t max_video_h_active_width[2];
		uint8_t max_video_v_active_height[2];
		struct dp_encoding_format_caps encoding_format_caps;
		struct dp_color_depth_caps rgb_color_depth_caps;
		struct dp_color_depth_caps ycbcr444_color_depth_caps;
		struct dp_color_depth_caps ycbcr422_color_depth_caps;
		struct dp_color_depth_caps ycbcr420_color_depth_caps;
	} fields;
	uint8_t raw[12];
};

union dp_128b_132b_training_aux_rd_interval {
	struct {
		uint8_t VALUE	:7;
		uint8_t UNIT	:1;
	} bits;
	uint8_t raw;
};

union edp_alpm_caps {
	struct {
		uint8_t AUX_WAKE_ALPM_CAP       :1;
		uint8_t PM_STATE_2A_SUPPORT     :1;
		uint8_t AUX_LESS_ALPM_CAP       :1;
		uint8_t RESERVED                :5;
	} bits;
	uint8_t raw;
};

union edp_psr_dpcd_caps {
	struct {
		uint8_t LINK_TRAINING_ON_EXIT_NOT_REQUIRED      :1;
		uint8_t PSR_SETUP_TIME  :3;
		uint8_t Y_COORDINATE_REQUIRED   :1;
		uint8_t SU_GRANULARITY_REQUIRED :1;
		uint8_t FRAME_SYNC_IS_NOT_NEEDED_FOR_SU :1;
		uint8_t RESERVED                :1;
	} bits;
	uint8_t raw;
};

struct edp_psr_info {
	uint8_t psr_version;
	union edp_psr_dpcd_caps psr_dpcd_caps;
	uint8_t psr2_su_y_granularity_cap;
	uint8_t force_psrsu_cap;
};

#endif /* DC_DP_TYPES_H */

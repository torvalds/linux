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

#ifndef __DAL_BIOS_PARSER_TYPES_H__

#define __DAL_BIOS_PARSER_TYPES_H__

#include "dm_services.h"
#include "include/signal_types.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/gpio_types.h"
#include "include/link_service_types.h"

/* TODO: include signal_types.h and remove this enum */
enum as_signal_type {
	AS_SIGNAL_TYPE_NONE = 0L, /* no signal */
	AS_SIGNAL_TYPE_DVI,
	AS_SIGNAL_TYPE_HDMI,
	AS_SIGNAL_TYPE_LVDS,
	AS_SIGNAL_TYPE_DISPLAY_PORT,
	AS_SIGNAL_TYPE_GPU_PLL,
	AS_SIGNAL_TYPE_XGMI,
	AS_SIGNAL_TYPE_UNKNOWN
};

enum bp_result {
	BP_RESULT_OK = 0, /* There was no error */
	BP_RESULT_BADINPUT, /*Bad input parameter */
	BP_RESULT_BADBIOSTABLE, /* Bad BIOS table */
	BP_RESULT_UNSUPPORTED, /* BIOS Table is not supported */
	BP_RESULT_NORECORD, /* Record can't be found */
	BP_RESULT_FAILURE
};

enum bp_encoder_control_action {
	/* direct VBIOS translation! Just to simplify the translation */
	ENCODER_CONTROL_DISABLE = 0,
	ENCODER_CONTROL_ENABLE,
	ENCODER_CONTROL_SETUP,
	ENCODER_CONTROL_INIT
};

enum bp_transmitter_control_action {
	/* direct VBIOS translation! Just to simplify the translation */
	TRANSMITTER_CONTROL_DISABLE = 0,
	TRANSMITTER_CONTROL_ENABLE,
	TRANSMITTER_CONTROL_BACKLIGHT_OFF,
	TRANSMITTER_CONTROL_BACKLIGHT_ON,
	TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS,
	TRANSMITTER_CONTROL_LCD_SETF_TEST_START,
	TRANSMITTER_CONTROL_LCD_SELF_TEST_STOP,
	TRANSMITTER_CONTROL_INIT,
	TRANSMITTER_CONTROL_DEACTIVATE,
	TRANSMITTER_CONTROL_ACTIAVATE,
	TRANSMITTER_CONTROL_SETUP,
	TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS,
	/* ATOM_TRANSMITTER_ACTION_POWER_ON. This action is for eDP only
	 * (power up the panel)
	 */
	TRANSMITTER_CONTROL_POWER_ON,
	/* ATOM_TRANSMITTER_ACTION_POWER_OFF. This action is for eDP only
	 * (power down the panel)
	 */
	TRANSMITTER_CONTROL_POWER_OFF
};

enum bp_external_encoder_control_action {
	EXTERNAL_ENCODER_CONTROL_DISABLE = 0,
	EXTERNAL_ENCODER_CONTROL_ENABLE = 1,
	EXTERNAL_ENCODER_CONTROL_INIT = 0x7,
	EXTERNAL_ENCODER_CONTROL_SETUP = 0xf,
	EXTERNAL_ENCODER_CONTROL_UNBLANK = 0x10,
	EXTERNAL_ENCODER_CONTROL_BLANK = 0x11,
};

enum bp_pipe_control_action {
	ASIC_PIPE_DISABLE = 0,
	ASIC_PIPE_ENABLE,
	ASIC_PIPE_INIT
};

struct bp_encoder_control {
	enum bp_encoder_control_action action;
	enum engine_id engine_id;
	enum transmitter transmitter;
	enum signal_type signal;
	enum dc_lane_count lanes_number;
	enum dc_color_depth color_depth;
	bool enable_dp_audio;
	uint32_t pixel_clock; /* khz */
};

struct bp_external_encoder_control {
	enum bp_external_encoder_control_action action;
	enum engine_id engine_id;
	enum dc_link_rate link_rate;
	enum dc_lane_count lanes_number;
	enum signal_type signal;
	enum dc_color_depth color_depth;
	bool coherent;
	struct graphics_object_id encoder_id;
	struct graphics_object_id connector_obj_id;
	uint32_t pixel_clock; /* in KHz */
};

struct bp_crtc_source_select {
	enum engine_id engine_id;
	enum controller_id controller_id;
	/* from GPU Tx aka asic_signal */
	enum signal_type signal;
	/* sink_signal may differ from asicSignal if Translator encoder */
	enum signal_type sink_signal;
	enum display_output_bit_depth display_output_bit_depth;
	bool enable_dp_audio;
};

struct bp_transmitter_control {
	enum bp_transmitter_control_action action;
	enum engine_id engine_id;
	enum transmitter transmitter; /* PhyId */
	enum dc_lane_count lanes_number;
	enum clock_source_id pll_id; /* needed for DCE 4.0 */
	enum signal_type signal;
	enum dc_color_depth color_depth; /* not used for DCE6.0 */
	enum hpd_source_id hpd_sel; /* ucHPDSel, used for DCe6.0 */
	struct graphics_object_id connector_obj_id;
	/* symClock; in 10kHz, pixel clock, in HDMI deep color mode, it should
	 * be pixel clock * deep_color_ratio (in KHz)
	 */
	uint32_t pixel_clock;
	uint32_t lane_select;
	uint32_t lane_settings;
	bool coherent;
	bool multi_path;
	bool single_pll_mode;
};

struct bp_hw_crtc_timing_parameters {
	enum controller_id controller_id;
	/* horizontal part */
	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t h_overscan_left;
	uint32_t h_overscan_right;
	uint32_t h_sync_start;
	uint32_t h_sync_width;

	/* vertical part */
	uint32_t v_total;
	uint32_t v_addressable;
	uint32_t v_overscan_top;
	uint32_t v_overscan_bottom;
	uint32_t v_sync_start;
	uint32_t v_sync_width;

	struct timing_flags {
		uint32_t INTERLACE:1;
		uint32_t PIXEL_REPETITION:4;
		uint32_t HSYNC_POSITIVE_POLARITY:1;
		uint32_t VSYNC_POSITIVE_POLARITY:1;
		uint32_t HORZ_COUNT_BY_TWO:1;
	} flags;
};

struct bp_adjust_pixel_clock_parameters {
	/* Input: Signal Type - to be converted to Encoder mode */
	enum signal_type signal_type;
	/* Input: Encoder object id */
	struct graphics_object_id encoder_object_id;
	/* Input: Pixel Clock (requested Pixel clock based on Video timing
	 * standard used) in KHz
	 */
	uint32_t pixel_clock;
	/* Output: Adjusted Pixel Clock (after VBIOS exec table) in KHz */
	uint32_t adjusted_pixel_clock;
	/* Output: If non-zero, this refDiv value should be used to calculate
	 * other ppll params */
	uint32_t reference_divider;
	/* Output: If non-zero, this postDiv value should be used to calculate
	 * other ppll params */
	uint32_t pixel_clock_post_divider;
	/* Input: Enable spread spectrum */
	bool ss_enable;
};

struct bp_pixel_clock_parameters {
	enum controller_id controller_id; /* (Which CRTC uses this PLL) */
	enum clock_source_id pll_id; /* Clock Source Id */
	/* signal_type -> Encoder Mode - needed by VBIOS Exec table */
	enum signal_type signal_type;
	/* Adjusted Pixel Clock (after VBIOS exec table)
	 * that becomes Target Pixel Clock (100 Hz units) */
	uint32_t target_pixel_clock_100hz;
	/* Calculated Reference divider of Display PLL */
	uint32_t reference_divider;
	/* Calculated Feedback divider of Display PLL */
	uint32_t feedback_divider;
	/* Calculated Fractional Feedback divider of Display PLL */
	uint32_t fractional_feedback_divider;
	/* Calculated Pixel Clock Post divider of Display PLL */
	uint32_t pixel_clock_post_divider;
	struct graphics_object_id encoder_object_id; /* Encoder object id */
	/* VBIOS returns a fixed display clock when DFS-bypass feature
	 * is enabled (KHz) */
	uint32_t dfs_bypass_display_clock;
	/* color depth to support HDMI deep color */
	enum transmitter_color_depth color_depth;

	struct program_pixel_clock_flags {
		uint32_t FORCE_PROGRAMMING_OF_PLL:1;
		/* Use Engine Clock as source for Display Clock when
		 * programming PLL */
		uint32_t USE_E_CLOCK_AS_SOURCE_FOR_D_CLOCK:1;
		/* Use external reference clock (refDivSrc for PLL) */
		uint32_t SET_EXTERNAL_REF_DIV_SRC:1;
		/* Use DFS bypass for Display clock. */
		uint32_t SET_DISPCLK_DFS_BYPASS:1;
		/* Force program PHY PLL only */
		uint32_t PROGRAM_PHY_PLL_ONLY:1;
		/* Support for YUV420 */
		uint32_t SUPPORT_YUV_420:1;
		/* Use XTALIN reference clock source */
		uint32_t SET_XTALIN_REF_SRC:1;
		/* Use GENLK reference clock source */
		uint32_t SET_GENLOCK_REF_DIV_SRC:1;
	} flags;
};

enum bp_dce_clock_type {
	DCECLOCK_TYPE_DISPLAY_CLOCK = 0,
	DCECLOCK_TYPE_DPREFCLK      = 1
};

/* DCE Clock Parameters structure for SetDceClock Exec command table */
struct bp_set_dce_clock_parameters {
	enum clock_source_id pll_id; /* Clock Source Id */
	/* Display clock or DPREFCLK value */
	uint32_t target_clock_frequency;
	/* Clock to set: =0: DISPCLK  =1: DPREFCLK  =2: PIXCLK */
	enum bp_dce_clock_type clock_type;

	struct set_dce_clock_flags {
		uint32_t USE_GENERICA_AS_SOURCE_FOR_DPREFCLK:1;
		/* Use XTALIN reference clock source */
		uint32_t USE_XTALIN_AS_SOURCE_FOR_DPREFCLK:1;
		/* Use PCIE reference clock source */
		uint32_t USE_PCIE_AS_SOURCE_FOR_DPREFCLK:1;
		/* Use GENLK reference clock source */
		uint32_t USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK:1;
	} flags;
};

struct spread_spectrum_flags {
	/* 1 = Center Spread; 0 = down spread */
	uint32_t CENTER_SPREAD:1;
	/* 1 = external; 0 = internal */
	uint32_t EXTERNAL_SS:1;
	/* 1 = delta-sigma type parameter; 0 = ver1 */
	uint32_t DS_TYPE:1;
};

struct bp_spread_spectrum_parameters {
	enum clock_source_id pll_id;
	uint32_t percentage;
	uint32_t ds_frac_amount;

	union {
		struct {
			uint32_t step;
			uint32_t delay;
			uint32_t range; /* In Hz unit */
		} ver1;
		struct {
			uint32_t feedback_amount;
			uint32_t nfrac_amount;
			uint32_t ds_frac_size;
		} ds;
	};

	struct spread_spectrum_flags flags;
};

struct bp_encoder_cap_info {
	uint32_t DP_HBR2_CAP:1;
	uint32_t DP_HBR2_EN:1;
	uint32_t DP_HBR3_EN:1;
	uint32_t HDMI_6GB_EN:1;
	uint32_t DP_IS_USB_C:1;
	uint32_t RESERVED:27;
};

#endif /*__DAL_BIOS_PARSER_TYPES_H__ */

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

#include "dm_services.h"

#include "ObjectID.h"
#include "atomfirmware.h"

#include "include/bios_parser_types.h"

#include "command_table_helper2.h"

bool dal_bios_parser_init_cmd_tbl_helper2(
	const struct command_table_helper **h,
	enum dce_version dce)
{
	switch (dce) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case DCE_VERSION_6_0:
	case DCE_VERSION_6_1:
	case DCE_VERSION_6_4:
		*h = dal_cmd_tbl_helper_dce60_get_table();
		return true;
#endif

	case DCE_VERSION_8_0:
	case DCE_VERSION_8_1:
	case DCE_VERSION_8_3:
		*h = dal_cmd_tbl_helper_dce80_get_table();
		return true;

	case DCE_VERSION_10_0:
		*h = dal_cmd_tbl_helper_dce110_get_table();
		return true;

	case DCE_VERSION_11_0:
		*h = dal_cmd_tbl_helper_dce110_get_table();
		return true;

	case DCE_VERSION_11_2:
	case DCE_VERSION_11_22:
	case DCE_VERSION_12_0:
	case DCE_VERSION_12_1:
		*h = dal_cmd_tbl_helper_dce112_get_table2();
		return true;
	case DCN_VERSION_1_0:
	case DCN_VERSION_1_01:
	case DCN_VERSION_2_0:
	case DCN_VERSION_2_1:
	case DCN_VERSION_2_01:
	case DCN_VERSION_3_0:
	case DCN_VERSION_3_01:
	case DCN_VERSION_3_02:
	case DCN_VERSION_3_03:
	case DCN_VERSION_3_1:
	case DCN_VERSION_3_14:
	case DCN_VERSION_3_15:
	case DCN_VERSION_3_16:
	case DCN_VERSION_3_2:
	case DCN_VERSION_3_21:
	case DCN_VERSION_3_5:
		*h = dal_cmd_tbl_helper_dce112_get_table2();
		return true;

	default:
		/* Unsupported DCE */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

/* real implementations */

bool dal_cmd_table_helper_controller_id_to_atom2(
	enum controller_id id,
	uint8_t *atom_id)
{
	if (atom_id == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	switch (id) {
	case CONTROLLER_ID_D0:
		*atom_id = ATOM_CRTC1;
		return true;
	case CONTROLLER_ID_D1:
		*atom_id = ATOM_CRTC2;
		return true;
	case CONTROLLER_ID_D2:
		*atom_id = ATOM_CRTC3;
		return true;
	case CONTROLLER_ID_D3:
		*atom_id = ATOM_CRTC4;
		return true;
	case CONTROLLER_ID_D4:
		*atom_id = ATOM_CRTC5;
		return true;
	case CONTROLLER_ID_D5:
		*atom_id = ATOM_CRTC6;
		return true;
	/* TODO :case CONTROLLER_ID_UNDERLAY0:
		*atom_id = ATOM_UNDERLAY_PIPE0;
		return true;
	*/
	case CONTROLLER_ID_UNDEFINED:
		*atom_id = ATOM_CRTC_INVALID;
		return true;
	default:
		/* Wrong controller id */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

/**
 * dal_cmd_table_helper_transmitter_bp_to_atom2 - Translate the Transmitter to the
 *                                     corresponding ATOM BIOS value
 *  @t: transmitter
 *  returns: digitalTransmitter
 *    // =00: Digital Transmitter1 ( UNIPHY linkAB )
 *    // =01: Digital Transmitter2 ( UNIPHY linkCD )
 *    // =02: Digital Transmitter3 ( UNIPHY linkEF )
 */
uint8_t dal_cmd_table_helper_transmitter_bp_to_atom2(
	enum transmitter t)
{
	switch (t) {
	case TRANSMITTER_UNIPHY_A:
	case TRANSMITTER_UNIPHY_B:
	case TRANSMITTER_TRAVIS_LCD:
		return 0;
	case TRANSMITTER_UNIPHY_C:
	case TRANSMITTER_UNIPHY_D:
		return 1;
	case TRANSMITTER_UNIPHY_E:
	case TRANSMITTER_UNIPHY_F:
		return 2;
	default:
		/* Invalid Transmitter Type! */
		BREAK_TO_DEBUGGER();
		return 0;
	}
}

uint32_t dal_cmd_table_helper_encoder_mode_bp_to_atom2(
	enum signal_type s,
	bool enable_dp_audio)
{
	switch (s) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		return ATOM_ENCODER_MODE_DVI;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return ATOM_ENCODER_MODE_HDMI;
	case SIGNAL_TYPE_LVDS:
		return ATOM_ENCODER_MODE_LVDS;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_VIRTUAL:
		if (enable_dp_audio)
			return ATOM_ENCODER_MODE_DP_AUDIO;
		else
			return ATOM_ENCODER_MODE_DP;
	case SIGNAL_TYPE_RGB:
		return ATOM_ENCODER_MODE_CRT;
	default:
		return ATOM_ENCODER_MODE_CRT;
	}
}

bool dal_cmd_table_helper_clock_source_id_to_ref_clk_src2(
	enum clock_source_id id,
	uint32_t *ref_clk_src_id)
{
	if (ref_clk_src_id == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	switch (id) {
	case CLOCK_SOURCE_ID_PLL1:
		*ref_clk_src_id = ENCODER_REFCLK_SRC_P1PLL;
		return true;
	case CLOCK_SOURCE_ID_PLL2:
		*ref_clk_src_id = ENCODER_REFCLK_SRC_P2PLL;
		return true;
	/*TODO:case CLOCK_SOURCE_ID_DCPLL:
		*ref_clk_src_id = ENCODER_REFCLK_SRC_DCPLL;
		return true;
	*/
	case CLOCK_SOURCE_ID_EXTERNAL:
		*ref_clk_src_id = ENCODER_REFCLK_SRC_EXTCLK;
		return true;
	case CLOCK_SOURCE_ID_UNDEFINED:
		*ref_clk_src_id = ENCODER_REFCLK_SRC_INVALID;
		return true;
	default:
		/* Unsupported clock source id */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

uint8_t dal_cmd_table_helper_encoder_id_to_atom2(
	enum encoder_id id)
{
	switch (id) {
	case ENCODER_ID_INTERNAL_LVDS:
		return ENCODER_OBJECT_ID_INTERNAL_LVDS;
	case ENCODER_ID_INTERNAL_TMDS1:
		return ENCODER_OBJECT_ID_INTERNAL_TMDS1;
	case ENCODER_ID_INTERNAL_TMDS2:
		return ENCODER_OBJECT_ID_INTERNAL_TMDS2;
	case ENCODER_ID_INTERNAL_DAC1:
		return ENCODER_OBJECT_ID_INTERNAL_DAC1;
	case ENCODER_ID_INTERNAL_DAC2:
		return ENCODER_OBJECT_ID_INTERNAL_DAC2;
	case ENCODER_ID_INTERNAL_LVTM1:
		return ENCODER_OBJECT_ID_INTERNAL_LVTM1;
	case ENCODER_ID_INTERNAL_HDMI:
		return ENCODER_OBJECT_ID_HDMI_INTERNAL;
	case ENCODER_ID_EXTERNAL_TRAVIS:
		return ENCODER_OBJECT_ID_TRAVIS;
	case ENCODER_ID_EXTERNAL_NUTMEG:
		return ENCODER_OBJECT_ID_NUTMEG;
	case ENCODER_ID_INTERNAL_KLDSCP_TMDS1:
		return ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1;
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		return ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1;
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		return ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2;
	case ENCODER_ID_EXTERNAL_MVPU_FPGA:
		return ENCODER_OBJECT_ID_MVPU_FPGA;
	case ENCODER_ID_INTERNAL_DDI:
		return ENCODER_OBJECT_ID_INTERNAL_DDI;
	case ENCODER_ID_INTERNAL_UNIPHY:
		return ENCODER_OBJECT_ID_INTERNAL_UNIPHY;
	case ENCODER_ID_INTERNAL_KLDSCP_LVTMA:
		return ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA;
	case ENCODER_ID_INTERNAL_UNIPHY1:
		return ENCODER_OBJECT_ID_INTERNAL_UNIPHY1;
	case ENCODER_ID_INTERNAL_UNIPHY2:
		return ENCODER_OBJECT_ID_INTERNAL_UNIPHY2;
	case ENCODER_ID_INTERNAL_UNIPHY3:
		return ENCODER_OBJECT_ID_INTERNAL_UNIPHY3;
	case ENCODER_ID_INTERNAL_WIRELESS:
		return ENCODER_OBJECT_ID_INTERNAL_VCE;
	case ENCODER_ID_INTERNAL_VIRTUAL:
		return ENCODER_OBJECT_ID_NONE;
	case ENCODER_ID_UNKNOWN:
		return ENCODER_OBJECT_ID_NONE;
	default:
		/* Invalid encoder id */
		BREAK_TO_DEBUGGER();
		return ENCODER_OBJECT_ID_NONE;
	}
}

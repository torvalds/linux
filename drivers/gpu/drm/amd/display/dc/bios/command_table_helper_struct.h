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

#ifndef __DAL_COMMAND_TABLE_HELPER_STRUCT_H__
#define __DAL_COMMAND_TABLE_HELPER_STRUCT_H__

#include "dce80/command_table_helper_dce80.h"
#include "dce110/command_table_helper_dce110.h"
#include "dce112/command_table_helper_dce112.h"

struct _DIG_ENCODER_CONTROL_PARAMETERS_V2;
struct command_table_helper {
	bool (*controller_id_to_atom)(enum controller_id id, uint8_t *atom_id);
	uint8_t (*encoder_action_to_atom)(
			enum bp_encoder_control_action action);
	uint32_t (*encoder_mode_bp_to_atom)(enum signal_type s,
			bool enable_dp_audio);
	bool (*engine_bp_to_atom)(enum engine_id engine_id,
			uint32_t *atom_engine_id);
	void (*assign_control_parameter)(
			const struct command_table_helper *h,
			struct bp_encoder_control *control,
			struct _DIG_ENCODER_CONTROL_PARAMETERS_V2 *ctrl_param);
	bool (*clock_source_id_to_atom)(enum clock_source_id id,
			uint32_t *atom_pll_id);
	bool (*clock_source_id_to_ref_clk_src)(
			enum clock_source_id id,
			uint32_t *ref_clk_src_id);
	uint8_t (*transmitter_bp_to_atom)(enum transmitter t);
	uint8_t (*encoder_id_to_atom)(enum encoder_id id);
	uint8_t (*clock_source_id_to_atom_phy_clk_src_id)(
			enum clock_source_id id);
	uint8_t (*signal_type_to_atom_dig_mode)(enum signal_type s);
	uint8_t (*hpd_sel_to_atom)(enum hpd_source_id id);
	uint8_t (*dig_encoder_sel_to_atom)(enum engine_id engine_id);
	uint8_t (*phy_id_to_atom)(enum transmitter t);
	uint8_t (*disp_power_gating_action_to_atom)(
			enum bp_pipe_control_action action);
	bool (*dc_clock_type_to_atom)(enum bp_dce_clock_type id,
			uint32_t *atom_clock_type);
	uint8_t (*transmitter_color_depth_to_atom)(enum transmitter_color_depth id);
};

#endif

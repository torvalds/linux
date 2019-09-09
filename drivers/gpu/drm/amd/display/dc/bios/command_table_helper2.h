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

#ifndef __DAL_COMMAND_TABLE_HELPER2_H__
#define __DAL_COMMAND_TABLE_HELPER2_H__

#include "dce80/command_table_helper_dce80.h"
#include "dce110/command_table_helper_dce110.h"
#include "dce112/command_table_helper2_dce112.h"
#include "command_table_helper_struct.h"

bool dal_bios_parser_init_cmd_tbl_helper2(const struct command_table_helper **h,
	enum dce_version dce);

bool dal_cmd_table_helper_controller_id_to_atom2(
	enum controller_id id,
	uint8_t *atom_id);

uint32_t dal_cmd_table_helper_encoder_mode_bp_to_atom2(
	enum signal_type s,
	bool enable_dp_audio);

bool dal_cmd_table_helper_clock_source_id_to_ref_clk_src2(
	enum clock_source_id id,
	uint32_t *ref_clk_src_id);

uint8_t dal_cmd_table_helper_transmitter_bp_to_atom2(
	enum transmitter t);

uint8_t dal_cmd_table_helper_encoder_id_to_atom2(
	enum encoder_id id);
#endif

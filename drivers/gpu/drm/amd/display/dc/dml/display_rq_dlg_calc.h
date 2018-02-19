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
 * Authors: AMD
 *
 */

#ifndef __DML2_DISPLAY_RQ_DLG_CALC_H__
#define __DML2_DISPLAY_RQ_DLG_CALC_H__

#include "dml_common_defs.h"
#include "display_rq_dlg_helpers.h"

struct display_mode_lib;

// Function: dml_rq_dlg_get_rq_params
//  Calculate requestor related parameters that register definition agnostic
//  (i.e. this layer does try to separate real values from register definition)
// Input:
//  pipe_src_param - pipe source configuration (e.g. vp, pitch, etc.)
// Output:
//  rq_param - values that can be used to setup RQ (e.g. swath_height, plane1_addr, etc.)
//
void dml_rq_dlg_get_rq_params(
		struct display_mode_lib *mode_lib,
		display_rq_params_st *rq_param,
		const display_pipe_source_params_st pipe_src_param);

// Function: dml_rq_dlg_get_rq_reg
//  Main entry point for test to get the register values out of this DML class.
//  This function calls <get_rq_param> and <extract_rq_regs> fucntions to calculate
//  and then populate the rq_regs struct
// Input:
//  pipe_src_param - pipe source configuration (e.g. vp, pitch, etc.)
// Output:
//  rq_regs - struct that holds all the RQ registers field value.
//            See also: <display_rq_regs_st>
void dml_rq_dlg_get_rq_reg(
		struct display_mode_lib *mode_lib,
		display_rq_regs_st *rq_regs,
		const display_pipe_source_params_st pipe_src_param);

// Function: dml_rq_dlg_get_dlg_params
//  Calculate deadline related parameters
//
void dml_rq_dlg_get_dlg_params(struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx,
		display_dlg_regs_st *disp_dlg_regs,
		display_ttu_regs_st *disp_ttu_regs,
		const display_rq_dlg_params_st rq_dlg_param,
		const display_dlg_sys_params_st dlg_sys_param,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en,
		const bool ignore_viewport_pos,
		const bool immediate_flip_support);

// Function: dml_rq_dlg_get_dlg_param_prefetch
//   For flip_bw programming guide change, now dml needs to calculate the flip_bytes and prefetch_bw
//   for ALL pipes and use this info to calculate the prefetch programming.
// Output: prefetch_param.prefetch_bw and flip_bytes
void dml_rq_dlg_get_dlg_params_prefetch(
		struct display_mode_lib *mode_lib,
		display_dlg_prefetch_param_st *prefetch_param,
		display_rq_dlg_params_st rq_dlg_param,
		display_dlg_sys_params_st dlg_sys_param,
		display_e2e_pipe_params_st e2e_pipe_param,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en);

// Function: dml_rq_dlg_get_dlg_reg
//   Calculate and return DLG and TTU register struct given the system setting
// Output:
//  dlg_regs - output DLG register struct
//  ttu_regs - output DLG TTU register struct
// Input:
//  e2e_pipe_param - "compacted" array of e2e pipe param struct
//  num_pipes - num of active "pipe" or "route"
//  pipe_idx - index that identifies the e2e_pipe_param that corresponding to this dlg
//  cstate - 0: when calculate min_ttu_vblank it is assumed cstate is not required. 1: Normal mode, cstate is considered.
//           Added for legacy or unrealistic timing tests.
void dml_rq_dlg_get_dlg_reg(
		struct display_mode_lib *mode_lib,
		display_dlg_regs_st *dlg_regs,
		display_ttu_regs_st *ttu_regs,
		display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en,
		const bool ignore_viewport_pos,
		const bool immediate_flip_support);

// Function: dml_rq_dlg_get_calculated_vstartup
//   Calculate and return vstartup
// Output:
//  unsigned int vstartup
// Input:
//  e2e_pipe_param - "compacted" array of e2e pipe param struct
//  num_pipes - num of active "pipe" or "route"
//  pipe_idx - index that identifies the e2e_pipe_param that corresponding to this dlg
// NOTE: this MUST be called after setting the prefetch mode!
unsigned int dml_rq_dlg_get_calculated_vstartup(
		struct display_mode_lib *mode_lib,
		display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx);

// Function: dml_rq_dlg_get_row_heights
//  Calculate dpte and meta row heights
void dml_rq_dlg_get_row_heights(
		struct display_mode_lib *mode_lib,
		unsigned int *o_dpte_row_height,
		unsigned int *o_meta_row_height,
		unsigned int vp_width,
		unsigned int data_pitch,
		int source_format,
		int tiling,
		int macro_tile_size,
		int source_scan,
		int is_chroma);

// Function: dml_rq_dlg_get_arb_params
void dml_rq_dlg_get_arb_params(struct display_mode_lib *mode_lib, display_arb_params_st *arb_param);

#endif

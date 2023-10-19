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

#ifndef __DML31_DISPLAY_RQ_DLG_CALC_H__
#define __DML31_DISPLAY_RQ_DLG_CALC_H__

#include "../display_rq_dlg_helpers.h"

struct display_mode_lib;

// Function: dml_rq_dlg_get_rq_reg
//  Main entry point for test to get the register values out of this DML class.
//  This function calls <get_rq_param> and <extract_rq_regs> fucntions to calculate
//  and then populate the rq_regs struct
// Input:
//  pipe_param - pipe source configuration (e.g. vp, pitch, scaling, dest, etc.)
// Output:
//  rq_regs - struct that holds all the RQ registers field value.
//            See also: <display_rq_regs_st>
void dml31_rq_dlg_get_rq_reg(struct display_mode_lib *mode_lib,
		display_rq_regs_st *rq_regs,
		const display_pipe_params_st *pipe_param);

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
void dml31_rq_dlg_get_dlg_reg(struct display_mode_lib             *mode_lib,
		display_dlg_regs_st          *dlg_regs,
		display_ttu_regs_st          *ttu_regs,
		const display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int            num_pipes,
		const unsigned int            pipe_idx,
		const bool                    cstate_en,
		const bool                    pstate_en,
		const bool                    vm_en,
		const bool                    ignore_viewport_pos,
		const bool                    immediate_flip_support);

#endif

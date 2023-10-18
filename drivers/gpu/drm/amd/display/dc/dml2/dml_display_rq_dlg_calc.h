/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 */

#ifndef __DML_DISPLAY_RQ_DLG_CALC_H__
#define __DML_DISPLAY_RQ_DLG_CALC_H__

#include "display_mode_core_structs.h"
#include "display_mode_lib_defines.h"

struct display_mode_lib_st;

// Function: dml_rq_dlg_get_rq_reg
//  Main entry point for test to get the register values out of this DML class.
//  This function calls <get_rq_param> and <extract_rq_regs> fucntions to calculate
//  and then populate the rq_regs struct
// Input:
//  Assume mode_program is already called
// Output:
//  rq_regs - struct that holds all the RQ registers field value.
//            See also: <display_rq_regs_st>

void dml_rq_dlg_get_rq_reg(dml_display_rq_regs_st *rq_regs,
			struct display_mode_lib_st *mode_lib,
			const dml_uint_t pipe_idx);

// Function: dml_rq_dlg_get_dlg_reg
//   Calculate and return DLG and TTU register struct given the system setting
// Output:
//  dlg_regs - output DLG register struct
//  ttu_regs - output DLG TTU register struct
// Input:
//  Assume mode_program is already called
//  pipe_idx - index that identifies the e2e_pipe_param that corresponding to this dlg
void dml_rq_dlg_get_dlg_reg(dml_display_dlg_regs_st        *dlg_regs,
				dml_display_ttu_regs_st        *ttu_regs,
				struct display_mode_lib_st        *mode_lib,
				const dml_uint_t            pipe_idx);

// Function: dml_rq_dlg_get_arb_params
void dml_rq_dlg_get_arb_params(struct display_mode_lib_st *mode_lib, dml_display_arb_params_st *arb_param);

#endif

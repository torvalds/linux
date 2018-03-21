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

#ifndef __DISPLAY_RQ_DLG_CALC_H__
#define __DISPLAY_RQ_DLG_CALC_H__

#include "dml_common_defs.h"

struct display_mode_lib;

#include "display_rq_dlg_helpers.h"

void dml1_extract_rq_regs(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		const struct _vcs_dpi_display_rq_params_st rq_param);
/* Function: dml_rq_dlg_get_rq_params
 *  Calculate requestor related parameters that register definition agnostic
 *  (i.e. this layer does try to separate real values from register definition)
 * Input:
 *  pipe_src_param - pipe source configuration (e.g. vp, pitch, etc.)
 * Output:
 *  rq_param - values that can be used to setup RQ (e.g. swath_height, plane1_addr, etc.)
 */
void dml1_rq_dlg_get_rq_params(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_params_st *rq_param,
		const struct _vcs_dpi_display_pipe_source_params_st pipe_src_param);


/* Function: dml_rq_dlg_get_dlg_params
 *  Calculate deadline related parameters
 */
void dml1_rq_dlg_get_dlg_params(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
		const struct _vcs_dpi_display_rq_dlg_params_st rq_dlg_param,
		const struct _vcs_dpi_display_dlg_sys_params_st dlg_sys_param,
		const struct _vcs_dpi_display_e2e_pipe_params_st e2e_pipe_param,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en,
		const bool iflip_en);

#endif

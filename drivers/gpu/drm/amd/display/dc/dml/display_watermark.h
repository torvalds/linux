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
#ifndef __DISPLAY_WATERMARK_H__
#define __DISPLAY_WATERMARK_H__

#include "dml_common_defs.h"

struct display_mode_lib;

double dml_wm_urgent_extra(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes);
double dml_wm_urgent_extra_max(struct display_mode_lib *mode_lib);

double dml_wm_urgent_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
double dml_wm_urgent(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes);
double dml_wm_pte_meta_urgent(struct display_mode_lib *mode_lib, double urgent_wm_us);
double dml_wm_dcfclk_deepsleep_mhz_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
double dml_wm_dcfclk_deepsleep_mhz(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes);

struct _vcs_dpi_cstate_pstate_watermarks_st dml_wm_cstate_pstate_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
struct _vcs_dpi_cstate_pstate_watermarks_st dml_wm_cstate_pstate(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes);

double dml_wm_writeback_pstate_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
double dml_wm_writeback_pstate(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes);

double dml_wm_expected_stutter_eff_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes);
double dml_wm_expected_stutter_eff_e2e_with_vblank(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes);

unsigned int dml_wm_e2e_to_wm(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes,
		struct _vcs_dpi_wm_calc_pipe_params_st *wm);

double dml_wm_calc_total_data_read_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes);
double dml_wm_calc_return_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes);

#endif

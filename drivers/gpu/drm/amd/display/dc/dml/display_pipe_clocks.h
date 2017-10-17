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
#ifndef __DISPLAY_PIPE_CLOCKS_H__
#define __DISPLAY_PIPE_CLOCKS_H__

#include "dml_common_defs.h"

struct display_mode_lib;

struct _vcs_dpi_display_pipe_clock_st dml_clks_get_pipe_clocks(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes);

bool dml_clks_pipe_clock_requirement_fit_power_constraint(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_dpp_in_grp);
#endif

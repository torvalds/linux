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
#ifndef __DISPLAY_MODE_LIB_H__
#define __DISPLAY_MODE_LIB_H__

#include "dm_services.h"
#include "dc_features.h"
#include "display_mode_structs.h"
#include "display_mode_enums.h"
#include "display_mode_vba.h"

enum dml_project {
	DML_PROJECT_UNDEFINED,
	DML_PROJECT_RAVEN1,
	DML_PROJECT_NAVI10,
	DML_PROJECT_NAVI10v2,
	DML_PROJECT_DCN21,
};

struct display_mode_lib;

struct dml_funcs {
	void (*rq_dlg_get_dlg_reg)(
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
	void (*rq_dlg_get_rq_reg)(
		struct display_mode_lib *mode_lib,
		display_rq_regs_st *rq_regs,
		const display_pipe_params_st pipe_param);
	void (*recalculate)(struct display_mode_lib *mode_lib);
	void (*validate)(struct display_mode_lib *mode_lib);
};

struct display_mode_lib {
	struct _vcs_dpi_ip_params_st ip;
	struct _vcs_dpi_soc_bounding_box_st soc;
	enum dml_project project;
	struct vba_vars_st vba;
	struct dal_logger *logger;
	struct dml_funcs funcs;
};

void dml_init_instance(struct display_mode_lib *lib,
		const struct _vcs_dpi_soc_bounding_box_st *soc_bb,
		const struct _vcs_dpi_ip_params_st *ip_params,
		enum dml_project project);

const char *dml_get_status_message(enum dm_validation_status status);

#endif

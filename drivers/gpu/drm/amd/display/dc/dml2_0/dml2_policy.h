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

#ifndef __DML2_POLICY_H__
#define __DML2_POLICY_H__

#include "display_mode_core_structs.h"

struct dml2_policy_build_synthetic_soc_states_params {
	const struct soc_bounding_box_st *in_bbox;
	struct soc_states_st *in_states;
	struct soc_states_st *out_states;
	int *dcfclk_stas_mhz;
	int num_dcfclk_stas;
};

struct dml2_policy_build_synthetic_soc_states_scratch {
	struct soc_state_bounding_box_st entry;
};

int dml2_policy_build_synthetic_soc_states(struct dml2_policy_build_synthetic_soc_states_scratch *s,
	struct dml2_policy_build_synthetic_soc_states_params *p);

void build_unoptimized_policy_settings(enum dml_project_id project, struct dml_mode_eval_policy_st *policy);

#endif

/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#ifndef _DML2_WRAPPER_FPU_H_
#define _DML2_WRAPPER_FPU_H_

#include "os_types.h"

struct dml2_context;
struct dc;
struct ip_params_st;
struct soc_bounding_box_st;
struct soc_states_st;

void initialize_dml2_ip_params(struct dml2_context *dml2, const struct dc *in_dc, struct ip_params_st *out);
void initialize_dml2_soc_bbox(struct dml2_context *dml2, const struct dc *in_dc, struct soc_bounding_box_st *out);
void initialize_dml2_soc_states(struct dml2_context *dml2,
	const struct dc *in_dc, const struct soc_bounding_box_st *in_bbox, struct soc_states_st *out);

#endif //_DML2_WRAPPER_FPU_H_


/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef _DCN21_RESOURCE_H_
#define _DCN21_RESOURCE_H_

#include "core_types.h"

#define TO_DCN21_RES_POOL(pool)\
	container_of(pool, struct dcn21_resource_pool, base)

struct dc;
struct resource_pool;
struct _vcs_dpi_display_pipe_params_st;

extern struct _vcs_dpi_ip_params_st dcn2_1_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn2_1_soc;

struct dcn21_resource_pool {
	struct resource_pool base;
};
struct resource_pool *dcn21_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);
bool dcn21_fast_validate_bw(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *pipe_split_from,
		int *vlevel_out,
		bool fast_validate);

#endif /* _DCN21_RESOURCE_H_ */

/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef _DCN32_RESOURCE_H_
#define _DCN32_RESOURCE_H_

#include "core_types.h"

#define DCN3_2_DET_SEG_SIZE 64

#define TO_DCN32_RES_POOL(pool)\
	container_of(pool, struct dcn32_resource_pool, base)

struct dcn32_resource_pool {
	struct resource_pool base;
};

struct resource_pool *dcn32_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

void dcn32_calculate_dlg_params(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel);

struct panel_cntl *dcn32_panel_cntl_create(
		const struct panel_cntl_init_data *init_data);

bool dcn32_acquire_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		int mpcc_id,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

bool dcn32_release_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

bool dcn32_remove_phantom_pipes(struct dc *dc,
		struct dc_state *context);

void dcn32_add_phantom_pipes(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		unsigned int pipe_cnt,
		unsigned int index);

bool dcn32_validate_bandwidth(struct dc *dc,
		struct dc_state *context,
		bool fast_validate);

int dcn32_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate);

void dcn32_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel);

uint32_t dcn32_helper_calculate_num_ways_for_subvp
		(struct dc *dc,
		struct dc_state *context);

void dcn32_merge_pipes_for_subvp(struct dc *dc,
		struct dc_state *context);

bool dcn32_all_pipes_have_stream_and_plane(struct dc *dc,
		struct dc_state *context);

bool dcn32_subvp_in_use(struct dc *dc,
		struct dc_state *context);

void dcn32_update_det_override_for_mpo(struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes);

#endif /* _DCN32_RESOURCE_H_ */

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
#define DCN3_2_MALL_MBLK_SIZE_BYTES 65536 // 64 * 1024
#define DCN3_2_MBLK_WIDTH 128
#define DCN3_2_MBLK_HEIGHT_4BPE 128
#define DCN3_2_MBLK_HEIGHT_8BPE 64

#define TO_DCN32_RES_POOL(pool)\
	container_of(pool, struct dcn32_resource_pool, base)

extern struct _vcs_dpi_ip_params_st dcn3_2_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn3_2_soc;

struct dcn32_resource_pool {
	struct resource_pool base;
};

struct resource_pool *dcn32_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

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

bool dcn32_mpo_in_use(struct dc_state *context);

struct pipe_ctx *dcn32_acquire_idle_pipe_for_head_pipe_in_layer(
		struct dc_state *state,
		const struct resource_pool *pool,
		struct dc_stream_state *stream,
		struct pipe_ctx *head_pipe);

void dcn32_determine_det_override(struct dc_state *context, display_e2e_pipe_params_st *pipes,
		bool *is_pipe_split_expected, int pipe_cnt);

#endif /* _DCN32_RESOURCE_H_ */

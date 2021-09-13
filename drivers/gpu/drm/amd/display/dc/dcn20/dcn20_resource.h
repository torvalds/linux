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

#ifndef __DC_RESOURCE_DCN20_H__
#define __DC_RESOURCE_DCN20_H__

#include "core_types.h"

#define TO_DCN20_RES_POOL(pool)\
	container_of(pool, struct dcn20_resource_pool, base)

struct dc;
struct resource_pool;
struct _vcs_dpi_display_pipe_params_st;

struct dcn20_resource_pool {
	struct resource_pool base;
};
struct resource_pool *dcn20_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

struct link_encoder *dcn20_link_encoder_create(
	const struct encoder_init_data *enc_init_data);

unsigned int dcn20_calc_max_scaled_time(
		unsigned int time_per_pixel,
		enum mmhubbub_wbif_mode mode,
		unsigned int urgent_watermark);
int dcn20_populate_dml_pipes_from_context(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		bool fast_validate);
struct pipe_ctx *dcn20_acquire_idle_pipe_for_layer(
		struct dc_state *state,
		const struct resource_pool *pool,
		struct dc_stream_state *stream);

struct stream_encoder *dcn20_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx);

struct dce_hwseq *dcn20_hwseq_create(
	struct dc_context *ctx);

bool dcn20_get_dcc_compression_cap(const struct dc *dc,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output);

void dcn20_dpp_destroy(struct dpp **dpp);

struct dpp *dcn20_dpp_create(
	struct dc_context *ctx,
	uint32_t inst);

struct input_pixel_processor *dcn20_ipp_create(
	struct dc_context *ctx, uint32_t inst);


struct output_pixel_processor *dcn20_opp_create(
	struct dc_context *ctx, uint32_t inst);

struct dce_aux *dcn20_aux_engine_create(
	struct dc_context *ctx, uint32_t inst);

struct dce_i2c_hw *dcn20_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst);

void dcn20_clock_source_destroy(struct clock_source **clk_src);

struct display_stream_compressor *dcn20_dsc_create(
	struct dc_context *ctx, uint32_t inst);
void dcn20_dsc_destroy(struct display_stream_compressor **dsc);

void dcn20_cap_soc_clocks(
		struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table max_clocks);
void dcn20_update_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table *max_clocks, unsigned int *uclk_states, unsigned int num_states);
struct hubp *dcn20_hubp_create(
	struct dc_context *ctx,
	uint32_t inst);
struct timing_generator *dcn20_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance);
struct mpc *dcn20_mpc_create(struct dc_context *ctx);
struct hubbub *dcn20_hubbub_create(struct dc_context *ctx);

bool dcn20_dwbc_create(struct dc_context *ctx, struct resource_pool *pool);
bool dcn20_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool);

void dcn20_set_mcif_arb_params(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt);
bool dcn20_validate_bandwidth(struct dc *dc, struct dc_state *context, bool fast_validate);
void dcn20_merge_pipes_for_validate(
		struct dc *dc,
		struct dc_state *context);
int dcn20_validate_apply_pipe_split_flags(
		struct dc *dc,
		struct dc_state *context,
		int vlevel,
		int *split,
		bool *merge);
void dcn20_release_dsc(struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct display_stream_compressor **dsc);
bool dcn20_validate_dsc(struct dc *dc, struct dc_state *new_ctx);
void dcn20_split_stream_for_mpc(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *primary_pipe,
		struct pipe_ctx *secondary_pipe);
bool dcn20_split_stream_for_odm(
		const struct dc *dc,
		struct resource_context *res_ctx,
		struct pipe_ctx *prev_odm_pipe,
		struct pipe_ctx *next_odm_pipe);
void dcn20_acquire_dsc(const struct dc *dc,
			struct resource_context *res_ctx,
			struct display_stream_compressor **dsc,
			int pipe_idx);
struct pipe_ctx *dcn20_find_secondary_pipe(struct dc *dc,
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe);
bool dcn20_fast_validate_bw(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *pipe_split_from,
		int *vlevel_out,
		bool fast_validate);
void dcn20_calculate_dlg_params(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel);

enum dc_status dcn20_build_mapped_resource(const struct dc *dc, struct dc_state *context, struct dc_stream_state *stream);
enum dc_status dcn20_add_stream_to_ctx(struct dc *dc, struct dc_state *new_ctx, struct dc_stream_state *dc_stream);
enum dc_status dcn20_add_dsc_to_stream_resource(struct dc *dc, struct dc_state *dc_ctx, struct dc_stream_state *dc_stream);
enum dc_status dcn20_remove_stream_from_ctx(struct dc *dc, struct dc_state *new_ctx, struct dc_stream_state *dc_stream);
enum dc_status dcn20_patch_unknown_plane_state(struct dc_plane_state *plane_state);

void dcn20_patch_bounding_box(
		struct dc *dc,
		struct _vcs_dpi_soc_bounding_box_st *bb);
void dcn20_cap_soc_clocks(
		struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table max_clocks);

#endif /* __DC_RESOURCE_DCN20_H__ */


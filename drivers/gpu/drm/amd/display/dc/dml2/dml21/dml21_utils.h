// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef _DML21_UTILS_H_
#define _DML21_UTILS_H_

struct dc_state;
struct dc_plane_state;
struct pipe_ctx;

struct dml2_context;
struct dml2_display_rq_regs;
struct dml2_display_dlg_regs;
struct dml2_display_ttu_regs;

int dml21_helper_find_dml_pipe_idx_by_stream_id(struct dml2_context *ctx, unsigned int stream_id);
int dml21_find_dml_pipe_idx_by_plane_id(struct dml2_context *ctx, unsigned int plane_id);
bool dml21_get_plane_id(const struct dc_state *state, const struct dc_plane_state *plane, unsigned int *plane_id);
void dml21_pipe_populate_global_sync(struct dml2_context *dml_ctx,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct dml2_per_stream_programming *stream_programming);
void dml21_populate_mall_allocation_size(struct dc_state *context,
		struct dml2_context *in_ctx,
		struct dml2_per_plane_programming *pln_prog,
		struct pipe_ctx *dc_pipe);
bool check_dp2p0_output_encoder(const struct pipe_ctx *pipe_ctx);
void find_valid_pipe_idx_for_stream_index(const struct dml2_context *dml_ctx, unsigned int *dml_pipe_idx, unsigned int stream_index);
void find_pipe_regs_idx(const struct dml2_context *dml_ctx,
		struct pipe_ctx *pipe, unsigned int *pipe_regs_idx);
int dml21_find_dc_pipes_for_plane(const struct dc *in_dc,
		struct dc_state *context,
		struct dml2_context *dml_ctx,
		struct pipe_ctx *dc_main_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__],
		struct pipe_ctx *dc_phantom_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__],
		int dml_plane_idx);
void dml21_program_dc_pipe(struct dml2_context *dml_ctx,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct dml2_per_plane_programming *pln_prog,
		struct dml2_per_stream_programming *stream_prog);
void dml21_handle_phantom_streams_planes(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx);
unsigned int dml21_get_dc_plane_idx_from_plane_id(unsigned int plane_id);
void dml21_build_fams2_programming(const struct dc *dc,
		struct dc_state *context,
		struct dml2_context *dml_ctx);
bool dml21_is_plane1_enabled(enum dml2_source_format_class source_format);
#endif

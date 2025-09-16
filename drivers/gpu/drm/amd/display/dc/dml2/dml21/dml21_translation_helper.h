// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef _DML21_TRANSLATION_HELPER_H_
#define _DML21_TRANSLATION_HELPER_H_

struct dc;
struct dc_state;
struct dcn_watermarks;
union dcn_watermark_set;
struct pipe_ctx;
struct dc_plane_state;

struct dml2_context;
struct dml2_configuration_options;
struct dml2_initialize_instance_in_out;

void dml21_populate_dml_init_params(struct dml2_initialize_instance_in_out *dml_init, const struct dml2_configuration_options *config, const struct dc *in_dc);
bool dml21_map_dc_state_into_dml_display_cfg(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx);
void dml21_copy_clocks_to_dc_state(struct dml2_context *in_ctx, struct dc_state *context);
void dml21_extract_watermark_sets(const struct dc *in_dc, union dcn_watermark_set *watermarks, struct dml2_context *in_ctx);
void dml21_map_hw_resources(struct dml2_context *dml_ctx);
void dml21_get_pipe_mcache_config(struct dc_state *context, struct pipe_ctx *pipe_ctx, struct dml2_per_plane_programming *pln_prog, struct dml2_pipe_configuration_descriptor *mcache_pipe_config);
void dml21_set_dc_p_state_type(struct pipe_ctx *pipe_ctx, struct dml2_per_stream_programming *stream_programming, bool sub_vp_enabled);
unsigned int map_plane_to_dml21_display_cfg(const struct dml2_context *dml_ctx, unsigned int stream_id, const struct dc_plane_state *plane, const struct dc_state *context);
#endif

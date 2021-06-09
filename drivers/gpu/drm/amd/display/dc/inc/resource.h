/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_

#include "core_types.h"
#include "core_status.h"
#include "dal_asic_id.h"
#include "dm_pp_smu.h"

#define MEMORY_TYPE_MULTIPLIER_CZ 4
#define MEMORY_TYPE_HBM 2


enum dce_version resource_parse_asic_id(
		struct hw_asic_id asic_id);

struct resource_caps {
	int num_timing_generator;
	int num_opp;
	int num_video_plane;
	int num_audio;
	int num_stream_encoder;
	int num_pll;
	int num_dwb;
	int num_ddc;
	int num_vmid;
	int num_dsc;
	unsigned int num_dig_link_enc; // Total number of DIGs (digital encoders) in DIO (Display Input/Output).
#if defined(CONFIG_DRM_AMD_DC_DCN)
	int num_hpo_dp_stream_encoder;
#endif
	int num_mpc_3dlut;
};

struct resource_straps {
	uint32_t hdmi_disable;
	uint32_t dc_pinstraps_audio;
	uint32_t audio_stream_number;
};

struct resource_create_funcs {
	void (*read_dce_straps)(
			struct dc_context *ctx, struct resource_straps *straps);

	struct audio *(*create_audio)(
			struct dc_context *ctx, unsigned int inst);

	struct stream_encoder *(*create_stream_encoder)(
			enum engine_id eng_id, struct dc_context *ctx);

#if defined(CONFIG_DRM_AMD_DC_DCN)
	struct hpo_dp_stream_encoder *(*create_hpo_dp_stream_encoder)(
			enum engine_id eng_id, struct dc_context *ctx);
#endif

	struct dce_hwseq *(*create_hwseq)(
			struct dc_context *ctx);
};

bool resource_construct(
	unsigned int num_virtual_links,
	struct dc *dc,
	struct resource_pool *pool,
	const struct resource_create_funcs *create_funcs);

struct resource_pool *dc_create_resource_pool(struct dc  *dc,
					      const struct dc_init_data *init_data,
					      enum dce_version dc_version);

void dc_destroy_resource_pool(struct dc *dc);

enum dc_status resource_map_pool_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

bool resource_build_scaling_params(struct pipe_ctx *pipe_ctx);

enum dc_status resource_build_scaling_params_for_context(
		const struct dc *dc,
		struct dc_state *context);

void resource_build_info_frame(struct pipe_ctx *pipe_ctx);

void resource_unreference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

void resource_reference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

int resource_get_clock_source_reference(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

bool resource_are_streams_timing_synchronizable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2);

bool resource_are_vblanks_synchronizable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2);

struct clock_source *resource_find_used_clk_src_for_sharing(
		struct resource_context *res_ctx,
		struct pipe_ctx *pipe_ctx);

struct clock_source *dc_resource_find_first_free_pll(
		struct resource_context *res_ctx,
		const struct resource_pool *pool);

struct pipe_ctx *resource_get_head_pipe_for_stream(
		struct resource_context *res_ctx,
		struct dc_stream_state *stream);

bool resource_attach_surfaces_to_context(
		struct dc_plane_state *const *plane_state,
		int surface_count,
		struct dc_stream_state *dc_stream,
		struct dc_state *context,
		const struct resource_pool *pool);

struct pipe_ctx *find_idle_secondary_pipe(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe);

bool resource_validate_attach_surfaces(
		const struct dc_validation_set set[],
		int set_count,
		const struct dc_state *old_context,
		struct dc_state *context,
		const struct resource_pool *pool);

void resource_validate_ctx_update_pointer_after_copy(
		const struct dc_state *src_ctx,
		struct dc_state *dst_ctx);

enum dc_status resource_map_clock_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

enum dc_status resource_map_phy_clock_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

bool pipe_need_reprogram(
		struct pipe_ctx *pipe_ctx_old,
		struct pipe_ctx *pipe_ctx);

void resource_build_bit_depth_reduction_params(struct dc_stream_state *stream,
		struct bit_depth_reduction_params *fmt_bit_depth);

void update_audio_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct audio *audio,
		bool acquired);

unsigned int resource_pixel_format_to_bpp(enum surface_pixel_format format);

void get_audio_check(struct audio_info *aud_modes,
	struct audio_check *aud_chk);

int get_num_mpc_splits(struct pipe_ctx *pipe);

int get_num_odm_splits(struct pipe_ctx *pipe);

#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_ */

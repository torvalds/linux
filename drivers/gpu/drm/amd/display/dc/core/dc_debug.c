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
 */
/*
 * dc_debug.c
 *
 *  Created on: Nov 3, 2016
 *      Author: yonsun
 */

#include "dm_services.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"

#include "resource.h"

#define DC_LOGGER_INIT(logger)


#define SURFACE_TRACE(...) do {\
		if (dc->debug.surface_trace) \
			DC_LOG_IF_TRACE(__VA_ARGS__); \
} while (0)

#define TIMING_TRACE(...) do {\
	if (dc->debug.timing_trace) \
		DC_LOG_SYNC(__VA_ARGS__); \
} while (0)

#define CLOCK_TRACE(...) do {\
	if (dc->debug.clock_trace) \
		DC_LOG_BANDWIDTH_CALCS(__VA_ARGS__); \
} while (0)

void pre_surface_trace(
		struct dc *dc,
		const struct dc_plane_state *const *plane_states,
		int surface_count)
{
	int i;
	DC_LOGGER_INIT(dc->ctx->logger);

	for (i = 0; i < surface_count; i++) {
		const struct dc_plane_state *plane_state = plane_states[i];

		SURFACE_TRACE("Planes %d:\n", i);

		SURFACE_TRACE(
				"plane_state->visible = %d;\n"
				"plane_state->flip_immediate = %d;\n"
				"plane_state->address.type = %d;\n"
				"plane_state->address.grph.addr.quad_part = 0x%llX;\n"
				"plane_state->address.grph.meta_addr.quad_part = 0x%llX;\n"
				"plane_state->scaling_quality.h_taps = %d;\n"
				"plane_state->scaling_quality.v_taps = %d;\n"
				"plane_state->scaling_quality.h_taps_c = %d;\n"
				"plane_state->scaling_quality.v_taps_c = %d;\n",
				plane_state->visible,
				plane_state->flip_immediate,
				plane_state->address.type,
				plane_state->address.grph.addr.quad_part,
				plane_state->address.grph.meta_addr.quad_part,
				plane_state->scaling_quality.h_taps,
				plane_state->scaling_quality.v_taps,
				plane_state->scaling_quality.h_taps_c,
				plane_state->scaling_quality.v_taps_c);

		SURFACE_TRACE(
				"plane_state->src_rect.x = %d;\n"
				"plane_state->src_rect.y = %d;\n"
				"plane_state->src_rect.width = %d;\n"
				"plane_state->src_rect.height = %d;\n"
				"plane_state->dst_rect.x = %d;\n"
				"plane_state->dst_rect.y = %d;\n"
				"plane_state->dst_rect.width = %d;\n"
				"plane_state->dst_rect.height = %d;\n"
				"plane_state->clip_rect.x = %d;\n"
				"plane_state->clip_rect.y = %d;\n"
				"plane_state->clip_rect.width = %d;\n"
				"plane_state->clip_rect.height = %d;\n",
				plane_state->src_rect.x,
				plane_state->src_rect.y,
				plane_state->src_rect.width,
				plane_state->src_rect.height,
				plane_state->dst_rect.x,
				plane_state->dst_rect.y,
				plane_state->dst_rect.width,
				plane_state->dst_rect.height,
				plane_state->clip_rect.x,
				plane_state->clip_rect.y,
				plane_state->clip_rect.width,
				plane_state->clip_rect.height);

		SURFACE_TRACE(
				"plane_state->plane_size.surface_size.x = %d;\n"
				"plane_state->plane_size.surface_size.y = %d;\n"
				"plane_state->plane_size.surface_size.width = %d;\n"
				"plane_state->plane_size.surface_size.height = %d;\n"
				"plane_state->plane_size.surface_pitch = %d;\n",
				plane_state->plane_size.surface_size.x,
				plane_state->plane_size.surface_size.y,
				plane_state->plane_size.surface_size.width,
				plane_state->plane_size.surface_size.height,
				plane_state->plane_size.surface_pitch);


		SURFACE_TRACE(
				"plane_state->tiling_info.gfx8.num_banks = %d;\n"
				"plane_state->tiling_info.gfx8.bank_width = %d;\n"
				"plane_state->tiling_info.gfx8.bank_width_c = %d;\n"
				"plane_state->tiling_info.gfx8.bank_height = %d;\n"
				"plane_state->tiling_info.gfx8.bank_height_c = %d;\n"
				"plane_state->tiling_info.gfx8.tile_aspect = %d;\n"
				"plane_state->tiling_info.gfx8.tile_aspect_c = %d;\n"
				"plane_state->tiling_info.gfx8.tile_split = %d;\n"
				"plane_state->tiling_info.gfx8.tile_split_c = %d;\n"
				"plane_state->tiling_info.gfx8.tile_mode = %d;\n"
				"plane_state->tiling_info.gfx8.tile_mode_c = %d;\n",
				plane_state->tiling_info.gfx8.num_banks,
				plane_state->tiling_info.gfx8.bank_width,
				plane_state->tiling_info.gfx8.bank_width_c,
				plane_state->tiling_info.gfx8.bank_height,
				plane_state->tiling_info.gfx8.bank_height_c,
				plane_state->tiling_info.gfx8.tile_aspect,
				plane_state->tiling_info.gfx8.tile_aspect_c,
				plane_state->tiling_info.gfx8.tile_split,
				plane_state->tiling_info.gfx8.tile_split_c,
				plane_state->tiling_info.gfx8.tile_mode,
				plane_state->tiling_info.gfx8.tile_mode_c);

		SURFACE_TRACE(
				"plane_state->tiling_info.gfx8.pipe_config = %d;\n"
				"plane_state->tiling_info.gfx8.array_mode = %d;\n"
				"plane_state->color_space = %d;\n"
				"plane_state->dcc.enable = %d;\n"
				"plane_state->format = %d;\n"
				"plane_state->rotation = %d;\n"
				"plane_state->stereo_format = %d;\n",
				plane_state->tiling_info.gfx8.pipe_config,
				plane_state->tiling_info.gfx8.array_mode,
				plane_state->color_space,
				plane_state->dcc.enable,
				plane_state->format,
				plane_state->rotation,
				plane_state->stereo_format);

		SURFACE_TRACE("plane_state->tiling_info.gfx9.swizzle = %d;\n",
				plane_state->tiling_info.gfx9.swizzle);

		SURFACE_TRACE("\n");
	}
	SURFACE_TRACE("\n");
}

void update_surface_trace(
		struct dc *dc,
		const struct dc_surface_update *updates,
		int surface_count)
{
	int i;
	DC_LOGGER_INIT(dc->ctx->logger);

	for (i = 0; i < surface_count; i++) {
		const struct dc_surface_update *update = &updates[i];

		SURFACE_TRACE("Update %d\n", i);
		if (update->flip_addr) {
			SURFACE_TRACE("flip_addr->address.type = %d;\n"
					"flip_addr->address.grph.addr.quad_part = 0x%llX;\n"
					"flip_addr->address.grph.meta_addr.quad_part = 0x%llX;\n"
					"flip_addr->flip_immediate = %d;\n",
					update->flip_addr->address.type,
					update->flip_addr->address.grph.addr.quad_part,
					update->flip_addr->address.grph.meta_addr.quad_part,
					update->flip_addr->flip_immediate);
		}

		if (update->plane_info) {
			SURFACE_TRACE(
					"plane_info->color_space = %d;\n"
					"plane_info->format = %d;\n"
					"plane_info->plane_size.surface_pitch = %d;\n"
					"plane_info->plane_size.surface_size.height = %d;\n"
					"plane_info->plane_size.surface_size.width = %d;\n"
					"plane_info->plane_size.surface_size.x = %d;\n"
					"plane_info->plane_size.surface_size.y = %d;\n"
					"plane_info->rotation = %d;\n"
					"plane_info->stereo_format = %d;\n",
					update->plane_info->color_space,
					update->plane_info->format,
					update->plane_info->plane_size.surface_pitch,
					update->plane_info->plane_size.surface_size.height,
					update->plane_info->plane_size.surface_size.width,
					update->plane_info->plane_size.surface_size.x,
					update->plane_info->plane_size.surface_size.y,
					update->plane_info->rotation,
					update->plane_info->stereo_format);

			SURFACE_TRACE(
					"plane_info->tiling_info.gfx8.num_banks = %d;\n"
					"plane_info->tiling_info.gfx8.bank_width = %d;\n"
					"plane_info->tiling_info.gfx8.bank_width_c = %d;\n"
					"plane_info->tiling_info.gfx8.bank_height = %d;\n"
					"plane_info->tiling_info.gfx8.bank_height_c = %d;\n"
					"plane_info->tiling_info.gfx8.tile_aspect = %d;\n"
					"plane_info->tiling_info.gfx8.tile_aspect_c = %d;\n"
					"plane_info->tiling_info.gfx8.tile_split = %d;\n"
					"plane_info->tiling_info.gfx8.tile_split_c = %d;\n"
					"plane_info->tiling_info.gfx8.tile_mode = %d;\n"
					"plane_info->tiling_info.gfx8.tile_mode_c = %d;\n",
					update->plane_info->tiling_info.gfx8.num_banks,
					update->plane_info->tiling_info.gfx8.bank_width,
					update->plane_info->tiling_info.gfx8.bank_width_c,
					update->plane_info->tiling_info.gfx8.bank_height,
					update->plane_info->tiling_info.gfx8.bank_height_c,
					update->plane_info->tiling_info.gfx8.tile_aspect,
					update->plane_info->tiling_info.gfx8.tile_aspect_c,
					update->plane_info->tiling_info.gfx8.tile_split,
					update->plane_info->tiling_info.gfx8.tile_split_c,
					update->plane_info->tiling_info.gfx8.tile_mode,
					update->plane_info->tiling_info.gfx8.tile_mode_c);

			SURFACE_TRACE(
					"plane_info->tiling_info.gfx8.pipe_config = %d;\n"
					"plane_info->tiling_info.gfx8.array_mode = %d;\n"
					"plane_info->visible = %d;\n"
					"plane_info->per_pixel_alpha = %d;\n",
					update->plane_info->tiling_info.gfx8.pipe_config,
					update->plane_info->tiling_info.gfx8.array_mode,
					update->plane_info->visible,
					update->plane_info->per_pixel_alpha);

			SURFACE_TRACE("surface->tiling_info.gfx9.swizzle = %d;\n",
					update->plane_info->tiling_info.gfx9.swizzle);
		}

		if (update->scaling_info) {
			SURFACE_TRACE(
					"scaling_info->src_rect.x = %d;\n"
					"scaling_info->src_rect.y = %d;\n"
					"scaling_info->src_rect.width = %d;\n"
					"scaling_info->src_rect.height = %d;\n"
					"scaling_info->dst_rect.x = %d;\n"
					"scaling_info->dst_rect.y = %d;\n"
					"scaling_info->dst_rect.width = %d;\n"
					"scaling_info->dst_rect.height = %d;\n"
					"scaling_info->clip_rect.x = %d;\n"
					"scaling_info->clip_rect.y = %d;\n"
					"scaling_info->clip_rect.width = %d;\n"
					"scaling_info->clip_rect.height = %d;\n"
					"scaling_info->scaling_quality.h_taps = %d;\n"
					"scaling_info->scaling_quality.v_taps = %d;\n"
					"scaling_info->scaling_quality.h_taps_c = %d;\n"
					"scaling_info->scaling_quality.v_taps_c = %d;\n",
					update->scaling_info->src_rect.x,
					update->scaling_info->src_rect.y,
					update->scaling_info->src_rect.width,
					update->scaling_info->src_rect.height,
					update->scaling_info->dst_rect.x,
					update->scaling_info->dst_rect.y,
					update->scaling_info->dst_rect.width,
					update->scaling_info->dst_rect.height,
					update->scaling_info->clip_rect.x,
					update->scaling_info->clip_rect.y,
					update->scaling_info->clip_rect.width,
					update->scaling_info->clip_rect.height,
					update->scaling_info->scaling_quality.h_taps,
					update->scaling_info->scaling_quality.v_taps,
					update->scaling_info->scaling_quality.h_taps_c,
					update->scaling_info->scaling_quality.v_taps_c);
		}
		SURFACE_TRACE("\n");
	}
	SURFACE_TRACE("\n");
}

void post_surface_trace(struct dc *dc)
{
	DC_LOGGER_INIT(dc->ctx->logger);

	SURFACE_TRACE("post surface process.\n");

}

void context_timing_trace(
		struct dc *dc,
		struct resource_context *res_ctx)
{
	int i;
	int h_pos[MAX_PIPES] = {0}, v_pos[MAX_PIPES] = {0};
	struct crtc_position position;
	unsigned int underlay_idx = dc->res_pool->underlay_pipe_index;
	DC_LOGGER_INIT(dc->ctx->logger);


	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];
		/* get_position() returns CRTC vertical/horizontal counter
		 * hence not applicable for underlay pipe
		 */
		if (pipe_ctx->stream == NULL || pipe_ctx->pipe_idx == underlay_idx)
			continue;

		pipe_ctx->stream_res.tg->funcs->get_position(pipe_ctx->stream_res.tg, &position);
		h_pos[i] = position.horizontal_count;
		v_pos[i] = position.vertical_count;
	}
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream == NULL || pipe_ctx->pipe_idx == underlay_idx)
			continue;

		TIMING_TRACE("OTG_%d   H_tot:%d  V_tot:%d   H_pos:%d  V_pos:%d\n",
				pipe_ctx->stream_res.tg->inst,
				pipe_ctx->stream->timing.h_total,
				pipe_ctx->stream->timing.v_total,
				h_pos[i], v_pos[i]);
	}
}

void context_clock_trace(
		struct dc *dc,
		struct dc_state *context)
{
#if defined(CONFIG_DRM_AMD_DC_DCN)
	DC_LOGGER_INIT(dc->ctx->logger);
	CLOCK_TRACE("Current: dispclk_khz:%d  max_dppclk_khz:%d  dcfclk_khz:%d\n"
			"dcfclk_deep_sleep_khz:%d  fclk_khz:%d  socclk_khz:%d\n",
			context->bw_ctx.bw.dcn.clk.dispclk_khz,
			context->bw_ctx.bw.dcn.clk.dppclk_khz,
			context->bw_ctx.bw.dcn.clk.dcfclk_khz,
			context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz,
			context->bw_ctx.bw.dcn.clk.fclk_khz,
			context->bw_ctx.bw.dcn.clk.socclk_khz);
	CLOCK_TRACE("Calculated: dispclk_khz:%d  max_dppclk_khz:%d  dcfclk_khz:%d\n"
			"dcfclk_deep_sleep_khz:%d  fclk_khz:%d  socclk_khz:%d\n",
			context->bw_ctx.bw.dcn.clk.dispclk_khz,
			context->bw_ctx.bw.dcn.clk.dppclk_khz,
			context->bw_ctx.bw.dcn.clk.dcfclk_khz,
			context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz,
			context->bw_ctx.bw.dcn.clk.fclk_khz,
			context->bw_ctx.bw.dcn.clk.socclk_khz);
#endif
}

/**
 * dc_status_to_str - convert dc_status to a human readable string
 * @status: dc_status to be converted
 *
 * Return:
 * A string describing the DC status.
 */
char *dc_status_to_str(enum dc_status status)
{
	switch (status) {
	case DC_OK:
		return "DC OK";
	case DC_NO_CONTROLLER_RESOURCE:
		return "No controller resource";
	case DC_NO_STREAM_ENC_RESOURCE:
		return "No stream encoder";
	case DC_NO_CLOCK_SOURCE_RESOURCE:
		return "No clock source";
	case DC_FAIL_CONTROLLER_VALIDATE:
		return "Controller validation failure";
	case DC_FAIL_ENC_VALIDATE:
		return "Encoder validation failure";
	case DC_FAIL_ATTACH_SURFACES:
		return "Surfaces attachment failure";
	case DC_FAIL_DETACH_SURFACES:
		return "Surfaces detachment failure";
	case DC_FAIL_SURFACE_VALIDATE:
		return "Surface validation failure";
	case DC_NO_DP_LINK_BANDWIDTH:
		return "No DP link bandwidth";
	case DC_EXCEED_DONGLE_CAP:
		return "Exceed dongle capability";
	case DC_SURFACE_PIXEL_FORMAT_UNSUPPORTED:
		return "Unsupported pixel format";
	case DC_FAIL_BANDWIDTH_VALIDATE:
		return "Bandwidth validation failure (BW and Watermark)";
	case DC_FAIL_SCALING:
		return "Scaling failure";
	case DC_FAIL_DP_LINK_TRAINING:
		return "DP link training failure";
	case DC_FAIL_DSC_VALIDATE:
		return "DSC validation failure";
	case DC_NO_DSC_RESOURCE:
		return "No DSC resource";
	case DC_FAIL_UNSUPPORTED_1:
		return "Unsupported";
	case DC_FAIL_CLK_EXCEED_MAX:
		return "Clk exceed max failure";
	case DC_FAIL_CLK_BELOW_MIN:
		return "Fail clk below minimum";
	case DC_FAIL_CLK_BELOW_CFG_REQUIRED:
		return "Fail clk below required CFG (hard_min in PPLIB)";
	case DC_ERROR_UNEXPECTED:
		return "Unexpected error";
	}

	return "Unexpected status error";
}

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
#include "hw_sequencer.h"

#include "resource.h"

#define SURFACE_TRACE(...) do {\
		if (dc->debug.surface_trace) \
			dm_logger_write(logger, \
					LOG_IF_TRACE, \
					##__VA_ARGS__); \
} while (0)

#define TIMING_TRACE(...) do {\
	if (dc->debug.timing_trace) \
		dm_logger_write(logger, \
				LOG_SYNC, \
				##__VA_ARGS__); \
} while (0)

void pre_surface_trace(
		const struct dc *dc,
		const struct dc_surface *const *surfaces,
		int surface_count)
{
	int i;
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dal_logger *logger =  core_dc->ctx->logger;

	for (i = 0; i < surface_count; i++) {
		const struct dc_surface *surface = surfaces[i];

		SURFACE_TRACE("Surface %d:\n", i);

		SURFACE_TRACE(
				"surface->visible = %d;\n"
				"surface->flip_immediate = %d;\n"
				"surface->address.type = %d;\n"
				"surface->address.grph.addr.quad_part = 0x%X;\n"
				"surface->address.grph.meta_addr.quad_part = 0x%X;\n"
				"surface->scaling_quality.h_taps = %d;\n"
				"surface->scaling_quality.v_taps = %d;\n"
				"surface->scaling_quality.h_taps_c = %d;\n"
				"surface->scaling_quality.v_taps_c = %d;\n",
				surface->visible,
				surface->flip_immediate,
				surface->address.type,
				surface->address.grph.addr.quad_part,
				surface->address.grph.meta_addr.quad_part,
				surface->scaling_quality.h_taps,
				surface->scaling_quality.v_taps,
				surface->scaling_quality.h_taps_c,
				surface->scaling_quality.v_taps_c);

		SURFACE_TRACE(
				"surface->src_rect.x = %d;\n"
				"surface->src_rect.y = %d;\n"
				"surface->src_rect.width = %d;\n"
				"surface->src_rect.height = %d;\n"
				"surface->dst_rect.x = %d;\n"
				"surface->dst_rect.y = %d;\n"
				"surface->dst_rect.width = %d;\n"
				"surface->dst_rect.height = %d;\n"
				"surface->clip_rect.x = %d;\n"
				"surface->clip_rect.y = %d;\n"
				"surface->clip_rect.width = %d;\n"
				"surface->clip_rect.height = %d;\n",
				surface->src_rect.x,
				surface->src_rect.y,
				surface->src_rect.width,
				surface->src_rect.height,
				surface->dst_rect.x,
				surface->dst_rect.y,
				surface->dst_rect.width,
				surface->dst_rect.height,
				surface->clip_rect.x,
				surface->clip_rect.y,
				surface->clip_rect.width,
				surface->clip_rect.height);

		SURFACE_TRACE(
				"surface->plane_size.grph.surface_size.x = %d;\n"
				"surface->plane_size.grph.surface_size.y = %d;\n"
				"surface->plane_size.grph.surface_size.width = %d;\n"
				"surface->plane_size.grph.surface_size.height = %d;\n"
				"surface->plane_size.grph.surface_pitch = %d;\n",
				surface->plane_size.grph.surface_size.x,
				surface->plane_size.grph.surface_size.y,
				surface->plane_size.grph.surface_size.width,
				surface->plane_size.grph.surface_size.height,
				surface->plane_size.grph.surface_pitch);


		SURFACE_TRACE(
				"surface->tiling_info.gfx8.num_banks = %d;\n"
				"surface->tiling_info.gfx8.bank_width = %d;\n"
				"surface->tiling_info.gfx8.bank_width_c = %d;\n"
				"surface->tiling_info.gfx8.bank_height = %d;\n"
				"surface->tiling_info.gfx8.bank_height_c = %d;\n"
				"surface->tiling_info.gfx8.tile_aspect = %d;\n"
				"surface->tiling_info.gfx8.tile_aspect_c = %d;\n"
				"surface->tiling_info.gfx8.tile_split = %d;\n"
				"surface->tiling_info.gfx8.tile_split_c = %d;\n"
				"surface->tiling_info.gfx8.tile_mode = %d;\n"
				"surface->tiling_info.gfx8.tile_mode_c = %d;\n",
				surface->tiling_info.gfx8.num_banks,
				surface->tiling_info.gfx8.bank_width,
				surface->tiling_info.gfx8.bank_width_c,
				surface->tiling_info.gfx8.bank_height,
				surface->tiling_info.gfx8.bank_height_c,
				surface->tiling_info.gfx8.tile_aspect,
				surface->tiling_info.gfx8.tile_aspect_c,
				surface->tiling_info.gfx8.tile_split,
				surface->tiling_info.gfx8.tile_split_c,
				surface->tiling_info.gfx8.tile_mode,
				surface->tiling_info.gfx8.tile_mode_c);

		SURFACE_TRACE(
				"surface->tiling_info.gfx8.pipe_config = %d;\n"
				"surface->tiling_info.gfx8.array_mode = %d;\n"
				"surface->color_space = %d;\n"
				"surface->dcc.enable = %d;\n"
				"surface->format = %d;\n"
				"surface->rotation = %d;\n"
				"surface->stereo_format = %d;\n",
				surface->tiling_info.gfx8.pipe_config,
				surface->tiling_info.gfx8.array_mode,
				surface->color_space,
				surface->dcc.enable,
				surface->format,
				surface->rotation,
				surface->stereo_format);
		SURFACE_TRACE("\n");
	}
	SURFACE_TRACE("\n");
}

void update_surface_trace(
		const struct dc *dc,
		const struct dc_surface_update *updates,
		int surface_count)
{
	int i;
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dal_logger *logger =  core_dc->ctx->logger;

	for (i = 0; i < surface_count; i++) {
		const struct dc_surface_update *update = &updates[i];

		SURFACE_TRACE("Update %d\n", i);
		if (update->flip_addr) {
			SURFACE_TRACE("flip_addr->address.type = %d;\n"
					"flip_addr->address.grph.addr.quad_part = 0x%X;\n"
					"flip_addr->address.grph.meta_addr.quad_part = 0x%X;\n"
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
					"plane_info->plane_size.grph.surface_pitch = %d;\n"
					"plane_info->plane_size.grph.surface_size.height = %d;\n"
					"plane_info->plane_size.grph.surface_size.width = %d;\n"
					"plane_info->plane_size.grph.surface_size.x = %d;\n"
					"plane_info->plane_size.grph.surface_size.y = %d;\n"
					"plane_info->rotation = %d;\n",
					update->plane_info->color_space,
					update->plane_info->format,
					update->plane_info->plane_size.grph.surface_pitch,
					update->plane_info->plane_size.grph.surface_size.height,
					update->plane_info->plane_size.grph.surface_size.width,
					update->plane_info->plane_size.grph.surface_size.x,
					update->plane_info->plane_size.grph.surface_size.y,
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
					"plane_info->visible = %d;\n",
					update->plane_info->tiling_info.gfx8.pipe_config,
					update->plane_info->tiling_info.gfx8.array_mode,
					update->plane_info->visible);
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

void post_surface_trace(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dal_logger *logger =  core_dc->ctx->logger;

	SURFACE_TRACE("post surface process.\n");

}

void context_timing_trace(
		const struct dc *dc,
		struct resource_context *res_ctx)
{
	int i;
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dal_logger *logger =  core_dc->ctx->logger;
	int h_pos[MAX_PIPES], v_pos[MAX_PIPES];

	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		pipe_ctx->tg->funcs->get_position(pipe_ctx->tg, &h_pos[i], &v_pos[i]);
	}
	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		TIMING_TRACE("OTG_%d   H_tot:%d  V_tot:%d   H_pos:%d  V_pos:%d\n",
				pipe_ctx->tg->inst,
				pipe_ctx->stream->public.timing.h_total,
				pipe_ctx->stream->public.timing.v_total,
				h_pos[i], v_pos[i]);
	}
}

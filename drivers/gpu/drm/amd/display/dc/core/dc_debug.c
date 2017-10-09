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

#define CLOCK_TRACE(...) do {\
	if (dc->debug.clock_trace) \
		dm_logger_write(logger, \
				LOG_BANDWIDTH_CALCS, \
				##__VA_ARGS__); \
} while (0)

void pre_surface_trace(
		struct dc *dc,
		const struct dc_plane_state *const *plane_states,
		int surface_count)
{
	int i;
	struct dc  *core_dc = dc;
	struct dal_logger *logger =  core_dc->ctx->logger;

	for (i = 0; i < surface_count; i++) {
		const struct dc_plane_state *plane_state = plane_states[i];

		SURFACE_TRACE("Planes %d:\n", i);

		SURFACE_TRACE(
				"plane_state->visible = %d;\n"
				"plane_state->flip_immediate = %d;\n"
				"plane_state->address.type = %d;\n"
				"plane_state->address.grph.addr.quad_part = 0x%X;\n"
				"plane_state->address.grph.meta_addr.quad_part = 0x%X;\n"
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
				"plane_state->plane_size.grph.surface_size.x = %d;\n"
				"plane_state->plane_size.grph.surface_size.y = %d;\n"
				"plane_state->plane_size.grph.surface_size.width = %d;\n"
				"plane_state->plane_size.grph.surface_size.height = %d;\n"
				"plane_state->plane_size.grph.surface_pitch = %d;\n",
				plane_state->plane_size.grph.surface_size.x,
				plane_state->plane_size.grph.surface_size.y,
				plane_state->plane_size.grph.surface_size.width,
				plane_state->plane_size.grph.surface_size.height,
				plane_state->plane_size.grph.surface_pitch);


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
	struct dc  *core_dc = dc;
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
	struct dc  *core_dc = dc;
	struct dal_logger *logger =  core_dc->ctx->logger;

	SURFACE_TRACE("post surface process.\n");

}

void context_timing_trace(
		struct dc *dc,
		struct resource_context *res_ctx)
{
	int i;
	struct dc  *core_dc = dc;
	struct dal_logger *logger =  core_dc->ctx->logger;
	int h_pos[MAX_PIPES], v_pos[MAX_PIPES];
	struct crtc_position position;
	unsigned int underlay_idx = core_dc->res_pool->underlay_pipe_index;


	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];
		/* get_position() returns CRTC vertical/horizontal counter
		 * hence not applicable for underlay pipe
		 */
		if (pipe_ctx->stream == NULL
				 || pipe_ctx->pipe_idx == underlay_idx)
			continue;

		pipe_ctx->stream_res.tg->funcs->get_position(pipe_ctx->stream_res.tg, &position);
		h_pos[i] = position.horizontal_count;
		v_pos[i] = position.vertical_count;
	}
	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
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
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	struct dc  *core_dc = dc;
	struct dal_logger *logger =  core_dc->ctx->logger;

	CLOCK_TRACE("Current: dispclk_khz:%d  dppclk_div:%d  dcfclk_khz:%d\n"
			"dcfclk_deep_sleep_khz:%d  fclk_khz:%d\n"
			"dram_ccm_us:%d  min_active_dram_ccm_us:%d\n",
			context->bw.dcn.calc_clk.dispclk_khz,
			context->bw.dcn.calc_clk.dppclk_div,
			context->bw.dcn.calc_clk.dcfclk_khz,
			context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz,
			context->bw.dcn.calc_clk.fclk_khz,
			context->bw.dcn.calc_clk.dram_ccm_us,
			context->bw.dcn.calc_clk.min_active_dram_ccm_us);
	CLOCK_TRACE("Calculated: dispclk_khz:%d  dppclk_div:%d  dcfclk_khz:%d\n"
			"dcfclk_deep_sleep_khz:%d  fclk_khz:%d\n"
			"dram_ccm_us:%d  min_active_dram_ccm_us:%d\n",
			context->bw.dcn.calc_clk.dispclk_khz,
			context->bw.dcn.calc_clk.dppclk_div,
			context->bw.dcn.calc_clk.dcfclk_khz,
			context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz,
			context->bw.dcn.calc_clk.fclk_khz,
			context->bw.dcn.calc_clk.dram_ccm_us,
			context->bw.dcn.calc_clk.min_active_dram_ccm_us);
#endif
}

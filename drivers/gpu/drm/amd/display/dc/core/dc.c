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

#include <linux/slab.h>
#include <linux/mm.h>

#include "dm_services.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"
#include "dce/dce_hwseq.h"

#include "resource.h"

#include "clk_mgr.h"
#include "clock_source.h"
#include "dc_bios_types.h"

#include "bios_parser_interface.h"
#include "include/irq_service_interface.h"
#include "transform.h"
#include "dmcu.h"
#include "dpp.h"
#include "timing_generator.h"
#include "abm.h"
#include "virtual/virtual_link_encoder.h"

#include "link_hwss.h"
#include "link_encoder.h"

#include "dc_link_ddc.h"
#include "dm_helpers.h"
#include "mem_input.h"
#include "hubp.h"

#include "dc_link_dp.h"
#include "dc_dmub_srv.h"

#include "dsc.h"

#include "vm_helper.h"

#include "dce/dce_i2c.h"

#include "dmub/dmub_srv.h"

#include "dce/dmub_hw_lock_mgr.h"

#define CTX \
	dc->ctx

#define DC_LOGGER \
	dc->ctx->logger

static const char DC_BUILD_ID[] = "production-build";

/**
 * DOC: Overview
 *
 * DC is the OS-agnostic component of the amdgpu DC driver.
 *
 * DC maintains and validates a set of structs representing the state of the
 * driver and writes that state to AMD hardware
 *
 * Main DC HW structs:
 *
 * struct dc - The central struct.  One per driver.  Created on driver load,
 * destroyed on driver unload.
 *
 * struct dc_context - One per driver.
 * Used as a backpointer by most other structs in dc.
 *
 * struct dc_link - One per connector (the physical DP, HDMI, miniDP, or eDP
 * plugpoints).  Created on driver load, destroyed on driver unload.
 *
 * struct dc_sink - One per display.  Created on boot or hotplug.
 * Destroyed on shutdown or hotunplug.  A dc_link can have a local sink
 * (the display directly attached).  It may also have one or more remote
 * sinks (in the Multi-Stream Transport case)
 *
 * struct resource_pool - One per driver.  Represents the hw blocks not in the
 * main pipeline.  Not directly accessible by dm.
 *
 * Main dc state structs:
 *
 * These structs can be created and destroyed as needed.  There is a full set of
 * these structs in dc->current_state representing the currently programmed state.
 *
 * struct dc_state - The global DC state to track global state information,
 * such as bandwidth values.
 *
 * struct dc_stream_state - Represents the hw configuration for the pipeline from
 * a framebuffer to a display.  Maps one-to-one with dc_sink.
 *
 * struct dc_plane_state - Represents a framebuffer.  Each stream has at least one,
 * and may have more in the Multi-Plane Overlay case.
 *
 * struct resource_context - Represents the programmable state of everything in
 * the resource_pool.  Not directly accessible by dm.
 *
 * struct pipe_ctx - A member of struct resource_context.  Represents the
 * internal hardware pipeline components.  Each dc_plane_state has either
 * one or two (in the pipe-split case).
 */

/*******************************************************************************
 * Private functions
 ******************************************************************************/

static inline void elevate_update_type(enum surface_update_type *original, enum surface_update_type new)
{
	if (new > *original)
		*original = new;
}

static void destroy_links(struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++) {
		if (NULL != dc->links[i])
			link_destroy(&dc->links[i]);
	}
}

static bool create_links(
		struct dc *dc,
		uint32_t num_virtual_links)
{
	int i;
	int connectors_num;
	struct dc_bios *bios = dc->ctx->dc_bios;

	dc->link_count = 0;

	connectors_num = bios->funcs->get_connectors_number(bios);

	if (connectors_num > ENUM_ID_COUNT) {
		dm_error(
			"DC: Number of connectors %d exceeds maximum of %d!\n",
			connectors_num,
			ENUM_ID_COUNT);
		return false;
	}

	dm_output_to_console(
		"DC: %s: connectors_num: physical:%d, virtual:%d\n",
		__func__,
		connectors_num,
		num_virtual_links);

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct dc_link *link;

		link_init_params.ctx = dc->ctx;
		/* next BIOS object table connector */
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link = link_create(&link_init_params);

		if (link) {
			bool should_destory_link = false;

			if (link->connector_signal == SIGNAL_TYPE_EDP) {
				if (dc->config.edp_not_connected) {
					if (!IS_DIAG_DC(dc->ctx->dce_environment))
						should_destory_link = true;
				} else {
					enum dc_connection_type type;
					dc_link_detect_sink(link, &type);
					if (type == dc_connection_none)
						should_destory_link = true;
				}
			}

			if (dc->config.force_enum_edp || !should_destory_link) {
				dc->links[dc->link_count] = link;
				link->dc = dc;
				++dc->link_count;
			} else {
				link_destroy(&link);
			}
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		struct dc_link *link = kzalloc(sizeof(*link), GFP_KERNEL);
		struct encoder_init_data enc_init = {0};

		if (link == NULL) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->link_index = dc->link_count;
		dc->links[dc->link_count] = link;
		dc->link_count++;

		link->ctx = dc->ctx;
		link->dc = dc;
		link->connector_signal = SIGNAL_TYPE_VIRTUAL;
		link->link_id.type = OBJECT_TYPE_CONNECTOR;
		link->link_id.id = CONNECTOR_ID_VIRTUAL;
		link->link_id.enum_id = ENUM_ID_1;
		link->link_enc = kzalloc(sizeof(*link->link_enc), GFP_KERNEL);

		if (!link->link_enc) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->link_status.dpcd_caps = &link->dpcd_caps;

		enc_init.ctx = dc->ctx;
		enc_init.channel = CHANNEL_ID_UNKNOWN;
		enc_init.hpd_source = HPD_SOURCEID_UNKNOWN;
		enc_init.transmitter = TRANSMITTER_UNKNOWN;
		enc_init.connector = link->link_id;
		enc_init.encoder.type = OBJECT_TYPE_ENCODER;
		enc_init.encoder.id = ENCODER_ID_INTERNAL_VIRTUAL;
		enc_init.encoder.enum_id = ENUM_ID_1;
		virtual_link_encoder_construct(link->link_enc, &enc_init);
	}

	return true;

failed_alloc:
	return false;
}

static struct dc_perf_trace *dc_perf_trace_create(void)
{
	return kzalloc(sizeof(struct dc_perf_trace), GFP_KERNEL);
}

static void dc_perf_trace_destroy(struct dc_perf_trace **perf_trace)
{
	kfree(*perf_trace);
	*perf_trace = NULL;
}

/**
 *****************************************************************************
 *  Function: dc_stream_adjust_vmin_vmax
 *
 *  @brief
 *     Looks up the pipe context of dc_stream_state and updates the
 *     vertical_total_min and vertical_total_max of the DRR, Dynamic Refresh
 *     Rate, which is a power-saving feature that targets reducing panel
 *     refresh rate while the screen is static
 *
 *  @param [in] dc: dc reference
 *  @param [in] stream: Initial dc stream state
 *  @param [in] adjust: Updated parameters for vertical_total_min and
 *  vertical_total_max
 *****************************************************************************
 */
bool dc_stream_adjust_vmin_vmax(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_crtc_timing_adjust *adjust)
{
	int i = 0;
	bool ret = false;

	stream->adjust = *adjust;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg) {
			dc->hwss.set_drr(&pipe,
					1,
					adjust->v_total_min,
					adjust->v_total_max,
					adjust->v_total_mid,
					adjust->v_total_mid_frame_num);

			ret = true;
		}
	}
	return ret;
}

bool dc_stream_get_crtc_position(struct dc *dc,
		struct dc_stream_state **streams, int num_streams,
		unsigned int *v_pos, unsigned int *nom_v_pos)
{
	/* TODO: Support multiple streams */
	const struct dc_stream_state *stream = streams[0];
	int i = 0;
	bool ret = false;
	struct crtc_position position;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.stream_enc) {
			dc->hwss.get_position(&pipe, 1, &position);

			*v_pos = position.vertical_count;
			*nom_v_pos = position.nominal_vcount;
			ret = true;
		}
	}
	return ret;
}

/**
 * dc_stream_configure_crc() - Configure CRC capture for the given stream.
 * @dc: DC Object
 * @stream: The stream to configure CRC on.
 * @enable: Enable CRC if true, disable otherwise.
 * @continuous: Capture CRC on every frame if true. Otherwise, only capture
 *              once.
 *
 * By default, only CRC0 is configured, and the entire frame is used to
 * calculate the crc.
 */
bool dc_stream_configure_crc(struct dc *dc, struct dc_stream_state *stream,
			     bool enable, bool continuous)
{
	int i;
	struct pipe_ctx *pipe;
	struct crc_params param;
	struct timing_generator *tg;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream == stream && !pipe->top_pipe && !pipe->prev_odm_pipe)
			break;
	}
	/* Stream not found */
	if (i == MAX_PIPES)
		return false;

	/* Always capture the full frame */
	param.windowa_x_start = 0;
	param.windowa_y_start = 0;
	param.windowa_x_end = pipe->stream->timing.h_addressable;
	param.windowa_y_end = pipe->stream->timing.v_addressable;
	param.windowb_x_start = 0;
	param.windowb_y_start = 0;
	param.windowb_x_end = pipe->stream->timing.h_addressable;
	param.windowb_y_end = pipe->stream->timing.v_addressable;

	param.dsc_mode = pipe->stream->timing.flags.DSC ? 1:0;
	param.odm_mode = pipe->next_odm_pipe ? 1:0;

	/* Default to the union of both windows */
	param.selection = UNION_WINDOW_A_B;
	param.continuous_mode = continuous;
	param.enable = enable;

	tg = pipe->stream_res.tg;

	/* Only call if supported */
	if (tg->funcs->configure_crc)
		return tg->funcs->configure_crc(tg, &param);
	DC_LOG_WARNING("CRC capture not supported.");
	return false;
}

/**
 * dc_stream_get_crc() - Get CRC values for the given stream.
 * @dc: DC object
 * @stream: The DC stream state of the stream to get CRCs from.
 * @r_cr, g_y, b_cb: CRC values for the three channels are stored here.
 *
 * dc_stream_configure_crc needs to be called beforehand to enable CRCs.
 * Return false if stream is not found, or if CRCs are not enabled.
 */
bool dc_stream_get_crc(struct dc *dc, struct dc_stream_state *stream,
		       uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb)
{
	int i;
	struct pipe_ctx *pipe;
	struct timing_generator *tg;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream == stream)
			break;
	}
	/* Stream not found */
	if (i == MAX_PIPES)
		return false;

	tg = pipe->stream_res.tg;

	if (tg->funcs->get_crc)
		return tg->funcs->get_crc(tg, r_cr, g_y, b_cb);
	DC_LOG_WARNING("CRC capture not supported.");
	return false;
}

void dc_stream_set_dyn_expansion(struct dc *dc, struct dc_stream_state *stream,
		enum dc_dynamic_expansion option)
{
	/* OPP FMT dyn expansion updates*/
	int i = 0;
	struct pipe_ctx *pipe_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream
				== stream) {
			pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
			pipe_ctx->stream_res.opp->dyn_expansion = option;
			pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
					pipe_ctx->stream_res.opp,
					COLOR_SPACE_YCBCR601,
					stream->timing.display_color_depth,
					stream->signal);
		}
	}
}

void dc_stream_set_dither_option(struct dc_stream_state *stream,
		enum dc_dither_option option)
{
	struct bit_depth_reduction_params params;
	struct dc_link *link = stream->link;
	struct pipe_ctx *pipes = NULL;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (link->dc->current_state->res_ctx.pipe_ctx[i].stream ==
				stream) {
			pipes = &link->dc->current_state->res_ctx.pipe_ctx[i];
			break;
		}
	}

	if (!pipes)
		return;
	if (option > DITHER_OPTION_MAX)
		return;

	stream->dither_option = option;

	memset(&params, 0, sizeof(params));
	resource_build_bit_depth_reduction_params(stream, &params);
	stream->bit_depth_params = params;

	if (pipes->plane_res.xfm &&
	    pipes->plane_res.xfm->funcs->transform_set_pixel_storage_depth) {
		pipes->plane_res.xfm->funcs->transform_set_pixel_storage_depth(
			pipes->plane_res.xfm,
			pipes->plane_res.scl_data.lb_params.depth,
			&stream->bit_depth_params);
	}

	pipes->stream_res.opp->funcs->
		opp_program_bit_depth_reduction(pipes->stream_res.opp, &params);
}

bool dc_stream_set_gamut_remap(struct dc *dc, const struct dc_stream_state *stream)
{
	int i = 0;
	bool ret = false;
	struct pipe_ctx *pipes;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream == stream) {
			pipes = &dc->current_state->res_ctx.pipe_ctx[i];
			dc->hwss.program_gamut_remap(pipes);
			ret = true;
		}
	}

	return ret;
}

bool dc_stream_program_csc_matrix(struct dc *dc, struct dc_stream_state *stream)
{
	int i = 0;
	bool ret = false;
	struct pipe_ctx *pipes;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream
				== stream) {

			pipes = &dc->current_state->res_ctx.pipe_ctx[i];
			dc->hwss.program_output_csc(dc,
					pipes,
					stream->output_color_space,
					stream->csc_color_matrix.matrix,
					pipes->stream_res.opp->inst);
			ret = true;
		}
	}

	return ret;
}

void dc_stream_set_static_screen_params(struct dc *dc,
		struct dc_stream_state **streams,
		int num_streams,
		const struct dc_static_screen_params *params)
{
	int i = 0;
	int j = 0;
	struct pipe_ctx *pipes_affected[MAX_PIPES];
	int num_pipes_affected = 0;

	for (i = 0; i < num_streams; i++) {
		struct dc_stream_state *stream = streams[i];

		for (j = 0; j < MAX_PIPES; j++) {
			if (dc->current_state->res_ctx.pipe_ctx[j].stream
					== stream) {
				pipes_affected[num_pipes_affected++] =
						&dc->current_state->res_ctx.pipe_ctx[j];
			}
		}
	}

	dc->hwss.set_static_screen_control(pipes_affected, num_pipes_affected, params);
}

static void dc_destruct(struct dc *dc)
{
	if (dc->current_state) {
		dc_release_state(dc->current_state);
		dc->current_state = NULL;
	}

	destroy_links(dc);

	if (dc->clk_mgr) {
		dc_destroy_clk_mgr(dc->clk_mgr);
		dc->clk_mgr = NULL;
	}

	dc_destroy_resource_pool(dc);

	if (dc->ctx->gpio_service)
		dal_gpio_service_destroy(&dc->ctx->gpio_service);

	if (dc->ctx->created_bios)
		dal_bios_parser_destroy(&dc->ctx->dc_bios);

	dc_perf_trace_destroy(&dc->ctx->perf_trace);

	kfree(dc->ctx);
	dc->ctx = NULL;

	kfree(dc->bw_vbios);
	dc->bw_vbios = NULL;

	kfree(dc->bw_dceip);
	dc->bw_dceip = NULL;

#ifdef CONFIG_DRM_AMD_DC_DCN
	kfree(dc->dcn_soc);
	dc->dcn_soc = NULL;

	kfree(dc->dcn_ip);
	dc->dcn_ip = NULL;

#endif
	kfree(dc->vm_helper);
	dc->vm_helper = NULL;

}

static bool dc_construct_ctx(struct dc *dc,
		const struct dc_init_data *init_params)
{
	struct dc_context *dc_ctx;
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;

	dc_ctx = kzalloc(sizeof(*dc_ctx), GFP_KERNEL);
	if (!dc_ctx)
		return false;

	dc_ctx->cgs_device = init_params->cgs_device;
	dc_ctx->driver_context = init_params->driver;
	dc_ctx->dc = dc;
	dc_ctx->asic_id = init_params->asic_id;
	dc_ctx->dc_sink_id_count = 0;
	dc_ctx->dc_stream_id_count = 0;
	dc_ctx->dce_environment = init_params->dce_environment;

	/* Create logger */

	dc_version = resource_parse_asic_id(init_params->asic_id);
	dc_ctx->dce_version = dc_version;

	dc_ctx->perf_trace = dc_perf_trace_create();
	if (!dc_ctx->perf_trace) {
		ASSERT_CRITICAL(false);
		return false;
	}

	dc->ctx = dc_ctx;

	return true;
}

static bool dc_construct(struct dc *dc,
		const struct dc_init_data *init_params)
{
	struct dc_context *dc_ctx;
	struct bw_calcs_dceip *dc_dceip;
	struct bw_calcs_vbios *dc_vbios;
#ifdef CONFIG_DRM_AMD_DC_DCN
	struct dcn_soc_bounding_box *dcn_soc;
	struct dcn_ip_params *dcn_ip;
#endif

	dc->config = init_params->flags;

	// Allocate memory for the vm_helper
	dc->vm_helper = kzalloc(sizeof(struct vm_helper), GFP_KERNEL);
	if (!dc->vm_helper) {
		dm_error("%s: failed to create dc->vm_helper\n", __func__);
		goto fail;
	}

	memcpy(&dc->bb_overrides, &init_params->bb_overrides, sizeof(dc->bb_overrides));

	dc_dceip = kzalloc(sizeof(*dc_dceip), GFP_KERNEL);
	if (!dc_dceip) {
		dm_error("%s: failed to create dceip\n", __func__);
		goto fail;
	}

	dc->bw_dceip = dc_dceip;

	dc_vbios = kzalloc(sizeof(*dc_vbios), GFP_KERNEL);
	if (!dc_vbios) {
		dm_error("%s: failed to create vbios\n", __func__);
		goto fail;
	}

	dc->bw_vbios = dc_vbios;
#ifdef CONFIG_DRM_AMD_DC_DCN
	dcn_soc = kzalloc(sizeof(*dcn_soc), GFP_KERNEL);
	if (!dcn_soc) {
		dm_error("%s: failed to create dcn_soc\n", __func__);
		goto fail;
	}

	dc->dcn_soc = dcn_soc;

	dcn_ip = kzalloc(sizeof(*dcn_ip), GFP_KERNEL);
	if (!dcn_ip) {
		dm_error("%s: failed to create dcn_ip\n", __func__);
		goto fail;
	}

	dc->dcn_ip = dcn_ip;
	dc->soc_bounding_box = init_params->soc_bounding_box;
#endif

	if (!dc_construct_ctx(dc, init_params)) {
		dm_error("%s: failed to create ctx\n", __func__);
		goto fail;
	}

        dc_ctx = dc->ctx;

	/* Resource should construct all asic specific resources.
	 * This should be the only place where we need to parse the asic id
	 */
	if (init_params->vbios_override)
		dc_ctx->dc_bios = init_params->vbios_override;
	else {
		/* Create BIOS parser */
		struct bp_init_data bp_init_data;

		bp_init_data.ctx = dc_ctx;
		bp_init_data.bios = init_params->asic_id.atombios_base_address;

		dc_ctx->dc_bios = dal_bios_parser_create(
				&bp_init_data, dc_ctx->dce_version);

		if (!dc_ctx->dc_bios) {
			ASSERT_CRITICAL(false);
			goto fail;
		}

		dc_ctx->created_bios = true;
	}

	dc->vendor_signature = init_params->vendor_signature;

	/* Create GPIO service */
	dc_ctx->gpio_service = dal_gpio_service_create(
			dc_ctx->dce_version,
			dc_ctx->dce_environment,
			dc_ctx);

	if (!dc_ctx->gpio_service) {
		ASSERT_CRITICAL(false);
		goto fail;
	}

	dc->res_pool = dc_create_resource_pool(dc, init_params, dc_ctx->dce_version);
	if (!dc->res_pool)
		goto fail;

	dc->clk_mgr = dc_clk_mgr_create(dc->ctx, dc->res_pool->pp_smu, dc->res_pool->dccg);
	if (!dc->clk_mgr)
		goto fail;
#ifdef CONFIG_DRM_AMD_DC_DCN3_0
	dc->clk_mgr->force_smu_not_present = init_params->force_smu_not_present;
#endif

	dc->debug.force_ignore_link_settings = init_params->force_ignore_link_settings;

	if (dc->res_pool->funcs->update_bw_bounding_box)
		dc->res_pool->funcs->update_bw_bounding_box(dc, dc->clk_mgr->bw_params);

	/* Creation of current_state must occur after dc->dml
	 * is initialized in dc_create_resource_pool because
	 * on creation it copies the contents of dc->dml
	 */

	dc->current_state = dc_create_state(dc);

	if (!dc->current_state) {
		dm_error("%s: failed to create validate ctx\n", __func__);
		goto fail;
	}

	dc_resource_state_construct(dc, dc->current_state);

	if (!create_links(dc, init_params->num_virtual_links))
		goto fail;

	return true;

fail:
	return false;
}

static bool disable_all_writeback_pipes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *context)
{
	int i;

	for (i = 0; i < stream->num_wb_info; i++)
		stream->writeback_info[i].wb_enabled = false;

	return true;
}

void apply_ctx_interdependent_lock(struct dc *dc, struct dc_state *context, struct dc_stream_state *stream, bool lock)
{
	int i = 0;

	/* Checks if interdependent update function pointer is NULL or not, takes care of DCE110 case */
	if (dc->hwss.interdependent_update_lock)
		dc->hwss.interdependent_update_lock(dc, context, lock);
	else {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

			// Copied conditions that were previously in dce110_apply_ctx_for_surface
			if (stream == pipe_ctx->stream) {
				if (!pipe_ctx->top_pipe &&
					(pipe_ctx->plane_state || old_pipe_ctx->plane_state))
					dc->hwss.pipe_control_lock(dc, pipe_ctx, lock);
			}
		}
	}
}

static void disable_dangling_plane(struct dc *dc, struct dc_state *context)
{
	int i, j;
	struct dc_state *dangling_context = dc_create_state(dc);
	struct dc_state *current_ctx;

	if (dangling_context == NULL)
		return;

	dc_resource_state_copy_construct(dc->current_state, dangling_context);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_stream_state *old_stream =
				dc->current_state->res_ctx.pipe_ctx[i].stream;
		bool should_disable = true;

		for (j = 0; j < context->stream_count; j++) {
			if (old_stream == context->streams[j]) {
				should_disable = false;
				break;
			}
		}
		if (should_disable && old_stream) {
			dc_rem_all_planes_for_stream(dc, old_stream, dangling_context);
			disable_all_writeback_pipes_for_stream(dc, old_stream, dangling_context);

			if (dc->hwss.apply_ctx_for_surface) {
				apply_ctx_interdependent_lock(dc, dc->current_state, old_stream, true);
				dc->hwss.apply_ctx_for_surface(dc, old_stream, 0, dangling_context);
				apply_ctx_interdependent_lock(dc, dc->current_state, old_stream, false);
				dc->hwss.post_unlock_program_front_end(dc, dangling_context);
			}
			if (dc->hwss.program_front_end_for_ctx) {
				dc->hwss.interdependent_update_lock(dc, dc->current_state, true);
				dc->hwss.program_front_end_for_ctx(dc, dangling_context);
				dc->hwss.interdependent_update_lock(dc, dc->current_state, false);
				dc->hwss.post_unlock_program_front_end(dc, dangling_context);
			}
		}
	}

	current_ctx = dc->current_state;
	dc->current_state = dangling_context;
	dc_release_state(current_ctx);
}

static void disable_vbios_mode_if_required(
		struct dc *dc,
		struct dc_state *context)
{
	unsigned int i, j;

	/* check if timing_changed, disable stream*/
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_stream_state *stream = NULL;
		struct dc_link *link = NULL;
		struct pipe_ctx *pipe = NULL;

		pipe = &context->res_ctx.pipe_ctx[i];
		stream = pipe->stream;
		if (stream == NULL)
			continue;

		if (stream->link->local_sink &&
			stream->link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
			link = stream->link;
		}

		if (link != NULL) {
			unsigned int enc_inst, tg_inst = 0;
			unsigned int pix_clk_100hz;

			enc_inst = link->link_enc->funcs->get_dig_frontend(link->link_enc);
			if (enc_inst != ENGINE_ID_UNKNOWN) {
				for (j = 0; j < dc->res_pool->stream_enc_count; j++) {
					if (dc->res_pool->stream_enc[j]->id == enc_inst) {
						tg_inst = dc->res_pool->stream_enc[j]->funcs->dig_source_otg(
							dc->res_pool->stream_enc[j]);
						break;
					}
				}

				dc->res_pool->dp_clock_source->funcs->get_pixel_clk_frequency_100hz(
					dc->res_pool->dp_clock_source,
					tg_inst, &pix_clk_100hz);

				if (link->link_status.link_active) {
					uint32_t requested_pix_clk_100hz =
						pipe->stream_res.pix_clk_params.requested_pix_clk_100hz;

					if (pix_clk_100hz != requested_pix_clk_100hz) {
						core_link_disable_stream(pipe);
						pipe->stream->dpms_off = false;
					}
				}
			}
		}
	}
}

static void wait_for_no_pipes_pending(struct dc *dc, struct dc_state *context)
{
	int i;
	PERF_TRACE();
	for (i = 0; i < MAX_PIPES; i++) {
		int count = 0;
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->plane_state)
			continue;

		/* Timeout 100 ms */
		while (count < 100000) {
			/* Must set to false to start with, due to OR in update function */
			pipe->plane_state->status.is_flip_pending = false;
			dc->hwss.update_pending_status(pipe);
			if (!pipe->plane_state->status.is_flip_pending)
				break;
			udelay(1);
			count++;
		}
		ASSERT(!pipe->plane_state->status.is_flip_pending);
	}
	PERF_TRACE();
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

struct dc *dc_create(const struct dc_init_data *init_params)
{
	struct dc *dc = kzalloc(sizeof(*dc), GFP_KERNEL);
	unsigned int full_pipe_count;

	if (NULL == dc)
		goto alloc_fail;

	if (init_params->dce_environment == DCE_ENV_VIRTUAL_HW) {
		if (false == dc_construct_ctx(dc, init_params)) {
			dc_destruct(dc);
			goto construct_fail;
		}
	} else {
		if (false == dc_construct(dc, init_params)) {
			dc_destruct(dc);
			goto construct_fail;
		}

		full_pipe_count = dc->res_pool->pipe_count;
		if (dc->res_pool->underlay_pipe_index != NO_UNDERLAY_PIPE)
			full_pipe_count--;
		dc->caps.max_streams = min(
				full_pipe_count,
				dc->res_pool->stream_enc_count);

		dc->optimize_seamless_boot_streams = 0;
		dc->caps.max_links = dc->link_count;
		dc->caps.max_audios = dc->res_pool->audio_count;
		dc->caps.linear_pitch_alignment = 64;

		dc->caps.max_dp_protocol_version = DP_VERSION_1_4;

		if (dc->res_pool->dmcu != NULL)
			dc->versions.dmcu_version = dc->res_pool->dmcu->dmcu_version;
	}

	/* Populate versioning information */
	dc->versions.dc_ver = DC_VER;

	dc->build_id = DC_BUILD_ID;

	DC_LOG_DC("Display Core initialized\n");



	return dc;

construct_fail:
	kfree(dc);

alloc_fail:
	return NULL;
}

void dc_hardware_init(struct dc *dc)
{
	if (dc->ctx->dce_environment != DCE_ENV_VIRTUAL_HW)
		dc->hwss.init_hw(dc);
}

void dc_init_callbacks(struct dc *dc,
		const struct dc_callback_init *init_params)
{
#ifdef CONFIG_DRM_AMD_DC_HDCP
	dc->ctx->cp_psp = init_params->cp_psp;
#endif
}

void dc_deinit_callbacks(struct dc *dc)
{
#ifdef CONFIG_DRM_AMD_DC_HDCP
	memset(&dc->ctx->cp_psp, 0, sizeof(dc->ctx->cp_psp));
#endif
}

void dc_destroy(struct dc **dc)
{
	dc_destruct(*dc);
	kfree(*dc);
	*dc = NULL;
}

static void enable_timing_multisync(
		struct dc *dc,
		struct dc_state *ctx)
{
	int i = 0, multisync_count = 0;
	int pipe_count = dc->res_pool->pipe_count;
	struct pipe_ctx *multisync_pipes[MAX_PIPES] = { NULL };

	for (i = 0; i < pipe_count; i++) {
		if (!ctx->res_ctx.pipe_ctx[i].stream ||
				!ctx->res_ctx.pipe_ctx[i].stream->triggered_crtc_reset.enabled)
			continue;
		if (ctx->res_ctx.pipe_ctx[i].stream == ctx->res_ctx.pipe_ctx[i].stream->triggered_crtc_reset.event_source)
			continue;
		multisync_pipes[multisync_count] = &ctx->res_ctx.pipe_ctx[i];
		multisync_count++;
	}

	if (multisync_count > 0) {
		dc->hwss.enable_per_frame_crtc_position_reset(
			dc, multisync_count, multisync_pipes);
	}
}

static void program_timing_sync(
		struct dc *dc,
		struct dc_state *ctx)
{
	int i, j, k;
	int group_index = 0;
	int num_group = 0;
	int pipe_count = dc->res_pool->pipe_count;
	struct pipe_ctx *unsynced_pipes[MAX_PIPES] = { NULL };

	for (i = 0; i < pipe_count; i++) {
		if (!ctx->res_ctx.pipe_ctx[i].stream || ctx->res_ctx.pipe_ctx[i].top_pipe)
			continue;

		unsynced_pipes[i] = &ctx->res_ctx.pipe_ctx[i];
	}

	for (i = 0; i < pipe_count; i++) {
		int group_size = 1;
		struct pipe_ctx *pipe_set[MAX_PIPES];

		if (!unsynced_pipes[i])
			continue;

		pipe_set[0] = unsynced_pipes[i];
		unsynced_pipes[i] = NULL;

		/* Add tg to the set, search rest of the tg's for ones with
		 * same timing, add all tgs with same timing to the group
		 */
		for (j = i + 1; j < pipe_count; j++) {
			if (!unsynced_pipes[j])
				continue;

			if (resource_are_streams_timing_synchronizable(
					unsynced_pipes[j]->stream,
					pipe_set[0]->stream)) {
				pipe_set[group_size] = unsynced_pipes[j];
				unsynced_pipes[j] = NULL;
				group_size++;
			}
		}

		/* set first unblanked pipe as master */
		for (j = 0; j < group_size; j++) {
			bool is_blanked;

			if (pipe_set[j]->stream_res.opp->funcs->dpg_is_blanked)
				is_blanked =
					pipe_set[j]->stream_res.opp->funcs->dpg_is_blanked(pipe_set[j]->stream_res.opp);
			else
				is_blanked =
					pipe_set[j]->stream_res.tg->funcs->is_blanked(pipe_set[j]->stream_res.tg);
			if (!is_blanked) {
				if (j == 0)
					break;

				swap(pipe_set[0], pipe_set[j]);
				break;
			}
		}


		for (k = 0; k < group_size; k++) {
			struct dc_stream_status *status = dc_stream_get_status_from_state(ctx, pipe_set[k]->stream);

			status->timing_sync_info.group_id = num_group;
			status->timing_sync_info.group_size = group_size;
			if (k == 0)
				status->timing_sync_info.master = true;
			else
				status->timing_sync_info.master = false;

		}
		/* remove any other unblanked pipes as they have already been synced */
		for (j = j + 1; j < group_size; j++) {
			bool is_blanked;

			if (pipe_set[j]->stream_res.opp->funcs->dpg_is_blanked)
				is_blanked =
					pipe_set[j]->stream_res.opp->funcs->dpg_is_blanked(pipe_set[j]->stream_res.opp);
			else
				is_blanked =
					pipe_set[j]->stream_res.tg->funcs->is_blanked(pipe_set[j]->stream_res.tg);
			if (!is_blanked) {
				group_size--;
				pipe_set[j] = pipe_set[group_size];
				j--;
			}
		}

		if (group_size > 1) {
			dc->hwss.enable_timing_synchronization(
				dc, group_index, group_size, pipe_set);
			group_index++;
		}
		num_group++;
	}
}

static bool context_changed(
		struct dc *dc,
		struct dc_state *context)
{
	uint8_t i;

	if (context->stream_count != dc->current_state->stream_count)
		return true;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		if (dc->current_state->streams[i] != context->streams[i])
			return true;
	}

	return false;
}

bool dc_validate_seamless_boot_timing(const struct dc *dc,
				const struct dc_sink *sink,
				struct dc_crtc_timing *crtc_timing)
{
	struct timing_generator *tg;
	struct stream_encoder *se = NULL;

	struct dc_crtc_timing hw_crtc_timing = {0};

	struct dc_link *link = sink->link;
	unsigned int i, enc_inst, tg_inst = 0;

	// Seamless port only support single DP and EDP so far
	if (sink->sink_signal != SIGNAL_TYPE_DISPLAY_PORT &&
		sink->sink_signal != SIGNAL_TYPE_EDP)
		return false;

	/* Check for enabled DIG to identify enabled display */
	if (!link->link_enc->funcs->is_dig_enabled(link->link_enc))
		return false;

	enc_inst = link->link_enc->funcs->get_dig_frontend(link->link_enc);

	if (enc_inst == ENGINE_ID_UNKNOWN)
		return false;

	for (i = 0; i < dc->res_pool->stream_enc_count; i++) {
		if (dc->res_pool->stream_enc[i]->id == enc_inst) {

			se = dc->res_pool->stream_enc[i];

			tg_inst = dc->res_pool->stream_enc[i]->funcs->dig_source_otg(
				dc->res_pool->stream_enc[i]);
			break;
		}
	}

	// tg_inst not found
	if (i == dc->res_pool->stream_enc_count)
		return false;

	if (tg_inst >= dc->res_pool->timing_generator_count)
		return false;

	tg = dc->res_pool->timing_generators[tg_inst];

	if (!tg->funcs->get_hw_timing)
		return false;

	if (!tg->funcs->get_hw_timing(tg, &hw_crtc_timing))
		return false;

	if (crtc_timing->h_total != hw_crtc_timing.h_total)
		return false;

	if (crtc_timing->h_border_left != hw_crtc_timing.h_border_left)
		return false;

	if (crtc_timing->h_addressable != hw_crtc_timing.h_addressable)
		return false;

	if (crtc_timing->h_border_right != hw_crtc_timing.h_border_right)
		return false;

	if (crtc_timing->h_front_porch != hw_crtc_timing.h_front_porch)
		return false;

	if (crtc_timing->h_sync_width != hw_crtc_timing.h_sync_width)
		return false;

	if (crtc_timing->v_total != hw_crtc_timing.v_total)
		return false;

	if (crtc_timing->v_border_top != hw_crtc_timing.v_border_top)
		return false;

	if (crtc_timing->v_addressable != hw_crtc_timing.v_addressable)
		return false;

	if (crtc_timing->v_border_bottom != hw_crtc_timing.v_border_bottom)
		return false;

	if (crtc_timing->v_front_porch != hw_crtc_timing.v_front_porch)
		return false;

	if (crtc_timing->v_sync_width != hw_crtc_timing.v_sync_width)
		return false;

	if (dc_is_dp_signal(link->connector_signal)) {
		unsigned int pix_clk_100hz;

		dc->res_pool->dp_clock_source->funcs->get_pixel_clk_frequency_100hz(
			dc->res_pool->dp_clock_source,
			tg_inst, &pix_clk_100hz);

		if (crtc_timing->pix_clk_100hz != pix_clk_100hz)
			return false;

		if (!se->funcs->dp_get_pixel_format)
			return false;

		if (!se->funcs->dp_get_pixel_format(
			se,
			&hw_crtc_timing.pixel_encoding,
			&hw_crtc_timing.display_color_depth))
			return false;

		if (hw_crtc_timing.display_color_depth != crtc_timing->display_color_depth)
			return false;

		if (hw_crtc_timing.pixel_encoding != crtc_timing->pixel_encoding)
			return false;
	}

	return true;
}

bool dc_enable_stereo(
	struct dc *dc,
	struct dc_state *context,
	struct dc_stream_state *streams[],
	uint8_t stream_count)
{
	bool ret = true;
	int i, j;
	struct pipe_ctx *pipe;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context != NULL)
			pipe = &context->res_ctx.pipe_ctx[i];
		else
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		for (j = 0 ; pipe && j < stream_count; j++)  {
			if (streams[j] && streams[j] == pipe->stream &&
				dc->hwss.setup_stereo)
				dc->hwss.setup_stereo(pipe, dc);
		}
	}

	return ret;
}

void dc_trigger_sync(struct dc *dc, struct dc_state *context)
{
	if (context->stream_count > 1 && !dc->debug.disable_timing_sync) {
		enable_timing_multisync(dc, context);
		program_timing_sync(dc, context);
	}
}

static uint8_t get_stream_mask(struct dc *dc, struct dc_state *context)
{
	int i;
	unsigned int stream_mask = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].stream)
			stream_mask |= 1 << i;
	}

	return stream_mask;
}

/*
 * Applies given context to HW and copy it into current context.
 * It's up to the user to release the src context afterwards.
 */
static enum dc_status dc_commit_state_no_check(struct dc *dc, struct dc_state *context)
{
	struct dc_bios *dcb = dc->ctx->dc_bios;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct pipe_ctx *pipe;
	int i, k, l;
	struct dc_stream_state *dc_streams[MAX_STREAMS] = {0};

#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
	dc_allow_idle_optimizations(dc, false);
#endif

	for (i = 0; i < context->stream_count; i++)
		dc_streams[i] =  context->streams[i];

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		disable_vbios_mode_if_required(dc, context);
		dc->hwss.enable_accelerated_mode(dc, context);
	}

	for (i = 0; i < context->stream_count; i++)
		if (context->streams[i]->apply_seamless_boot_optimization)
			dc->optimize_seamless_boot_streams++;

	if (context->stream_count > dc->optimize_seamless_boot_streams ||
		context->stream_count == 0)
		dc->hwss.prepare_bandwidth(dc, context);

	disable_dangling_plane(dc, context);
	/* re-program planes for existing stream, in case we need to
	 * free up plane resource for later use
	 */
	if (dc->hwss.apply_ctx_for_surface) {
		for (i = 0; i < context->stream_count; i++) {
			if (context->streams[i]->mode_changed)
				continue;
			apply_ctx_interdependent_lock(dc, context, context->streams[i], true);
			dc->hwss.apply_ctx_for_surface(
				dc, context->streams[i],
				context->stream_status[i].plane_count,
				context); /* use new pipe config in new context */
			apply_ctx_interdependent_lock(dc, context, context->streams[i], false);
			dc->hwss.post_unlock_program_front_end(dc, context);
		}
	}

	/* Program hardware */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, pipe);
	}

	result = dc->hwss.apply_ctx_to_hw(dc, context);

	if (result != DC_OK)
		return result;

	dc_trigger_sync(dc, context);

	/* Program all planes within new context*/
	if (dc->hwss.program_front_end_for_ctx) {
		dc->hwss.interdependent_update_lock(dc, context, true);
		dc->hwss.program_front_end_for_ctx(dc, context);
		dc->hwss.interdependent_update_lock(dc, context, false);
		dc->hwss.post_unlock_program_front_end(dc, context);
	}
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_link *link = context->streams[i]->link;

		if (!context->streams[i]->mode_changed)
			continue;

		if (dc->hwss.apply_ctx_for_surface) {
			apply_ctx_interdependent_lock(dc, context, context->streams[i], true);
			dc->hwss.apply_ctx_for_surface(
					dc, context->streams[i],
					context->stream_status[i].plane_count,
					context);
			apply_ctx_interdependent_lock(dc, context, context->streams[i], false);
			dc->hwss.post_unlock_program_front_end(dc, context);
		}

		/*
		 * enable stereo
		 * TODO rework dc_enable_stereo call to work with validation sets?
		 */
		for (k = 0; k < MAX_PIPES; k++) {
			pipe = &context->res_ctx.pipe_ctx[k];

			for (l = 0 ; pipe && l < context->stream_count; l++)  {
				if (context->streams[l] &&
					context->streams[l] == pipe->stream &&
					dc->hwss.setup_stereo)
					dc->hwss.setup_stereo(pipe, dc);
			}
		}

		CONN_MSG_MODE(link, "{%dx%d, %dx%d@%dKhz}",
				context->streams[i]->timing.h_addressable,
				context->streams[i]->timing.v_addressable,
				context->streams[i]->timing.h_total,
				context->streams[i]->timing.v_total,
				context->streams[i]->timing.pix_clk_100hz / 10);
	}

	dc_enable_stereo(dc, context, dc_streams, context->stream_count);

	if (context->stream_count > dc->optimize_seamless_boot_streams ||
		context->stream_count == 0) {
		/* Must wait for no flips to be pending before doing optimize bw */
		wait_for_no_pipes_pending(dc, context);
		/* pplib is notified if disp_num changed */
		dc->hwss.optimize_bandwidth(dc, context);
	}

	context->stream_mask = get_stream_mask(dc, context);

	if (context->stream_mask != dc->current_state->stream_mask)
		dc_dmub_srv_notify_stream_mask(dc->ctx->dmub_srv, context->stream_mask);

	for (i = 0; i < context->stream_count; i++)
		context->streams[i]->mode_changed = false;

	dc_release_state(dc->current_state);

	dc->current_state = context;

	dc_retain_state(dc->current_state);

	return result;
}

bool dc_commit_state(struct dc *dc, struct dc_state *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	int i;

	if (false == context_changed(dc, context))
		return DC_OK;

	DC_LOG_DC("%s: %d streams\n",
				__func__, context->stream_count);

	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];

		dc_stream_log(dc, stream);
	}

	result = dc_commit_state_no_check(dc, context);

	return (result == DC_OK);
}

#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
bool dc_acquire_release_mpc_3dlut(
		struct dc *dc, bool acquire,
		struct dc_stream_state *stream,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper)
{
	int pipe_idx;
	bool ret = false;
	bool found_pipe_idx = false;
	const struct resource_pool *pool = dc->res_pool;
	struct resource_context *res_ctx = &dc->current_state->res_ctx;
	int mpcc_id = 0;

	if (pool && res_ctx) {
		if (acquire) {
			/*find pipe idx for the given stream*/
			for (pipe_idx = 0; pipe_idx < pool->pipe_count; pipe_idx++) {
				if (res_ctx->pipe_ctx[pipe_idx].stream == stream) {
					found_pipe_idx = true;
					mpcc_id = res_ctx->pipe_ctx[pipe_idx].plane_res.hubp->inst;
					break;
				}
			}
		} else
			found_pipe_idx = true;/*for release pipe_idx is not required*/

		if (found_pipe_idx) {
			if (acquire && pool->funcs->acquire_post_bldn_3dlut)
				ret = pool->funcs->acquire_post_bldn_3dlut(res_ctx, pool, mpcc_id, lut, shaper);
			else if (acquire == false && pool->funcs->release_post_bldn_3dlut)
				ret = pool->funcs->release_post_bldn_3dlut(res_ctx, pool, lut, shaper);
		}
	}
	return ret;
}
#endif
static bool is_flip_pending_in_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	struct pipe_ctx *pipe;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->plane_state)
			continue;

		/* Must set to false to start with, due to OR in update function */
		pipe->plane_state->status.is_flip_pending = false;
		dc->hwss.update_pending_status(pipe);
		if (pipe->plane_state->status.is_flip_pending)
			return true;
	}
	return false;
}

bool dc_post_update_surfaces_to_stream(struct dc *dc)
{
	int i;
	struct dc_state *context = dc->current_state;

	if ((!dc->optimized_required) || dc->optimize_seamless_boot_streams > 0)
		return true;

	post_surface_trace(dc);

	if (is_flip_pending_in_pipes(dc, context))
		return true;

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].stream == NULL ||
		    context->res_ctx.pipe_ctx[i].plane_state == NULL) {
			context->res_ctx.pipe_ctx[i].pipe_idx = i;
			dc->hwss.disable_plane(dc, &context->res_ctx.pipe_ctx[i]);
		}

	dc->hwss.optimize_bandwidth(dc, context);

	dc->optimized_required = false;
	dc->wm_optimized_required = false;

	return true;
}

static void init_state(struct dc *dc, struct dc_state *context)
{
	/* Each context must have their own instance of VBA and in order to
	 * initialize and obtain IP and SOC the base DML instance from DC is
	 * initially copied into every context
	 */
#ifdef CONFIG_DRM_AMD_DC_DCN
	memcpy(&context->bw_ctx.dml, &dc->dml, sizeof(struct display_mode_lib));
#endif
}

struct dc_state *dc_create_state(struct dc *dc)
{
	struct dc_state *context = kvzalloc(sizeof(struct dc_state),
					    GFP_KERNEL);

	if (!context)
		return NULL;

	init_state(dc, context);

	kref_init(&context->refcount);

	return context;
}

struct dc_state *dc_copy_state(struct dc_state *src_ctx)
{
	int i, j;
	struct dc_state *new_ctx = kvmalloc(sizeof(struct dc_state), GFP_KERNEL);

	if (!new_ctx)
		return NULL;
	memcpy(new_ctx, src_ctx, sizeof(struct dc_state));

	for (i = 0; i < MAX_PIPES; i++) {
			struct pipe_ctx *cur_pipe = &new_ctx->res_ctx.pipe_ctx[i];

			if (cur_pipe->top_pipe)
				cur_pipe->top_pipe =  &new_ctx->res_ctx.pipe_ctx[cur_pipe->top_pipe->pipe_idx];

			if (cur_pipe->bottom_pipe)
				cur_pipe->bottom_pipe = &new_ctx->res_ctx.pipe_ctx[cur_pipe->bottom_pipe->pipe_idx];

			if (cur_pipe->prev_odm_pipe)
				cur_pipe->prev_odm_pipe =  &new_ctx->res_ctx.pipe_ctx[cur_pipe->prev_odm_pipe->pipe_idx];

			if (cur_pipe->next_odm_pipe)
				cur_pipe->next_odm_pipe = &new_ctx->res_ctx.pipe_ctx[cur_pipe->next_odm_pipe->pipe_idx];

	}

	for (i = 0; i < new_ctx->stream_count; i++) {
			dc_stream_retain(new_ctx->streams[i]);
			for (j = 0; j < new_ctx->stream_status[i].plane_count; j++)
				dc_plane_state_retain(
					new_ctx->stream_status[i].plane_states[j]);
	}

	kref_init(&new_ctx->refcount);

	return new_ctx;
}

void dc_retain_state(struct dc_state *context)
{
	kref_get(&context->refcount);
}

static void dc_state_free(struct kref *kref)
{
	struct dc_state *context = container_of(kref, struct dc_state, refcount);
	dc_resource_state_destruct(context);
	kvfree(context);
}

void dc_release_state(struct dc_state *context)
{
	kref_put(&context->refcount, dc_state_free);
}

bool dc_set_generic_gpio_for_stereo(bool enable,
		struct gpio_service *gpio_service)
{
	enum gpio_result gpio_result = GPIO_RESULT_NON_SPECIFIC_ERROR;
	struct gpio_pin_info pin_info;
	struct gpio *generic;
	struct gpio_generic_mux_config *config = kzalloc(sizeof(struct gpio_generic_mux_config),
			   GFP_KERNEL);

	if (!config)
		return false;
	pin_info = dal_gpio_get_generic_pin_info(gpio_service, GPIO_ID_GENERIC, 0);

	if (pin_info.mask == 0xFFFFFFFF || pin_info.offset == 0xFFFFFFFF) {
		kfree(config);
		return false;
	} else {
		generic = dal_gpio_service_create_generic_mux(
			gpio_service,
			pin_info.offset,
			pin_info.mask);
	}

	if (!generic) {
		kfree(config);
		return false;
	}

	gpio_result = dal_gpio_open(generic, GPIO_MODE_OUTPUT);

	config->enable_output_from_mux = enable;
	config->mux_select = GPIO_SIGNAL_SOURCE_PASS_THROUGH_STEREO_SYNC;

	if (gpio_result == GPIO_RESULT_OK)
		gpio_result = dal_mux_setup_config(generic, config);

	if (gpio_result == GPIO_RESULT_OK) {
		dal_gpio_close(generic);
		dal_gpio_destroy_generic_mux(&generic);
		kfree(config);
		return true;
	} else {
		dal_gpio_close(generic);
		dal_gpio_destroy_generic_mux(&generic);
		kfree(config);
		return false;
	}
}

static bool is_surface_in_context(
		const struct dc_state *context,
		const struct dc_plane_state *plane_state)
{
	int j;

	for (j = 0; j < MAX_PIPES; j++) {
		const struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (plane_state == pipe_ctx->plane_state) {
			return true;
		}
	}

	return false;
}

static enum surface_update_type get_plane_info_update_type(const struct dc_surface_update *u)
{
	union surface_update_flags *update_flags = &u->surface->update_flags;
	enum surface_update_type update_type = UPDATE_TYPE_FAST;

	if (!u->plane_info)
		return UPDATE_TYPE_FAST;

	if (u->plane_info->color_space != u->surface->color_space) {
		update_flags->bits.color_space_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}

	if (u->plane_info->horizontal_mirror != u->surface->horizontal_mirror) {
		update_flags->bits.horizontal_mirror_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}

	if (u->plane_info->rotation != u->surface->rotation) {
		update_flags->bits.rotation_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_FULL);
	}

	if (u->plane_info->format != u->surface->format) {
		update_flags->bits.pixel_format_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_FULL);
	}

	if (u->plane_info->stereo_format != u->surface->stereo_format) {
		update_flags->bits.stereo_format_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_FULL);
	}

	if (u->plane_info->per_pixel_alpha != u->surface->per_pixel_alpha) {
		update_flags->bits.per_pixel_alpha_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}

	if (u->plane_info->global_alpha_value != u->surface->global_alpha_value) {
		update_flags->bits.global_alpha_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}

	if (u->plane_info->dcc.enable != u->surface->dcc.enable
			|| u->plane_info->dcc.independent_64b_blks != u->surface->dcc.independent_64b_blks
			|| u->plane_info->dcc.meta_pitch != u->surface->dcc.meta_pitch) {
		update_flags->bits.dcc_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}

	if (resource_pixel_format_to_bpp(u->plane_info->format) !=
			resource_pixel_format_to_bpp(u->surface->format)) {
		/* different bytes per element will require full bandwidth
		 * and DML calculation
		 */
		update_flags->bits.bpp_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_FULL);
	}

	if (u->plane_info->plane_size.surface_pitch != u->surface->plane_size.surface_pitch
			|| u->plane_info->plane_size.chroma_pitch != u->surface->plane_size.chroma_pitch) {
		update_flags->bits.plane_size_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);
	}


	if (memcmp(&u->plane_info->tiling_info, &u->surface->tiling_info,
			sizeof(union dc_tiling_info)) != 0) {
		update_flags->bits.swizzle_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_MED);

		/* todo: below are HW dependent, we should add a hook to
		 * DCE/N resource and validated there.
		 */
		if (u->plane_info->tiling_info.gfx9.swizzle != DC_SW_LINEAR) {
			/* swizzled mode requires RQ to be setup properly,
			 * thus need to run DML to calculate RQ settings
			 */
			update_flags->bits.bandwidth_change = 1;
			elevate_update_type(&update_type, UPDATE_TYPE_FULL);
		}
	}

	/* This should be UPDATE_TYPE_FAST if nothing has changed. */
	return update_type;
}

static enum surface_update_type get_scaling_info_update_type(
		const struct dc_surface_update *u)
{
	union surface_update_flags *update_flags = &u->surface->update_flags;

	if (!u->scaling_info)
		return UPDATE_TYPE_FAST;

	if (u->scaling_info->clip_rect.width != u->surface->clip_rect.width
			|| u->scaling_info->clip_rect.height != u->surface->clip_rect.height
			|| u->scaling_info->dst_rect.width != u->surface->dst_rect.width
			|| u->scaling_info->dst_rect.height != u->surface->dst_rect.height
			|| u->scaling_info->scaling_quality.integer_scaling !=
				u->surface->scaling_quality.integer_scaling
			) {
		update_flags->bits.scaling_change = 1;

		if ((u->scaling_info->dst_rect.width < u->surface->dst_rect.width
			|| u->scaling_info->dst_rect.height < u->surface->dst_rect.height)
				&& (u->scaling_info->dst_rect.width < u->surface->src_rect.width
					|| u->scaling_info->dst_rect.height < u->surface->src_rect.height))
			/* Making dst rect smaller requires a bandwidth change */
			update_flags->bits.bandwidth_change = 1;
	}

	if (u->scaling_info->src_rect.width != u->surface->src_rect.width
		|| u->scaling_info->src_rect.height != u->surface->src_rect.height) {

		update_flags->bits.scaling_change = 1;
		if (u->scaling_info->src_rect.width > u->surface->src_rect.width
				|| u->scaling_info->src_rect.height > u->surface->src_rect.height)
			/* Making src rect bigger requires a bandwidth change */
			update_flags->bits.clock_change = 1;
	}

	if (u->scaling_info->src_rect.x != u->surface->src_rect.x
			|| u->scaling_info->src_rect.y != u->surface->src_rect.y
			|| u->scaling_info->clip_rect.x != u->surface->clip_rect.x
			|| u->scaling_info->clip_rect.y != u->surface->clip_rect.y
			|| u->scaling_info->dst_rect.x != u->surface->dst_rect.x
			|| u->scaling_info->dst_rect.y != u->surface->dst_rect.y)
		update_flags->bits.position_change = 1;

	if (update_flags->bits.clock_change
			|| update_flags->bits.bandwidth_change
			|| update_flags->bits.scaling_change)
		return UPDATE_TYPE_FULL;

	if (update_flags->bits.position_change)
		return UPDATE_TYPE_MED;

	return UPDATE_TYPE_FAST;
}

static enum surface_update_type det_surface_update(const struct dc *dc,
		const struct dc_surface_update *u)
{
	const struct dc_state *context = dc->current_state;
	enum surface_update_type type;
	enum surface_update_type overall_type = UPDATE_TYPE_FAST;
	union surface_update_flags *update_flags = &u->surface->update_flags;

	if (u->flip_addr)
		update_flags->bits.addr_update = 1;

	if (!is_surface_in_context(context, u->surface) || u->surface->force_full_update) {
		update_flags->raw = 0xFFFFFFFF;
		return UPDATE_TYPE_FULL;
	}

	update_flags->raw = 0; // Reset all flags

	type = get_plane_info_update_type(u);
	elevate_update_type(&overall_type, type);

	type = get_scaling_info_update_type(u);
	elevate_update_type(&overall_type, type);

	if (u->flip_addr)
		update_flags->bits.addr_update = 1;

	if (u->in_transfer_func)
		update_flags->bits.in_transfer_func_change = 1;

	if (u->input_csc_color_matrix)
		update_flags->bits.input_csc_change = 1;

	if (u->coeff_reduction_factor)
		update_flags->bits.coeff_reduction_change = 1;

	if (u->gamut_remap_matrix)
		update_flags->bits.gamut_remap_change = 1;

	if (u->gamma) {
		enum surface_pixel_format format = SURFACE_PIXEL_FORMAT_GRPH_BEGIN;

		if (u->plane_info)
			format = u->plane_info->format;
		else if (u->surface)
			format = u->surface->format;

		if (dce_use_lut(format))
			update_flags->bits.gamma_change = 1;
	}

	if (u->hdr_mult.value)
		if (u->hdr_mult.value != u->surface->hdr_mult.value) {
			update_flags->bits.hdr_mult = 1;
			elevate_update_type(&overall_type, UPDATE_TYPE_MED);
		}

	if (update_flags->bits.in_transfer_func_change) {
		type = UPDATE_TYPE_MED;
		elevate_update_type(&overall_type, type);
	}

	if (update_flags->bits.input_csc_change
			|| update_flags->bits.coeff_reduction_change
			|| update_flags->bits.gamma_change
			|| update_flags->bits.gamut_remap_change) {
		type = UPDATE_TYPE_FULL;
		elevate_update_type(&overall_type, type);
	}

	return overall_type;
}

static enum surface_update_type check_update_surfaces_for_stream(
		struct dc *dc,
		struct dc_surface_update *updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		const struct dc_stream_status *stream_status)
{
	int i;
	enum surface_update_type overall_type = UPDATE_TYPE_FAST;

#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
	if (dc->idle_optimizations_allowed)
		overall_type = UPDATE_TYPE_FULL;

#endif
	if (stream_status == NULL || stream_status->plane_count != surface_count)
		overall_type = UPDATE_TYPE_FULL;

	/* some stream updates require passive update */
	if (stream_update) {
		union stream_update_flags *su_flags = &stream_update->stream->update_flags;

		if ((stream_update->src.height != 0 && stream_update->src.width != 0) ||
			(stream_update->dst.height != 0 && stream_update->dst.width != 0) ||
			stream_update->integer_scaling_update)
			su_flags->bits.scaling = 1;

		if (stream_update->out_transfer_func)
			su_flags->bits.out_tf = 1;

		if (stream_update->abm_level)
			su_flags->bits.abm_level = 1;

		if (stream_update->dpms_off)
			su_flags->bits.dpms_off = 1;

		if (stream_update->gamut_remap)
			su_flags->bits.gamut_remap = 1;

		if (stream_update->wb_update)
			su_flags->bits.wb_update = 1;

		if (stream_update->dsc_config)
			su_flags->bits.dsc_changed = 1;

		if (su_flags->raw != 0)
			overall_type = UPDATE_TYPE_FULL;

		if (stream_update->output_csc_transform || stream_update->output_color_space)
			su_flags->bits.out_csc = 1;
	}

	for (i = 0 ; i < surface_count; i++) {
		enum surface_update_type type =
				det_surface_update(dc, &updates[i]);

		elevate_update_type(&overall_type, type);
	}

	return overall_type;
}

/**
 * dc_check_update_surfaces_for_stream() - Determine update type (fast, med, or full)
 *
 * See :c:type:`enum surface_update_type <surface_update_type>` for explanation of update types
 */
enum surface_update_type dc_check_update_surfaces_for_stream(
		struct dc *dc,
		struct dc_surface_update *updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		const struct dc_stream_status *stream_status)
{
	int i;
	enum surface_update_type type;

	if (stream_update)
		stream_update->stream->update_flags.raw = 0;
	for (i = 0; i < surface_count; i++)
		updates[i].surface->update_flags.raw = 0;

	type = check_update_surfaces_for_stream(dc, updates, surface_count, stream_update, stream_status);
	if (type == UPDATE_TYPE_FULL) {
		if (stream_update) {
			uint32_t dsc_changed = stream_update->stream->update_flags.bits.dsc_changed;
			stream_update->stream->update_flags.raw = 0xFFFFFFFF;
			stream_update->stream->update_flags.bits.dsc_changed = dsc_changed;
		}
		for (i = 0; i < surface_count; i++)
			updates[i].surface->update_flags.raw = 0xFFFFFFFF;
	}

	if (type == UPDATE_TYPE_FAST) {
		// If there's an available clock comparator, we use that.
		if (dc->clk_mgr->funcs->are_clock_states_equal) {
			if (!dc->clk_mgr->funcs->are_clock_states_equal(&dc->clk_mgr->clks, &dc->current_state->bw_ctx.bw.dcn.clk))
				dc->optimized_required = true;
		// Else we fallback to mem compare.
		} else if (memcmp(&dc->current_state->bw_ctx.bw.dcn.clk, &dc->clk_mgr->clks, offsetof(struct dc_clocks, prev_p_state_change_support)) != 0) {
			dc->optimized_required = true;
		}

		dc->optimized_required |= dc->wm_optimized_required;
	}

	return type;
}

static struct dc_stream_status *stream_get_status(
	struct dc_state *ctx,
	struct dc_stream_state *stream)
{
	uint8_t i;

	for (i = 0; i < ctx->stream_count; i++) {
		if (stream == ctx->streams[i]) {
			return &ctx->stream_status[i];
		}
	}

	return NULL;
}

static const enum surface_update_type update_surface_trace_level = UPDATE_TYPE_FULL;

static void copy_surface_update_to_plane(
		struct dc_plane_state *surface,
		struct dc_surface_update *srf_update)
{
	if (srf_update->flip_addr) {
		surface->address = srf_update->flip_addr->address;
		surface->flip_immediate =
			srf_update->flip_addr->flip_immediate;
		surface->time.time_elapsed_in_us[surface->time.index] =
			srf_update->flip_addr->flip_timestamp_in_us -
				surface->time.prev_update_time_in_us;
		surface->time.prev_update_time_in_us =
			srf_update->flip_addr->flip_timestamp_in_us;
		surface->time.index++;
		if (surface->time.index >= DC_PLANE_UPDATE_TIMES_MAX)
			surface->time.index = 0;

		surface->triplebuffer_flips = srf_update->flip_addr->triplebuffer_flips;
	}

	if (srf_update->scaling_info) {
		surface->scaling_quality =
				srf_update->scaling_info->scaling_quality;
		surface->dst_rect =
				srf_update->scaling_info->dst_rect;
		surface->src_rect =
				srf_update->scaling_info->src_rect;
		surface->clip_rect =
				srf_update->scaling_info->clip_rect;
	}

	if (srf_update->plane_info) {
		surface->color_space =
				srf_update->plane_info->color_space;
		surface->format =
				srf_update->plane_info->format;
		surface->plane_size =
				srf_update->plane_info->plane_size;
		surface->rotation =
				srf_update->plane_info->rotation;
		surface->horizontal_mirror =
				srf_update->plane_info->horizontal_mirror;
		surface->stereo_format =
				srf_update->plane_info->stereo_format;
		surface->tiling_info =
				srf_update->plane_info->tiling_info;
		surface->visible =
				srf_update->plane_info->visible;
		surface->per_pixel_alpha =
				srf_update->plane_info->per_pixel_alpha;
		surface->global_alpha =
				srf_update->plane_info->global_alpha;
		surface->global_alpha_value =
				srf_update->plane_info->global_alpha_value;
		surface->dcc =
				srf_update->plane_info->dcc;
		surface->layer_index =
				srf_update->plane_info->layer_index;
	}

	if (srf_update->gamma &&
			(surface->gamma_correction !=
					srf_update->gamma)) {
		memcpy(&surface->gamma_correction->entries,
			&srf_update->gamma->entries,
			sizeof(struct dc_gamma_entries));
		surface->gamma_correction->is_identity =
			srf_update->gamma->is_identity;
		surface->gamma_correction->num_entries =
			srf_update->gamma->num_entries;
		surface->gamma_correction->type =
			srf_update->gamma->type;
	}

	if (srf_update->in_transfer_func &&
			(surface->in_transfer_func !=
				srf_update->in_transfer_func)) {
		surface->in_transfer_func->sdr_ref_white_level =
			srf_update->in_transfer_func->sdr_ref_white_level;
		surface->in_transfer_func->tf =
			srf_update->in_transfer_func->tf;
		surface->in_transfer_func->type =
			srf_update->in_transfer_func->type;
		memcpy(&surface->in_transfer_func->tf_pts,
			&srf_update->in_transfer_func->tf_pts,
			sizeof(struct dc_transfer_func_distributed_points));
	}

	if (srf_update->func_shaper &&
			(surface->in_shaper_func !=
			srf_update->func_shaper))
		memcpy(surface->in_shaper_func, srf_update->func_shaper,
		sizeof(*surface->in_shaper_func));

	if (srf_update->lut3d_func &&
			(surface->lut3d_func !=
			srf_update->lut3d_func))
		memcpy(surface->lut3d_func, srf_update->lut3d_func,
		sizeof(*surface->lut3d_func));

	if (srf_update->hdr_mult.value)
		surface->hdr_mult =
				srf_update->hdr_mult;

	if (srf_update->blend_tf &&
			(surface->blend_tf !=
			srf_update->blend_tf))
		memcpy(surface->blend_tf, srf_update->blend_tf,
		sizeof(*surface->blend_tf));

	if (srf_update->input_csc_color_matrix)
		surface->input_csc_color_matrix =
			*srf_update->input_csc_color_matrix;

	if (srf_update->coeff_reduction_factor)
		surface->coeff_reduction_factor =
			*srf_update->coeff_reduction_factor;

	if (srf_update->gamut_remap_matrix)
		surface->gamut_remap_matrix =
			*srf_update->gamut_remap_matrix;
}

static void copy_stream_update_to_stream(struct dc *dc,
					 struct dc_state *context,
					 struct dc_stream_state *stream,
					 struct dc_stream_update *update)
{
	struct dc_context *dc_ctx = dc->ctx;

	if (update == NULL || stream == NULL)
		return;

	if (update->src.height && update->src.width)
		stream->src = update->src;

	if (update->dst.height && update->dst.width)
		stream->dst = update->dst;

	if (update->out_transfer_func &&
	    stream->out_transfer_func != update->out_transfer_func) {
		stream->out_transfer_func->sdr_ref_white_level =
			update->out_transfer_func->sdr_ref_white_level;
		stream->out_transfer_func->tf = update->out_transfer_func->tf;
		stream->out_transfer_func->type =
			update->out_transfer_func->type;
		memcpy(&stream->out_transfer_func->tf_pts,
		       &update->out_transfer_func->tf_pts,
		       sizeof(struct dc_transfer_func_distributed_points));
	}

	if (update->hdr_static_metadata)
		stream->hdr_static_metadata = *update->hdr_static_metadata;

	if (update->abm_level)
		stream->abm_level = *update->abm_level;

	if (update->periodic_interrupt0)
		stream->periodic_interrupt0 = *update->periodic_interrupt0;

	if (update->periodic_interrupt1)
		stream->periodic_interrupt1 = *update->periodic_interrupt1;

	if (update->gamut_remap)
		stream->gamut_remap_matrix = *update->gamut_remap;

	/* Note: this being updated after mode set is currently not a use case
	 * however if it arises OCSC would need to be reprogrammed at the
	 * minimum
	 */
	if (update->output_color_space)
		stream->output_color_space = *update->output_color_space;

	if (update->output_csc_transform)
		stream->csc_color_matrix = *update->output_csc_transform;

	if (update->vrr_infopacket)
		stream->vrr_infopacket = *update->vrr_infopacket;

	if (update->dpms_off)
		stream->dpms_off = *update->dpms_off;

	if (update->vsc_infopacket)
		stream->vsc_infopacket = *update->vsc_infopacket;

	if (update->vsp_infopacket)
		stream->vsp_infopacket = *update->vsp_infopacket;

	if (update->dither_option)
		stream->dither_option = *update->dither_option;
	/* update current stream with writeback info */
	if (update->wb_update) {
		int i;

		stream->num_wb_info = update->wb_update->num_wb_info;
		ASSERT(stream->num_wb_info <= MAX_DWB_PIPES);
		for (i = 0; i < stream->num_wb_info; i++)
			stream->writeback_info[i] =
				update->wb_update->writeback_info[i];
	}
	if (update->dsc_config) {
		struct dc_dsc_config old_dsc_cfg = stream->timing.dsc_cfg;
		uint32_t old_dsc_enabled = stream->timing.flags.DSC;
		uint32_t enable_dsc = (update->dsc_config->num_slices_h != 0 &&
				       update->dsc_config->num_slices_v != 0);

		/* Use temporarry context for validating new DSC config */
		struct dc_state *dsc_validate_context = dc_create_state(dc);

		if (dsc_validate_context) {
			dc_resource_state_copy_construct(dc->current_state, dsc_validate_context);

			stream->timing.dsc_cfg = *update->dsc_config;
			stream->timing.flags.DSC = enable_dsc;
			if (!dc->res_pool->funcs->validate_bandwidth(dc, dsc_validate_context, true)) {
				stream->timing.dsc_cfg = old_dsc_cfg;
				stream->timing.flags.DSC = old_dsc_enabled;
				update->dsc_config = NULL;
			}

			dc_release_state(dsc_validate_context);
		} else {
			DC_ERROR("Failed to allocate new validate context for DSC change\n");
			update->dsc_config = NULL;
		}
	}
}

static void commit_planes_do_stream_update(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		enum surface_update_type update_type,
		struct dc_state *context)
{
	int j;
	bool should_program_abm;

	// Stream updates
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (!pipe_ctx->top_pipe &&  !pipe_ctx->prev_odm_pipe && pipe_ctx->stream == stream) {

			if (stream_update->periodic_interrupt0 &&
					dc->hwss.setup_periodic_interrupt)
				dc->hwss.setup_periodic_interrupt(dc, pipe_ctx, VLINE0);

			if (stream_update->periodic_interrupt1 &&
					dc->hwss.setup_periodic_interrupt)
				dc->hwss.setup_periodic_interrupt(dc, pipe_ctx, VLINE1);

			if ((stream_update->hdr_static_metadata && !stream->use_dynamic_meta) ||
					stream_update->vrr_infopacket ||
					stream_update->vsc_infopacket ||
					stream_update->vsp_infopacket) {
				resource_build_info_frame(pipe_ctx);
				dc->hwss.update_info_frame(pipe_ctx);
			}

			if (stream_update->hdr_static_metadata &&
					stream->use_dynamic_meta &&
					dc->hwss.set_dmdata_attributes &&
					pipe_ctx->stream->dmdata_address.quad_part != 0)
				dc->hwss.set_dmdata_attributes(pipe_ctx);

			if (stream_update->gamut_remap)
				dc_stream_set_gamut_remap(dc, stream);

			if (stream_update->output_csc_transform)
				dc_stream_program_csc_matrix(dc, stream);

			if (stream_update->dither_option) {
				struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;
				resource_build_bit_depth_reduction_params(pipe_ctx->stream,
									&pipe_ctx->stream->bit_depth_params);
				pipe_ctx->stream_res.opp->funcs->opp_program_fmt(pipe_ctx->stream_res.opp,
						&stream->bit_depth_params,
						&stream->clamping);
				while (odm_pipe) {
					odm_pipe->stream_res.opp->funcs->opp_program_fmt(odm_pipe->stream_res.opp,
							&stream->bit_depth_params,
							&stream->clamping);
					odm_pipe = odm_pipe->next_odm_pipe;
				}
			}

			/* Full fe update*/
			if (update_type == UPDATE_TYPE_FAST)
				continue;

			if (stream_update->dsc_config)
				dp_update_dsc_config(pipe_ctx);

			if (stream_update->dpms_off) {
				if (*stream_update->dpms_off) {
					core_link_disable_stream(pipe_ctx);
					/* for dpms, keep acquired resources*/
					if (pipe_ctx->stream_res.audio && !dc->debug.az_endpoint_mute_only)
						pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

					dc->optimized_required = true;

				} else {
					if (dc->optimize_seamless_boot_streams == 0)
						dc->hwss.prepare_bandwidth(dc, dc->current_state);

					core_link_enable_stream(dc->current_state, pipe_ctx);
				}
			}

			if (stream_update->abm_level && pipe_ctx->stream_res.abm) {
				should_program_abm = true;

				// if otg funcs defined check if blanked before programming
				if (pipe_ctx->stream_res.tg->funcs->is_blanked)
					if (pipe_ctx->stream_res.tg->funcs->is_blanked(pipe_ctx->stream_res.tg))
						should_program_abm = false;

				if (should_program_abm) {
					if (*stream_update->abm_level == ABM_LEVEL_IMMEDIATE_DISABLE) {
						dc->hwss.set_abm_immediate_disable(pipe_ctx);
					} else {
						pipe_ctx->stream_res.abm->funcs->set_abm_level(
							pipe_ctx->stream_res.abm, stream->abm_level);
					}
				}
			}
		}
	}
}

static void commit_planes_for_stream(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		enum surface_update_type update_type,
		struct dc_state *context)
{
	bool mpcc_disconnected = false;
	int i, j;
	struct pipe_ctx *top_pipe_to_program = NULL;

	if (dc->optimize_seamless_boot_streams > 0 && surface_count > 0) {
		/* Optimize seamless boot flag keeps clocks and watermarks high until
		 * first flip. After first flip, optimization is required to lower
		 * bandwidth. Important to note that it is expected UEFI will
		 * only light up a single display on POST, therefore we only expect
		 * one stream with seamless boot flag set.
		 */
		if (stream->apply_seamless_boot_optimization) {
			stream->apply_seamless_boot_optimization = false;
			dc->optimize_seamless_boot_streams--;

			if (dc->optimize_seamless_boot_streams == 0)
				dc->optimized_required = true;
		}
	}

	if (update_type == UPDATE_TYPE_FULL) {
#if defined(CONFIG_DRM_AMD_DC_DCN3_0)
		dc_allow_idle_optimizations(dc, false);

#endif
		if (dc->optimize_seamless_boot_streams == 0)
			dc->hwss.prepare_bandwidth(dc, context);

		context_clock_trace(dc, context);
	}

	if (update_type != UPDATE_TYPE_FAST && dc->hwss.interdependent_update_lock &&
		dc->hwss.disconnect_pipes && dc->hwss.wait_for_pending_cleared){
		dc->hwss.interdependent_update_lock(dc, context, true);
		mpcc_disconnected = dc->hwss.disconnect_pipes(dc, context);
		dc->hwss.interdependent_update_lock(dc, context, false);
		if (mpcc_disconnected)
			dc->hwss.wait_for_pending_cleared(dc, context);
	}

	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (!pipe_ctx->top_pipe &&
			!pipe_ctx->prev_odm_pipe &&
			pipe_ctx->stream &&
			pipe_ctx->stream == stream) {
			top_pipe_to_program = pipe_ctx;
		}
	}

	if ((update_type != UPDATE_TYPE_FAST) && stream->update_flags.bits.dsc_changed)
		if (top_pipe_to_program->stream_res.tg->funcs->lock_doublebuffer_enable) {
			if (should_use_dmub_lock(stream->link)) {
				union dmub_hw_lock_flags hw_locks = { 0 };
				struct dmub_hw_lock_inst_flags inst_flags = { 0 };

				hw_locks.bits.lock_dig = 1;
				inst_flags.dig_inst = top_pipe_to_program->stream_res.tg->inst;

				dmub_hw_lock_mgr_cmd(dc->ctx->dmub_srv,
							true,
							&hw_locks,
							&inst_flags);
			} else
				top_pipe_to_program->stream_res.tg->funcs->lock_doublebuffer_enable(
						top_pipe_to_program->stream_res.tg);
		}

	if ((update_type != UPDATE_TYPE_FAST) && dc->hwss.interdependent_update_lock)
		dc->hwss.interdependent_update_lock(dc, context, true);
	else
		/* Lock the top pipe while updating plane addrs, since freesync requires
		 *  plane addr update event triggers to be synchronized.
		 *  top_pipe_to_program is expected to never be NULL
		 */
		dc->hwss.pipe_control_lock(dc, top_pipe_to_program, true);


	// Stream updates
	if (stream_update)
		commit_planes_do_stream_update(dc, stream, stream_update, update_type, context);

	if (surface_count == 0) {
		/*
		 * In case of turning off screen, no need to program front end a second time.
		 * just return after program blank.
		 */
		if (dc->hwss.apply_ctx_for_surface)
			dc->hwss.apply_ctx_for_surface(dc, stream, 0, context);
		if (dc->hwss.program_front_end_for_ctx)
			dc->hwss.program_front_end_for_ctx(dc, context);

		if ((update_type != UPDATE_TYPE_FAST) && dc->hwss.interdependent_update_lock)
			dc->hwss.interdependent_update_lock(dc, context, false);
		else
			dc->hwss.pipe_control_lock(dc, top_pipe_to_program, false);

		dc->hwss.post_unlock_program_front_end(dc, context);
		return;
	}

	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {
		for (i = 0; i < surface_count; i++) {
			struct dc_plane_state *plane_state = srf_updates[i].surface;
			/*set logical flag for lock/unlock use*/
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];
				if (!pipe_ctx->plane_state)
					continue;
				if (pipe_ctx->plane_state != plane_state)
					continue;
				plane_state->triplebuffer_flips = false;
				if (update_type == UPDATE_TYPE_FAST &&
					dc->hwss.program_triplebuffer != NULL &&
					!plane_state->flip_immediate && dc->debug.enable_tri_buf) {
						/*triple buffer for VUpdate  only*/
						plane_state->triplebuffer_flips = true;
				}
			}
		}
	}

	// Update Type FULL, Surface updates
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (!pipe_ctx->top_pipe &&
			!pipe_ctx->prev_odm_pipe &&
			pipe_ctx->stream &&
			pipe_ctx->stream == stream) {
			struct dc_stream_status *stream_status = NULL;

			if (!pipe_ctx->plane_state)
				continue;

			/* Full fe update*/
			if (update_type == UPDATE_TYPE_FAST)
				continue;

			ASSERT(!pipe_ctx->plane_state->triplebuffer_flips);

			if (dc->hwss.program_triplebuffer != NULL && dc->debug.enable_tri_buf) {
				/*turn off triple buffer for full update*/
				dc->hwss.program_triplebuffer(
					dc, pipe_ctx, pipe_ctx->plane_state->triplebuffer_flips);
			}
			stream_status =
				stream_get_status(context, pipe_ctx->stream);

			if (dc->hwss.apply_ctx_for_surface)
				dc->hwss.apply_ctx_for_surface(
					dc, pipe_ctx->stream, stream_status->plane_count, context);
		}
	}
	if (dc->hwss.program_front_end_for_ctx && update_type != UPDATE_TYPE_FAST) {
		dc->hwss.program_front_end_for_ctx(dc, context);
#ifdef CONFIG_DRM_AMD_DC_DCN
		if (dc->debug.validate_dml_output) {
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				struct pipe_ctx cur_pipe = context->res_ctx.pipe_ctx[i];
				if (cur_pipe.stream == NULL)
					continue;

				cur_pipe.plane_res.hubp->funcs->validate_dml_output(
						cur_pipe.plane_res.hubp, dc->ctx,
						&context->res_ctx.pipe_ctx[i].rq_regs,
						&context->res_ctx.pipe_ctx[i].dlg_regs,
						&context->res_ctx.pipe_ctx[i].ttu_regs);
			}
		}
#endif
	}

	// Update Type FAST, Surface updates
	if (update_type == UPDATE_TYPE_FAST) {
		if (dc->hwss.set_flip_control_gsl)
			for (i = 0; i < surface_count; i++) {
				struct dc_plane_state *plane_state = srf_updates[i].surface;

				for (j = 0; j < dc->res_pool->pipe_count; j++) {
					struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

					if (pipe_ctx->stream != stream)
						continue;

					if (pipe_ctx->plane_state != plane_state)
						continue;

					// GSL has to be used for flip immediate
					dc->hwss.set_flip_control_gsl(pipe_ctx,
							plane_state->flip_immediate);
				}
			}
		/* Perform requested Updates */
		for (i = 0; i < surface_count; i++) {
			struct dc_plane_state *plane_state = srf_updates[i].surface;

			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

				if (pipe_ctx->stream != stream)
					continue;

				if (pipe_ctx->plane_state != plane_state)
					continue;
				/*program triple buffer after lock based on flip type*/
				if (dc->hwss.program_triplebuffer != NULL && dc->debug.enable_tri_buf) {
					/*only enable triplebuffer for  fast_update*/
					dc->hwss.program_triplebuffer(
						dc, pipe_ctx, plane_state->triplebuffer_flips);
				}
				if (srf_updates[i].flip_addr)
					dc->hwss.update_plane_addr(dc, pipe_ctx);
			}
		}
	}

	if ((update_type != UPDATE_TYPE_FAST) && dc->hwss.interdependent_update_lock)
		dc->hwss.interdependent_update_lock(dc, context, false);
	else
		dc->hwss.pipe_control_lock(dc, top_pipe_to_program, false);

	if ((update_type != UPDATE_TYPE_FAST) && stream->update_flags.bits.dsc_changed)
		if (top_pipe_to_program->stream_res.tg->funcs->lock_doublebuffer_enable) {
			top_pipe_to_program->stream_res.tg->funcs->wait_for_state(
					top_pipe_to_program->stream_res.tg,
					CRTC_STATE_VACTIVE);
			top_pipe_to_program->stream_res.tg->funcs->wait_for_state(
					top_pipe_to_program->stream_res.tg,
					CRTC_STATE_VBLANK);
			top_pipe_to_program->stream_res.tg->funcs->wait_for_state(
					top_pipe_to_program->stream_res.tg,
					CRTC_STATE_VACTIVE);

			if (stream && should_use_dmub_lock(stream->link)) {
				union dmub_hw_lock_flags hw_locks = { 0 };
				struct dmub_hw_lock_inst_flags inst_flags = { 0 };

				hw_locks.bits.lock_dig = 1;
				inst_flags.dig_inst = top_pipe_to_program->stream_res.tg->inst;

				dmub_hw_lock_mgr_cmd(dc->ctx->dmub_srv,
							false,
							&hw_locks,
							&inst_flags);
			} else
				top_pipe_to_program->stream_res.tg->funcs->lock_doublebuffer_disable(
					top_pipe_to_program->stream_res.tg);
		}

	if (update_type != UPDATE_TYPE_FAST)
		dc->hwss.post_unlock_program_front_end(dc, context);

	// Fire manual trigger only when bottom plane is flipped
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (pipe_ctx->bottom_pipe ||
				!pipe_ctx->stream ||
				pipe_ctx->stream != stream ||
				!pipe_ctx->plane_state->update_flags.bits.addr_update)
			continue;

		if (pipe_ctx->stream_res.tg->funcs->program_manual_trigger)
			pipe_ctx->stream_res.tg->funcs->program_manual_trigger(pipe_ctx->stream_res.tg);
	}
}

void dc_commit_updates_for_stream(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		struct dc_state *state)
{
	const struct dc_stream_status *stream_status;
	enum surface_update_type update_type;
	struct dc_state *context;
	struct dc_context *dc_ctx = dc->ctx;
	int i, j;

	stream_status = dc_stream_get_status(stream);
	context = dc->current_state;

	update_type = dc_check_update_surfaces_for_stream(
				dc, srf_updates, surface_count, stream_update, stream_status);

	if (update_type >= update_surface_trace_level)
		update_surface_trace(dc, srf_updates, surface_count);


	if (update_type >= UPDATE_TYPE_FULL) {

		/* initialize scratch memory for building context */
		context = dc_create_state(dc);
		if (context == NULL) {
			DC_ERROR("Failed to allocate new validate context!\n");
			return;
		}

		dc_resource_state_copy_construct(state, context);

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];
			struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

			if (new_pipe->plane_state && new_pipe->plane_state != old_pipe->plane_state)
				new_pipe->plane_state->force_full_update = true;
		}
	}


	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *surface = srf_updates[i].surface;

		copy_surface_update_to_plane(surface, &srf_updates[i]);

		if (update_type >= UPDATE_TYPE_MED) {
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx =
					&context->res_ctx.pipe_ctx[j];

				if (pipe_ctx->plane_state != surface)
					continue;

				resource_build_scaling_params(pipe_ctx);
			}
		}
	}

	copy_stream_update_to_stream(dc, context, stream, stream_update);

	if (update_type >= UPDATE_TYPE_FULL) {
		if (!dc->res_pool->funcs->validate_bandwidth(dc, context, false)) {
			DC_ERROR("Mode validation failed for stream update!\n");
			dc_release_state(context);
			return;
		}
	}

	commit_planes_for_stream(
				dc,
				srf_updates,
				surface_count,
				stream,
				stream_update,
				update_type,
				context);
	/*update current_State*/
	if (dc->current_state != context) {

		struct dc_state *old = dc->current_state;

		dc->current_state = context;
		dc_release_state(old);

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->plane_state && pipe_ctx->stream == stream)
				pipe_ctx->plane_state->force_full_update = false;
		}
	}
	/*let's use current_state to update watermark etc*/
	if (update_type >= UPDATE_TYPE_FULL)
		dc_post_update_surfaces_to_stream(dc);

	return;

}

uint8_t dc_get_current_stream_count(struct dc *dc)
{
	return dc->current_state->stream_count;
}

struct dc_stream_state *dc_get_stream_at_index(struct dc *dc, uint8_t i)
{
	if (i < dc->current_state->stream_count)
		return dc->current_state->streams[i];
	return NULL;
}

struct dc_stream_state *dc_stream_find_from_link(const struct dc_link *link)
{
	uint8_t i;
	struct dc_context *ctx = link->ctx;

	for (i = 0; i < ctx->dc->current_state->stream_count; i++) {
		if (ctx->dc->current_state->streams[i]->link == link)
			return ctx->dc->current_state->streams[i];
	}

	return NULL;
}

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	return dal_irq_service_to_irq_source(dc->res_pool->irqs, src_id, ext_id);
}

/**
 * dc_interrupt_set() - Enable/disable an AMD hw interrupt source
 */
bool dc_interrupt_set(struct dc *dc, enum dc_irq_source src, bool enable)
{

	if (dc == NULL)
		return false;

	return dal_irq_service_set(dc->res_pool->irqs, src, enable);
}

void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src)
{
	dal_irq_service_ack(dc->res_pool->irqs, src);
}

void dc_power_down_on_boot(struct dc *dc)
{
	if (dc->ctx->dce_environment != DCE_ENV_VIRTUAL_HW &&
			dc->hwss.power_down_on_boot)
		dc->hwss.power_down_on_boot(dc);
}

void dc_set_power_state(
	struct dc *dc,
	enum dc_acpi_cm_power_state power_state)
{
	struct kref refcount;
	struct display_mode_lib *dml;

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		dc_resource_state_construct(dc, dc->current_state);

		if (dc->ctx->dmub_srv)
			dc_dmub_srv_wait_phy_init(dc->ctx->dmub_srv);

		dc->hwss.init_hw(dc);

		if (dc->hwss.init_sys_ctx != NULL &&
			dc->vm_pa_config.valid) {
			dc->hwss.init_sys_ctx(dc->hwseq, dc, &dc->vm_pa_config);
		}

		break;
	default:
		ASSERT(dc->current_state->stream_count == 0);
		/* Zero out the current context so that on resume we start with
		 * clean state, and dc hw programming optimizations will not
		 * cause any trouble.
		 */
		dml = kzalloc(sizeof(struct display_mode_lib),
				GFP_KERNEL);

		ASSERT(dml);
		if (!dml)
			return;

		/* Preserve refcount */
		refcount = dc->current_state->refcount;
		/* Preserve display mode lib */
		memcpy(dml, &dc->current_state->bw_ctx.dml, sizeof(struct display_mode_lib));

		dc_resource_state_destruct(dc->current_state);
		memset(dc->current_state, 0,
				sizeof(*dc->current_state));

		dc->current_state->refcount = refcount;
		dc->current_state->bw_ctx.dml = *dml;

		kfree(dml);

		break;
	}
}

void dc_resume(struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++)
		core_link_resume(dc->links[i]);
}

bool dc_is_dmcu_initialized(struct dc *dc)
{
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (dmcu)
		return dmcu->funcs->is_dmcu_initialized(dmcu);
	return false;
}

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd)
{

	struct dc_link *link = dc->links[link_index];
	struct ddc_service *ddc = link->ddc;
	return dce_i2c_submit_command(
		dc->res_pool,
		ddc->ddc_pin,
		cmd);
}

bool dc_submit_i2c_oem(
		struct dc *dc,
		struct i2c_command *cmd)
{
	struct ddc_service *ddc = dc->res_pool->oem_device;
	return dce_i2c_submit_command(
		dc->res_pool,
		ddc->ddc_pin,
		cmd);
}

static bool link_add_remote_sink_helper(struct dc_link *dc_link, struct dc_sink *sink)
{
	if (dc_link->sink_count >= MAX_SINKS_PER_LINK) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	dc_sink_retain(sink);

	dc_link->remote_sinks[dc_link->sink_count] = sink;
	dc_link->sink_count++;

	return true;
}

/**
 * dc_link_add_remote_sink() - Create a sink and attach it to an existing link
 *
 * EDID length is in bytes
 */
struct dc_sink *dc_link_add_remote_sink(
		struct dc_link *link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data)
{
	struct dc_sink *dc_sink;
	enum dc_edid_status edid_status;

	if (len > DC_MAX_EDID_BUFFER_SIZE) {
		dm_error("Max EDID buffer size breached!\n");
		return NULL;
	}

	if (!init_data) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (!init_data->link) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dc_sink = dc_sink_create(init_data);

	if (!dc_sink)
		return NULL;

	memmove(dc_sink->dc_edid.raw_edid, edid, len);
	dc_sink->dc_edid.length = len;

	if (!link_add_remote_sink_helper(
			link,
			dc_sink))
		goto fail_add_sink;

	edid_status = dm_helpers_parse_edid_caps(
			link->ctx,
			&dc_sink->dc_edid,
			&dc_sink->edid_caps);

	/*
	 * Treat device as no EDID device if EDID
	 * parsing fails
	 */
	if (edid_status != EDID_OK) {
		dc_sink->dc_edid.length = 0;
		dm_error("Bad EDID, status%d!\n", edid_status);
	}

	return dc_sink;

fail_add_sink:
	dc_sink_release(dc_sink);
	return NULL;
}

/**
 * dc_link_remove_remote_sink() - Remove a remote sink from a dc_link
 *
 * Note that this just removes the struct dc_sink - it doesn't
 * program hardware or alter other members of dc_link
 */
void dc_link_remove_remote_sink(struct dc_link *link, struct dc_sink *sink)
{
	int i;

	if (!link->sink_count) {
		BREAK_TO_DEBUGGER();
		return;
	}

	for (i = 0; i < link->sink_count; i++) {
		if (link->remote_sinks[i] == sink) {
			dc_sink_release(sink);
			link->remote_sinks[i] = NULL;

			/* shrink array to remove empty place */
			while (i < link->sink_count - 1) {
				link->remote_sinks[i] = link->remote_sinks[i+1];
				i++;
			}
			link->remote_sinks[i] = NULL;
			link->sink_count--;
			return;
		}
	}
}

void get_clock_requirements_for_state(struct dc_state *state, struct AsicStateEx *info)
{
	info->displayClock				= (unsigned int)state->bw_ctx.bw.dcn.clk.dispclk_khz;
	info->engineClock				= (unsigned int)state->bw_ctx.bw.dcn.clk.dcfclk_khz;
	info->memoryClock				= (unsigned int)state->bw_ctx.bw.dcn.clk.dramclk_khz;
	info->maxSupportedDppClock		= (unsigned int)state->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz;
	info->dppClock					= (unsigned int)state->bw_ctx.bw.dcn.clk.dppclk_khz;
	info->socClock					= (unsigned int)state->bw_ctx.bw.dcn.clk.socclk_khz;
	info->dcfClockDeepSleep			= (unsigned int)state->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz;
	info->fClock					= (unsigned int)state->bw_ctx.bw.dcn.clk.fclk_khz;
	info->phyClock					= (unsigned int)state->bw_ctx.bw.dcn.clk.phyclk_khz;
}
enum dc_status dc_set_clock(struct dc *dc, enum dc_clock_type clock_type, uint32_t clk_khz, uint32_t stepping)
{
	if (dc->hwss.set_clock)
		return dc->hwss.set_clock(dc, clock_type, clk_khz, stepping);
	return DC_ERROR_UNEXPECTED;
}
void dc_get_clock(struct dc *dc, enum dc_clock_type clock_type, struct dc_clock_config *clock_cfg)
{
	if (dc->hwss.get_clock)
		dc->hwss.get_clock(dc, clock_type, clock_cfg);
}

/* enable/disable eDP PSR without specify stream for eDP */
bool dc_set_psr_allow_active(struct dc *dc, bool enable)
{
	int i;

	for (i = 0; i < dc->current_state->stream_count ; i++) {
		struct dc_link *link;
		struct dc_stream_state *stream = dc->current_state->streams[i];

		link = stream->link;
		if (!link)
			continue;

		if (link->psr_settings.psr_feature_enabled) {
			if (enable && !link->psr_settings.psr_allow_active)
				return dc_link_set_psr_allow_active(link, true, false);
			else if (!enable && link->psr_settings.psr_allow_active)
				return dc_link_set_psr_allow_active(link, false, true);
		}
	}

	return true;
}

#if defined(CONFIG_DRM_AMD_DC_DCN3_0)

void dc_allow_idle_optimizations(struct dc *dc, bool allow)
{
	if (dc->debug.disable_idle_power_optimizations)
		return;

	if (allow == dc->idle_optimizations_allowed)
		return;

	if (dc->hwss.apply_idle_power_optimizations && dc->hwss.apply_idle_power_optimizations(dc, allow))
		dc->idle_optimizations_allowed = allow;
}

/*
 * blank all streams, and set min and max memory clock to
 * lowest and highest DPM level, respectively
 */
void dc_unlock_memory_clock_frequency(struct dc *dc)
{
	unsigned int i;

	for (i = 0; i < MAX_PIPES; i++)
		if (dc->current_state->res_ctx.pipe_ctx[i].plane_state)
			core_link_disable_stream(&dc->current_state->res_ctx.pipe_ctx[i]);

	dc->clk_mgr->funcs->set_hard_min_memclk(dc->clk_mgr, false);
	dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);
}

/*
 * set min memory clock to the min required for current mode,
 * max to maxDPM, and unblank streams
 */
void dc_lock_memory_clock_frequency(struct dc *dc)
{
	unsigned int i;

	dc->clk_mgr->funcs->get_memclk_states_from_smu(dc->clk_mgr);
	dc->clk_mgr->funcs->set_hard_min_memclk(dc->clk_mgr, true);
	dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);

	for (i = 0; i < MAX_PIPES; i++)
		if (dc->current_state->res_ctx.pipe_ctx[i].plane_state)
			core_link_enable_stream(dc->current_state, &dc->current_state->res_ctx.pipe_ctx[i]);
}

bool dc_is_plane_eligible_for_idle_optimizaitons(struct dc *dc,
						 struct dc_plane_state *plane)
{
	return false;
}
#endif

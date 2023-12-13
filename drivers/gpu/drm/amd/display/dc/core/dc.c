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

#include "dm_services.h"

#include "amdgpu.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"
#include "dce/dce_hwseq.h"

#include "resource.h"
#include "dc_state.h"
#include "dc_state_priv.h"

#include "gpio_service_interface.h"
#include "clk_mgr.h"
#include "clock_source.h"
#include "dc_bios_types.h"

#include "bios_parser_interface.h"
#include "bios/bios_parser_helper.h"
#include "include/irq_service_interface.h"
#include "transform.h"
#include "dmcu.h"
#include "dpp.h"
#include "timing_generator.h"
#include "abm.h"
#include "virtual/virtual_link_encoder.h"
#include "hubp.h"

#include "link_hwss.h"
#include "link_encoder.h"
#include "link_enc_cfg.h"

#include "link.h"
#include "dm_helpers.h"
#include "mem_input.h"

#include "dc_dmub_srv.h"

#include "dsc.h"

#include "vm_helper.h"

#include "dce/dce_i2c.h"

#include "dmub/dmub_srv.h"

#include "dce/dmub_psr.h"

#include "dce/dmub_hw_lock_mgr.h"

#include "dc_trace.h"

#include "hw_sequencer_private.h"

#include "dml2/dml2_internal_types.h"

#include "dce/dmub_outbox.h"

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

/* Private functions */

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
			dc->link_srv->destroy_link(&dc->links[i]);
	}
}

static uint32_t get_num_of_internal_disp(struct dc_link **links, uint32_t num_links)
{
	int i;
	uint32_t count = 0;

	for (i = 0; i < num_links; i++) {
		if (links[i]->connector_signal == SIGNAL_TYPE_EDP ||
				links[i]->is_internal_display)
			count++;
	}

	return count;
}

static int get_seamless_boot_stream_count(struct dc_state *ctx)
{
	uint8_t i;
	uint8_t seamless_boot_stream_count = 0;

	for (i = 0; i < ctx->stream_count; i++)
		if (ctx->streams[i]->apply_seamless_boot_optimization)
			seamless_boot_stream_count++;

	return seamless_boot_stream_count;
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

	DC_LOG_DC("BIOS object table - number of connectors: %d", connectors_num);

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

		DC_LOG_DC("BIOS object table - printing link object info for connector number: %d, link_index: %d", i, dc->link_count);

		link_init_params.ctx = dc->ctx;
		/* next BIOS object table connector */
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link = dc->link_srv->create_link(&link_init_params);

		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
		}
	}

	DC_LOG_DC("BIOS object table - end");

	/* Create a link for each usb4 dpia port */
	for (i = 0; i < dc->res_pool->usb4_dpia_count; i++) {
		struct link_init_data link_init_params = {0};
		struct dc_link *link;

		link_init_params.ctx = dc->ctx;
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link_init_params.is_dpia_link = true;

		link = dc->link_srv->create_link(&link_init_params);
		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
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

	dc->caps.num_of_internal_disp = get_num_of_internal_disp(dc->links, dc->link_count);

	return true;

failed_alloc:
	return false;
}

/* Create additional DIG link encoder objects if fewer than the platform
 * supports were created during link construction. This can happen if the
 * number of physical connectors is less than the number of DIGs.
 */
static bool create_link_encoders(struct dc *dc)
{
	bool res = true;
	unsigned int num_usb4_dpia = dc->res_pool->res_cap->num_usb4_dpia;
	unsigned int num_dig_link_enc = dc->res_pool->res_cap->num_dig_link_enc;
	int i;

	/* A platform without USB4 DPIA endpoints has a fixed mapping between DIG
	 * link encoders and physical display endpoints and does not require
	 * additional link encoder objects.
	 */
	if (num_usb4_dpia == 0)
		return res;

	/* Create as many link encoder objects as the platform supports. DPIA
	 * endpoints can be programmably mapped to any DIG.
	 */
	if (num_dig_link_enc > dc->res_pool->dig_link_enc_count) {
		for (i = 0; i < num_dig_link_enc; i++) {
			struct link_encoder *link_enc = dc->res_pool->link_encoders[i];

			if (!link_enc && dc->res_pool->funcs->link_enc_create_minimal) {
				link_enc = dc->res_pool->funcs->link_enc_create_minimal(dc->ctx,
						(enum engine_id)(ENGINE_ID_DIGA + i));
				if (link_enc) {
					dc->res_pool->link_encoders[i] = link_enc;
					dc->res_pool->dig_link_enc_count++;
				} else {
					res = false;
				}
			}
		}
	}

	return res;
}

/* Destroy any additional DIG link encoder objects created by
 * create_link_encoders().
 * NB: Must only be called after destroy_links().
 */
static void destroy_link_encoders(struct dc *dc)
{
	unsigned int num_usb4_dpia;
	unsigned int num_dig_link_enc;
	int i;

	if (!dc->res_pool)
		return;

	num_usb4_dpia = dc->res_pool->res_cap->num_usb4_dpia;
	num_dig_link_enc = dc->res_pool->res_cap->num_dig_link_enc;

	/* A platform without USB4 DPIA endpoints has a fixed mapping between DIG
	 * link encoders and physical display endpoints and does not require
	 * additional link encoder objects.
	 */
	if (num_usb4_dpia == 0)
		return;

	for (i = 0; i < num_dig_link_enc; i++) {
		struct link_encoder *link_enc = dc->res_pool->link_encoders[i];

		if (link_enc) {
			link_enc->funcs->destroy(&link_enc);
			dc->res_pool->link_encoders[i] = NULL;
			dc->res_pool->dig_link_enc_count--;
		}
	}
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
 *  dc_stream_adjust_vmin_vmax - look up pipe context & update parts of DRR
 *  @dc:     dc reference
 *  @stream: Initial dc stream state
 *  @adjust: Updated parameters for vertical_total_min and vertical_total_max
 *
 *  Looks up the pipe context of dc_stream_state and updates the
 *  vertical_total_min and vertical_total_max of the DRR, Dynamic Refresh
 *  Rate, which is a power-saving feature that targets reducing panel
 *  refresh rate while the screen is static
 *
 *  Return: %true if the pipe context is found and adjusted;
 *          %false if the pipe context is not found.
 */
bool dc_stream_adjust_vmin_vmax(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_crtc_timing_adjust *adjust)
{
	int i;

	/*
	 * Don't adjust DRR while there's bandwidth optimizations pending to
	 * avoid conflicting with firmware updates.
	 */
	if (dc->ctx->dce_version > DCE_VERSION_MAX)
		if (dc->optimized_required)
			return false;

	if (!memcmp(&stream->adjust, adjust, sizeof(*adjust)))
		return true;

	stream->adjust.v_total_max = adjust->v_total_max;
	stream->adjust.v_total_mid = adjust->v_total_mid;
	stream->adjust.v_total_mid_frame_num = adjust->v_total_mid_frame_num;
	stream->adjust.v_total_min = adjust->v_total_min;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg) {
			dc->hwss.set_drr(&pipe,
					1,
					*adjust);

			return true;
		}
	}
	return false;
}

/**
 * dc_stream_get_last_used_drr_vtotal - Looks up the pipe context of
 * dc_stream_state and gets the last VTOTAL used by DRR (Dynamic Refresh Rate)
 *
 * @dc: [in] dc reference
 * @stream: [in] Initial dc stream state
 * @refresh_rate: [in] new refresh_rate
 *
 * Return: %true if the pipe context is found and there is an associated
 *         timing_generator for the DC;
 *         %false if the pipe context is not found or there is no
 *         timing_generator for the DC.
 */
bool dc_stream_get_last_used_drr_vtotal(struct dc *dc,
		struct dc_stream_state *stream,
		uint32_t *refresh_rate)
{
	bool status = false;

	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg) {
			/* Only execute if a function pointer has been defined for
			 * the DC version in question
			 */
			if (pipe->stream_res.tg->funcs->get_last_used_drr_vtotal) {
				pipe->stream_res.tg->funcs->get_last_used_drr_vtotal(pipe->stream_res.tg, refresh_rate);

				status = true;

				break;
			}
		}
	}

	return status;
}

bool dc_stream_get_crtc_position(struct dc *dc,
		struct dc_stream_state **streams, int num_streams,
		unsigned int *v_pos, unsigned int *nom_v_pos)
{
	/* TODO: Support multiple streams */
	const struct dc_stream_state *stream = streams[0];
	int i;
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

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
static inline void
dc_stream_forward_dmub_crc_window(struct dc_dmub_srv *dmub_srv,
		struct rect *rect, struct otg_phy_mux *mux_mapping, bool is_stop)
{
	union dmub_rb_cmd cmd = {0};

	cmd.secure_display.roi_info.phy_id = mux_mapping->phy_output_num;
	cmd.secure_display.roi_info.otg_id = mux_mapping->otg_output_num;

	if (is_stop) {
		cmd.secure_display.header.type = DMUB_CMD__SECURE_DISPLAY;
		cmd.secure_display.header.sub_type = DMUB_CMD__SECURE_DISPLAY_CRC_STOP_UPDATE;
	} else {
		cmd.secure_display.header.type = DMUB_CMD__SECURE_DISPLAY;
		cmd.secure_display.header.sub_type = DMUB_CMD__SECURE_DISPLAY_CRC_WIN_NOTIFY;
		cmd.secure_display.roi_info.x_start = rect->x;
		cmd.secure_display.roi_info.y_start = rect->y;
		cmd.secure_display.roi_info.x_end = rect->x + rect->width;
		cmd.secure_display.roi_info.y_end = rect->y + rect->height;
	}

	dc_wake_and_execute_dmub_cmd(dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);
}

static inline void
dc_stream_forward_dmcu_crc_window(struct dmcu *dmcu,
		struct rect *rect, struct otg_phy_mux *mux_mapping, bool is_stop)
{
	if (is_stop)
		dmcu->funcs->stop_crc_win_update(dmcu, mux_mapping);
	else
		dmcu->funcs->forward_crc_window(dmcu, rect, mux_mapping);
}

bool
dc_stream_forward_crc_window(struct dc_stream_state *stream,
		struct rect *rect, bool is_stop)
{
	struct dmcu *dmcu;
	struct dc_dmub_srv *dmub_srv;
	struct otg_phy_mux mux_mapping;
	struct pipe_ctx *pipe;
	int i;
	struct dc *dc = stream->ctx->dc;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream == stream && !pipe->top_pipe && !pipe->prev_odm_pipe)
			break;
	}

	/* Stream not found */
	if (i == MAX_PIPES)
		return false;

	mux_mapping.phy_output_num = stream->link->link_enc_hw_inst;
	mux_mapping.otg_output_num = pipe->stream_res.tg->inst;

	dmcu = dc->res_pool->dmcu;
	dmub_srv = dc->ctx->dmub_srv;

	/* forward to dmub */
	if (dmub_srv)
		dc_stream_forward_dmub_crc_window(dmub_srv, rect, &mux_mapping, is_stop);
	/* forward to dmcu */
	else if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		dc_stream_forward_dmcu_crc_window(dmcu, rect, &mux_mapping, is_stop);
	else
		return false;

	return true;
}
#endif /* CONFIG_DRM_AMD_SECURE_DISPLAY */

/**
 * dc_stream_configure_crc() - Configure CRC capture for the given stream.
 * @dc: DC Object
 * @stream: The stream to configure CRC on.
 * @enable: Enable CRC if true, disable otherwise.
 * @crc_window: CRC window (x/y start/end) information
 * @continuous: Capture CRC on every frame if true. Otherwise, only capture
 *              once.
 *
 * By default, only CRC0 is configured, and the entire frame is used to
 * calculate the CRC.
 *
 * Return: %false if the stream is not found or CRC capture is not supported;
 *         %true if the stream has been configured.
 */
bool dc_stream_configure_crc(struct dc *dc, struct dc_stream_state *stream,
			     struct crc_params *crc_window, bool enable, bool continuous)
{
	struct pipe_ctx *pipe;
	struct crc_params param;
	struct timing_generator *tg;

	pipe = resource_get_otg_master_for_stream(
			&dc->current_state->res_ctx, stream);

	/* Stream not found */
	if (pipe == NULL)
		return false;

	/* By default, capture the full frame */
	param.windowa_x_start = 0;
	param.windowa_y_start = 0;
	param.windowa_x_end = pipe->stream->timing.h_addressable;
	param.windowa_y_end = pipe->stream->timing.v_addressable;
	param.windowb_x_start = 0;
	param.windowb_y_start = 0;
	param.windowb_x_end = pipe->stream->timing.h_addressable;
	param.windowb_y_end = pipe->stream->timing.v_addressable;

	if (crc_window) {
		param.windowa_x_start = crc_window->windowa_x_start;
		param.windowa_y_start = crc_window->windowa_y_start;
		param.windowa_x_end = crc_window->windowa_x_end;
		param.windowa_y_end = crc_window->windowa_y_end;
		param.windowb_x_start = crc_window->windowb_x_start;
		param.windowb_y_start = crc_window->windowb_y_start;
		param.windowb_x_end = crc_window->windowb_x_end;
		param.windowb_y_end = crc_window->windowb_y_end;
	}

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
 *
 * @dc: DC object.
 * @stream: The DC stream state of the stream to get CRCs from.
 * @r_cr: CRC value for the red component.
 * @g_y:  CRC value for the green component.
 * @b_cb: CRC value for the blue component.
 *
 * dc_stream_configure_crc needs to be called beforehand to enable CRCs.
 *
 * Return:
 * %false if stream is not found, or if CRCs are not enabled.
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
	int i;
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
	int i;
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
	int i;
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
	int i, j;
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
	// reset link encoder assignment table on destruct
	if (dc->res_pool && dc->res_pool->funcs->link_encs_assign)
		link_enc_cfg_init(dc, dc->current_state);

	if (dc->current_state) {
		dc_state_release(dc->current_state);
		dc->current_state = NULL;
	}

	destroy_links(dc);

	destroy_link_encoders(dc);

	if (dc->clk_mgr) {
		dc_destroy_clk_mgr(dc->clk_mgr);
		dc->clk_mgr = NULL;
	}

	dc_destroy_resource_pool(dc);

	if (dc->link_srv)
		link_destroy_link_service(&dc->link_srv);

	if (dc->ctx->gpio_service)
		dal_gpio_service_destroy(&dc->ctx->gpio_service);

	if (dc->ctx->created_bios)
		dal_bios_parser_destroy(&dc->ctx->dc_bios);

	kfree(dc->ctx->logger);
	dc_perf_trace_destroy(&dc->ctx->perf_trace);

	kfree(dc->ctx);
	dc->ctx = NULL;

	kfree(dc->bw_vbios);
	dc->bw_vbios = NULL;

	kfree(dc->bw_dceip);
	dc->bw_dceip = NULL;

	kfree(dc->dcn_soc);
	dc->dcn_soc = NULL;

	kfree(dc->dcn_ip);
	dc->dcn_ip = NULL;

	kfree(dc->vm_helper);
	dc->vm_helper = NULL;

}

static bool dc_construct_ctx(struct dc *dc,
		const struct dc_init_data *init_params)
{
	struct dc_context *dc_ctx;

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
	dc_ctx->dcn_reg_offsets = init_params->dcn_reg_offsets;
	dc_ctx->nbio_reg_offsets = init_params->nbio_reg_offsets;
	dc_ctx->clk_reg_offsets = init_params->clk_reg_offsets;

	/* Create logger */
	dc_ctx->logger = kmalloc(sizeof(*dc_ctx->logger), GFP_KERNEL);

	if (!dc_ctx->logger) {
		kfree(dc_ctx);
		return false;
	}

	dc_ctx->logger->dev = adev_to_drm(init_params->driver);
	dc->dml.logger = dc_ctx->logger;

	dc_ctx->dce_version = resource_parse_asic_id(init_params->asic_id);

	dc_ctx->perf_trace = dc_perf_trace_create();
	if (!dc_ctx->perf_trace) {
		kfree(dc_ctx);
		ASSERT_CRITICAL(false);
		return false;
	}

	dc->ctx = dc_ctx;

	dc->link_srv = link_create_link_service();
	if (!dc->link_srv)
		return false;

	return true;
}

static bool dc_construct(struct dc *dc,
		const struct dc_init_data *init_params)
{
	struct dc_context *dc_ctx;
	struct bw_calcs_dceip *dc_dceip;
	struct bw_calcs_vbios *dc_vbios;
	struct dcn_soc_bounding_box *dcn_soc;
	struct dcn_ip_params *dcn_ip;

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

	/* set i2c speed if not done by the respective dcnxxx__resource.c */
	if (dc->caps.i2c_speed_in_khz_hdcp == 0)
		dc->caps.i2c_speed_in_khz_hdcp = dc->caps.i2c_speed_in_khz;
	if (dc->caps.max_optimizable_video_width == 0)
		dc->caps.max_optimizable_video_width = 5120;
	dc->clk_mgr = dc_clk_mgr_create(dc->ctx, dc->res_pool->pp_smu, dc->res_pool->dccg);
	if (!dc->clk_mgr)
		goto fail;
#ifdef CONFIG_DRM_AMD_DC_FP
	dc->clk_mgr->force_smu_not_present = init_params->force_smu_not_present;

	if (dc->res_pool->funcs->update_bw_bounding_box) {
		DC_FP_START();
		dc->res_pool->funcs->update_bw_bounding_box(dc, dc->clk_mgr->bw_params);
		DC_FP_END();
	}
#endif

	if (!create_links(dc, init_params->num_virtual_links))
		goto fail;

	/* Create additional DIG link encoder objects if fewer than the platform
	 * supports were created during link construction.
	 */
	if (!create_link_encoders(dc))
		goto fail;

	/* Creation of current_state must occur after dc->dml
	 * is initialized in dc_create_resource_pool because
	 * on creation it copies the contents of dc->dml
	 */

	dc->current_state = dc_state_create(dc);

	if (!dc->current_state) {
		dm_error("%s: failed to create validate ctx\n", __func__);
		goto fail;
	}

	return true;

fail:
	return false;
}

static void disable_all_writeback_pipes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *context)
{
	int i;

	for (i = 0; i < stream->num_wb_info; i++)
		stream->writeback_info[i].wb_enabled = false;
}

static void apply_ctx_interdependent_lock(struct dc *dc,
					  struct dc_state *context,
					  struct dc_stream_state *stream,
					  bool lock)
{
	int i;

	/* Checks if interdependent update function pointer is NULL or not, takes care of DCE110 case */
	if (dc->hwss.interdependent_update_lock)
		dc->hwss.interdependent_update_lock(dc, context, lock);
	else {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

			// Copied conditions that were previously in dce110_apply_ctx_for_surface
			if (stream == pipe_ctx->stream) {
				if (resource_is_pipe_type(pipe_ctx, OPP_HEAD) &&
					(pipe_ctx->plane_state || old_pipe_ctx->plane_state))
					dc->hwss.pipe_control_lock(dc, pipe_ctx, lock);
			}
		}
	}
}

static void dc_update_viusal_confirm_color(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	if (dc->ctx->dce_version >= DCN_VERSION_1_0) {
		memset(&pipe_ctx->visual_confirm_color, 0, sizeof(struct tg_color));

		if (dc->debug.visual_confirm == VISUAL_CONFIRM_HDR)
			get_hdr_visual_confirm_color(pipe_ctx, &(pipe_ctx->visual_confirm_color));
		else if (dc->debug.visual_confirm == VISUAL_CONFIRM_SURFACE)
			get_surface_visual_confirm_color(pipe_ctx, &(pipe_ctx->visual_confirm_color));
		else if (dc->debug.visual_confirm == VISUAL_CONFIRM_SWIZZLE)
			get_surface_tile_visual_confirm_color(pipe_ctx, &(pipe_ctx->visual_confirm_color));
		else {
			if (dc->ctx->dce_version < DCN_VERSION_2_0)
				color_space_to_black_color(
					dc, pipe_ctx->stream->output_color_space, &(pipe_ctx->visual_confirm_color));
		}
		if (dc->ctx->dce_version >= DCN_VERSION_2_0) {
			if (dc->debug.visual_confirm == VISUAL_CONFIRM_MPCTREE)
				get_mpctree_visual_confirm_color(pipe_ctx, &(pipe_ctx->visual_confirm_color));
			else if (dc->debug.visual_confirm == VISUAL_CONFIRM_SUBVP)
				get_subvp_visual_confirm_color(dc, context, pipe_ctx, &(pipe_ctx->visual_confirm_color));
			else if (dc->debug.visual_confirm == VISUAL_CONFIRM_MCLK_SWITCH)
				get_mclk_switch_visual_confirm_color(dc, context, pipe_ctx, &(pipe_ctx->visual_confirm_color));
		}
	}
}

static void disable_dangling_plane(struct dc *dc, struct dc_state *context)
{
	int i, j;
	struct dc_state *dangling_context = dc_state_create_current_copy(dc);
	struct dc_state *current_ctx;
	struct pipe_ctx *pipe;
	struct timing_generator *tg;

	if (dangling_context == NULL)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_stream_state *old_stream =
				dc->current_state->res_ctx.pipe_ctx[i].stream;
		bool should_disable = true;
		bool pipe_split_change = false;

		if ((context->res_ctx.pipe_ctx[i].top_pipe) &&
			(dc->current_state->res_ctx.pipe_ctx[i].top_pipe))
			pipe_split_change = context->res_ctx.pipe_ctx[i].top_pipe->pipe_idx !=
				dc->current_state->res_ctx.pipe_ctx[i].top_pipe->pipe_idx;
		else
			pipe_split_change = context->res_ctx.pipe_ctx[i].top_pipe !=
				dc->current_state->res_ctx.pipe_ctx[i].top_pipe;

		for (j = 0; j < context->stream_count; j++) {
			if (old_stream == context->streams[j]) {
				should_disable = false;
				break;
			}
		}
		if (!should_disable && pipe_split_change &&
				dc->current_state->stream_count != context->stream_count)
			should_disable = true;

		if (old_stream && !dc->current_state->res_ctx.pipe_ctx[i].top_pipe &&
				!dc->current_state->res_ctx.pipe_ctx[i].prev_odm_pipe) {
			struct pipe_ctx *old_pipe, *new_pipe;

			old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
			new_pipe = &context->res_ctx.pipe_ctx[i];

			if (old_pipe->plane_state && !new_pipe->plane_state)
				should_disable = true;
		}

		if (should_disable && old_stream) {
			bool is_phantom = dc_state_get_stream_subvp_type(dc->current_state, old_stream) == SUBVP_PHANTOM;
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];
			tg = pipe->stream_res.tg;
			/* When disabling plane for a phantom pipe, we must turn on the
			 * phantom OTG so the disable programming gets the double buffer
			 * update. Otherwise the pipe will be left in a partially disabled
			 * state that can result in underflow or hang when enabling it
			 * again for different use.
			 */
			if (is_phantom) {
				if (tg->funcs->enable_crtc) {
					int main_pipe_width, main_pipe_height;
					struct dc_stream_state *old_paired_stream = dc_state_get_paired_subvp_stream(dc->current_state, old_stream);

					main_pipe_width = old_paired_stream->dst.width;
					main_pipe_height = old_paired_stream->dst.height;
					if (dc->hwss.blank_phantom)
						dc->hwss.blank_phantom(dc, tg, main_pipe_width, main_pipe_height);
					tg->funcs->enable_crtc(tg);
				}
			}

			if (is_phantom)
				dc_state_rem_all_phantom_planes_for_stream(dc, old_stream, dangling_context, true);
			else
				dc_state_rem_all_planes_for_stream(dc, old_stream, dangling_context);
			disable_all_writeback_pipes_for_stream(dc, old_stream, dangling_context);

			if (pipe->stream && pipe->plane_state)
				dc_update_viusal_confirm_color(dc, context, pipe);

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
			/* We need to put the phantom OTG back into it's default (disabled) state or we
			 * can get corruption when transition from one SubVP config to a different one.
			 * The OTG is set to disable on falling edge of VUPDATE so the plane disable
			 * will still get it's double buffer update.
			 */
			if (is_phantom) {
				if (tg->funcs->disable_phantom_crtc)
					tg->funcs->disable_phantom_crtc(tg);
			}
		}
	}

	current_ctx = dc->current_state;
	dc->current_state = dangling_context;
	dc_state_release(current_ctx);
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

		if (stream->apply_seamless_boot_optimization)
			continue;

		// only looking for first odm pipe
		if (pipe->prev_odm_pipe)
			continue;

		if (stream->link->local_sink &&
			stream->link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
			link = stream->link;
		}

		if (link != NULL && link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
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
						dc->link_srv->set_dpms_off(pipe);
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

		if (!pipe->plane_state || dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM)
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

/* Public functions */

struct dc *dc_create(const struct dc_init_data *init_params)
{
	struct dc *dc = kzalloc(sizeof(*dc), GFP_KERNEL);
	unsigned int full_pipe_count;

	if (!dc)
		return NULL;

	if (init_params->dce_environment == DCE_ENV_VIRTUAL_HW) {
		if (!dc_construct_ctx(dc, init_params))
			goto destruct_dc;
	} else {
		if (!dc_construct(dc, init_params))
			goto destruct_dc;

		full_pipe_count = dc->res_pool->pipe_count;
		if (dc->res_pool->underlay_pipe_index != NO_UNDERLAY_PIPE)
			full_pipe_count--;
		dc->caps.max_streams = min(
				full_pipe_count,
				dc->res_pool->stream_enc_count);

		dc->caps.max_links = dc->link_count;
		dc->caps.max_audios = dc->res_pool->audio_count;
		dc->caps.linear_pitch_alignment = 64;

		dc->caps.max_dp_protocol_version = DP_VERSION_1_4;

		dc->caps.max_otg_num = dc->res_pool->res_cap->num_timing_generator;

		if (dc->res_pool->dmcu != NULL)
			dc->versions.dmcu_version = dc->res_pool->dmcu->dmcu_version;
	}

	dc->dcn_reg_offsets = init_params->dcn_reg_offsets;
	dc->nbio_reg_offsets = init_params->nbio_reg_offsets;
	dc->clk_reg_offsets = init_params->clk_reg_offsets;

	/* Populate versioning information */
	dc->versions.dc_ver = DC_VER;

	dc->build_id = DC_BUILD_ID;

	DC_LOG_DC("Display Core initialized\n");



	return dc;

destruct_dc:
	dc_destruct(dc);
	kfree(dc);
	return NULL;
}

static void detect_edp_presence(struct dc *dc)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	struct dc_link *edp_link = NULL;
	enum dc_connection_type type;
	int i;
	int edp_num;

	dc_get_edp_links(dc, edp_links, &edp_num);
	if (!edp_num)
		return;

	for (i = 0; i < edp_num; i++) {
		edp_link = edp_links[i];
		if (dc->config.edp_not_connected) {
			edp_link->edp_sink_present = false;
		} else {
			dc_link_detect_connection_type(edp_link, &type);
			edp_link->edp_sink_present = (type != dc_connection_none);
		}
	}
}

void dc_hardware_init(struct dc *dc)
{

	detect_edp_presence(dc);
	if (dc->ctx->dce_environment != DCE_ENV_VIRTUAL_HW)
		dc->hwss.init_hw(dc);
}

void dc_init_callbacks(struct dc *dc,
		const struct dc_callback_init *init_params)
{
	dc->ctx->cp_psp = init_params->cp_psp;
}

void dc_deinit_callbacks(struct dc *dc)
{
	memset(&dc->ctx->cp_psp, 0, sizeof(dc->ctx->cp_psp));
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
	int i, multisync_count = 0;
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
		if (!ctx->res_ctx.pipe_ctx[i].stream
				|| ctx->res_ctx.pipe_ctx[i].top_pipe
				|| ctx->res_ctx.pipe_ctx[i].prev_odm_pipe)
			continue;

		unsynced_pipes[i] = &ctx->res_ctx.pipe_ctx[i];
	}

	for (i = 0; i < pipe_count; i++) {
		int group_size = 1;
		enum timing_synchronization_type sync_type = NOT_SYNCHRONIZABLE;
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
			if (sync_type != TIMING_SYNCHRONIZABLE &&
				dc->hwss.enable_vblanks_synchronization &&
				unsynced_pipes[j]->stream_res.tg->funcs->align_vblanks &&
				resource_are_vblanks_synchronizable(
					unsynced_pipes[j]->stream,
					pipe_set[0]->stream)) {
				sync_type = VBLANK_SYNCHRONIZABLE;
				pipe_set[group_size] = unsynced_pipes[j];
				unsynced_pipes[j] = NULL;
				group_size++;
			} else
			if (sync_type != VBLANK_SYNCHRONIZABLE &&
				resource_are_streams_timing_synchronizable(
					unsynced_pipes[j]->stream,
					pipe_set[0]->stream)) {
				sync_type = TIMING_SYNCHRONIZABLE;
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
			struct dc_stream_status *status = dc_state_get_stream_status(ctx, pipe_set[k]->stream);

			status->timing_sync_info.group_id = num_group;
			status->timing_sync_info.group_size = group_size;
			if (k == 0)
				status->timing_sync_info.master = true;
			else
				status->timing_sync_info.master = false;

		}

		/* remove any other unblanked pipes as they have already been synced */
		if (dc->config.use_pipe_ctx_sync_logic) {
			/* check pipe's syncd to decide which pipe to be removed */
			for (j = 1; j < group_size; j++) {
				if (pipe_set[j]->pipe_idx_syncd == pipe_set[0]->pipe_idx_syncd) {
					group_size--;
					pipe_set[j] = pipe_set[group_size];
					j--;
				} else
					/* link slave pipe's syncd with master pipe */
					pipe_set[j]->pipe_idx_syncd = pipe_set[0]->pipe_idx_syncd;
			}
		} else {
			/* remove any other pipes by checking valid plane */
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
		}

		if (group_size > 1) {
			if (sync_type == TIMING_SYNCHRONIZABLE) {
				dc->hwss.enable_timing_synchronization(
					dc, ctx, group_index, group_size, pipe_set);
			} else
				if (sync_type == VBLANK_SYNCHRONIZABLE) {
				dc->hwss.enable_vblanks_synchronization(
					dc, group_index, group_size, pipe_set);
				}
			group_index++;
		}
		num_group++;
	}
}

static bool streams_changed(struct dc *dc,
			    struct dc_stream_state *streams[],
			    uint8_t stream_count)
{
	uint8_t i;

	if (stream_count != dc->current_state->stream_count)
		return true;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		if (dc->current_state->streams[i] != streams[i])
			return true;
		if (!streams[i]->link->link_state_valid)
			return true;
	}

	return false;
}

bool dc_validate_boot_timing(const struct dc *dc,
				const struct dc_sink *sink,
				struct dc_crtc_timing *crtc_timing)
{
	struct timing_generator *tg;
	struct stream_encoder *se = NULL;

	struct dc_crtc_timing hw_crtc_timing = {0};

	struct dc_link *link = sink->link;
	unsigned int i, enc_inst, tg_inst = 0;

	/* Support seamless boot on EDP displays only */
	if (sink->sink_signal != SIGNAL_TYPE_EDP) {
		return false;
	}

	if (dc->debug.force_odm_combine)
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

	if (tg_inst != link->link_enc->preferred_engine)
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

	/* block DSC for now, as VBIOS does not currently support DSC timings */
	if (crtc_timing->flags.DSC)
		return false;

	if (dc_is_dp_signal(link->connector_signal)) {
		unsigned int pix_clk_100hz;
		uint32_t numOdmPipes = 1;
		uint32_t id_src[4] = {0};

		dc->res_pool->dp_clock_source->funcs->get_pixel_clk_frequency_100hz(
			dc->res_pool->dp_clock_source,
			tg_inst, &pix_clk_100hz);

		if (tg->funcs->get_optc_source)
			tg->funcs->get_optc_source(tg,
						&numOdmPipes, &id_src[0], &id_src[1]);

		if (numOdmPipes == 2)
			pix_clk_100hz *= 2;
		if (numOdmPipes == 4)
			pix_clk_100hz *= 4;

		// Note: In rare cases, HW pixclk may differ from crtc's pixclk
		// slightly due to rounding issues in 10 kHz units.
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

	if (link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED) {
		return false;
	}

	if (dc->link_srv->edp_is_ilr_optimization_required(link, crtc_timing)) {
		DC_LOG_EVENT_LINK_TRAINING("Seamless boot disabled to optimize eDP link rate\n");
		return false;
	}

	return true;
}

static inline bool should_update_pipe_for_stream(
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream)
{
	return (pipe_ctx->stream && pipe_ctx->stream == stream);
}

static inline bool should_update_pipe_for_plane(
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct dc_plane_state *plane_state)
{
	return (pipe_ctx->plane_state == plane_state);
}

void dc_enable_stereo(
	struct dc *dc,
	struct dc_state *context,
	struct dc_stream_state *streams[],
	uint8_t stream_count)
{
	int i, j;
	struct pipe_ctx *pipe;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context != NULL) {
			pipe = &context->res_ctx.pipe_ctx[i];
		} else {
			context = dc->current_state;
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		}

		for (j = 0; pipe && j < stream_count; j++)  {
			if (should_update_pipe_for_stream(context, pipe, streams[j]) &&
				dc->hwss.setup_stereo)
				dc->hwss.setup_stereo(pipe, dc);
		}
	}
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

void dc_z10_restore(const struct dc *dc)
{
	if (dc->hwss.z10_restore)
		dc->hwss.z10_restore(dc);
}

void dc_z10_save_init(struct dc *dc)
{
	if (dc->hwss.z10_save_init)
		dc->hwss.z10_save_init(dc);
}

/**
 * dc_commit_state_no_check - Apply context to the hardware
 *
 * @dc: DC object with the current status to be updated
 * @context: New state that will become the current status at the end of this function
 *
 * Applies given context to the hardware and copy it into current context.
 * It's up to the user to release the src context afterwards.
 *
 * Return: an enum dc_status result code for the operation
 */
static enum dc_status dc_commit_state_no_check(struct dc *dc, struct dc_state *context)
{
	struct dc_bios *dcb = dc->ctx->dc_bios;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct pipe_ctx *pipe;
	int i, k, l;
	struct dc_stream_state *dc_streams[MAX_STREAMS] = {0};
	struct dc_state *old_state;
	bool subvp_prev_use = false;

	dc_z10_restore(dc);
	dc_allow_idle_optimizations(dc, false);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		/* Check old context for SubVP */
		subvp_prev_use |= (dc_state_get_pipe_subvp_type(dc->current_state, old_pipe) == SUBVP_PHANTOM);
		if (subvp_prev_use)
			break;
	}

	for (i = 0; i < context->stream_count; i++)
		dc_streams[i] =  context->streams[i];

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		disable_vbios_mode_if_required(dc, context);
		dc->hwss.enable_accelerated_mode(dc, context);
	}

	if (context->stream_count > get_seamless_boot_stream_count(context) ||
		context->stream_count == 0)
		dc->hwss.prepare_bandwidth(dc, context);

	/* When SubVP is active, all HW programming must be done while
	 * SubVP lock is acquired
	 */
	if (dc->hwss.subvp_pipe_control_lock)
		dc->hwss.subvp_pipe_control_lock(dc, context, true, true, NULL, subvp_prev_use);

	if (dc->hwss.update_dsc_pg)
		dc->hwss.update_dsc_pg(dc, context, false);

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

	if (result != DC_OK) {
		/* Application of dc_state to hardware stopped. */
		dc->current_state->res_ctx.link_enc_cfg_ctx.mode = LINK_ENC_CFG_STEADY;
		return result;
	}

	dc_trigger_sync(dc, context);

	/* Full update should unconditionally be triggered when dc_commit_state_no_check is called */
	for (i = 0; i < context->stream_count; i++) {
		uint32_t prev_dsc_changed = context->streams[i]->update_flags.bits.dsc_changed;

		context->streams[i]->update_flags.raw = 0xFFFFFFFF;
		context->streams[i]->update_flags.bits.dsc_changed = prev_dsc_changed;
	}

	/* Program all planes within new context*/
	if (dc->hwss.program_front_end_for_ctx) {
		dc->hwss.interdependent_update_lock(dc, context, true);
		dc->hwss.program_front_end_for_ctx(dc, context);
		dc->hwss.interdependent_update_lock(dc, context, false);
		dc->hwss.post_unlock_program_front_end(dc, context);
	}

	if (dc->hwss.commit_subvp_config)
		dc->hwss.commit_subvp_config(dc, context);
	if (dc->hwss.subvp_pipe_control_lock)
		dc->hwss.subvp_pipe_control_lock(dc, context, false, true, NULL, subvp_prev_use);

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

	if (context->stream_count > get_seamless_boot_stream_count(context) ||
		context->stream_count == 0) {
		/* Must wait for no flips to be pending before doing optimize bw */
		wait_for_no_pipes_pending(dc, context);
		/* pplib is notified if disp_num changed */
		dc->hwss.optimize_bandwidth(dc, context);
		/* Need to do otg sync again as otg could be out of sync due to otg
		 * workaround applied during clock update
		 */
		dc_trigger_sync(dc, context);
	}

	if (dc->hwss.update_dsc_pg)
		dc->hwss.update_dsc_pg(dc, context, true);

	if (dc->ctx->dce_version >= DCE_VERSION_MAX)
		TRACE_DCN_CLOCK_STATE(&context->bw_ctx.bw.dcn.clk);
	else
		TRACE_DCE_CLOCK_STATE(&context->bw_ctx.bw.dce);

	context->stream_mask = get_stream_mask(dc, context);

	if (context->stream_mask != dc->current_state->stream_mask)
		dc_dmub_srv_notify_stream_mask(dc->ctx->dmub_srv, context->stream_mask);

	for (i = 0; i < context->stream_count; i++)
		context->streams[i]->mode_changed = false;

	/* Clear update flags that were set earlier to avoid redundant programming */
	for (i = 0; i < context->stream_count; i++) {
		context->streams[i]->update_flags.raw = 0x0;
	}

	old_state = dc->current_state;
	dc->current_state = context;

	dc_state_release(old_state);

	dc_state_retain(dc->current_state);

	return result;
}

static bool commit_minimal_transition_state(struct dc *dc,
		struct dc_state *transition_base_context);

/**
 * dc_commit_streams - Commit current stream state
 *
 * @dc: DC object with the commit state to be configured in the hardware
 * @streams: Array with a list of stream state
 * @stream_count: Total of streams
 *
 * Function responsible for commit streams change to the hardware.
 *
 * Return:
 * Return DC_OK if everything work as expected, otherwise, return a dc_status
 * code.
 */
enum dc_status dc_commit_streams(struct dc *dc,
				 struct dc_stream_state *streams[],
				 uint8_t stream_count)
{
	int i, j;
	struct dc_state *context;
	enum dc_status res = DC_OK;
	struct dc_validation_set set[MAX_STREAMS] = {0};
	struct pipe_ctx *pipe;
	bool handle_exit_odm2to1 = false;

	if (dc->ctx->dce_environment == DCE_ENV_VIRTUAL_HW)
		return res;

	if (!streams_changed(dc, streams, stream_count))
		return res;

	DC_LOG_DC("%s: %d streams\n", __func__, stream_count);

	for (i = 0; i < stream_count; i++) {
		struct dc_stream_state *stream = streams[i];
		struct dc_stream_status *status = dc_stream_get_status(stream);

		dc_stream_log(dc, stream);

		set[i].stream = stream;

		if (status) {
			set[i].plane_count = status->plane_count;
			for (j = 0; j < status->plane_count; j++)
				set[i].plane_states[j] = status->plane_states[j];
		}
	}

	/* ODM Combine 2:1 power optimization is only applied for single stream
	 * scenario, it uses extra pipes than needed to reduce power consumption
	 * We need to switch off this feature to make room for new streams.
	 */
	if (stream_count > dc->current_state->stream_count &&
			dc->current_state->stream_count == 1) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe->next_odm_pipe)
				handle_exit_odm2to1 = true;
		}
	}

	if (handle_exit_odm2to1)
		res = commit_minimal_transition_state(dc, dc->current_state);

	context = dc_state_create_current_copy(dc);
	if (!context)
		goto context_alloc_fail;

	res = dc_validate_with_context(dc, set, stream_count, context, false);
	if (res != DC_OK) {
		BREAK_TO_DEBUGGER();
		goto fail;
	}

	res = dc_commit_state_no_check(dc, context);

	for (i = 0; i < stream_count; i++) {
		for (j = 0; j < context->stream_count; j++) {
			if (streams[i]->stream_id == context->streams[j]->stream_id)
				streams[i]->out.otg_offset = context->stream_status[j].primary_otg_inst;

			if (dc_is_embedded_signal(streams[i]->signal)) {
				struct dc_stream_status *status = dc_state_get_stream_status(context, streams[i]);

				if (dc->hwss.is_abm_supported)
					status->is_abm_supported = dc->hwss.is_abm_supported(dc, context, streams[i]);
				else
					status->is_abm_supported = true;
			}
		}
	}

fail:
	dc_state_release(context);

context_alloc_fail:

	DC_LOG_DC("%s Finished.\n", __func__);

	return res;
}

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
			else if (!acquire && pool->funcs->release_post_bldn_3dlut)
				ret = pool->funcs->release_post_bldn_3dlut(res_ctx, pool, lut, shaper);
		}
	}
	return ret;
}

static bool is_flip_pending_in_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	struct pipe_ctx *pipe;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// Don't check flip pending on phantom pipes
		if (!pipe->plane_state || (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM))
			continue;

		/* Must set to false to start with, due to OR in update function */
		pipe->plane_state->status.is_flip_pending = false;
		dc->hwss.update_pending_status(pipe);
		if (pipe->plane_state->status.is_flip_pending)
			return true;
	}
	return false;
}

/* Perform updates here which need to be deferred until next vupdate
 *
 * i.e. blnd lut, 3dlut, and shaper lut bypass regs are double buffered
 * but forcing lut memory to shutdown state is immediate. This causes
 * single frame corruption as lut gets disabled mid-frame unless shutdown
 * is deferred until after entering bypass.
 */
static void process_deferred_updates(struct dc *dc)
{
	int i = 0;

	if (dc->debug.enable_mem_low_power.bits.cm) {
		ASSERT(dc->dcn_ip->max_num_dpp);
		for (i = 0; i < dc->dcn_ip->max_num_dpp; i++)
			if (dc->res_pool->dpps[i]->funcs->dpp_deferred_update)
				dc->res_pool->dpps[i]->funcs->dpp_deferred_update(dc->res_pool->dpps[i]);
	}
}

void dc_post_update_surfaces_to_stream(struct dc *dc)
{
	int i;
	struct dc_state *context = dc->current_state;

	if ((!dc->optimized_required) || get_seamless_boot_stream_count(context) > 0)
		return;

	post_surface_trace(dc);

	/*
	 * Only relevant for DCN behavior where we can guarantee the optimization
	 * is safe to apply - retain the legacy behavior for DCE.
	 */

	if (dc->ctx->dce_version < DCE_VERSION_MAX)
		TRACE_DCE_CLOCK_STATE(&context->bw_ctx.bw.dce);
	else {
		TRACE_DCN_CLOCK_STATE(&context->bw_ctx.bw.dcn.clk);

		if (is_flip_pending_in_pipes(dc, context))
			return;

		for (i = 0; i < dc->res_pool->pipe_count; i++)
			if (context->res_ctx.pipe_ctx[i].stream == NULL ||
					context->res_ctx.pipe_ctx[i].plane_state == NULL) {
				context->res_ctx.pipe_ctx[i].pipe_idx = i;
				dc->hwss.disable_plane(dc, context, &context->res_ctx.pipe_ctx[i]);
			}

		process_deferred_updates(dc);

		dc->hwss.optimize_bandwidth(dc, context);

		if (dc->hwss.update_dsc_pg)
			dc->hwss.update_dsc_pg(dc, context, true);
	}

	dc->optimized_required = false;
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
			|| u->plane_info->dcc.dcc_ind_blk != u->surface->dcc.dcc_ind_blk
			|| u->plane_info->dcc.meta_pitch != u->surface->dcc.meta_pitch) {
		/* During DCC on/off, stutter period is calculated before
		 * DCC has fully transitioned. This results in incorrect
		 * stutter period calculation. Triggering a full update will
		 * recalculate stutter period.
		 */
		update_flags->bits.dcc_change = 1;
		elevate_update_type(&update_type, UPDATE_TYPE_FULL);
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
		const struct dc *dc,
		const struct dc_surface_update *u)
{
	union surface_update_flags *update_flags = &u->surface->update_flags;

	if (!u->scaling_info)
		return UPDATE_TYPE_FAST;

	if (u->scaling_info->dst_rect.width != u->surface->dst_rect.width
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

	if (u->scaling_info->src_rect.width > dc->caps.max_optimizable_video_width &&
		(u->scaling_info->clip_rect.width > u->surface->clip_rect.width ||
		 u->scaling_info->clip_rect.height > u->surface->clip_rect.height))
		 /* Changing clip size of a large surface may result in MPC slice count change */
		update_flags->bits.bandwidth_change = 1;

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

	if (!is_surface_in_context(context, u->surface) || u->surface->force_full_update) {
		update_flags->raw = 0xFFFFFFFF;
		return UPDATE_TYPE_FULL;
	}

	update_flags->raw = 0; // Reset all flags

	type = get_plane_info_update_type(u);
	elevate_update_type(&overall_type, type);

	type = get_scaling_info_update_type(dc, u);
	elevate_update_type(&overall_type, type);

	if (u->flip_addr) {
		update_flags->bits.addr_update = 1;
		if (u->flip_addr->address.tmz_surface != u->surface->address.tmz_surface) {
			update_flags->bits.tmz_changed = 1;
			elevate_update_type(&overall_type, UPDATE_TYPE_FULL);
		}
	}
	if (u->in_transfer_func)
		update_flags->bits.in_transfer_func_change = 1;

	if (u->input_csc_color_matrix)
		update_flags->bits.input_csc_change = 1;

	if (u->coeff_reduction_factor)
		update_flags->bits.coeff_reduction_change = 1;

	if (u->gamut_remap_matrix)
		update_flags->bits.gamut_remap_change = 1;

	if (u->blend_tf)
		update_flags->bits.gamma_change = 1;

	if (u->gamma) {
		enum surface_pixel_format format = SURFACE_PIXEL_FORMAT_GRPH_BEGIN;

		if (u->plane_info)
			format = u->plane_info->format;
		else if (u->surface)
			format = u->surface->format;

		if (dce_use_lut(format))
			update_flags->bits.gamma_change = 1;
	}

	if (u->lut3d_func || u->func_shaper)
		update_flags->bits.lut_3d = 1;

	if (u->hdr_mult.value)
		if (u->hdr_mult.value != u->surface->hdr_mult.value) {
			update_flags->bits.hdr_mult = 1;
			elevate_update_type(&overall_type, UPDATE_TYPE_MED);
		}

	if (update_flags->bits.in_transfer_func_change) {
		type = UPDATE_TYPE_MED;
		elevate_update_type(&overall_type, type);
	}

	if (update_flags->bits.lut_3d) {
		type = UPDATE_TYPE_FULL;
		elevate_update_type(&overall_type, type);
	}

	if (dc->debug.enable_legacy_fast_update &&
			(update_flags->bits.gamma_change ||
			update_flags->bits.gamut_remap_change ||
			update_flags->bits.input_csc_change ||
			update_flags->bits.coeff_reduction_change)) {
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

	if (dc->idle_optimizations_allowed)
		overall_type = UPDATE_TYPE_FULL;

	if (stream_status == NULL || stream_status->plane_count != surface_count)
		overall_type = UPDATE_TYPE_FULL;

	if (stream_update && stream_update->pending_test_pattern) {
		overall_type = UPDATE_TYPE_FULL;
	}

	/* some stream updates require passive update */
	if (stream_update) {
		union stream_update_flags *su_flags = &stream_update->stream->update_flags;

		if ((stream_update->src.height != 0 && stream_update->src.width != 0) ||
			(stream_update->dst.height != 0 && stream_update->dst.width != 0) ||
			stream_update->integer_scaling_update)
			su_flags->bits.scaling = 1;

		if (dc->debug.enable_legacy_fast_update && stream_update->out_transfer_func)
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

		if (stream_update->mst_bw_update)
			su_flags->bits.mst_bw = 1;

		if (stream_update->stream && stream_update->stream->freesync_on_desktop &&
			(stream_update->vrr_infopacket || stream_update->allow_freesync ||
				stream_update->vrr_active_variable || stream_update->vrr_active_fixed))
			su_flags->bits.fams_changed = 1;

		if (su_flags->raw != 0)
			overall_type = UPDATE_TYPE_FULL;

		if (stream_update->output_csc_transform || stream_update->output_color_space)
			su_flags->bits.out_csc = 1;

		/* Output transfer function changes do not require bandwidth recalculation,
		 * so don't trigger a full update
		 */
		if (!dc->debug.enable_legacy_fast_update && stream_update->out_transfer_func)
			su_flags->bits.out_tf = 1;
	}

	for (i = 0 ; i < surface_count; i++) {
		enum surface_update_type type =
				det_surface_update(dc, &updates[i]);

		elevate_update_type(&overall_type, type);
	}

	return overall_type;
}

/*
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

	if (update->periodic_interrupt)
		stream->periodic_interrupt = *update->periodic_interrupt;

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

	if (update->allow_freesync)
		stream->allow_freesync = *update->allow_freesync;

	if (update->vrr_active_variable)
		stream->vrr_active_variable = *update->vrr_active_variable;

	if (update->vrr_active_fixed)
		stream->vrr_active_fixed = *update->vrr_active_fixed;

	if (update->dpms_off)
		stream->dpms_off = *update->dpms_off;

	if (update->hfvsif_infopacket)
		stream->hfvsif_infopacket = *update->hfvsif_infopacket;

	if (update->vtem_infopacket)
		stream->vtem_infopacket = *update->vtem_infopacket;

	if (update->vsc_infopacket)
		stream->vsc_infopacket = *update->vsc_infopacket;

	if (update->vsp_infopacket)
		stream->vsp_infopacket = *update->vsp_infopacket;

	if (update->adaptive_sync_infopacket)
		stream->adaptive_sync_infopacket = *update->adaptive_sync_infopacket;

	if (update->dither_option)
		stream->dither_option = *update->dither_option;

	if (update->pending_test_pattern)
		stream->test_pattern = *update->pending_test_pattern;
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
		struct dc_state *dsc_validate_context = dc_state_create_copy(dc->current_state);

		if (dsc_validate_context) {
			stream->timing.dsc_cfg = *update->dsc_config;
			stream->timing.flags.DSC = enable_dsc;
			if (!dc->res_pool->funcs->validate_bandwidth(dc, dsc_validate_context, true)) {
				stream->timing.dsc_cfg = old_dsc_cfg;
				stream->timing.flags.DSC = old_dsc_enabled;
				update->dsc_config = NULL;
			}

			dc_state_release(dsc_validate_context);
		} else {
			DC_ERROR("Failed to allocate new validate context for DSC change\n");
			update->dsc_config = NULL;
		}
	}
}

static void backup_plane_states_for_stream(
		struct dc_plane_state plane_states[MAX_SURFACE_NUM],
		struct dc_stream_state *stream)
{
	int i;
	struct dc_stream_status *status = dc_stream_get_status(stream);

	if (!status)
		return;

	for (i = 0; i < status->plane_count; i++)
		plane_states[i] = *status->plane_states[i];
}

static void restore_plane_states_for_stream(
		struct dc_plane_state plane_states[MAX_SURFACE_NUM],
		struct dc_stream_state *stream)
{
	int i;
	struct dc_stream_status *status = dc_stream_get_status(stream);

	if (!status)
		return;

	for (i = 0; i < status->plane_count; i++)
		*status->plane_states[i] = plane_states[i];
}

static bool update_planes_and_stream_state(struct dc *dc,
		struct dc_surface_update *srf_updates, int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		enum surface_update_type *new_update_type,
		struct dc_state **new_context)
{
	struct dc_state *context;
	int i, j;
	enum surface_update_type update_type;
	const struct dc_stream_status *stream_status;
	struct dc_context *dc_ctx = dc->ctx;

	stream_status = dc_stream_get_status(stream);

	if (!stream_status) {
		if (surface_count) /* Only an error condition if surf_count non-zero*/
			ASSERT(false);

		return false; /* Cannot commit surface to stream that is not committed */
	}

	context = dc->current_state;
	backup_plane_states_for_stream(dc->current_state->scratch.plane_states, stream);
	update_type = dc_check_update_surfaces_for_stream(
			dc, srf_updates, surface_count, stream_update, stream_status);

	/* update current stream with the new updates */
	copy_stream_update_to_stream(dc, context, stream, stream_update);

	/* do not perform surface update if surface has invalid dimensions
	 * (all zero) and no scaling_info is provided
	 */
	if (surface_count > 0) {
		for (i = 0; i < surface_count; i++) {
			if ((srf_updates[i].surface->src_rect.width == 0 ||
				 srf_updates[i].surface->src_rect.height == 0 ||
				 srf_updates[i].surface->dst_rect.width == 0 ||
				 srf_updates[i].surface->dst_rect.height == 0) &&
				(!srf_updates[i].scaling_info ||
				  srf_updates[i].scaling_info->src_rect.width == 0 ||
				  srf_updates[i].scaling_info->src_rect.height == 0 ||
				  srf_updates[i].scaling_info->dst_rect.width == 0 ||
				  srf_updates[i].scaling_info->dst_rect.height == 0)) {
				DC_ERROR("Invalid src/dst rects in surface update!\n");
				return false;
			}
		}
	}

	if (update_type >= update_surface_trace_level)
		update_surface_trace(dc, srf_updates, surface_count);

	for (i = 0; i < surface_count; i++)
		copy_surface_update_to_plane(srf_updates[i].surface, &srf_updates[i]);

	if (update_type >= UPDATE_TYPE_FULL) {
		struct dc_plane_state *new_planes[MAX_SURFACES] = {0};

		for (i = 0; i < surface_count; i++)
			new_planes[i] = srf_updates[i].surface;

		/* initialize scratch memory for building context */
		context = dc_state_create_copy(dc->current_state);
		if (context == NULL) {
			DC_ERROR("Failed to allocate new validate context!\n");
			return false;
		}

		/* For each full update, remove all existing phantom pipes first.
		 * Ensures that we have enough pipes for newly added MPO planes
		 */
		dc_state_remove_phantom_streams_and_planes(dc, context);
		dc_state_release_phantom_streams_and_planes(dc, context);

		/*remove old surfaces from context */
		if (!dc_state_rem_all_planes_for_stream(dc, stream, context)) {

			BREAK_TO_DEBUGGER();
			goto fail;
		}

		/* add surface to context */
		if (!dc_state_add_all_planes_for_stream(dc, stream, new_planes, surface_count, context)) {

			BREAK_TO_DEBUGGER();
			goto fail;
		}
	}

	/* save update parameters into surface */
	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *surface = srf_updates[i].surface;

		if (update_type >= UPDATE_TYPE_MED) {
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

				if (pipe_ctx->plane_state != surface)
					continue;

				resource_build_scaling_params(pipe_ctx);
			}
		}
	}

	if (update_type == UPDATE_TYPE_FULL) {
		if (!dc->res_pool->funcs->validate_bandwidth(dc, context, false)) {
			BREAK_TO_DEBUGGER();
			goto fail;
		}

		for (i = 0; i < context->stream_count; i++) {
			struct pipe_ctx *otg_master = resource_get_otg_master_for_stream(&context->res_ctx,
					context->streams[i]);

			if (otg_master && otg_master->stream->test_pattern.type != DP_TEST_PATTERN_VIDEO_MODE)
				resource_build_test_pattern_params(&context->res_ctx, otg_master);
		}
	}

	*new_context = context;
	*new_update_type = update_type;
	backup_plane_states_for_stream(context->scratch.plane_states, stream);

	return true;

fail:
	dc_state_release(context);

	return false;

}

static void commit_planes_do_stream_update(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		enum surface_update_type update_type,
		struct dc_state *context)
{
	int j;

	// Stream updates
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (resource_is_pipe_type(pipe_ctx, OTG_MASTER) && pipe_ctx->stream == stream) {

			if (stream_update->periodic_interrupt && dc->hwss.setup_periodic_interrupt)
				dc->hwss.setup_periodic_interrupt(dc, pipe_ctx);

			if ((stream_update->hdr_static_metadata && !stream->use_dynamic_meta) ||
					stream_update->vrr_infopacket ||
					stream_update->vsc_infopacket ||
					stream_update->vsp_infopacket ||
					stream_update->hfvsif_infopacket ||
					stream_update->adaptive_sync_infopacket ||
					stream_update->vtem_infopacket) {
				resource_build_info_frame(pipe_ctx);
				dc->hwss.update_info_frame(pipe_ctx);

				if (dc_is_dp_signal(pipe_ctx->stream->signal))
					dc->link_srv->dp_trace_source_sequence(
							pipe_ctx->stream->link,
							DPCD_SOURCE_SEQ_AFTER_UPDATE_INFO_FRAME);
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
				dc->link_srv->update_dsc_config(pipe_ctx);

			if (stream_update->mst_bw_update) {
				if (stream_update->mst_bw_update->is_increase)
					dc->link_srv->increase_mst_payload(pipe_ctx,
							stream_update->mst_bw_update->mst_stream_bw);
 				else
					dc->link_srv->reduce_mst_payload(pipe_ctx,
							stream_update->mst_bw_update->mst_stream_bw);
 			}

			if (stream_update->pending_test_pattern) {
				dc_link_dp_set_test_pattern(stream->link,
					stream->test_pattern.type,
					stream->test_pattern.color_space,
					stream->test_pattern.p_link_settings,
					stream->test_pattern.p_custom_pattern,
					stream->test_pattern.cust_pattern_size);
			}

			if (stream_update->dpms_off) {
				if (*stream_update->dpms_off) {
					dc->link_srv->set_dpms_off(pipe_ctx);
					/* for dpms, keep acquired resources*/
					if (pipe_ctx->stream_res.audio && !dc->debug.az_endpoint_mute_only)
						pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

					dc->optimized_required = true;

				} else {
					if (get_seamless_boot_stream_count(context) == 0)
						dc->hwss.prepare_bandwidth(dc, dc->current_state);
					dc->link_srv->set_dpms_on(dc->current_state, pipe_ctx);
				}
			} else if (pipe_ctx->stream->link->wa_flags.blank_stream_on_ocs_change && stream_update->output_color_space
					&& !stream->dpms_off && dc_is_dp_signal(pipe_ctx->stream->signal)) {
				/*
				 * Workaround for firmware issue in some receivers where they don't pick up
				 * correct output color space unless DP link is disabled/re-enabled
				 */
				dc->link_srv->set_dpms_on(dc->current_state, pipe_ctx);
			}

			if (stream_update->abm_level && pipe_ctx->stream_res.abm) {
				bool should_program_abm = true;

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

static bool dc_dmub_should_send_dirty_rect_cmd(struct dc *dc, struct dc_stream_state *stream)
{
	if ((stream->link->psr_settings.psr_version == DC_PSR_VERSION_SU_1
			|| stream->link->psr_settings.psr_version == DC_PSR_VERSION_1)
			&& stream->ctx->dce_version >= DCN_VERSION_3_1)
		return true;

	if (stream->link->replay_settings.config.replay_supported)
		return true;

	return false;
}

void dc_dmub_update_dirty_rect(struct dc *dc,
			       int surface_count,
			       struct dc_stream_state *stream,
			       struct dc_surface_update *srf_updates,
			       struct dc_state *context)
{
	union dmub_rb_cmd cmd;
	struct dmub_cmd_update_dirty_rect_data *update_dirty_rect;
	unsigned int i, j;
	unsigned int panel_inst = 0;

	if (!dc_dmub_should_send_dirty_rect_cmd(dc, stream))
		return;

	if (!dc_get_edp_link_panel_inst(dc, stream->link, &panel_inst))
		return;

	memset(&cmd, 0x0, sizeof(cmd));
	cmd.update_dirty_rect.header.type = DMUB_CMD__UPDATE_DIRTY_RECT;
	cmd.update_dirty_rect.header.sub_type = 0;
	cmd.update_dirty_rect.header.payload_bytes =
		sizeof(cmd.update_dirty_rect) -
		sizeof(cmd.update_dirty_rect.header);
	update_dirty_rect = &cmd.update_dirty_rect.update_dirty_rect_data;
	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *plane_state = srf_updates[i].surface;
		const struct dc_flip_addrs *flip_addr = srf_updates[i].flip_addr;

		if (!srf_updates[i].surface || !flip_addr)
			continue;
		/* Do not send in immediate flip mode */
		if (srf_updates[i].surface->flip_immediate)
			continue;

		update_dirty_rect->dirty_rect_count = flip_addr->dirty_rect_count;
		memcpy(update_dirty_rect->src_dirty_rects, flip_addr->dirty_rects,
				sizeof(flip_addr->dirty_rects));
		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

			if (pipe_ctx->stream != stream)
				continue;
			if (pipe_ctx->plane_state != plane_state)
				continue;

			update_dirty_rect->panel_inst = panel_inst;
			update_dirty_rect->pipe_idx = j;
			dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);
		}
	}
}

static void build_dmub_update_dirty_rect(
		struct dc *dc,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_surface_update *srf_updates,
		struct dc_state *context,
		struct dc_dmub_cmd dc_dmub_cmd[],
		unsigned int *dmub_cmd_count)
{
	union dmub_rb_cmd cmd;
	struct dmub_cmd_update_dirty_rect_data *update_dirty_rect;
	unsigned int i, j;
	unsigned int panel_inst = 0;

	if (!dc_dmub_should_send_dirty_rect_cmd(dc, stream))
		return;

	if (!dc_get_edp_link_panel_inst(dc, stream->link, &panel_inst))
		return;

	memset(&cmd, 0x0, sizeof(cmd));
	cmd.update_dirty_rect.header.type = DMUB_CMD__UPDATE_DIRTY_RECT;
	cmd.update_dirty_rect.header.sub_type = 0;
	cmd.update_dirty_rect.header.payload_bytes =
		sizeof(cmd.update_dirty_rect) -
		sizeof(cmd.update_dirty_rect.header);
	update_dirty_rect = &cmd.update_dirty_rect.update_dirty_rect_data;
	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *plane_state = srf_updates[i].surface;
		const struct dc_flip_addrs *flip_addr = srf_updates[i].flip_addr;

		if (!srf_updates[i].surface || !flip_addr)
			continue;
		/* Do not send in immediate flip mode */
		if (srf_updates[i].surface->flip_immediate)
			continue;
		update_dirty_rect->cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
		update_dirty_rect->dirty_rect_count = flip_addr->dirty_rect_count;
		memcpy(update_dirty_rect->src_dirty_rects, flip_addr->dirty_rects,
				sizeof(flip_addr->dirty_rects));
		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

			if (pipe_ctx->stream != stream)
				continue;
			if (pipe_ctx->plane_state != plane_state)
				continue;
			update_dirty_rect->panel_inst = panel_inst;
			update_dirty_rect->pipe_idx = j;
			dc_dmub_cmd[*dmub_cmd_count].dmub_cmd = cmd;
			dc_dmub_cmd[*dmub_cmd_count].wait_type = DM_DMUB_WAIT_TYPE_NO_WAIT;
			(*dmub_cmd_count)++;
		}
	}
}


/**
 * build_dmub_cmd_list() - Build an array of DMCUB commands to be sent to DMCUB
 *
 * @dc: Current DC state
 * @srf_updates: Array of surface updates
 * @surface_count: Number of surfaces that have an updated
 * @stream: Corresponding stream to be updated in the current flip
 * @context: New DC state to be programmed
 *
 * @dc_dmub_cmd: Array of DMCUB commands to be sent to DMCUB
 * @dmub_cmd_count: Count indicating the number of DMCUB commands in dc_dmub_cmd array
 *
 * This function builds an array of DMCUB commands to be sent to DMCUB. This function is required
 * to build an array of commands and have them sent while the OTG lock is acquired.
 *
 * Return: void
 */
static void build_dmub_cmd_list(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_state *context,
		struct dc_dmub_cmd dc_dmub_cmd[],
		unsigned int *dmub_cmd_count)
{
	// Initialize cmd count to 0
	*dmub_cmd_count = 0;
	build_dmub_update_dirty_rect(dc, surface_count, stream, srf_updates, context, dc_dmub_cmd, dmub_cmd_count);
}

static void commit_planes_for_stream_fast(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		enum surface_update_type update_type,
		struct dc_state *context)
{
	int i, j;
	struct pipe_ctx *top_pipe_to_program = NULL;
	struct dc_stream_status *stream_status = NULL;
	dc_z10_restore(dc);

	top_pipe_to_program = resource_get_otg_master_for_stream(
			&context->res_ctx,
			stream);

	if (dc->debug.visual_confirm) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			if (pipe->stream && pipe->plane_state)
				dc_update_viusal_confirm_color(dc, context, pipe);
		}
	}

	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *plane_state = srf_updates[i].surface;
		/*set logical flag for lock/unlock use*/
		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

			if (!pipe_ctx->plane_state)
				continue;
			if (should_update_pipe_for_plane(context, pipe_ctx, plane_state))
				continue;
			pipe_ctx->plane_state->triplebuffer_flips = false;
			if (update_type == UPDATE_TYPE_FAST &&
			    dc->hwss.program_triplebuffer &&
			    !pipe_ctx->plane_state->flip_immediate && dc->debug.enable_tri_buf) {
				/*triple buffer for VUpdate  only*/
				pipe_ctx->plane_state->triplebuffer_flips = true;
			}
		}
	}

	stream_status = dc_state_get_stream_status(context, stream);

	build_dmub_cmd_list(dc,
			srf_updates,
			surface_count,
			stream,
			context,
			context->dc_dmub_cmd,
			&(context->dmub_cmd_count));
	hwss_build_fast_sequence(dc,
			context->dc_dmub_cmd,
			context->dmub_cmd_count,
			context->block_sequence,
			&(context->block_sequence_steps),
			top_pipe_to_program,
			stream_status);
	hwss_execute_sequence(dc,
			context->block_sequence,
			context->block_sequence_steps);
	/* Clear update flags so next flip doesn't have redundant programming
	 * (if there's no stream update, the update flags are not cleared).
	 * Surface updates are cleared unconditionally at the beginning of each flip,
	 * so no need to clear here.
	 */
	if (top_pipe_to_program->stream)
		top_pipe_to_program->stream->update_flags.raw = 0;
}

static void wait_for_outstanding_hw_updates(struct dc *dc, const struct dc_state *dc_context)
{
/*
 * This function calls HWSS to wait for any potentially double buffered
 * operations to complete. It should be invoked as a pre-amble prior
 * to full update programming before asserting any HW locks.
 */
	int pipe_idx;
	int opp_inst;
	int opp_count = dc->res_pool->res_cap->num_opp;
	struct hubp *hubp;
	int mpcc_inst;
	const struct pipe_ctx *pipe_ctx;

	for (pipe_idx = 0; pipe_idx < dc->res_pool->pipe_count; pipe_idx++) {
		pipe_ctx = &dc_context->res_ctx.pipe_ctx[pipe_idx];

		if (!pipe_ctx->stream)
			continue;

		if (pipe_ctx->stream_res.tg->funcs->wait_drr_doublebuffer_pending_clear)
			pipe_ctx->stream_res.tg->funcs->wait_drr_doublebuffer_pending_clear(pipe_ctx->stream_res.tg);

		hubp = pipe_ctx->plane_res.hubp;
		if (!hubp)
			continue;

		mpcc_inst = hubp->inst;
		// MPCC inst is equal to pipe index in practice
		for (opp_inst = 0; opp_inst < opp_count; opp_inst++) {
			if ((dc->res_pool->opps[opp_inst] != NULL) &&
				(dc->res_pool->opps[opp_inst]->mpcc_disconnect_pending[mpcc_inst])) {
				dc->res_pool->mpc->funcs->wait_for_idle(dc->res_pool->mpc, mpcc_inst);
				dc->res_pool->opps[opp_inst]->mpcc_disconnect_pending[mpcc_inst] = false;
				break;
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
	int i, j;
	struct pipe_ctx *top_pipe_to_program = NULL;
	bool should_lock_all_pipes = (update_type != UPDATE_TYPE_FAST);
	bool subvp_prev_use = false;
	bool subvp_curr_use = false;
	uint8_t current_stream_mask = 0;

	// Once we apply the new subvp context to hardware it won't be in the
	// dc->current_state anymore, so we have to cache it before we apply
	// the new SubVP context
	subvp_prev_use = false;
	dc_z10_restore(dc);
	if (update_type == UPDATE_TYPE_FULL)
		wait_for_outstanding_hw_updates(dc, context);

	if (update_type == UPDATE_TYPE_FULL) {
		dc_allow_idle_optimizations(dc, false);

		if (get_seamless_boot_stream_count(context) == 0)
			dc->hwss.prepare_bandwidth(dc, context);

		if (dc->hwss.update_dsc_pg)
			dc->hwss.update_dsc_pg(dc, context, false);

		context_clock_trace(dc, context);
	}

	top_pipe_to_program = resource_get_otg_master_for_stream(
				&context->res_ctx,
				stream);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		// Check old context for SubVP
		subvp_prev_use |= (dc_state_get_pipe_subvp_type(dc->current_state, old_pipe) == SUBVP_PHANTOM);
		if (subvp_prev_use)
			break;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM) {
			subvp_curr_use = true;
			break;
		}
	}

	if (dc->debug.visual_confirm)
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			if (pipe->stream && pipe->plane_state)
				dc_update_viusal_confirm_color(dc, context, pipe);
		}

	if (stream->test_pattern.type != DP_TEST_PATTERN_VIDEO_MODE) {
		struct pipe_ctx *mpcc_pipe;
		struct pipe_ctx *odm_pipe;

		for (mpcc_pipe = top_pipe_to_program; mpcc_pipe; mpcc_pipe = mpcc_pipe->bottom_pipe)
			for (odm_pipe = mpcc_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
				odm_pipe->ttu_regs.min_ttu_vblank = MAX_TTU;
	}

	if ((update_type != UPDATE_TYPE_FAST) && stream->update_flags.bits.dsc_changed)
		if (top_pipe_to_program &&
			top_pipe_to_program->stream_res.tg->funcs->lock_doublebuffer_enable) {
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

	if (should_lock_all_pipes && dc->hwss.interdependent_update_lock) {
		if (dc->hwss.subvp_pipe_control_lock)
				dc->hwss.subvp_pipe_control_lock(dc, context, true, should_lock_all_pipes, NULL, subvp_prev_use);
		dc->hwss.interdependent_update_lock(dc, context, true);

	} else {
		if (dc->hwss.subvp_pipe_control_lock)
			dc->hwss.subvp_pipe_control_lock(dc, context, true, should_lock_all_pipes, top_pipe_to_program, subvp_prev_use);
		/* Lock the top pipe while updating plane addrs, since freesync requires
		 *  plane addr update event triggers to be synchronized.
		 *  top_pipe_to_program is expected to never be NULL
		 */
		dc->hwss.pipe_control_lock(dc, top_pipe_to_program, true);
	}

	dc_dmub_update_dirty_rect(dc, surface_count, stream, srf_updates, context);

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

		if (should_lock_all_pipes && dc->hwss.interdependent_update_lock) {
			dc->hwss.interdependent_update_lock(dc, context, false);
		} else {
			dc->hwss.pipe_control_lock(dc, top_pipe_to_program, false);
		}
		dc->hwss.post_unlock_program_front_end(dc, context);

		if (update_type != UPDATE_TYPE_FAST)
			if (dc->hwss.commit_subvp_config)
				dc->hwss.commit_subvp_config(dc, context);

		/* Since phantom pipe programming is moved to post_unlock_program_front_end,
		 * move the SubVP lock to after the phantom pipes have been setup
		 */
		if (dc->hwss.subvp_pipe_control_lock)
			dc->hwss.subvp_pipe_control_lock(dc, context, false, should_lock_all_pipes,
							 NULL, subvp_prev_use);
		return;
	}

	if (update_type != UPDATE_TYPE_FAST) {
		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

			if ((dc->debug.visual_confirm == VISUAL_CONFIRM_SUBVP ||
				dc->debug.visual_confirm == VISUAL_CONFIRM_MCLK_SWITCH) &&
				pipe_ctx->stream && pipe_ctx->plane_state) {
				/* Only update visual confirm for SUBVP and Mclk switching here.
				 * The bar appears on all pipes, so we need to update the bar on all displays,
				 * so the information doesn't get stale.
				 */
				dc->hwss.update_visual_confirm_color(dc, pipe_ctx,
						pipe_ctx->plane_res.hubp->inst);
			}
		}
	}

	for (i = 0; i < surface_count; i++) {
		struct dc_plane_state *plane_state = srf_updates[i].surface;
		/*set logical flag for lock/unlock use*/
		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];
			if (!pipe_ctx->plane_state)
				continue;
			if (should_update_pipe_for_plane(context, pipe_ctx, plane_state))
				continue;
			pipe_ctx->plane_state->triplebuffer_flips = false;
			if (update_type == UPDATE_TYPE_FAST &&
				dc->hwss.program_triplebuffer != NULL &&
				!pipe_ctx->plane_state->flip_immediate && dc->debug.enable_tri_buf) {
					/*triple buffer for VUpdate  only*/
					pipe_ctx->plane_state->triplebuffer_flips = true;
			}
		}
		if (update_type == UPDATE_TYPE_FULL) {
			/* force vsync flip when reconfiguring pipes to prevent underflow */
			plane_state->flip_immediate = false;
		}
	}

	// Update Type FULL, Surface updates
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (!pipe_ctx->top_pipe &&
			!pipe_ctx->prev_odm_pipe &&
			should_update_pipe_for_stream(context, pipe_ctx, stream)) {
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
		if (dc->debug.validate_dml_output) {
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				struct pipe_ctx *cur_pipe = &context->res_ctx.pipe_ctx[i];
				if (cur_pipe->stream == NULL)
					continue;

				cur_pipe->plane_res.hubp->funcs->validate_dml_output(
						cur_pipe->plane_res.hubp, dc->ctx,
						&context->res_ctx.pipe_ctx[i].rq_regs,
						&context->res_ctx.pipe_ctx[i].dlg_regs,
						&context->res_ctx.pipe_ctx[i].ttu_regs);
			}
		}
	}

	// Update Type FAST, Surface updates
	if (update_type == UPDATE_TYPE_FAST) {
		if (dc->hwss.set_flip_control_gsl)
			for (i = 0; i < surface_count; i++) {
				struct dc_plane_state *plane_state = srf_updates[i].surface;

				for (j = 0; j < dc->res_pool->pipe_count; j++) {
					struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

					if (!should_update_pipe_for_stream(context, pipe_ctx, stream))
						continue;

					if (!should_update_pipe_for_plane(context, pipe_ctx, plane_state))
						continue;

					// GSL has to be used for flip immediate
					dc->hwss.set_flip_control_gsl(pipe_ctx,
							pipe_ctx->plane_state->flip_immediate);
				}
			}

		/* Perform requested Updates */
		for (i = 0; i < surface_count; i++) {
			struct dc_plane_state *plane_state = srf_updates[i].surface;

			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

				if (!should_update_pipe_for_stream(context, pipe_ctx, stream))
					continue;

				if (!should_update_pipe_for_plane(context, pipe_ctx, plane_state))
					continue;

				/*program triple buffer after lock based on flip type*/
				if (dc->hwss.program_triplebuffer != NULL && dc->debug.enable_tri_buf) {
					/*only enable triplebuffer for  fast_update*/
					dc->hwss.program_triplebuffer(
						dc, pipe_ctx, pipe_ctx->plane_state->triplebuffer_flips);
				}
				if (pipe_ctx->plane_state->update_flags.bits.addr_update)
					dc->hwss.update_plane_addr(dc, pipe_ctx);
			}
		}
	}

	if (should_lock_all_pipes && dc->hwss.interdependent_update_lock) {
		dc->hwss.interdependent_update_lock(dc, context, false);
	} else {
		dc->hwss.pipe_control_lock(dc, top_pipe_to_program, false);
	}

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

			if (should_use_dmub_lock(stream->link)) {
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

	if (subvp_curr_use) {
		/* If enabling subvp or transitioning from subvp->subvp, enable the
		 * phantom streams before we program front end for the phantom pipes.
		 */
		if (update_type != UPDATE_TYPE_FAST) {
			if (dc->hwss.enable_phantom_streams)
				dc->hwss.enable_phantom_streams(dc, context);
		}
	}

	if (update_type != UPDATE_TYPE_FAST)
		dc->hwss.post_unlock_program_front_end(dc, context);

	if (subvp_prev_use && !subvp_curr_use) {
		/* If disabling subvp, disable phantom streams after front end
		 * programming has completed (we turn on phantom OTG in order
		 * to complete the plane disable for phantom pipes).
		 */
		dc->hwss.apply_ctx_to_hw(dc, context);
	}

	if (update_type != UPDATE_TYPE_FAST)
		if (dc->hwss.commit_subvp_config)
			dc->hwss.commit_subvp_config(dc, context);
	/* Since phantom pipe programming is moved to post_unlock_program_front_end,
	 * move the SubVP lock to after the phantom pipes have been setup
	 */
	if (should_lock_all_pipes && dc->hwss.interdependent_update_lock) {
		if (dc->hwss.subvp_pipe_control_lock)
			dc->hwss.subvp_pipe_control_lock(dc, context, false, should_lock_all_pipes, NULL, subvp_prev_use);
	} else {
		if (dc->hwss.subvp_pipe_control_lock)
			dc->hwss.subvp_pipe_control_lock(dc, context, false, should_lock_all_pipes, top_pipe_to_program, subvp_prev_use);
	}

	// Fire manual trigger only when bottom plane is flipped
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (!pipe_ctx->plane_state)
			continue;

		if (pipe_ctx->bottom_pipe || pipe_ctx->next_odm_pipe ||
				!pipe_ctx->stream || !should_update_pipe_for_stream(context, pipe_ctx, stream) ||
				!pipe_ctx->plane_state->update_flags.bits.addr_update ||
				pipe_ctx->plane_state->skip_manual_trigger)
			continue;

		if (pipe_ctx->stream_res.tg->funcs->program_manual_trigger)
			pipe_ctx->stream_res.tg->funcs->program_manual_trigger(pipe_ctx->stream_res.tg);
	}

	current_stream_mask = get_stream_mask(dc, context);
	if (current_stream_mask != context->stream_mask) {
		context->stream_mask = current_stream_mask;
		dc_dmub_srv_notify_stream_mask(dc->ctx->dmub_srv, current_stream_mask);
	}
}

/**
 * could_mpcc_tree_change_for_active_pipes - Check if an OPP associated with MPCC might change
 *
 * @dc: Used to get the current state status
 * @stream: Target stream, which we want to remove the attached planes
 * @srf_updates: Array of surface updates
 * @surface_count: Number of surface update
 * @is_plane_addition: [in] Fill out with true if it is a plane addition case
 *
 * DCN32x and newer support a feature named Dynamic ODM which can conflict with
 * the MPO if used simultaneously in some specific configurations (e.g.,
 * 4k@144). This function checks if the incoming context requires applying a
 * transition state with unnecessary pipe splitting and ODM disabled to
 * circumvent our hardware limitations to prevent this edge case. If the OPP
 * associated with an MPCC might change due to plane additions, this function
 * returns true.
 *
 * Return:
 * Return true if OPP and MPCC might change, otherwise, return false.
 */
static bool could_mpcc_tree_change_for_active_pipes(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_surface_update *srf_updates,
		int surface_count,
		bool *is_plane_addition)
{

	struct dc_stream_status *cur_stream_status = stream_get_status(dc->current_state, stream);
	bool force_minimal_pipe_splitting = false;
	bool subvp_active = false;
	uint32_t i;

	*is_plane_addition = false;

	if (cur_stream_status &&
			dc->current_state->stream_count > 0 &&
			dc->debug.pipe_split_policy != MPC_SPLIT_AVOID) {
		/* determine if minimal transition is required due to MPC*/
		if (surface_count > 0) {
			if (cur_stream_status->plane_count > surface_count) {
				force_minimal_pipe_splitting = true;
			} else if (cur_stream_status->plane_count < surface_count) {
				force_minimal_pipe_splitting = true;
				*is_plane_addition = true;
			}
		}
	}

	if (cur_stream_status &&
			dc->current_state->stream_count == 1 &&
			dc->debug.enable_single_display_2to1_odm_policy) {
		/* determine if minimal transition is required due to dynamic ODM*/
		if (surface_count > 0) {
			if (cur_stream_status->plane_count > 2 && cur_stream_status->plane_count > surface_count) {
				force_minimal_pipe_splitting = true;
			} else if (surface_count > 2 && cur_stream_status->plane_count < surface_count) {
				force_minimal_pipe_splitting = true;
				*is_plane_addition = true;
			}
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (dc_state_get_pipe_subvp_type(dc->current_state, pipe) != SUBVP_NONE) {
			subvp_active = true;
			break;
		}
	}

	/* For SubVP when adding or removing planes we need to add a minimal transition
	 * (even when disabling all planes). Whenever disabling a phantom pipe, we
	 * must use the minimal transition path to disable the pipe correctly.
	 *
	 * We want to use the minimal transition whenever subvp is active, not only if
	 * a plane is being added / removed from a subvp stream (MPO plane can be added
	 * to a DRR pipe of SubVP + DRR config, in which case we still want to run through
	 * a min transition to disable subvp.
	 */
	if (cur_stream_status && subvp_active) {
		/* determine if minimal transition is required due to SubVP*/
		if (cur_stream_status->plane_count > surface_count) {
			force_minimal_pipe_splitting = true;
		} else if (cur_stream_status->plane_count < surface_count) {
			force_minimal_pipe_splitting = true;
			*is_plane_addition = true;
		}
	}

	return force_minimal_pipe_splitting;
}

struct pipe_split_policy_backup {
	bool dynamic_odm_policy;
	bool subvp_policy;
	enum pipe_split_policy mpc_policy;
};

static void release_minimal_transition_state(struct dc *dc,
		struct dc_state *context, struct pipe_split_policy_backup *policy)
{
	dc_state_release(context);
	/* restore previous pipe split and odm policy */
	if (!dc->config.is_vmin_only_asic)
		dc->debug.pipe_split_policy = policy->mpc_policy;
	dc->debug.enable_single_display_2to1_odm_policy = policy->dynamic_odm_policy;
	dc->debug.force_disable_subvp = policy->subvp_policy;
}

static struct dc_state *create_minimal_transition_state(struct dc *dc,
		struct dc_state *base_context, struct pipe_split_policy_backup *policy)
{
	struct dc_state *minimal_transition_context = NULL;
	unsigned int i, j;

	if (!dc->config.is_vmin_only_asic) {
		policy->mpc_policy = dc->debug.pipe_split_policy;
		dc->debug.pipe_split_policy = MPC_SPLIT_AVOID;
	}
	policy->dynamic_odm_policy = dc->debug.enable_single_display_2to1_odm_policy;
	dc->debug.enable_single_display_2to1_odm_policy = false;
	policy->subvp_policy = dc->debug.force_disable_subvp;
	dc->debug.force_disable_subvp = true;

	minimal_transition_context = dc_state_create_copy(base_context);

	/* commit minimal state */
	if (dc->res_pool->funcs->validate_bandwidth(dc, minimal_transition_context, false)) {
		for (i = 0; i < minimal_transition_context->stream_count; i++) {
			struct dc_stream_status *stream_status = &minimal_transition_context->stream_status[i];

			for (j = 0; j < stream_status->plane_count; j++) {
				struct dc_plane_state *plane_state = stream_status->plane_states[j];

				/* force vsync flip when reconfiguring pipes to prevent underflow
				 * and corruption
				 */
				plane_state->flip_immediate = false;
			}
		}
	} else {
		/* this should never happen */
		release_minimal_transition_state(dc, minimal_transition_context, policy);
		BREAK_TO_DEBUGGER();
		minimal_transition_context = NULL;
	}
	return minimal_transition_context;
}

static bool commit_minimal_transition_state_for_windowed_mpo_odm(struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	bool success = false;
	struct dc_state *minimal_transition_context;
	struct pipe_split_policy_backup policy;

	/* commit based on new context */
	/* Since all phantom pipes are removed in full validation,
	 * we have to save and restore the subvp/mall config when
	 * we do a minimal transition since the flags marking the
	 * pipe as subvp/phantom will be cleared (dc copy constructor
	 * creates a shallow copy).
	 */
	minimal_transition_context = create_minimal_transition_state(dc,
			context, &policy);
	if (minimal_transition_context) {
		if (dc->hwss.is_pipe_topology_transition_seamless(
					dc, dc->current_state, minimal_transition_context) &&
			dc->hwss.is_pipe_topology_transition_seamless(
					dc, minimal_transition_context, context)) {
			DC_LOG_DC("%s base = new state\n", __func__);

			success = dc_commit_state_no_check(dc, minimal_transition_context) == DC_OK;
		}
		release_minimal_transition_state(dc, minimal_transition_context, &policy);
	}

	if (!success) {
		/* commit based on current context */
		restore_plane_states_for_stream(dc->current_state->scratch.plane_states, stream);
		minimal_transition_context = create_minimal_transition_state(dc,
				dc->current_state, &policy);
		if (minimal_transition_context) {
			if (dc->hwss.is_pipe_topology_transition_seamless(
					dc, dc->current_state, minimal_transition_context) &&
				dc->hwss.is_pipe_topology_transition_seamless(
						dc, minimal_transition_context, context)) {
				DC_LOG_DC("%s base = current state\n", __func__);
				success = dc_commit_state_no_check(dc, minimal_transition_context) == DC_OK;
			}
			release_minimal_transition_state(dc, minimal_transition_context, &policy);
		}
		restore_plane_states_for_stream(context->scratch.plane_states, stream);
	}

	ASSERT(success);
	return success;
}

/**
 * commit_minimal_transition_state - Create a transition pipe split state
 *
 * @dc: Used to get the current state status
 * @transition_base_context: New transition state
 *
 * In some specific configurations, such as pipe split on multi-display with
 * MPO and/or Dynamic ODM, removing a plane may cause unsupported pipe
 * programming when moving to new planes. To mitigate those types of problems,
 * this function adds a transition state that minimizes pipe usage before
 * programming the new configuration. When adding a new plane, the current
 * state requires the least pipes, so it is applied without splitting. When
 * removing a plane, the new state requires the least pipes, so it is applied
 * without splitting.
 *
 * Return:
 * Return false if something is wrong in the transition state.
 */
static bool commit_minimal_transition_state(struct dc *dc,
		struct dc_state *transition_base_context)
{
	struct dc_state *transition_context;
	struct pipe_split_policy_backup policy;
	enum dc_status ret = DC_ERROR_UNEXPECTED;
	unsigned int i, j;
	unsigned int pipe_in_use = 0;
	bool subvp_in_use = false;
	bool odm_in_use = false;

	/* check current pipes in use*/
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &transition_base_context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state)
			pipe_in_use++;
	}

	/* If SubVP is enabled and we are adding or removing planes from any main subvp
	 * pipe, we must use the minimal transition.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream && dc_state_get_pipe_subvp_type(dc->current_state, pipe) == SUBVP_PHANTOM) {
			subvp_in_use = true;
			break;
		}
	}

	/* If ODM is enabled and we are adding or removing planes from any ODM
	 * pipe, we must use the minimal transition.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &transition_base_context->res_ctx.pipe_ctx[i];

		if (resource_is_pipe_type(pipe, OTG_MASTER)) {
			odm_in_use = resource_get_odm_slice_count(pipe) > 1;
			break;
		}
	}

	/* When the OS add a new surface if we have been used all of pipes with odm combine
	 * and mpc split feature, it need use commit_minimal_transition_state to transition safely.
	 * After OS exit MPO, it will back to use odm and mpc split with all of pipes, we need
	 * call it again. Otherwise return true to skip.
	 *
	 * Reduce the scenarios to use dc_commit_state_no_check in the stage of flip. Especially
	 * enter/exit MPO when DCN still have enough resources.
	 */
	if (pipe_in_use != dc->res_pool->pipe_count && !subvp_in_use && !odm_in_use)
		return true;

	DC_LOG_DC("%s base = %s state, reason = %s\n", __func__,
			dc->current_state == transition_base_context ? "current" : "new",
			subvp_in_use ? "Subvp In Use" :
			odm_in_use ? "ODM in Use" :
			dc->debug.pipe_split_policy != MPC_SPLIT_AVOID ? "MPC in Use" :
			"Unknown");

	transition_context = create_minimal_transition_state(dc,
			transition_base_context, &policy);
	if (transition_context) {
		ret = dc_commit_state_no_check(dc, transition_context);
		release_minimal_transition_state(dc, transition_context, &policy);
	}

	if (ret != DC_OK) {
		/* this should never happen */
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* force full surface update */
	for (i = 0; i < dc->current_state->stream_count; i++) {
		for (j = 0; j < dc->current_state->stream_status[i].plane_count; j++) {
			dc->current_state->stream_status[i].plane_states[j]->update_flags.raw = 0xFFFFFFFF;
		}
	}

	return true;
}

/**
 * update_seamless_boot_flags() - Helper function for updating seamless boot flags
 *
 * @dc: Current DC state
 * @context: New DC state to be programmed
 * @surface_count: Number of surfaces that have an updated
 * @stream: Corresponding stream to be updated in the current flip
 *
 * Updating seamless boot flags do not need to be part of the commit sequence. This
 * helper function will update the seamless boot flags on each flip (if required)
 * outside of the HW commit sequence (fast or slow).
 *
 * Return: void
 */
static void update_seamless_boot_flags(struct dc *dc,
		struct dc_state *context,
		int surface_count,
		struct dc_stream_state *stream)
{
	if (get_seamless_boot_stream_count(context) > 0 && surface_count > 0) {
		/* Optimize seamless boot flag keeps clocks and watermarks high until
		 * first flip. After first flip, optimization is required to lower
		 * bandwidth. Important to note that it is expected UEFI will
		 * only light up a single display on POST, therefore we only expect
		 * one stream with seamless boot flag set.
		 */
		if (stream->apply_seamless_boot_optimization) {
			stream->apply_seamless_boot_optimization = false;

			if (get_seamless_boot_stream_count(context) == 0)
				dc->optimized_required = true;
		}
	}
}

static void populate_fast_updates(struct dc_fast_update *fast_update,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_update *stream_update)
{
	int i = 0;

	if (stream_update) {
		fast_update[0].out_transfer_func = stream_update->out_transfer_func;
		fast_update[0].output_csc_transform = stream_update->output_csc_transform;
	}

	for (i = 0; i < surface_count; i++) {
		fast_update[i].flip_addr = srf_updates[i].flip_addr;
		fast_update[i].gamma = srf_updates[i].gamma;
		fast_update[i].gamut_remap_matrix = srf_updates[i].gamut_remap_matrix;
		fast_update[i].input_csc_color_matrix = srf_updates[i].input_csc_color_matrix;
		fast_update[i].coeff_reduction_factor = srf_updates[i].coeff_reduction_factor;
	}
}

static bool fast_updates_exist(struct dc_fast_update *fast_update, int surface_count)
{
	int i;

	if (fast_update[0].out_transfer_func ||
		fast_update[0].output_csc_transform)
		return true;

	for (i = 0; i < surface_count; i++) {
		if (fast_update[i].flip_addr ||
				fast_update[i].gamma ||
				fast_update[i].gamut_remap_matrix ||
				fast_update[i].input_csc_color_matrix ||
				fast_update[i].coeff_reduction_factor)
			return true;
	}

	return false;
}

static bool full_update_required(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		struct dc_stream_state *stream)
{

	int i;
	struct dc_stream_status *stream_status;
	const struct dc_state *context = dc->current_state;

	for (i = 0; i < surface_count; i++) {
		if (srf_updates &&
				(srf_updates[i].plane_info ||
				srf_updates[i].scaling_info ||
				(srf_updates[i].hdr_mult.value &&
				srf_updates[i].hdr_mult.value != srf_updates->surface->hdr_mult.value) ||
				srf_updates[i].in_transfer_func ||
				srf_updates[i].func_shaper ||
				srf_updates[i].lut3d_func ||
				srf_updates[i].surface->force_full_update ||
				(srf_updates[i].flip_addr &&
				srf_updates[i].flip_addr->address.tmz_surface != srf_updates[i].surface->address.tmz_surface) ||
				!is_surface_in_context(context, srf_updates[i].surface)))
			return true;
	}

	if (stream_update &&
			(((stream_update->src.height != 0 && stream_update->src.width != 0) ||
			(stream_update->dst.height != 0 && stream_update->dst.width != 0) ||
			stream_update->integer_scaling_update) ||
			stream_update->hdr_static_metadata ||
			stream_update->abm_level ||
			stream_update->periodic_interrupt ||
			stream_update->vrr_infopacket ||
			stream_update->vsc_infopacket ||
			stream_update->vsp_infopacket ||
			stream_update->hfvsif_infopacket ||
			stream_update->vtem_infopacket ||
			stream_update->adaptive_sync_infopacket ||
			stream_update->dpms_off ||
			stream_update->allow_freesync ||
			stream_update->vrr_active_variable ||
			stream_update->vrr_active_fixed ||
			stream_update->gamut_remap ||
			stream_update->output_color_space ||
			stream_update->dither_option ||
			stream_update->wb_update ||
			stream_update->dsc_config ||
			stream_update->mst_bw_update ||
			stream_update->func_shaper ||
			stream_update->lut3d_func ||
			stream_update->pending_test_pattern))
		return true;

	if (stream) {
		stream_status = dc_stream_get_status(stream);
		if (stream_status == NULL || stream_status->plane_count != surface_count)
			return true;
	}
	if (dc->idle_optimizations_allowed)
		return true;

	return false;
}

static bool fast_update_only(struct dc *dc,
		struct dc_fast_update *fast_update,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		struct dc_stream_state *stream)
{
	return fast_updates_exist(fast_update, surface_count)
			&& !full_update_required(dc, srf_updates, surface_count, stream_update, stream);
}

static bool should_commit_minimal_transition_for_windowed_mpo_odm(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *context)
{
	struct pipe_ctx *cur_pipe, *new_pipe;
	bool cur_is_odm_in_use, new_is_odm_in_use;
	struct dc_stream_status *cur_stream_status = stream_get_status(dc->current_state, stream);
	struct dc_stream_status *new_stream_status = stream_get_status(context, stream);

	if (!dc->debug.enable_single_display_2to1_odm_policy ||
			!dc->config.enable_windowed_mpo_odm)
		/* skip the check if windowed MPO ODM or dynamic ODM is turned
		 * off.
		 */
		return false;

	if (context == dc->current_state)
		/* skip the check for fast update */
		return false;

	if (new_stream_status->plane_count != cur_stream_status->plane_count)
		/* plane count changed, not a plane scaling update so not the
		 * case we are looking for
		 */
		return false;

	cur_pipe = resource_get_otg_master_for_stream(&dc->current_state->res_ctx, stream);
	new_pipe = resource_get_otg_master_for_stream(&context->res_ctx, stream);
	cur_is_odm_in_use = resource_get_odm_slice_count(cur_pipe) > 1;
	new_is_odm_in_use = resource_get_odm_slice_count(new_pipe) > 1;
	if (cur_is_odm_in_use == new_is_odm_in_use)
		/* ODM state isn't changed, not the case we are looking for */
		return false;

	if (dc->hwss.is_pipe_topology_transition_seamless &&
			dc->hwss.is_pipe_topology_transition_seamless(
					dc, dc->current_state, context))
		/* transition can be achieved without the need for committing
		 * minimal transition state first
		 */
		return false;

	return true;
}

bool dc_update_planes_and_stream(struct dc *dc,
		struct dc_surface_update *srf_updates, int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update)
{
	struct dc_state *context;
	enum surface_update_type update_type;
	int i;
	struct dc_fast_update fast_update[MAX_SURFACES] = {0};

	/* In cases where MPO and split or ODM are used transitions can
	 * cause underflow. Apply stream configuration with minimal pipe
	 * split first to avoid unsupported transitions for active pipes.
	 */
	bool force_minimal_pipe_splitting = 0;
	bool is_plane_addition = 0;
	bool is_fast_update_only;

	populate_fast_updates(fast_update, srf_updates, surface_count, stream_update);
	is_fast_update_only = fast_update_only(dc, fast_update, srf_updates,
			surface_count, stream_update, stream);
	force_minimal_pipe_splitting = could_mpcc_tree_change_for_active_pipes(
			dc,
			stream,
			srf_updates,
			surface_count,
			&is_plane_addition);

	/* on plane addition, minimal state is the current one */
	if (force_minimal_pipe_splitting && is_plane_addition &&
		!commit_minimal_transition_state(dc, dc->current_state))
				return false;

	if (!update_planes_and_stream_state(
			dc,
			srf_updates,
			surface_count,
			stream,
			stream_update,
			&update_type,
			&context))
		return false;

	/* on plane removal, minimal state is the new one */
	if (force_minimal_pipe_splitting && !is_plane_addition) {
		/* Since all phantom pipes are removed in full validation,
		 * we have to save and restore the subvp/mall config when
		 * we do a minimal transition since the flags marking the
		 * pipe as subvp/phantom will be cleared (dc copy constructor
		 * creates a shallow copy).
		 */
		if (!commit_minimal_transition_state(dc, context)) {
			dc_state_release(context);
			return false;
		}
		update_type = UPDATE_TYPE_FULL;
	}

	/* when windowed MPO ODM is supported, we need to handle a special case
	 * where we can transition between ODM combine and MPC combine due to
	 * plane scaling update. This transition will require us to commit
	 * minimal transition state. The condition to trigger this update can't
	 * be predicted by could_mpcc_tree_change_for_active_pipes because we
	 * can only determine it after DML validation. Therefore we can't rely
	 * on the existing commit minimal transition state sequence. Instead
	 * we have to add additional handling here to handle this transition
	 * with its own special sequence.
	 */
	if (should_commit_minimal_transition_for_windowed_mpo_odm(dc, stream, context))
		commit_minimal_transition_state_for_windowed_mpo_odm(dc,
				context, stream);
	update_seamless_boot_flags(dc, context, surface_count, stream);
	if (is_fast_update_only && !dc->debug.enable_legacy_fast_update) {
		commit_planes_for_stream_fast(dc,
				srf_updates,
				surface_count,
				stream,
				stream_update,
				update_type,
				context);
	} else {
		if (!stream_update &&
				dc->hwss.is_pipe_topology_transition_seamless &&
				!dc->hwss.is_pipe_topology_transition_seamless(
						dc, dc->current_state, context)) {
			DC_LOG_ERROR("performing non-seamless pipe topology transition with surface only update!\n");
			BREAK_TO_DEBUGGER();
		}
		commit_planes_for_stream(
				dc,
				srf_updates,
				surface_count,
				stream,
				stream_update,
				update_type,
				context);
	}

	if (dc->current_state != context) {

		/* Since memory free requires elevated IRQL, an interrupt
		 * request is generated by mem free. If this happens
		 * between freeing and reassigning the context, our vsync
		 * interrupt will call into dc and cause a memory
		 * corruption BSOD. Hence, we first reassign the context,
		 * then free the old context.
		 */

		struct dc_state *old = dc->current_state;

		dc->current_state = context;
		dc_state_release(old);

		// clear any forced full updates
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->plane_state && pipe_ctx->stream == stream)
				pipe_ctx->plane_state->force_full_update = false;
		}
	}
	return true;
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
	struct dc_fast_update fast_update[MAX_SURFACES] = {0};

	populate_fast_updates(fast_update, srf_updates, surface_count, stream_update);
	stream_status = dc_stream_get_status(stream);
	context = dc->current_state;

	update_type = dc_check_update_surfaces_for_stream(
				dc, srf_updates, surface_count, stream_update, stream_status);

	/* TODO: Since change commit sequence can have a huge impact,
	 * we decided to only enable it for DCN3x. However, as soon as
	 * we get more confident about this change we'll need to enable
	 * the new sequence for all ASICs.
	 */
	if (dc->ctx->dce_version >= DCN_VERSION_3_2) {
		/*
		 * Previous frame finished and HW is ready for optimization.
		 */
		if (update_type == UPDATE_TYPE_FAST)
			dc_post_update_surfaces_to_stream(dc);

		dc_update_planes_and_stream(dc, srf_updates,
					    surface_count, stream,
					    stream_update);
		return;
	}

	if (update_type >= update_surface_trace_level)
		update_surface_trace(dc, srf_updates, surface_count);


	if (update_type >= UPDATE_TYPE_FULL) {

		/* initialize scratch memory for building context */
		context = dc_state_create_copy(state);
		if (context == NULL) {
			DC_ERROR("Failed to allocate new validate context!\n");
			return;
		}

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];
			struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

			if (new_pipe->plane_state && new_pipe->plane_state != old_pipe->plane_state)
				new_pipe->plane_state->force_full_update = true;
		}
	} else if (update_type == UPDATE_TYPE_FAST) {
		/*
		 * Previous frame finished and HW is ready for optimization.
		 */
		dc_post_update_surfaces_to_stream(dc);
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
			dc_state_release(context);
			return;
		}
	}

	TRACE_DC_PIPE_STATE(pipe_ctx, i, MAX_PIPES);

	update_seamless_boot_flags(dc, context, surface_count, stream);
	if (fast_update_only(dc, fast_update, srf_updates, surface_count, stream_update, stream) &&
			!dc->debug.enable_legacy_fast_update) {
		commit_planes_for_stream_fast(dc,
				srf_updates,
				surface_count,
				stream,
				stream_update,
				update_type,
				context);
	} else {
		commit_planes_for_stream(
				dc,
				srf_updates,
				surface_count,
				stream,
				stream_update,
				update_type,
				context);
	}
	/*update current_State*/
	if (dc->current_state != context) {

		struct dc_state *old = dc->current_state;

		dc->current_state = context;
		dc_state_release(old);

		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->plane_state && pipe_ctx->stream == stream)
				pipe_ctx->plane_state->force_full_update = false;
		}
	}

	/* Legacy optimization path for DCE. */
	if (update_type >= UPDATE_TYPE_FULL && dc_ctx->dce_version < DCE_VERSION_MAX) {
		dc_post_update_surfaces_to_stream(dc);
		TRACE_DCE_CLOCK_STATE(&context->bw_ctx.bw.dce);
	}

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

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	return dal_irq_service_to_irq_source(dc->res_pool->irqs, src_id, ext_id);
}

/*
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
	if (!dc->current_state)
		return;

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		dc_state_construct(dc, dc->current_state);

		dc_z10_restore(dc);

		dc->hwss.init_hw(dc);

		if (dc->hwss.init_sys_ctx != NULL &&
			dc->vm_pa_config.valid) {
			dc->hwss.init_sys_ctx(dc->hwseq, dc, &dc->vm_pa_config);
		}

		break;
	default:
		ASSERT(dc->current_state->stream_count == 0);

		dc_state_destruct(dc->current_state);

		break;
	}
}

void dc_resume(struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++)
		dc->link_srv->resume(dc->links[i]);
}

bool dc_is_dmcu_initialized(struct dc *dc)
{
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (dmcu)
		return dmcu->funcs->is_dmcu_initialized(dmcu);
	return false;
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
	bool allow_active;

	for (i = 0; i < dc->current_state->stream_count ; i++) {
		struct dc_link *link;
		struct dc_stream_state *stream = dc->current_state->streams[i];

		link = stream->link;
		if (!link)
			continue;

		if (link->psr_settings.psr_feature_enabled) {
			if (enable && !link->psr_settings.psr_allow_active) {
				allow_active = true;
				if (!dc_link_set_psr_allow_active(link, &allow_active, false, false, NULL))
					return false;
			} else if (!enable && link->psr_settings.psr_allow_active) {
				allow_active = false;
				if (!dc_link_set_psr_allow_active(link, &allow_active, true, false, NULL))
					return false;
			}
		}
	}

	return true;
}

void dc_allow_idle_optimizations(struct dc *dc, bool allow)
{
	if (dc->debug.disable_idle_power_optimizations)
		return;

	if (dc->caps.ips_support && (dc->config.disable_ips == DMUB_IPS_DISABLE_ALL))
		return;

	if (dc->clk_mgr != NULL && dc->clk_mgr->funcs->is_smu_present)
		if (!dc->clk_mgr->funcs->is_smu_present(dc->clk_mgr))
			return;

	if (allow == dc->idle_optimizations_allowed)
		return;

	if (dc->hwss.apply_idle_power_optimizations && dc->hwss.apply_idle_power_optimizations(dc, allow))
		dc->idle_optimizations_allowed = allow;
}

bool dc_dmub_is_ips_idle_state(struct dc *dc)
{
	uint32_t idle_state = 0;

	if (dc->debug.disable_idle_power_optimizations)
		return false;

	if (!dc->caps.ips_support || (dc->config.disable_ips == DMUB_IPS_DISABLE_ALL))
		return false;

	if (dc->hwss.get_idle_state)
		idle_state = dc->hwss.get_idle_state(dc);

	if (!(idle_state & DMUB_IPS1_ALLOW_MASK) ||
		!(idle_state & DMUB_IPS2_ALLOW_MASK))
		return true;

	return false;
}

/* set min and max memory clock to lowest and highest DPM level, respectively */
void dc_unlock_memory_clock_frequency(struct dc *dc)
{
	if (dc->clk_mgr->funcs->set_hard_min_memclk)
		dc->clk_mgr->funcs->set_hard_min_memclk(dc->clk_mgr, false);

	if (dc->clk_mgr->funcs->set_hard_max_memclk)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);
}

/* set min memory clock to the min required for current mode, max to maxDPM */
void dc_lock_memory_clock_frequency(struct dc *dc)
{
	if (dc->clk_mgr->funcs->get_memclk_states_from_smu)
		dc->clk_mgr->funcs->get_memclk_states_from_smu(dc->clk_mgr);

	if (dc->clk_mgr->funcs->set_hard_min_memclk)
		dc->clk_mgr->funcs->set_hard_min_memclk(dc->clk_mgr, true);

	if (dc->clk_mgr->funcs->set_hard_max_memclk)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);
}

static void blank_and_force_memclk(struct dc *dc, bool apply, unsigned int memclk_mhz)
{
	struct dc_state *context = dc->current_state;
	struct hubp *hubp;
	struct pipe_ctx *pipe;
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream != NULL) {
			dc->hwss.disable_pixel_data(dc, pipe, true);

			// wait for double buffer
			pipe->stream_res.tg->funcs->wait_for_state(pipe->stream_res.tg, CRTC_STATE_VACTIVE);
			pipe->stream_res.tg->funcs->wait_for_state(pipe->stream_res.tg, CRTC_STATE_VBLANK);
			pipe->stream_res.tg->funcs->wait_for_state(pipe->stream_res.tg, CRTC_STATE_VACTIVE);

			hubp = pipe->plane_res.hubp;
			hubp->funcs->set_blank_regs(hubp, true);
		}
	}

	dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, memclk_mhz);
	dc->clk_mgr->funcs->set_min_memclk(dc->clk_mgr, memclk_mhz);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream != NULL) {
			dc->hwss.disable_pixel_data(dc, pipe, false);

			hubp = pipe->plane_res.hubp;
			hubp->funcs->set_blank_regs(hubp, false);
		}
	}
}


/**
 * dc_enable_dcmode_clk_limit() - lower clocks in dc (battery) mode
 * @dc: pointer to dc of the dm calling this
 * @enable: True = transition to DC mode, false = transition back to AC mode
 *
 * Some SoCs define additional clock limits when in DC mode, DM should
 * invoke this function when the platform undergoes a power source transition
 * so DC can apply/unapply the limit. This interface may be disruptive to
 * the onscreen content.
 *
 * Context: Triggered by OS through DM interface, or manually by escape calls.
 * Need to hold a dclock when doing so.
 *
 * Return: none (void function)
 *
 */
void dc_enable_dcmode_clk_limit(struct dc *dc, bool enable)
{
	unsigned int softMax = 0, maxDPM = 0, funcMin = 0, i;
	bool p_state_change_support;

	if (!dc->config.dc_mode_clk_limit_support)
		return;

	softMax = dc->clk_mgr->bw_params->dc_mode_softmax_memclk;
	for (i = 0; i < dc->clk_mgr->bw_params->clk_table.num_entries; i++) {
		if (dc->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz > maxDPM)
			maxDPM = dc->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz;
	}
	funcMin = (dc->clk_mgr->clks.dramclk_khz + 999) / 1000;
	p_state_change_support = dc->clk_mgr->clks.p_state_change_support;

	if (enable && !dc->clk_mgr->dc_mode_softmax_enabled) {
		if (p_state_change_support) {
			if (funcMin <= softMax)
				dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, softMax);
			// else: No-Op
		} else {
			if (funcMin <= softMax)
				blank_and_force_memclk(dc, true, softMax);
			// else: No-Op
		}
	} else if (!enable && dc->clk_mgr->dc_mode_softmax_enabled) {
		if (p_state_change_support) {
			if (funcMin <= softMax)
				dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, maxDPM);
			// else: No-Op
		} else {
			if (funcMin <= softMax)
				blank_and_force_memclk(dc, true, maxDPM);
			// else: No-Op
		}
	}
	dc->clk_mgr->dc_mode_softmax_enabled = enable;
}
bool dc_is_plane_eligible_for_idle_optimizations(struct dc *dc, struct dc_plane_state *plane,
		struct dc_cursor_attributes *cursor_attr)
{
	if (dc->hwss.does_plane_fit_in_mall && dc->hwss.does_plane_fit_in_mall(dc, plane, cursor_attr))
		return true;
	return false;
}

/* cleanup on driver unload */
void dc_hardware_release(struct dc *dc)
{
	dc_mclk_switch_using_fw_based_vblank_stretch_shut_down(dc);

	if (dc->hwss.hardware_release)
		dc->hwss.hardware_release(dc);
}

void dc_mclk_switch_using_fw_based_vblank_stretch_shut_down(struct dc *dc)
{
	if (dc->current_state)
		dc->current_state->bw_ctx.bw.dcn.clk.fw_based_mclk_switching_shut_down = true;
}

/**
 * dc_is_dmub_outbox_supported - Check if DMUB firmware support outbox notification
 *
 * @dc: [in] dc structure
 *
 * Checks whether DMUB FW supports outbox notifications, if supported DM
 * should register outbox interrupt prior to actually enabling interrupts
 * via dc_enable_dmub_outbox
 *
 * Return:
 * True if DMUB FW supports outbox notifications, False otherwise
 */
bool dc_is_dmub_outbox_supported(struct dc *dc)
{
	switch (dc->ctx->asic_id.chip_family) {

	case FAMILY_YELLOW_CARP:
		/* DCN31 B0 USB4 DPIA needs dmub notifications for interrupts */
		if (dc->ctx->asic_id.hw_internal_rev == YELLOW_CARP_B0 &&
		    !dc->debug.dpia_debug.bits.disable_dpia)
			return true;
	break;

	case AMDGPU_FAMILY_GC_11_0_1:
	case AMDGPU_FAMILY_GC_11_5_0:
		if (!dc->debug.dpia_debug.bits.disable_dpia)
			return true;
	break;

	default:
		break;
	}

	/* dmub aux needs dmub notifications to be enabled */
	return dc->debug.enable_dmub_aux_for_legacy_ddc;

}

/**
 * dc_enable_dmub_notifications - Check if dmub fw supports outbox
 *
 * @dc: [in] dc structure
 *
 * Calls dc_is_dmub_outbox_supported to check if dmub fw supports outbox
 * notifications. All DMs shall switch to dc_is_dmub_outbox_supported.  This
 * API shall be removed after switching.
 *
 * Return:
 * True if DMUB FW supports outbox notifications, False otherwise
 */
bool dc_enable_dmub_notifications(struct dc *dc)
{
	return dc_is_dmub_outbox_supported(dc);
}

/**
 * dc_enable_dmub_outbox - Enables DMUB unsolicited notification
 *
 * @dc: [in] dc structure
 *
 * Enables DMUB unsolicited notifications to x86 via outbox.
 */
void dc_enable_dmub_outbox(struct dc *dc)
{
	struct dc_context *dc_ctx = dc->ctx;

	dmub_enable_outbox_notification(dc_ctx->dmub_srv);
	DC_LOG_DC("%s: dmub outbox notifications enabled\n", __func__);
}

/**
 * dc_process_dmub_aux_transfer_async - Submits aux command to dmub via inbox message
 *                                      Sets port index appropriately for legacy DDC
 * @dc: dc structure
 * @link_index: link index
 * @payload: aux payload
 *
 * Returns: True if successful, False if failure
 */
bool dc_process_dmub_aux_transfer_async(struct dc *dc,
				uint32_t link_index,
				struct aux_payload *payload)
{
	uint8_t action;
	union dmub_rb_cmd cmd = {0};

	ASSERT(payload->length <= 16);

	cmd.dp_aux_access.header.type = DMUB_CMD__DP_AUX_ACCESS;
	cmd.dp_aux_access.header.payload_bytes = 0;
	/* For dpia, ddc_pin is set to NULL */
	if (!dc->links[link_index]->ddc->ddc_pin)
		cmd.dp_aux_access.aux_control.type = AUX_CHANNEL_DPIA;
	else
		cmd.dp_aux_access.aux_control.type = AUX_CHANNEL_LEGACY_DDC;

	cmd.dp_aux_access.aux_control.instance = dc->links[link_index]->ddc_hw_inst;
	cmd.dp_aux_access.aux_control.sw_crc_enabled = 0;
	cmd.dp_aux_access.aux_control.timeout = 0;
	cmd.dp_aux_access.aux_control.dpaux.address = payload->address;
	cmd.dp_aux_access.aux_control.dpaux.is_i2c_over_aux = payload->i2c_over_aux;
	cmd.dp_aux_access.aux_control.dpaux.length = payload->length;

	/* set aux action */
	if (payload->i2c_over_aux) {
		if (payload->write) {
			if (payload->mot)
				action = DP_AUX_REQ_ACTION_I2C_WRITE_MOT;
			else
				action = DP_AUX_REQ_ACTION_I2C_WRITE;
		} else {
			if (payload->mot)
				action = DP_AUX_REQ_ACTION_I2C_READ_MOT;
			else
				action = DP_AUX_REQ_ACTION_I2C_READ;
			}
	} else {
		if (payload->write)
			action = DP_AUX_REQ_ACTION_DPCD_WRITE;
		else
			action = DP_AUX_REQ_ACTION_DPCD_READ;
	}

	cmd.dp_aux_access.aux_control.dpaux.action = action;

	if (payload->length && payload->write) {
		memcpy(cmd.dp_aux_access.aux_control.dpaux.data,
			payload->data,
			payload->length
			);
	}

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

uint8_t get_link_index_from_dpia_port_index(const struct dc *dc,
					    uint8_t dpia_port_index)
{
	uint8_t index, link_index = 0xFF;

	for (index = 0; index < dc->link_count; index++) {
		/* ddc_hw_inst has dpia port index for dpia links
		 * and ddc instance for legacy links
		 */
		if (!dc->links[index]->ddc->ddc_pin) {
			if (dc->links[index]->ddc_hw_inst == dpia_port_index) {
				link_index = index;
				break;
			}
		}
	}
	ASSERT(link_index != 0xFF);
	return link_index;
}

/**
 * dc_process_dmub_set_config_async - Submits set_config command
 *
 * @dc: [in] dc structure
 * @link_index: [in] link_index: link index
 * @payload: [in] aux payload
 * @notify: [out] set_config immediate reply
 *
 * Submits set_config command to dmub via inbox message.
 *
 * Return:
 * True if successful, False if failure
 */
bool dc_process_dmub_set_config_async(struct dc *dc,
				uint32_t link_index,
				struct set_config_cmd_payload *payload,
				struct dmub_notification *notify)
{
	union dmub_rb_cmd cmd = {0};
	bool is_cmd_complete = true;

	/* prepare SET_CONFIG command */
	cmd.set_config_access.header.type = DMUB_CMD__DPIA;
	cmd.set_config_access.header.sub_type = DMUB_CMD__DPIA_SET_CONFIG_ACCESS;

	cmd.set_config_access.set_config_control.instance = dc->links[link_index]->ddc_hw_inst;
	cmd.set_config_access.set_config_control.cmd_pkt.msg_type = payload->msg_type;
	cmd.set_config_access.set_config_control.cmd_pkt.msg_data = payload->msg_data;

	if (!dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)) {
		/* command is not processed by dmub */
		notify->sc_status = SET_CONFIG_UNKNOWN_ERROR;
		return is_cmd_complete;
	}

	/* command processed by dmub, if ret_status is 1, it is completed instantly */
	if (cmd.set_config_access.header.ret_status == 1)
		notify->sc_status = cmd.set_config_access.set_config_control.immed_status;
	else
		/* cmd pending, will receive notification via outbox */
		is_cmd_complete = false;

	return is_cmd_complete;
}

/**
 * dc_process_dmub_set_mst_slots - Submits MST solt allocation
 *
 * @dc: [in] dc structure
 * @link_index: [in] link index
 * @mst_alloc_slots: [in] mst slots to be allotted
 * @mst_slots_in_use: [out] mst slots in use returned in failure case
 *
 * Submits mst slot allocation command to dmub via inbox message
 *
 * Return:
 * DC_OK if successful, DC_ERROR if failure
 */
enum dc_status dc_process_dmub_set_mst_slots(const struct dc *dc,
				uint32_t link_index,
				uint8_t mst_alloc_slots,
				uint8_t *mst_slots_in_use)
{
	union dmub_rb_cmd cmd = {0};

	/* prepare MST_ALLOC_SLOTS command */
	cmd.set_mst_alloc_slots.header.type = DMUB_CMD__DPIA;
	cmd.set_mst_alloc_slots.header.sub_type = DMUB_CMD__DPIA_MST_ALLOC_SLOTS;

	cmd.set_mst_alloc_slots.mst_slots_control.instance = dc->links[link_index]->ddc_hw_inst;
	cmd.set_mst_alloc_slots.mst_slots_control.mst_alloc_slots = mst_alloc_slots;

	if (!dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		/* command is not processed by dmub */
		return DC_ERROR_UNEXPECTED;

	/* command processed by dmub, if ret_status is 1 */
	if (cmd.set_config_access.header.ret_status != 1)
		/* command processing error */
		return DC_ERROR_UNEXPECTED;

	/* command processed and we have a status of 2, mst not enabled in dpia */
	if (cmd.set_mst_alloc_slots.mst_slots_control.immed_status == 2)
		return DC_FAIL_UNSUPPORTED_1;

	/* previously configured mst alloc and used slots did not match */
	if (cmd.set_mst_alloc_slots.mst_slots_control.immed_status == 3) {
		*mst_slots_in_use = cmd.set_mst_alloc_slots.mst_slots_control.mst_slots_in_use;
		return DC_NOT_SUPPORTED;
	}

	return DC_OK;
}

/**
 * dc_process_dmub_dpia_hpd_int_enable - Submits DPIA DPD interruption
 *
 * @dc: [in] dc structure
 * @hpd_int_enable: [in] 1 for hpd int enable, 0 to disable
 *
 * Submits dpia hpd int enable command to dmub via inbox message
 */
void dc_process_dmub_dpia_hpd_int_enable(const struct dc *dc,
				uint32_t hpd_int_enable)
{
	union dmub_rb_cmd cmd = {0};

	cmd.dpia_hpd_int_enable.header.type = DMUB_CMD__DPIA_HPD_INT_ENABLE;
	cmd.dpia_hpd_int_enable.enable = hpd_int_enable;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	DC_LOG_DEBUG("%s: hpd_int_enable(%d)\n", __func__, hpd_int_enable);
}

/**
 * dc_print_dmub_diagnostic_data - Print DMUB diagnostic data for debugging
 *
 * @dc: [in] dc structure
 *
 *
 */
void dc_print_dmub_diagnostic_data(const struct dc *dc)
{
	dc_dmub_srv_log_diagnostic_data(dc->ctx->dmub_srv);
}

/**
 * dc_disable_accelerated_mode - disable accelerated mode
 * @dc: dc structure
 */
void dc_disable_accelerated_mode(struct dc *dc)
{
	bios_set_scratch_acc_mode_change(dc->ctx->dc_bios, 0);
}


/**
 *  dc_notify_vsync_int_state - notifies vsync enable/disable state
 *  @dc: dc structure
 *  @stream: stream where vsync int state changed
 *  @enable: whether vsync is enabled or disabled
 *
 *  Called when vsync is enabled/disabled Will notify DMUB to start/stop ABM
 *  interrupts after steady state is reached.
 */
void dc_notify_vsync_int_state(struct dc *dc, struct dc_stream_state *stream, bool enable)
{
	int i;
	int edp_num;
	struct pipe_ctx *pipe = NULL;
	struct dc_link *link = stream->sink->link;
	struct dc_link *edp_links[MAX_NUM_EDP];


	if (link->psr_settings.psr_feature_enabled)
		return;

	if (link->replay_settings.replay_feature_enabled)
		return;

	/*find primary pipe associated with stream*/
	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg)
			break;
	}

	if (i == MAX_PIPES) {
		ASSERT(0);
		return;
	}

	dc_get_edp_links(dc, edp_links, &edp_num);

	/* Determine panel inst */
	for (i = 0; i < edp_num; i++) {
		if (edp_links[i] == link)
			break;
	}

	if (i == edp_num) {
		return;
	}

	if (pipe->stream_res.abm && pipe->stream_res.abm->funcs->set_abm_pause)
		pipe->stream_res.abm->funcs->set_abm_pause(pipe->stream_res.abm, !enable, i, pipe->stream_res.tg->inst);
}

/*****************************************************************************
 *  dc_abm_save_restore() - Interface to DC for save+pause and restore+un-pause
 *                          ABM
 *  @dc: dc structure
 *	@stream: stream where vsync int state changed
 *  @pData: abm hw states
 *
 ****************************************************************************/
bool dc_abm_save_restore(
		struct dc *dc,
		struct dc_stream_state *stream,
		struct abm_save_restore *pData)
{
	int i;
	int edp_num;
	struct pipe_ctx *pipe = NULL;
	struct dc_link *link = stream->sink->link;
	struct dc_link *edp_links[MAX_NUM_EDP];


	/*find primary pipe associated with stream*/
	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg)
			break;
	}

	if (i == MAX_PIPES) {
		ASSERT(0);
		return false;
	}

	dc_get_edp_links(dc, edp_links, &edp_num);

	/* Determine panel inst */
	for (i = 0; i < edp_num; i++)
		if (edp_links[i] == link)
			break;

	if (i == edp_num)
		return false;

	if (pipe->stream_res.abm &&
		pipe->stream_res.abm->funcs->save_restore)
		return pipe->stream_res.abm->funcs->save_restore(
				pipe->stream_res.abm,
				i,
				pData);
	return false;
}

void dc_query_current_properties(struct dc *dc, struct dc_current_properties *properties)
{
	unsigned int i;
	bool subvp_sw_cursor_req = false;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		if (check_subvp_sw_cursor_fallback_req(dc, dc->current_state->streams[i])) {
			subvp_sw_cursor_req = true;
			break;
		}
	}
	properties->cursor_size_limit = subvp_sw_cursor_req ? 64 : dc->caps.max_cursor_size;
}

/**
 * dc_set_edp_power() - DM controls eDP power to be ON/OFF
 *
 * Called when DM wants to power on/off eDP.
 *     Only work on links with flag skip_implict_edp_power_control is set.
 *
 * @dc: Current DC state
 * @edp_link: a link with eDP connector signal type
 * @powerOn: power on/off eDP
 *
 * Return: void
 */
void dc_set_edp_power(const struct dc *dc, struct dc_link *edp_link,
				 bool powerOn)
{
	if (edp_link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	if (edp_link->skip_implict_edp_power_control == false)
		return;

	edp_link->dc->link_srv->edp_set_panel_power(edp_link, powerOn);
}

/*
 *****************************************************************************
 * dc_get_power_profile_for_dc_state() - extracts power profile from dc state
 *
 * Called when DM wants to make power policy decisions based on dc_state
 *
 *****************************************************************************
 */
struct dc_power_profile dc_get_power_profile_for_dc_state(const struct dc_state *context)
{
	struct dc_power_profile profile = { 0 };

	profile.power_level += !context->bw_ctx.bw.dcn.clk.p_state_change_support;

	return profile;
}


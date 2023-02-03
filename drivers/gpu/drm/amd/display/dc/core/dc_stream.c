/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "basics/dc_common.h"
#include "dc.h"
#include "core_types.h"
#include "resource.h"
#include "ipp.h"
#include "timing_generator.h"
#include "dc_dmub_srv.h"

#define DC_LOGGER dc->ctx->logger

/*******************************************************************************
 * Private functions
 ******************************************************************************/
void update_stream_signal(struct dc_stream_state *stream, struct dc_sink *sink)
{
	if (sink->sink_signal == SIGNAL_TYPE_NONE)
		stream->signal = stream->link->connector_signal;
	else
		stream->signal = sink->sink_signal;

	if (dc_is_dvi_signal(stream->signal)) {
		if (stream->ctx->dc->caps.dual_link_dvi &&
			(stream->timing.pix_clk_100hz / 10) > TMDS_MAX_PIXEL_CLOCK &&
			sink->sink_signal != SIGNAL_TYPE_DVI_SINGLE_LINK)
			stream->signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		else
			stream->signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
}

static bool dc_stream_construct(struct dc_stream_state *stream,
	struct dc_sink *dc_sink_data)
{
	uint32_t i = 0;

	stream->sink = dc_sink_data;
	dc_sink_retain(dc_sink_data);

	stream->ctx = dc_sink_data->ctx;
	stream->link = dc_sink_data->link;
	stream->sink_patches = dc_sink_data->edid_caps.panel_patch;
	stream->converter_disable_audio = dc_sink_data->converter_disable_audio;
	stream->qs_bit = dc_sink_data->edid_caps.qs_bit;
	stream->qy_bit = dc_sink_data->edid_caps.qy_bit;

	/* Copy audio modes */
	/* TODO - Remove this translation */
	for (i = 0; i < (dc_sink_data->edid_caps.audio_mode_count); i++)
	{
		stream->audio_info.modes[i].channel_count = dc_sink_data->edid_caps.audio_modes[i].channel_count;
		stream->audio_info.modes[i].format_code = dc_sink_data->edid_caps.audio_modes[i].format_code;
		stream->audio_info.modes[i].sample_rates.all = dc_sink_data->edid_caps.audio_modes[i].sample_rate;
		stream->audio_info.modes[i].sample_size = dc_sink_data->edid_caps.audio_modes[i].sample_size;
	}
	stream->audio_info.mode_count = dc_sink_data->edid_caps.audio_mode_count;
	stream->audio_info.audio_latency = dc_sink_data->edid_caps.audio_latency;
	stream->audio_info.video_latency = dc_sink_data->edid_caps.video_latency;
	memmove(
		stream->audio_info.display_name,
		dc_sink_data->edid_caps.display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);
	stream->audio_info.manufacture_id = dc_sink_data->edid_caps.manufacturer_id;
	stream->audio_info.product_id = dc_sink_data->edid_caps.product_id;
	stream->audio_info.flags.all = dc_sink_data->edid_caps.speaker_flags;

	if (dc_sink_data->dc_container_id != NULL) {
		struct dc_container_id *dc_container_id = dc_sink_data->dc_container_id;

		stream->audio_info.port_id[0] = dc_container_id->portId[0];
		stream->audio_info.port_id[1] = dc_container_id->portId[1];
	} else {
		/* TODO - WindowDM has implemented,
		other DMs need Unhardcode port_id */
		stream->audio_info.port_id[0] = 0x5558859e;
		stream->audio_info.port_id[1] = 0xd989449;
	}

	/* EDID CAP translation for HDMI 2.0 */
	stream->timing.flags.LTE_340MCSC_SCRAMBLE = dc_sink_data->edid_caps.lte_340mcsc_scramble;

	memset(&stream->timing.dsc_cfg, 0, sizeof(stream->timing.dsc_cfg));
	stream->timing.dsc_cfg.num_slices_h = 0;
	stream->timing.dsc_cfg.num_slices_v = 0;
	stream->timing.dsc_cfg.bits_per_pixel = 128;
	stream->timing.dsc_cfg.block_pred_enable = 1;
	stream->timing.dsc_cfg.linebuf_depth = 9;
	stream->timing.dsc_cfg.version_minor = 2;
	stream->timing.dsc_cfg.ycbcr422_simple = 0;

	update_stream_signal(stream, dc_sink_data);

	stream->out_transfer_func = dc_create_transfer_func();
	if (stream->out_transfer_func == NULL) {
		dc_sink_release(dc_sink_data);
		return false;
	}
	stream->out_transfer_func->type = TF_TYPE_BYPASS;

	stream->stream_id = stream->ctx->dc_stream_id_count;
	stream->ctx->dc_stream_id_count++;

	return true;
}

static void dc_stream_destruct(struct dc_stream_state *stream)
{
	dc_sink_release(stream->sink);
	if (stream->out_transfer_func != NULL) {
		dc_transfer_func_release(stream->out_transfer_func);
		stream->out_transfer_func = NULL;
	}
}

void dc_stream_retain(struct dc_stream_state *stream)
{
	kref_get(&stream->refcount);
}

static void dc_stream_free(struct kref *kref)
{
	struct dc_stream_state *stream = container_of(kref, struct dc_stream_state, refcount);

	dc_stream_destruct(stream);
	kfree(stream);
}

void dc_stream_release(struct dc_stream_state *stream)
{
	if (stream != NULL) {
		kref_put(&stream->refcount, dc_stream_free);
	}
}

struct dc_stream_state *dc_create_stream_for_sink(
		struct dc_sink *sink)
{
	struct dc_stream_state *stream;

	if (sink == NULL)
		return NULL;

	stream = kzalloc(sizeof(struct dc_stream_state), GFP_KERNEL);
	if (stream == NULL)
		goto alloc_fail;

	if (dc_stream_construct(stream, sink) == false)
		goto construct_fail;

	kref_init(&stream->refcount);

	return stream;

construct_fail:
	kfree(stream);

alloc_fail:
	return NULL;
}

struct dc_stream_state *dc_copy_stream(const struct dc_stream_state *stream)
{
	struct dc_stream_state *new_stream;

	new_stream = kmemdup(stream, sizeof(struct dc_stream_state), GFP_KERNEL);
	if (!new_stream)
		return NULL;

	if (new_stream->sink)
		dc_sink_retain(new_stream->sink);

	if (new_stream->out_transfer_func)
		dc_transfer_func_retain(new_stream->out_transfer_func);

	new_stream->stream_id = new_stream->ctx->dc_stream_id_count;
	new_stream->ctx->dc_stream_id_count++;

	/* If using dynamic encoder assignment, wait till stream committed to assign encoder. */
	if (new_stream->ctx->dc->res_pool->funcs->link_encs_assign)
		new_stream->link_enc = NULL;

	kref_init(&new_stream->refcount);

	return new_stream;
}

/**
 * dc_stream_get_status_from_state - Get stream status from given dc state
 * @state: DC state to find the stream status in
 * @stream: The stream to get the stream status for
 *
 * The given stream is expected to exist in the given dc state. Otherwise, NULL
 * will be returned.
 */
struct dc_stream_status *dc_stream_get_status_from_state(
	struct dc_state *state,
	struct dc_stream_state *stream)
{
	uint8_t i;

	if (state == NULL)
		return NULL;

	for (i = 0; i < state->stream_count; i++) {
		if (stream == state->streams[i])
			return &state->stream_status[i];
	}

	return NULL;
}

/**
 * dc_stream_get_status() - Get current stream status of the given stream state
 * @stream: The stream to get the stream status for.
 *
 * The given stream is expected to exist in dc->current_state. Otherwise, NULL
 * will be returned.
 */
struct dc_stream_status *dc_stream_get_status(
	struct dc_stream_state *stream)
{
	struct dc *dc = stream->ctx->dc;
	return dc_stream_get_status_from_state(dc->current_state, stream);
}

static void program_cursor_attributes(
	struct dc *dc,
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes)
{
	int i;
	struct resource_context *res_ctx;
	struct pipe_ctx *pipe_to_program = NULL;

	if (!stream)
		return;

	res_ctx = &dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		if (!pipe_to_program) {
			pipe_to_program = pipe_ctx;
			dc->hwss.cursor_lock(dc, pipe_to_program, true);
			if (pipe_to_program->next_odm_pipe)
				dc->hwss.cursor_lock(dc, pipe_to_program->next_odm_pipe, true);
		}

		dc->hwss.set_cursor_attribute(pipe_ctx);

		dc_send_update_cursor_info_to_dmu(pipe_ctx, i);
		if (dc->hwss.set_cursor_sdr_white_level)
			dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
	}

	if (pipe_to_program) {
		dc->hwss.cursor_lock(dc, pipe_to_program, false);
		if (pipe_to_program->next_odm_pipe)
			dc->hwss.cursor_lock(dc, pipe_to_program->next_odm_pipe, false);
	}
}

#ifndef TRIM_FSFT
/*
 * dc_optimize_timing_for_fsft() - dc to optimize timing
 */
bool dc_optimize_timing_for_fsft(
	struct dc_stream_state *pStream,
	unsigned int max_input_rate_in_khz)
{
	struct dc  *dc;

	dc = pStream->ctx->dc;

	return (dc->hwss.optimize_timing_for_fsft &&
		dc->hwss.optimize_timing_for_fsft(dc, &pStream->timing, max_input_rate_in_khz));
}
#endif

/*
 * dc_stream_set_cursor_attributes() - Update cursor attributes and set cursor surface address
 */
bool dc_stream_set_cursor_attributes(
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes)
{
	struct dc  *dc;
	bool reset_idle_optimizations = false;

	if (NULL == stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}
	if (NULL == attributes) {
		dm_error("DC: attributes is NULL!\n");
		return false;
	}

	if (attributes->address.quad_part == 0) {
		dm_output_to_console("DC: Cursor address is 0!\n");
		return false;
	}

	dc = stream->ctx->dc;

	if (dc->debug.allow_sw_cursor_fallback && attributes->height * attributes->width * 4 > 16384)
		if (stream->mall_stream_config.type == SUBVP_MAIN)
			return false;

	stream->cursor_attributes = *attributes;

	dc_z10_restore(dc);
	/* disable idle optimizations while updating cursor */
	if (dc->idle_optimizations_allowed) {
		dc_allow_idle_optimizations(dc, false);
		reset_idle_optimizations = true;
	}

	program_cursor_attributes(dc, stream, attributes);

	/* re-enable idle optimizations if necessary */
	if (reset_idle_optimizations)
		dc_allow_idle_optimizations(dc, true);

	return true;
}

static void program_cursor_position(
	struct dc *dc,
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position)
{
	int i;
	struct resource_context *res_ctx;
	struct pipe_ctx *pipe_to_program = NULL;

	if (!stream)
		return;

	res_ctx = &dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream ||
				(!pipe_ctx->plane_res.mi  && !pipe_ctx->plane_res.hubp) ||
				!pipe_ctx->plane_state ||
				(!pipe_ctx->plane_res.xfm && !pipe_ctx->plane_res.dpp) ||
				(!pipe_ctx->plane_res.ipp && !pipe_ctx->plane_res.dpp))
			continue;

		if (!pipe_to_program) {
			pipe_to_program = pipe_ctx;
			dc->hwss.cursor_lock(dc, pipe_to_program, true);
		}

		dc->hwss.set_cursor_position(pipe_ctx);

		dc_send_update_cursor_info_to_dmu(pipe_ctx, i);
	}

	if (pipe_to_program)
		dc->hwss.cursor_lock(dc, pipe_to_program, false);
}

bool dc_stream_set_cursor_position(
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position)
{
	struct dc  *dc = stream->ctx->dc;
	bool reset_idle_optimizations = false;

	if (NULL == stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	dc = stream->ctx->dc;
	dc_z10_restore(dc);

	/* disable idle optimizations if enabling cursor */
	if (dc->idle_optimizations_allowed && (!stream->cursor_position.enable || dc->debug.exit_idle_opt_for_cursor_updates)
			&& position->enable) {
		dc_allow_idle_optimizations(dc, false);
		reset_idle_optimizations = true;
	}

	stream->cursor_position = *position;

	program_cursor_position(dc, stream, position);
	/* re-enable idle optimizations if necessary */
	if (reset_idle_optimizations)
		dc_allow_idle_optimizations(dc, true);

	return true;
}

bool dc_stream_add_writeback(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_writeback_info *wb_info)
{
	bool isDrc = false;
	int i = 0;
	struct dwbc *dwb;

	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (wb_info == NULL) {
		dm_error("DC: dc_writeback_info is NULL!\n");
		return false;
	}

	if (wb_info->dwb_pipe_inst >= MAX_DWB_PIPES) {
		dm_error("DC: writeback pipe is invalid!\n");
		return false;
	}

	wb_info->dwb_params.out_transfer_func = stream->out_transfer_func;

	dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
	dwb->dwb_is_drc = false;

	/* recalculate and apply DML parameters */

	for (i = 0; i < stream->num_wb_info; i++) {
		/*dynamic update*/
		if (stream->writeback_info[i].wb_enabled &&
			stream->writeback_info[i].dwb_pipe_inst == wb_info->dwb_pipe_inst) {
			stream->writeback_info[i] = *wb_info;
			isDrc = true;
		}
	}

	if (!isDrc) {
		stream->writeback_info[stream->num_wb_info++] = *wb_info;
	}

	if (dc->hwss.enable_writeback) {
		struct dc_stream_status *stream_status = dc_stream_get_status(stream);
		struct dwbc *dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
		dwb->otg_inst = stream_status->primary_otg_inst;
	}
	if (IS_DIAG_DC(dc->ctx->dce_environment)) {
		if (!dc->hwss.update_bandwidth(dc, dc->current_state)) {
			dm_error("DC: update_bandwidth failed!\n");
			return false;
		}

		/* enable writeback */
		if (dc->hwss.enable_writeback) {
			struct dwbc *dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];

			if (dwb->funcs->is_enabled(dwb)) {
				/* writeback pipe already enabled, only need to update */
				dc->hwss.update_writeback(dc, wb_info, dc->current_state);
			} else {
				/* Enable writeback pipe from scratch*/
				dc->hwss.enable_writeback(dc, wb_info, dc->current_state);
			}
		}
	}
	return true;
}

bool dc_stream_remove_writeback(struct dc *dc,
		struct dc_stream_state *stream,
		uint32_t dwb_pipe_inst)
{
	int i = 0, j = 0;
	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (dwb_pipe_inst >= MAX_DWB_PIPES) {
		dm_error("DC: writeback pipe is invalid!\n");
		return false;
	}

//	stream->writeback_info[dwb_pipe_inst].wb_enabled = false;
	for (i = 0; i < stream->num_wb_info; i++) {
		/*dynamic update*/
		if (stream->writeback_info[i].wb_enabled &&
			stream->writeback_info[i].dwb_pipe_inst == dwb_pipe_inst) {
			stream->writeback_info[i].wb_enabled = false;
		}
	}

	/* remove writeback info for disabled writeback pipes from stream */
	for (i = 0, j = 0; i < stream->num_wb_info; i++) {
		if (stream->writeback_info[i].wb_enabled) {
			if (j < i)
				/* trim the array */
				stream->writeback_info[j] = stream->writeback_info[i];
			j++;
		}
	}
	stream->num_wb_info = j;

	if (IS_DIAG_DC(dc->ctx->dce_environment)) {
		/* recalculate and apply DML parameters */
		if (!dc->hwss.update_bandwidth(dc, dc->current_state)) {
			dm_error("DC: update_bandwidth failed!\n");
			return false;
		}

		/* disable writeback */
		if (dc->hwss.disable_writeback)
			dc->hwss.disable_writeback(dc, dwb_pipe_inst);
	}
	return true;
}

bool dc_stream_warmup_writeback(struct dc *dc,
		int num_dwb,
		struct dc_writeback_info *wb_info)
{
	if (dc->hwss.mmhubbub_warmup)
		return dc->hwss.mmhubbub_warmup(dc, num_dwb, wb_info);
	else
		return false;
}
uint32_t dc_stream_get_vblank_counter(const struct dc_stream_state *stream)
{
	uint8_t i;
	struct dc  *dc = stream->ctx->dc;
	struct resource_context *res_ctx =
		&dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		return tg->funcs->get_frame_count(tg);
	}

	return 0;
}

bool dc_stream_send_dp_sdp(const struct dc_stream_state *stream,
		const uint8_t *custom_sdp_message,
		unsigned int sdp_message_size)
{
	int i;
	struct dc  *dc;
	struct resource_context *res_ctx;

	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	dc = stream->ctx->dc;
	res_ctx = &dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		if (dc->hwss.send_immediate_sdp_message != NULL)
			dc->hwss.send_immediate_sdp_message(pipe_ctx,
								custom_sdp_message,
								sdp_message_size);
		else
			DC_LOG_WARNING("%s:send_immediate_sdp_message not implemented on this ASIC\n",
			__func__);

	}

	return true;
}

bool dc_stream_get_scanoutpos(const struct dc_stream_state *stream,
				  uint32_t *v_blank_start,
				  uint32_t *v_blank_end,
				  uint32_t *h_position,
				  uint32_t *v_position)
{
	uint8_t i;
	bool ret = false;
	struct dc  *dc = stream->ctx->dc;
	struct resource_context *res_ctx =
		&dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		tg->funcs->get_scanoutpos(tg,
					  v_blank_start,
					  v_blank_end,
					  h_position,
					  v_position);

		ret = true;
		break;
	}

	return ret;
}

bool dc_stream_dmdata_status_done(struct dc *dc, struct dc_stream_state *stream)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	if (!dc->hwss.dmdata_status_done)
		return false;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream == stream)
			break;
	}
	/* Stream not found, by default we'll assume HUBP fetched dm data */
	if (i == MAX_PIPES)
		return true;

	return dc->hwss.dmdata_status_done(pipe);
}

bool dc_stream_set_dynamic_metadata(struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_dmdata_attributes *attr)
{
	struct pipe_ctx *pipe_ctx = NULL;
	struct hubp *hubp;
	int i;

	/* Dynamic metadata is only supported on HDMI or DP */
	if (!dc_is_hdmi_signal(stream->signal) && !dc_is_dp_signal(stream->signal))
		return false;

	/* Check hardware support */
	if (!dc->hwss.program_dmdata_engine)
		return false;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx->stream == stream)
			break;
	}

	if (i == MAX_PIPES)
		return false;

	hubp = pipe_ctx->plane_res.hubp;
	if (hubp == NULL)
		return false;

	pipe_ctx->stream->dmdata_address = attr->address;

	dc->hwss.program_dmdata_engine(pipe_ctx);

	if (hubp->funcs->dmdata_set_attributes != NULL &&
			pipe_ctx->stream->dmdata_address.quad_part != 0) {
		hubp->funcs->dmdata_set_attributes(hubp, attr);
	}

	return true;
}

enum dc_status dc_stream_add_dsc_to_resource(struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	if (dc->res_pool->funcs->add_dsc_to_stream_resource) {
		return dc->res_pool->funcs->add_dsc_to_stream_resource(dc, state, stream);
	} else {
		return DC_NO_DSC_RESOURCE;
	}
}

struct pipe_ctx *dc_stream_get_pipe_ctx(struct dc_stream_state *stream)
{
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &stream->ctx->dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream)
			return pipe;
	}

	return NULL;
}

void dc_stream_log(const struct dc *dc, const struct dc_stream_state *stream)
{
	DC_LOG_DC(
			"core_stream 0x%p: src: %d, %d, %d, %d; dst: %d, %d, %d, %d, colorSpace:%d\n",
			stream,
			stream->src.x,
			stream->src.y,
			stream->src.width,
			stream->src.height,
			stream->dst.x,
			stream->dst.y,
			stream->dst.width,
			stream->dst.height,
			stream->output_color_space);
	DC_LOG_DC(
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d, pixelencoder:%d, displaycolorDepth:%d\n",
			stream->timing.pix_clk_100hz / 10,
			stream->timing.h_total,
			stream->timing.v_total,
			stream->timing.pixel_encoding,
			stream->timing.display_color_depth);
	DC_LOG_DC(
			"\tlink: %d\n",
			stream->link->link_index);

	DC_LOG_DC(
			"\tdsc: %d, mst_pbn: %d\n",
			stream->timing.flags.DSC,
			stream->timing.dsc_cfg.mst_pbn);

	if (stream->sink) {
		if (stream->sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
			stream->sink->sink_signal != SIGNAL_TYPE_NONE) {

			DC_LOG_DC(
					"\tdispname: %s signal: %x\n",
					stream->sink->edid_caps.display_name,
					stream->signal);
		}
	}
}


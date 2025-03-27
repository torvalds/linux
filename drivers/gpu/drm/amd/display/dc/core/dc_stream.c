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
#include "dc_state_priv.h"
#include "dc_stream_priv.h"

#define DC_LOGGER dc->ctx->logger
#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif
#ifndef MAX
#define MAX(x, y) ((x > y) ? x : y)
#endif

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

bool dc_stream_construct(struct dc_stream_state *stream,
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
	for (i = 0; i < (dc_sink_data->edid_caps.audio_mode_count); i++) {
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

	stream->out_transfer_func.type = TF_TYPE_BYPASS;

	dc_stream_assign_stream_id(stream);

	return true;
}

void dc_stream_destruct(struct dc_stream_state *stream)
{
	dc_sink_release(stream->sink);
}

void dc_stream_assign_stream_id(struct dc_stream_state *stream)
{
	/* MSB is reserved to indicate phantoms */
	stream->stream_id = stream->ctx->dc_stream_id_count;
	stream->ctx->dc_stream_id_count++;
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

	dc_stream_assign_stream_id(new_stream);

	/* If using dynamic encoder assignment, wait till stream committed to assign encoder. */
	if (new_stream->ctx->dc->res_pool->funcs->link_encs_assign)
		new_stream->link_enc = NULL;

	kref_init(&new_stream->refcount);

	return new_stream;
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
	return dc_state_get_stream_status(dc->current_state, stream);
}

void program_cursor_attributes(
	struct dc *dc,
	struct dc_stream_state *stream)
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
		if (dc->ctx->dmub_srv)
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

/*
 * dc_stream_set_cursor_attributes() - Update cursor attributes and set cursor surface address
 */
bool dc_stream_set_cursor_attributes(
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes)
{
	struct dc  *dc;

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

	/* SubVP is not compatible with HW cursor larger than 64 x 64 x 4.
	 * Therefore, if cursor is greater than 64 x 64 x 4, fallback to SW cursor in the following case:
	 * 1. If the config is a candidate for SubVP high refresh (both single an dual display configs)
	 * 2. If not subvp high refresh, for single display cases, if resolution is >= 5K and refresh rate < 120hz
	 * 3. If not subvp high refresh, for multi display cases, if resolution is >= 4K and refresh rate < 120hz
	 */
	if (dc->debug.allow_sw_cursor_fallback &&
		attributes->height * attributes->width * 4 > 16384 &&
		!stream->hw_cursor_req) {
		if (check_subvp_sw_cursor_fallback_req(dc, stream))
			return false;
	}

	stream->cursor_attributes = *attributes;

	return true;
}

bool dc_stream_program_cursor_attributes(
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes)
{
	struct dc  *dc;
	bool reset_idle_optimizations = false;

	dc = stream ? stream->ctx->dc : NULL;

	if (dc_stream_set_cursor_attributes(stream, attributes)) {
		dc_z10_restore(dc);
		/* disable idle optimizations while updating cursor */
		if (dc->idle_optimizations_allowed) {
			dc_allow_idle_optimizations(dc, false);
			reset_idle_optimizations = true;
		}

		program_cursor_attributes(dc, stream);

		/* re-enable idle optimizations if necessary */
		if (reset_idle_optimizations && !dc->debug.disable_dmub_reallow_idle)
			dc_allow_idle_optimizations(dc, true);

		return true;
	}

	return false;
}

void program_cursor_position(
	struct dc *dc,
	struct dc_stream_state *stream)
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
		if (dc->ctx->dmub_srv)
			dc_send_update_cursor_info_to_dmu(pipe_ctx, i);
	}

	if (pipe_to_program)
		dc->hwss.cursor_lock(dc, pipe_to_program, false);
}

bool dc_stream_set_cursor_position(
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position)
{
	if (NULL == stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	stream->cursor_position = *position;


	return true;
}

bool dc_stream_program_cursor_position(
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position)
{
	struct dc *dc;
	bool reset_idle_optimizations = false;
	const struct dc_cursor_position *old_position;

	if (!stream)
		return false;

	old_position = &stream->cursor_position;
	dc = stream->ctx->dc;

	if (dc_stream_set_cursor_position(stream, position)) {
		dc_z10_restore(dc);

		/* disable idle optimizations if enabling cursor */
		if (dc->idle_optimizations_allowed &&
		    (!old_position->enable || dc->debug.exit_idle_opt_for_cursor_updates) &&
		    position->enable) {
			dc_allow_idle_optimizations(dc, false);
			reset_idle_optimizations = true;
		}

		program_cursor_position(dc, stream);
		/* re-enable idle optimizations if necessary */
		if (reset_idle_optimizations && !dc->debug.disable_dmub_reallow_idle)
			dc_allow_idle_optimizations(dc, true);

		/* apply/update visual confirm */
		if (dc->debug.visual_confirm == VISUAL_CONFIRM_HW_CURSOR) {
			/* update software state */
			int i;

			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				struct pipe_ctx *pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

				/* adjust visual confirm color for all pipes with current stream */
				if (stream == pipe_ctx->stream) {
					get_cursor_visual_confirm_color(pipe_ctx, &(pipe_ctx->visual_confirm_color));

					/* programming hardware */
					if (pipe_ctx->plane_state)
						dc->hwss.update_visual_confirm_color(dc, pipe_ctx,
								pipe_ctx->plane_res.hubp->mpcc_id);
				}
			}
		}

		return true;
	}

	return false;
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

	dc_exit_ips_for_hw_access(dc);

	wb_info->dwb_params.out_transfer_func = &stream->out_transfer_func;

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
		ASSERT(stream->num_wb_info + 1 <= MAX_DWB_PIPES);
		stream->writeback_info[stream->num_wb_info++] = *wb_info;
	}

	if (dc->hwss.enable_writeback) {
		struct dc_stream_status *stream_status = dc_stream_get_status(stream);
		struct dwbc *dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
		if (stream_status)
			dwb->otg_inst = stream_status->primary_otg_inst;
	}

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

	return true;
}

bool dc_stream_fc_disable_writeback(struct dc *dc,
		struct dc_stream_state *stream,
		uint32_t dwb_pipe_inst)
{
	struct dwbc *dwb = dc->res_pool->dwbc[dwb_pipe_inst];

	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (dwb_pipe_inst >= MAX_DWB_PIPES) {
		dm_error("DC: writeback pipe is invalid!\n");
		return false;
	}

	if (stream->num_wb_info > MAX_DWB_PIPES) {
		dm_error("DC: num_wb_info is invalid!\n");
		return false;
	}

	dc_exit_ips_for_hw_access(dc);

	if (dwb->funcs->set_fc_enable)
		dwb->funcs->set_fc_enable(dwb, DWB_FRAME_CAPTURE_DISABLE);

	return true;
}

bool dc_stream_remove_writeback(struct dc *dc,
		struct dc_stream_state *stream,
		uint32_t dwb_pipe_inst)
{
	unsigned int i, j;
	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (dwb_pipe_inst >= MAX_DWB_PIPES) {
		dm_error("DC: writeback pipe is invalid!\n");
		return false;
	}

	if (stream->num_wb_info > MAX_DWB_PIPES) {
		dm_error("DC: num_wb_info is invalid!\n");
		return false;
	}

	/* remove writeback info for disabled writeback pipes from stream */
	for (i = 0, j = 0; i < stream->num_wb_info; i++) {
		if (stream->writeback_info[i].wb_enabled) {

			if (stream->writeback_info[i].dwb_pipe_inst == dwb_pipe_inst)
				stream->writeback_info[i].wb_enabled = false;

			/* trim the array */
			if (j < i) {
				memcpy(&stream->writeback_info[j], &stream->writeback_info[i],
						sizeof(struct dc_writeback_info));
				j++;
			}
		}
	}
	stream->num_wb_info = j;

	/* recalculate and apply DML parameters */
	if (!dc->hwss.update_bandwidth(dc, dc->current_state)) {
		dm_error("DC: update_bandwidth failed!\n");
		return false;
	}

	dc_exit_ips_for_hw_access(dc);

	/* disable writeback */
	if (dc->hwss.disable_writeback) {
		struct dwbc *dwb = dc->res_pool->dwbc[dwb_pipe_inst];

		if (dwb->funcs->is_enabled(dwb))
			dc->hwss.disable_writeback(dc, dwb_pipe_inst);
	}

	return true;
}

uint32_t dc_stream_get_vblank_counter(const struct dc_stream_state *stream)
{
	uint8_t i;
	struct dc  *dc = stream->ctx->dc;
	struct resource_context *res_ctx =
		&dc->current_state->res_ctx;

	dc_exit_ips_for_hw_access(dc);

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream || !tg)
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

	dc_exit_ips_for_hw_access(dc);

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

	dc_exit_ips_for_hw_access(dc);

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream || !tg)
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

	dc_exit_ips_for_hw_access(dc);

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

	dc_exit_ips_for_hw_access(dc);

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
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d, pixel_encoding:%s, color_depth:%s\n",
			stream->timing.pix_clk_100hz / 10,
			stream->timing.h_total,
			stream->timing.v_total,
			dc_pixel_encoding_to_str(stream->timing.pixel_encoding),
			dc_color_depth_to_str(stream->timing.display_color_depth));
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

/*
 * Finds the greatest index in refresh_rate_hz that contains a value <= refresh
 */
static int dc_stream_get_nearest_smallest_index(struct dc_stream_state *stream, int refresh)
{
	for (int i = 0; i < (LUMINANCE_DATA_TABLE_SIZE - 1); ++i) {
		if ((stream->lumin_data.refresh_rate_hz[i] <= refresh) && (refresh < stream->lumin_data.refresh_rate_hz[i + 1])) {
			return i;
		}
	}
	return 9;
}

/*
 * Finds a corresponding brightness for a given refresh rate between 2 given indices, where index1 < index2
 */
static int dc_stream_get_brightness_millinits_linear_interpolation (struct dc_stream_state *stream,
								     int index1,
								     int index2,
								     int refresh_hz)
{
	long long slope = 0;
	if (stream->lumin_data.refresh_rate_hz[index2] != stream->lumin_data.refresh_rate_hz[index1]) {
		slope = (stream->lumin_data.luminance_millinits[index2] - stream->lumin_data.luminance_millinits[index1]) /
			    (stream->lumin_data.refresh_rate_hz[index2] - stream->lumin_data.refresh_rate_hz[index1]);
	}

	int y_intercept = stream->lumin_data.luminance_millinits[index2] - slope * stream->lumin_data.refresh_rate_hz[index2];

	return (y_intercept + refresh_hz * slope);
}

/*
 * Finds a corresponding refresh rate for a given brightness between 2 given indices, where index1 < index2
 */
static int dc_stream_get_refresh_hz_linear_interpolation (struct dc_stream_state *stream,
							   int index1,
							   int index2,
							   int brightness_millinits)
{
	long long slope = 1;
	if (stream->lumin_data.refresh_rate_hz[index2] != stream->lumin_data.refresh_rate_hz[index1]) {
		slope = (stream->lumin_data.luminance_millinits[index2] - stream->lumin_data.luminance_millinits[index1]) /
				(stream->lumin_data.refresh_rate_hz[index2] - stream->lumin_data.refresh_rate_hz[index1]);
	}

	int y_intercept = stream->lumin_data.luminance_millinits[index2] - slope * stream->lumin_data.refresh_rate_hz[index2];

	return ((int)div64_s64((brightness_millinits - y_intercept), slope));
}

/*
 * Finds the current brightness in millinits given a refresh rate
 */
static int dc_stream_get_brightness_millinits_from_refresh (struct dc_stream_state *stream, int refresh_hz)
{
	int nearest_smallest_index = dc_stream_get_nearest_smallest_index(stream, refresh_hz);
	int nearest_smallest_value = stream->lumin_data.refresh_rate_hz[nearest_smallest_index];

	if (nearest_smallest_value == refresh_hz)
		return stream->lumin_data.luminance_millinits[nearest_smallest_index];

	if (nearest_smallest_index >= 9)
		return dc_stream_get_brightness_millinits_linear_interpolation(stream, nearest_smallest_index - 1, nearest_smallest_index, refresh_hz);

	if (nearest_smallest_value == stream->lumin_data.refresh_rate_hz[nearest_smallest_index + 1])
		return stream->lumin_data.luminance_millinits[nearest_smallest_index];

	return dc_stream_get_brightness_millinits_linear_interpolation(stream, nearest_smallest_index, nearest_smallest_index + 1, refresh_hz);
}

/*
 * Finds the lowest/highest refresh rate (depending on search_for_max_increase)
 * that can be achieved from starting_refresh_hz while staying
 * within flicker criteria
 */
static int dc_stream_calculate_flickerless_refresh_rate(struct dc_stream_state *stream,
							 int current_brightness,
							 int starting_refresh_hz,
							 bool is_gaming,
							 bool search_for_max_increase)
{
	int nearest_smallest_index = dc_stream_get_nearest_smallest_index(stream, starting_refresh_hz);

	int flicker_criteria_millinits = is_gaming ?
					 stream->lumin_data.flicker_criteria_milli_nits_GAMING :
					 stream->lumin_data.flicker_criteria_milli_nits_STATIC;

	int safe_upper_bound = current_brightness + flicker_criteria_millinits;
	int safe_lower_bound = current_brightness - flicker_criteria_millinits;
	int lumin_millinits_temp = 0;

	int offset = -1;
	if (search_for_max_increase) {
		offset = 1;
	}

	/*
	 * Increments up or down by 1 depending on search_for_max_increase
	 */
	for (int i = nearest_smallest_index; (i > 0 && !search_for_max_increase) || (i < (LUMINANCE_DATA_TABLE_SIZE - 1) && search_for_max_increase); i += offset) {

		lumin_millinits_temp = stream->lumin_data.luminance_millinits[i + offset];

		if ((lumin_millinits_temp >= safe_upper_bound) || (lumin_millinits_temp <= safe_lower_bound)) {

			if (stream->lumin_data.refresh_rate_hz[i + offset] == stream->lumin_data.refresh_rate_hz[i])
				return stream->lumin_data.refresh_rate_hz[i];

			int target_brightness = (stream->lumin_data.luminance_millinits[i + offset] >= (current_brightness + flicker_criteria_millinits)) ?
											current_brightness + flicker_criteria_millinits :
											current_brightness - flicker_criteria_millinits;

			int refresh = 0;

			/*
			 * Need the second input to be < third input for dc_stream_get_refresh_hz_linear_interpolation
			 */
			if (search_for_max_increase)
				refresh = dc_stream_get_refresh_hz_linear_interpolation(stream, i, i + offset, target_brightness);
			else
				refresh = dc_stream_get_refresh_hz_linear_interpolation(stream, i + offset, i, target_brightness);

			if (refresh == stream->lumin_data.refresh_rate_hz[i + offset])
				return stream->lumin_data.refresh_rate_hz[i + offset];

			return refresh;
		}
	}

	if (search_for_max_increase)
		return (int)div64_s64((long long)stream->timing.pix_clk_100hz*100, stream->timing.v_total*(long long)stream->timing.h_total);
	else
		return stream->lumin_data.refresh_rate_hz[0];
}

/*
 * Gets the max delta luminance within a specified refresh range
 */
static int dc_stream_get_max_delta_lumin_millinits(struct dc_stream_state *stream, int hz1, int hz2, bool isGaming)
{
	int lower_refresh_brightness = dc_stream_get_brightness_millinits_from_refresh (stream, hz1);
	int higher_refresh_brightness = dc_stream_get_brightness_millinits_from_refresh (stream, hz2);

	int min = lower_refresh_brightness;
	int max = higher_refresh_brightness;

	/*
	 * Static screen, therefore no need to scan through array
	 */
	if (!isGaming) {
		if (lower_refresh_brightness >= higher_refresh_brightness) {
			return lower_refresh_brightness - higher_refresh_brightness;
		}
		return higher_refresh_brightness - lower_refresh_brightness;
	}

	min = MIN(lower_refresh_brightness, higher_refresh_brightness);
	max = MAX(lower_refresh_brightness, higher_refresh_brightness);

	int nearest_smallest_index = dc_stream_get_nearest_smallest_index(stream, hz1);

	for (; nearest_smallest_index < (LUMINANCE_DATA_TABLE_SIZE - 1) &&
			stream->lumin_data.refresh_rate_hz[nearest_smallest_index + 1] <= hz2 ; nearest_smallest_index++) {
		min = MIN(min, stream->lumin_data.luminance_millinits[nearest_smallest_index + 1]);
		max = MAX(max, stream->lumin_data.luminance_millinits[nearest_smallest_index + 1]);
	}

	return (max - min);
}

/*
 * Determines the max flickerless instant vtotal delta for a stream.
 * Determines vtotal increase/decrease based on the bool "increase"
 */
static unsigned int dc_stream_get_max_flickerless_instant_vtotal_delta(struct dc_stream_state *stream, bool is_gaming, bool increase)
{
	if (stream->timing.v_total * stream->timing.h_total == 0)
		return 0;

	int current_refresh_hz = (int)div64_s64((long long)stream->timing.pix_clk_100hz*100, stream->timing.v_total*(long long)stream->timing.h_total);

	int safe_refresh_hz = dc_stream_calculate_flickerless_refresh_rate(stream,
							 dc_stream_get_brightness_millinits_from_refresh(stream, current_refresh_hz),
							 current_refresh_hz,
							 is_gaming,
							 increase);

	int safe_refresh_v_total = (int)div64_s64((long long)stream->timing.pix_clk_100hz*100, safe_refresh_hz*(long long)stream->timing.h_total);

	if (increase)
		return (((int) stream->timing.v_total - safe_refresh_v_total) >= 0) ? (stream->timing.v_total - safe_refresh_v_total) : 0;

	return ((safe_refresh_v_total - (int) stream->timing.v_total) >= 0) ? (safe_refresh_v_total - stream->timing.v_total) : 0;
}

/*
 * Finds the highest refresh rate that can be achieved
 * from starting_refresh_hz while staying within flicker criteria
 */
int dc_stream_calculate_max_flickerless_refresh_rate(struct dc_stream_state *stream, int starting_refresh_hz, bool is_gaming)
{
	if (!stream->lumin_data.is_valid)
		return 0;

	int current_brightness = dc_stream_get_brightness_millinits_from_refresh(stream, starting_refresh_hz);

	return dc_stream_calculate_flickerless_refresh_rate(stream,
							    current_brightness,
							    starting_refresh_hz,
							    is_gaming,
							    true);
}

/*
 * Finds the lowest refresh rate that can be achieved
 * from starting_refresh_hz while staying within flicker criteria
 */
int dc_stream_calculate_min_flickerless_refresh_rate(struct dc_stream_state *stream, int starting_refresh_hz, bool is_gaming)
{
	if (!stream->lumin_data.is_valid)
			return 0;

	int current_brightness = dc_stream_get_brightness_millinits_from_refresh(stream, starting_refresh_hz);

	return dc_stream_calculate_flickerless_refresh_rate(stream,
							    current_brightness,
							    starting_refresh_hz,
							    is_gaming,
							    false);
}

/*
 * Determines if there will be a flicker when moving between 2 refresh rates
 */
bool dc_stream_is_refresh_rate_range_flickerless(struct dc_stream_state *stream, int hz1, int hz2, bool is_gaming)
{

	/*
	 * Assume that we wont flicker if there is invalid data
	 */
	if (!stream->lumin_data.is_valid)
		return false;

	int dl = dc_stream_get_max_delta_lumin_millinits(stream, hz1, hz2, is_gaming);

	int flicker_criteria_millinits = (is_gaming) ?
					  stream->lumin_data.flicker_criteria_milli_nits_GAMING :
					  stream->lumin_data.flicker_criteria_milli_nits_STATIC;

	return (dl <= flicker_criteria_millinits);
}

/*
 * Determines the max instant vtotal delta increase that can be applied without
 * flickering for a given stream
 */
unsigned int dc_stream_get_max_flickerless_instant_vtotal_decrease(struct dc_stream_state *stream,
									  bool is_gaming)
{
	if (!stream->lumin_data.is_valid)
		return 0;

	return dc_stream_get_max_flickerless_instant_vtotal_delta(stream, is_gaming, true);
}

/*
 * Determines the max instant vtotal delta decrease that can be applied without
 * flickering for a given stream
 */
unsigned int dc_stream_get_max_flickerless_instant_vtotal_increase(struct dc_stream_state *stream,
									  bool is_gaming)
{
	if (!stream->lumin_data.is_valid)
		return 0;

	return dc_stream_get_max_flickerless_instant_vtotal_delta(stream, is_gaming, false);
}

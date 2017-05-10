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

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"

#include "resource.h"

#include "clock_source.h"
#include "dc_bios_types.h"

#include "dce_calcs.h"
#include "bios_parser_interface.h"
#include "include/irq_service_interface.h"
#include "transform.h"
#include "timing_generator.h"
#include "virtual/virtual_link_encoder.h"

#include "link_hwss.h"
#include "link_encoder.h"

#include "dc_link_ddc.h"
#include "dm_helpers.h"
#include "mem_input.h"

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void destroy_links(struct core_dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++) {
		if (NULL != dc->links[i])
			link_destroy(&dc->links[i]);
	}
}

static bool create_links(
		struct core_dc *dc,
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

	if (connectors_num == 0 && num_virtual_links == 0) {
		dm_error("DC: Number of connectors is zero!\n");
	}

	dm_output_to_console(
		"DC: %s: connectors_num: physical:%d, virtual:%d\n",
		__func__,
		connectors_num,
		num_virtual_links);

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct core_link *link;

		link_init_params.ctx = dc->ctx;
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link = link_create(&link_init_params);

		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
		} else {
			dm_error("DC: failed to create link!\n");
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		struct core_link *link = dm_alloc(sizeof(*link));
		struct encoder_init_data enc_init = {0};

		if (link == NULL) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->ctx = dc->ctx;
		link->dc = dc;
		link->public.connector_signal = SIGNAL_TYPE_VIRTUAL;
		link->link_id.type = OBJECT_TYPE_CONNECTOR;
		link->link_id.id = CONNECTOR_ID_VIRTUAL;
		link->link_id.enum_id = ENUM_ID_1;
		link->link_enc = dm_alloc(sizeof(*link->link_enc));

		enc_init.ctx = dc->ctx;
		enc_init.channel = CHANNEL_ID_UNKNOWN;
		enc_init.hpd_source = HPD_SOURCEID_UNKNOWN;
		enc_init.transmitter = TRANSMITTER_UNKNOWN;
		enc_init.connector = link->link_id;
		enc_init.encoder.type = OBJECT_TYPE_ENCODER;
		enc_init.encoder.id = ENCODER_ID_INTERNAL_VIRTUAL;
		enc_init.encoder.enum_id = ENUM_ID_1;
		virtual_link_encoder_construct(link->link_enc, &enc_init);

		link->public.link_index = dc->link_count;
		dc->links[dc->link_count] = link;
		dc->link_count++;
	}

	return true;

failed_alloc:
	return false;
}

static bool stream_adjust_vmin_vmax(struct dc *dc,
		const struct dc_stream **stream, int num_streams,
		int vmin, int vmax)
{
	/* TODO: Support multiple streams */
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream[0]);
	int i = 0;
	bool ret = false;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &core_dc->current_context->res_ctx.pipe_ctx[i];

		if (pipe->stream == core_stream && pipe->stream_enc) {
			core_dc->hwss.set_drr(&pipe, 1, vmin, vmax);

			/* build and update the info frame */
			resource_build_info_frame(pipe);
			core_dc->hwss.update_info_frame(pipe);

			ret = true;
		}
	}
	return ret;
}

static bool stream_get_crtc_position(struct dc *dc,
		const struct dc_stream **stream, int num_streams,
		unsigned int *v_pos, unsigned int *nom_v_pos)
{
	/* TODO: Support multiple streams */
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream[0]);
	int i = 0;
	bool ret = false;
	struct crtc_position position;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe =
				&core_dc->current_context->res_ctx.pipe_ctx[i];

		if (pipe->stream == core_stream && pipe->stream_enc) {
			core_dc->hwss.get_position(&pipe, 1, &position);

			*v_pos = position.vertical_count;
			*nom_v_pos = position.nominal_vcount;
			ret = true;
		}
	}
	return ret;
}

static bool set_gamut_remap(struct dc *dc, const struct dc_stream *stream)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream);
	int i = 0;
	bool ret = false;
	struct pipe_ctx *pipes;

	for (i = 0; i < MAX_PIPES; i++) {
		if (core_dc->current_context->res_ctx.pipe_ctx[i].stream
				== core_stream) {

			pipes = &core_dc->current_context->res_ctx.pipe_ctx[i];
			core_dc->hwss.set_plane_config(core_dc, pipes,
					&core_dc->current_context->res_ctx);
			ret = true;
		}
	}

	return ret;
}

static void set_static_screen_events(struct dc *dc,
		const struct dc_stream **stream,
		int num_streams,
		const struct dc_static_screen_events *events)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i = 0;
	int j = 0;
	struct pipe_ctx *pipes_affected[MAX_PIPES];
	int num_pipes_affected = 0;

	for (i = 0; i < num_streams; i++) {
		struct core_stream *core_stream = DC_STREAM_TO_CORE(stream[i]);

		for (j = 0; j < MAX_PIPES; j++) {
			if (core_dc->current_context->res_ctx.pipe_ctx[j].stream
					== core_stream) {
				pipes_affected[num_pipes_affected++] =
						&core_dc->current_context->res_ctx.pipe_ctx[j];
			}
		}
	}

	core_dc->hwss.set_static_screen_control(pipes_affected, num_pipes_affected, events);
}

/* This function is not expected to fail, proper implementation of
 * validation will prevent this from ever being called for unsupported
 * configurations.
 */
static void stream_update_scaling(
		const struct dc *dc,
		const struct dc_stream *dc_stream,
		const struct rect *src,
		const struct rect *dst)
{
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct validate_context *cur_ctx = core_dc->current_context;
	int i;

	if (src)
		stream->public.src = *src;

	if (dst)
		stream->public.dst = *dst;

	for (i = 0; i < cur_ctx->stream_count; i++) {
		struct core_stream *cur_stream = cur_ctx->streams[i];

		if (stream == cur_stream) {
			struct dc_stream_status *status = &cur_ctx->stream_status[i];

			if (status->surface_count)
				if (!dc_commit_surfaces_to_stream(
						&core_dc->public,
						status->surfaces,
						status->surface_count,
						&cur_stream->public))
					/* Need to debug validation */
					BREAK_TO_DEBUGGER();

			return;
		}
	}
}

static void set_drive_settings(struct dc *dc,
		struct link_training_settings *lt_settings,
		const struct dc_link *link)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i;

	for (i = 0; i < core_dc->link_count; i++) {
		if (&core_dc->links[i]->public == link)
			break;
	}

	if (i >= core_dc->link_count)
		ASSERT_CRITICAL(false);

	dc_link_dp_set_drive_settings(&core_dc->links[i]->public, lt_settings);
}

static void perform_link_training(struct dc *dc,
		struct dc_link_settings *link_setting,
		bool skip_video_pattern)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i;

	for (i = 0; i < core_dc->link_count; i++)
		dc_link_dp_perform_link_training(
			&core_dc->links[i]->public,
			link_setting,
			skip_video_pattern);
}

static void set_preferred_link_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		const struct dc_link *link)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);

	core_link->public.verified_link_cap.lane_count =
				link_setting->lane_count;
	core_link->public.verified_link_cap.link_rate =
				link_setting->link_rate;
	dp_retrain_link_dp_test(core_link, link_setting, false);
}

static void enable_hpd(const struct dc_link *link)
{
	dc_link_dp_enable_hpd(link);
}

static void disable_hpd(const struct dc_link *link)
{
	dc_link_dp_disable_hpd(link);
}


static void set_test_pattern(
		const struct dc_link *link,
		enum dp_test_pattern test_pattern,
		const struct link_training_settings *p_link_settings,
		const unsigned char *p_custom_pattern,
		unsigned int cust_pattern_size)
{
	if (link != NULL)
		dc_link_dp_set_test_pattern(
			link,
			test_pattern,
			p_link_settings,
			p_custom_pattern,
			cust_pattern_size);
}

void set_dither_option(const struct dc_stream *dc_stream,
		enum dc_dither_option option)
{
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct bit_depth_reduction_params params;
	struct core_link *core_link = DC_LINK_TO_CORE(stream->status.link);
	struct pipe_ctx *pipes =
			core_link->dc->current_context->res_ctx.pipe_ctx;

	memset(&params, 0, sizeof(params));
	if (!stream)
		return;
	if (option > DITHER_OPTION_MAX)
		return;
	if (option == DITHER_OPTION_DEFAULT) {
		switch (stream->public.timing.display_color_depth) {
		case COLOR_DEPTH_666:
			stream->public.dither_option = DITHER_OPTION_SPATIAL6;
			break;
		case COLOR_DEPTH_888:
			stream->public.dither_option = DITHER_OPTION_SPATIAL8;
			break;
		case COLOR_DEPTH_101010:
			stream->public.dither_option = DITHER_OPTION_SPATIAL10;
			break;
		default:
			option = DITHER_OPTION_DISABLE;
		}
	} else {
		stream->public.dither_option = option;
	}
	resource_build_bit_depth_reduction_params(stream,
				&params);
	stream->bit_depth_params = params;
	pipes->opp->funcs->
		opp_program_bit_depth_reduction(pipes->opp, &params);
}

static void allocate_dc_stream_funcs(struct core_dc *core_dc)
{
	core_dc->public.stream_funcs.stream_update_scaling = stream_update_scaling;
	if (core_dc->hwss.set_drr != NULL) {
		core_dc->public.stream_funcs.adjust_vmin_vmax =
				stream_adjust_vmin_vmax;
	}

	core_dc->public.stream_funcs.set_static_screen_events =
			set_static_screen_events;

	core_dc->public.stream_funcs.get_crtc_position =
			stream_get_crtc_position;

	core_dc->public.stream_funcs.set_gamut_remap =
			set_gamut_remap;

	core_dc->public.stream_funcs.set_dither_option =
			set_dither_option;

	core_dc->public.link_funcs.set_drive_settings =
			set_drive_settings;

	core_dc->public.link_funcs.perform_link_training =
			perform_link_training;

	core_dc->public.link_funcs.set_preferred_link_settings =
			set_preferred_link_settings;

	core_dc->public.link_funcs.enable_hpd =
			enable_hpd;

	core_dc->public.link_funcs.disable_hpd =
			disable_hpd;

	core_dc->public.link_funcs.set_test_pattern =
			set_test_pattern;
}

static void destruct(struct core_dc *dc)
{
	dc_resource_validate_ctx_destruct(dc->current_context);

	destroy_links(dc);

	dc_destroy_resource_pool(dc);

	if (dc->ctx->gpio_service)
		dal_gpio_service_destroy(&dc->ctx->gpio_service);

	if (dc->ctx->i2caux)
		dal_i2caux_destroy(&dc->ctx->i2caux);

	if (dc->ctx->created_bios)
		dal_bios_parser_destroy(&dc->ctx->dc_bios);

	if (dc->ctx->logger)
		dal_logger_destroy(&dc->ctx->logger);

	dm_free(dc->current_context);
	dc->current_context = NULL;

	dm_free(dc->ctx);
	dc->ctx = NULL;
}

static bool construct(struct core_dc *dc,
		const struct dc_init_data *init_params)
{
	struct dal_logger *logger;
	struct dc_context *dc_ctx = dm_alloc(sizeof(*dc_ctx));
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;

	if (!dc_ctx) {
		dm_error("%s: failed to create ctx\n", __func__);
		goto ctx_fail;
	}

	dc->current_context = dm_alloc(sizeof(*dc->current_context));

	if (!dc->current_context) {
		dm_error("%s: failed to create validate ctx\n", __func__);
		goto val_ctx_fail;
	}

	dc_ctx->cgs_device = init_params->cgs_device;
	dc_ctx->driver_context = init_params->driver;
	dc_ctx->dc = &dc->public;
	dc_ctx->asic_id = init_params->asic_id;

	/* Create logger */
	logger = dal_logger_create(dc_ctx);

	if (!logger) {
		/* can *not* call logger. call base driver 'print error' */
		dm_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}
	dc_ctx->logger = logger;
	dc->ctx = dc_ctx;
	dc->ctx->dce_environment = init_params->dce_environment;

	dc_version = resource_parse_asic_id(init_params->asic_id);
	dc->ctx->dce_version = dc_version;

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
				&bp_init_data, dc_version);

		if (!dc_ctx->dc_bios) {
			ASSERT_CRITICAL(false);
			goto bios_fail;
		}

		dc_ctx->created_bios = true;
		}

	/* Create I2C AUX */
	dc_ctx->i2caux = dal_i2caux_create(dc_ctx);

	if (!dc_ctx->i2caux) {
		ASSERT_CRITICAL(false);
		goto failed_to_create_i2caux;
	}

	/* Create GPIO service */
	dc_ctx->gpio_service = dal_gpio_service_create(
			dc_version,
			dc_ctx->dce_environment,
			dc_ctx);

	if (!dc_ctx->gpio_service) {
		ASSERT_CRITICAL(false);
		goto gpio_fail;
	}

	dc->res_pool = dc_create_resource_pool(
			dc,
			init_params->num_virtual_links,
			dc_version,
			init_params->asic_id);
	if (!dc->res_pool)
		goto create_resource_fail;

	if (!create_links(dc, init_params->num_virtual_links))
		goto create_links_fail;

	allocate_dc_stream_funcs(dc);

	return true;

	/**** error handling here ****/
create_links_fail:
create_resource_fail:
gpio_fail:
failed_to_create_i2caux:
bios_fail:
logger_fail:
val_ctx_fail:
ctx_fail:
	destruct(dc);
	return false;
}

/*
void ProgramPixelDurationV(unsigned int pixelClockInKHz )
{
	fixed31_32 pixel_duration = Fixed31_32(100000000, pixelClockInKHz) * 10;
	unsigned int pixDurationInPico = round(pixel_duration);

	DPG_PIPE_ARBITRATION_CONTROL1 arb_control;

	arb_control.u32All = ReadReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	arb_control.u32All = ReadReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV0_REPEATER_PROGRAM, 0x11);

	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV1_REPEATER_PROGRAM, 0x11);
}
*/

/*******************************************************************************
 * Public functions
 ******************************************************************************/

struct dc *dc_create(const struct dc_init_data *init_params)
 {
	struct core_dc *core_dc = dm_alloc(sizeof(*core_dc));
	unsigned int full_pipe_count;

	if (NULL == core_dc)
		goto alloc_fail;

	if (false == construct(core_dc, init_params))
		goto construct_fail;

	/*TODO: separate HW and SW initialization*/
	core_dc->hwss.init_hw(core_dc);

	full_pipe_count = core_dc->res_pool->pipe_count;
	if (core_dc->res_pool->underlay_pipe_index != NO_UNDERLAY_PIPE)
		full_pipe_count--;
	core_dc->public.caps.max_streams = min(
			full_pipe_count,
			core_dc->res_pool->stream_enc_count);

	core_dc->public.caps.max_links = core_dc->link_count;
	core_dc->public.caps.max_audios = core_dc->res_pool->audio_count;

	core_dc->public.config = init_params->flags;

	dm_logger_write(core_dc->ctx->logger, LOG_DC,
			"Display Core initialized\n");


	/* TODO: missing feature to be enabled */
	core_dc->public.debug.disable_dfs_bypass = true;

	return &core_dc->public;

construct_fail:
	dm_free(core_dc);

alloc_fail:
	return NULL;
}

void dc_destroy(struct dc **dc)
{
	struct core_dc *core_dc = DC_TO_CORE(*dc);
	destruct(core_dc);
	dm_free(core_dc);
	*dc = NULL;
}

static bool is_validation_required(
		const struct core_dc *dc,
		const struct dc_validation_set set[],
		int set_count)
{
	const struct validate_context *context = dc->current_context;
	int i, j;

	if (context->stream_count != set_count)
		return true;

	for (i = 0; i < set_count; i++) {

		if (set[i].surface_count != context->stream_status[i].surface_count)
			return true;
		if (!is_stream_unchanged(DC_STREAM_TO_CORE(set[i].stream), context->streams[i]))
			return true;

		for (j = 0; j < set[i].surface_count; j++) {
			struct dc_surface temp_surf = { 0 };

			temp_surf = *context->stream_status[i].surfaces[j];
			temp_surf.clip_rect = set[i].surfaces[j]->clip_rect;
			temp_surf.dst_rect.x = set[i].surfaces[j]->dst_rect.x;
			temp_surf.dst_rect.y = set[i].surfaces[j]->dst_rect.y;

			if (memcmp(&temp_surf, set[i].surfaces[j], sizeof(temp_surf)) != 0)
				return true;
		}
	}

	return false;
}

struct validate_context *dc_get_validate_context(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(sizeof(struct validate_context));
	if(context == NULL)
		goto context_alloc_fail;

	if (!is_validation_required(core_dc, set, set_count)) {
		dc_resource_validate_ctx_copy_construct(core_dc->current_context, context);
		return context;
	}

	result = core_dc->res_pool->funcs->validate_with_context(
						core_dc, set, set_count, context);

context_alloc_fail:
	if (result != DC_OK) {
		dm_logger_write(core_dc->ctx->logger, LOG_WARNING,
				"%s:resource validation failed, dc_status:%d\n",
				__func__,
				result);

		dc_resource_validate_ctx_destruct(context);
		dm_free(context);
		context = NULL;
	}

	return context;

}

bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	struct validate_context *ctx;

	ctx = dc_get_validate_context(dc, set, set_count);
	if (ctx) {
		dc_resource_validate_ctx_destruct(ctx);
		dm_free(ctx);
		return true;
	}

	return false;
}

bool dc_validate_guaranteed(
		const struct dc *dc,
		const struct dc_stream *stream)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool->funcs->validate_guaranteed(
					core_dc, stream, context);

	dc_resource_validate_ctx_destruct(context);
	dm_free(context);

context_alloc_fail:
	if (result != DC_OK) {
		dm_logger_write(core_dc->ctx->logger, LOG_WARNING,
			"%s:guaranteed validation failed, dc_status:%d\n",
			__func__,
			result);
		}

	return (result == DC_OK);
}

static void program_timing_sync(
		struct core_dc *core_dc,
		struct validate_context *ctx)
{
	int i, j;
	int group_index = 0;
	int pipe_count = core_dc->res_pool->pipe_count;
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
			struct pipe_ctx *temp;

			if (!pipe_set[j]->tg->funcs->is_blanked(pipe_set[j]->tg)) {
				if (j == 0)
					break;

				temp = pipe_set[0];
				pipe_set[0] = pipe_set[j];
				pipe_set[j] = temp;
				break;
			}
		}

		/* remove any other unblanked pipes as they have already been synced */
		for (j = j + 1; j < group_size; j++) {
			if (!pipe_set[j]->tg->funcs->is_blanked(pipe_set[j]->tg)) {
				group_size--;
				pipe_set[j] = pipe_set[group_size];
				j--;
			}
		}

		if (group_size > 1) {
			core_dc->hwss.enable_timing_synchronization(
				core_dc, group_index, group_size, pipe_set);
			group_index++;
		}
	}
}

static bool streams_changed(
		struct core_dc *dc,
		const struct dc_stream *streams[],
		uint8_t stream_count)
{
	uint8_t i;

	if (stream_count != dc->current_context->stream_count)
		return true;

	for (i = 0; i < dc->current_context->stream_count; i++) {
		if (&dc->current_context->streams[i]->public != streams[i])
			return true;
	}

	return false;
}

bool dc_commit_streams(
	struct dc *dc,
	const struct dc_stream *streams[],
	uint8_t stream_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;
	struct dc_validation_set set[MAX_STREAMS] = { {0, {0} } };
	int i, j;

	if (false == streams_changed(core_dc, streams, stream_count))
		return DC_OK;

	dm_logger_write(core_dc->ctx->logger, LOG_DC, "%s: %d streams\n",
				__func__, stream_count);

	for (i = 0; i < stream_count; i++) {
		const struct dc_stream *stream = streams[i];
		const struct dc_stream_status *status = dc_stream_get_status(stream);
		int j;

		dc_stream_log(stream,
				core_dc->ctx->logger,
				LOG_DC);

		set[i].stream = stream;

		if (status) {
			set[i].surface_count = status->surface_count;
			for (j = 0; j < status->surface_count; j++)
				set[i].surfaces[j] = status->surfaces[j];
		}

	}

	context = dm_alloc(sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool->funcs->validate_with_context(core_dc, set, stream_count, context);
	if (result != DC_OK){
		dm_logger_write(core_dc->ctx->logger, LOG_ERROR,
					"%s: Context validation failed! dc_status:%d\n",
					__func__,
					result);
		BREAK_TO_DEBUGGER();
		dc_resource_validate_ctx_destruct(context);
		goto fail;
	}

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		core_dc->hwss.enable_accelerated_mode(core_dc);
	}

	if (result == DC_OK) {
		result = core_dc->hwss.apply_ctx_to_hw(core_dc, context);
	}

	program_timing_sync(core_dc, context);

	for (i = 0; i < context->stream_count; i++) {
		const struct core_sink *sink = context->streams[i]->sink;

		for (j = 0; j < context->stream_status[i].surface_count; j++) {
			struct core_surface *surface =
					DC_SURFACE_TO_CORE(context->stream_status[i].surfaces[j]);

			core_dc->hwss.apply_ctx_for_surface(core_dc, surface, context);
		}

		CONN_MSG_MODE(sink->link, "{%dx%d, %dx%d@%dKhz}",
				context->streams[i]->public.timing.h_addressable,
				context->streams[i]->public.timing.v_addressable,
				context->streams[i]->public.timing.h_total,
				context->streams[i]->public.timing.v_total,
				context->streams[i]->public.timing.pix_clk_khz);
	}

	dc_resource_validate_ctx_destruct(core_dc->current_context);
	dm_free(core_dc->current_context);

	core_dc->current_context = context;

	return (result == DC_OK);

fail:
	dm_free(context);

context_alloc_fail:
	return (result == DC_OK);
}

bool dc_pre_update_surfaces_to_stream(
		struct dc *dc,
		const struct dc_surface *const *new_surfaces,
		uint8_t new_surface_count,
		const struct dc_stream *dc_stream)
{
	return true;
}

bool dc_post_update_surfaces_to_stream(struct dc *dc)
{
	int i;
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct validate_context *context = core_dc->current_context;

	post_surface_trace(dc);

	for (i = 0; i < core_dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].stream == NULL) {
			context->res_ctx.pipe_ctx[i].pipe_idx = i;
			core_dc->hwss.power_down_front_end(
					core_dc, &context->res_ctx.pipe_ctx[i]);
		}

	core_dc->hwss.set_bandwidth(core_dc, context, true);

	return true;
}

bool dc_commit_surfaces_to_stream(
		struct dc *dc,
		const struct dc_surface **new_surfaces,
		uint8_t new_surface_count,
		const struct dc_stream *dc_stream)
{
	struct dc_surface_update updates[MAX_SURFACES];
	struct dc_flip_addrs flip_addr[MAX_SURFACES];
	struct dc_plane_info plane_info[MAX_SURFACES];
	struct dc_scaling_info scaling_info[MAX_SURFACES];
	int i;

	memset(updates, 0, sizeof(updates));
	memset(flip_addr, 0, sizeof(flip_addr));
	memset(plane_info, 0, sizeof(plane_info));
	memset(scaling_info, 0, sizeof(scaling_info));

	for (i = 0; i < new_surface_count; i++) {
		updates[i].surface = new_surfaces[i];
		updates[i].gamma =
			(struct dc_gamma *)new_surfaces[i]->gamma_correction;
		flip_addr[i].address = new_surfaces[i]->address;
		flip_addr[i].flip_immediate = new_surfaces[i]->flip_immediate;
		plane_info[i].color_space = new_surfaces[i]->color_space;
		plane_info[i].format = new_surfaces[i]->format;
		plane_info[i].plane_size = new_surfaces[i]->plane_size;
		plane_info[i].rotation = new_surfaces[i]->rotation;
		plane_info[i].horizontal_mirror = new_surfaces[i]->horizontal_mirror;
		plane_info[i].stereo_format = new_surfaces[i]->stereo_format;
		plane_info[i].tiling_info = new_surfaces[i]->tiling_info;
		plane_info[i].visible = new_surfaces[i]->visible;
		plane_info[i].dcc = new_surfaces[i]->dcc;
		scaling_info[i].scaling_quality = new_surfaces[i]->scaling_quality;
		scaling_info[i].src_rect = new_surfaces[i]->src_rect;
		scaling_info[i].dst_rect = new_surfaces[i]->dst_rect;
		scaling_info[i].clip_rect = new_surfaces[i]->clip_rect;

		updates[i].flip_addr = &flip_addr[i];
		updates[i].plane_info = &plane_info[i];
		updates[i].scaling_info = &scaling_info[i];
	}
	dc_update_surfaces_for_stream(dc, updates, new_surface_count, dc_stream);

	return dc_post_update_surfaces_to_stream(dc);
}

static bool is_surface_in_context(
		const struct validate_context *context,
		const struct dc_surface *surface)
{
	int j;

	for (j = 0; j < MAX_PIPES; j++) {
		const struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

		if (surface == &pipe_ctx->surface->public) {
			return true;
		}
	}

	return false;
}

static unsigned int pixel_format_to_bpp(enum surface_pixel_format format)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		return 12;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		return 16;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		return 32;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		return 64;
	default:
		ASSERT_CRITICAL(false);
		return -1;
	}
}

static enum surface_update_type get_plane_info_update_type(
		const struct dc_surface_update *u,
		int surface_index)
{
	struct dc_plane_info temp_plane_info = { { { { 0 } } } };

	if (!u->plane_info)
		return UPDATE_TYPE_FAST;

	/* Copy all parameters that will cause a full update
	 * from current surface, the rest of the parameters
	 * from provided plane configuration.
	 * Perform memory compare and special validation
	 * for those that can cause fast/medium updates
	 */

	/* Full update parameters */
	temp_plane_info.color_space = u->surface->color_space;
	temp_plane_info.dcc = u->surface->dcc;
	temp_plane_info.horizontal_mirror = u->surface->horizontal_mirror;
	temp_plane_info.plane_size = u->surface->plane_size;
	temp_plane_info.rotation = u->surface->rotation;
	temp_plane_info.stereo_format = u->surface->stereo_format;
	temp_plane_info.tiling_info = u->surface->tiling_info;

	/* Special Validation parameters */
	temp_plane_info.format = u->plane_info->format;

	if (surface_index == 0)
		temp_plane_info.visible = u->plane_info->visible;
	else
		temp_plane_info.visible = u->surface->visible;

	if (memcmp(u->plane_info, &temp_plane_info,
			sizeof(struct dc_plane_info)) != 0)
		return UPDATE_TYPE_FULL;

	if (pixel_format_to_bpp(u->plane_info->format) !=
			pixel_format_to_bpp(u->surface->format)) {
		return UPDATE_TYPE_FULL;
	} else {
		return UPDATE_TYPE_MED;
	}
}

static enum surface_update_type  get_scaling_info_update_type(
		const struct dc_surface_update *u)
{
	struct dc_scaling_info temp_scaling_info = { { 0 } };

	if (!u->scaling_info)
		return UPDATE_TYPE_FAST;

	/* Copy all parameters that will cause a full update
	 * from current surface, the rest of the parameters
	 * from provided plane configuration.
	 * Perform memory compare and special validation
	 * for those that can cause fast/medium updates
	 */

	/* Full Update Parameters */
	temp_scaling_info.dst_rect = u->surface->dst_rect;
	temp_scaling_info.src_rect = u->surface->src_rect;
	temp_scaling_info.scaling_quality = u->surface->scaling_quality;

	/* Special validation required */
	temp_scaling_info.clip_rect = u->scaling_info->clip_rect;

	if (memcmp(u->scaling_info, &temp_scaling_info,
			sizeof(struct dc_scaling_info)) != 0)
		return UPDATE_TYPE_FULL;

	/* Check Clip rectangles if not equal
	 * difference is in offsets == > UPDATE_TYPE_MED
	 * difference is in dimensions == > UPDATE_TYPE_FULL
	 */
	if (memcmp(&u->scaling_info->clip_rect,
			&u->surface->clip_rect, sizeof(struct rect)) != 0) {
		if ((u->scaling_info->clip_rect.height ==
			u->surface->clip_rect.height) &&
			(u->scaling_info->clip_rect.width ==
			u->surface->clip_rect.width)) {
			return UPDATE_TYPE_MED;
		} else {
			return UPDATE_TYPE_FULL;
		}
	}

	return UPDATE_TYPE_FAST;
}

static enum surface_update_type det_surface_update(
		const struct core_dc *dc,
		const struct dc_surface_update *u,
		int surface_index)
{
	const struct validate_context *context = dc->current_context;
	enum surface_update_type type = UPDATE_TYPE_FAST;
	enum surface_update_type overall_type = UPDATE_TYPE_FAST;

	if (!is_surface_in_context(context, u->surface))
		return UPDATE_TYPE_FULL;

	type = get_plane_info_update_type(u, surface_index);
	if (overall_type < type)
		overall_type = type;

	type = get_scaling_info_update_type(u);
	if (overall_type < type)
		overall_type = type;

	if (u->in_transfer_func ||
		u->hdr_static_metadata) {
		if (overall_type < UPDATE_TYPE_MED)
			overall_type = UPDATE_TYPE_MED;
	}

	return overall_type;
}

enum surface_update_type dc_check_update_surfaces_for_stream(
		struct dc *dc,
		struct dc_surface_update *updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		const struct dc_stream_status *stream_status)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i;
	enum surface_update_type overall_type = UPDATE_TYPE_FAST;

	if (stream_status == NULL || stream_status->surface_count != surface_count)
		return UPDATE_TYPE_FULL;

	if (stream_update)
		return UPDATE_TYPE_FULL;

	for (i = 0 ; i < surface_count; i++) {
		enum surface_update_type type =
				det_surface_update(core_dc, &updates[i], i);

		if (type == UPDATE_TYPE_FULL)
			return type;

		if (overall_type < type)
			overall_type = type;
	}

	return overall_type;
}

void dc_update_surfaces_for_stream(struct dc *dc,
		struct dc_surface_update *surface_updates, int surface_count,
		const struct dc_stream *dc_stream)
{
	dc_update_surfaces_and_stream(dc, surface_updates, surface_count,
			dc_stream, NULL);
}

enum surface_update_type update_surface_trace_level = UPDATE_TYPE_FULL;

void dc_update_surfaces_and_stream(struct dc *dc,
		struct dc_surface_update *srf_updates, int surface_count,
		const struct dc_stream *dc_stream,
		struct dc_stream_update *stream_update)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct validate_context *context;
	int i, j;
	enum surface_update_type update_type;
	const struct dc_stream_status *stream_status;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	stream_status = dc_stream_get_status(dc_stream);
	ASSERT(stream_status);
	if (!stream_status)
		return; /* Cannot commit surface to stream that is not committed */

	update_type = dc_check_update_surfaces_for_stream(
			dc, srf_updates, surface_count, stream_update, stream_status);

	if (update_type >= update_surface_trace_level)
		update_surface_trace(dc, srf_updates, surface_count);

	if (update_type >= UPDATE_TYPE_FULL) {
		const struct dc_surface *new_surfaces[MAX_SURFACES] = { 0 };

		for (i = 0; i < surface_count; i++)
			new_surfaces[i] = srf_updates[i].surface;

		/* initialize scratch memory for building context */
		context = dm_alloc(sizeof(*context));
		dc_resource_validate_ctx_copy_construct(
				core_dc->current_context, context);

		/* add surface to context */
		if (!resource_attach_surfaces_to_context(
				new_surfaces, surface_count, dc_stream,
				context, core_dc->res_pool)) {
			BREAK_TO_DEBUGGER();
			goto fail;
		}
	} else {
		context = core_dc->current_context;
	}

	/* update current stream with the new updates */
	if (stream_update) {
		if ((stream_update->src.height != 0) &&
				(stream_update->src.width != 0))
			stream->public.src = stream_update->src;

		if ((stream_update->dst.height != 0) &&
				(stream_update->dst.width != 0))
			stream->public.dst = stream_update->dst;

		if (stream_update->out_transfer_func &&
				stream_update->out_transfer_func !=
				dc_stream->out_transfer_func) {
			if (stream_update->out_transfer_func->type !=
					TF_TYPE_UNKNOWN) {
				if (dc_stream->out_transfer_func != NULL)
					dc_transfer_func_release
					(dc_stream->out_transfer_func);
				dc_transfer_func_retain(stream_update->
					out_transfer_func);
				stream->public.out_transfer_func =
					stream_update->out_transfer_func;
			}
		}
	}

	/* save update parameters into surface */
	for (i = 0; i < surface_count; i++) {
		struct core_surface *surface =
				DC_SURFACE_TO_CORE(srf_updates[i].surface);

		if (srf_updates[i].flip_addr) {
			surface->public.address = srf_updates[i].flip_addr->address;
			surface->public.flip_immediate =
					srf_updates[i].flip_addr->flip_immediate;
		}

		if (srf_updates[i].scaling_info) {
			surface->public.scaling_quality =
					srf_updates[i].scaling_info->scaling_quality;
			surface->public.dst_rect =
					srf_updates[i].scaling_info->dst_rect;
			surface->public.src_rect =
					srf_updates[i].scaling_info->src_rect;
			surface->public.clip_rect =
					srf_updates[i].scaling_info->clip_rect;
		}

		if (srf_updates[i].plane_info) {
			surface->public.color_space =
					srf_updates[i].plane_info->color_space;
			surface->public.format =
					srf_updates[i].plane_info->format;
			surface->public.plane_size =
					srf_updates[i].plane_info->plane_size;
			surface->public.rotation =
					srf_updates[i].plane_info->rotation;
			surface->public.horizontal_mirror =
					srf_updates[i].plane_info->horizontal_mirror;
			surface->public.stereo_format =
					srf_updates[i].plane_info->stereo_format;
			surface->public.tiling_info =
					srf_updates[i].plane_info->tiling_info;
			surface->public.visible =
					srf_updates[i].plane_info->visible;
			surface->public.dcc =
					srf_updates[i].plane_info->dcc;
		}

		if (update_type >= UPDATE_TYPE_MED) {
			for (j = 0; j < core_dc->res_pool->pipe_count; j++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

				if (pipe_ctx->surface != surface)
					continue;

				resource_build_scaling_params(pipe_ctx);
			}
		}

		if (srf_updates[i].gamma &&
			srf_updates[i].gamma != surface->public.gamma_correction) {
			if (surface->public.gamma_correction != NULL)
				dc_gamma_release(&surface->public.
						gamma_correction);

			dc_gamma_retain(srf_updates[i].gamma);
			surface->public.gamma_correction =
						srf_updates[i].gamma;
		}

		if (srf_updates[i].in_transfer_func &&
			srf_updates[i].in_transfer_func != surface->public.in_transfer_func) {
			if (surface->public.in_transfer_func != NULL)
				dc_transfer_func_release(
						surface->public.
						in_transfer_func);

			dc_transfer_func_retain(
					srf_updates[i].in_transfer_func);
			surface->public.in_transfer_func =
					srf_updates[i].in_transfer_func;
		}

		if (srf_updates[i].hdr_static_metadata)
			surface->public.hdr_static_ctx =
				*(srf_updates[i].hdr_static_metadata);
	}

	if (update_type == UPDATE_TYPE_FULL) {
		if (!core_dc->res_pool->funcs->validate_bandwidth(core_dc, context)) {
			BREAK_TO_DEBUGGER();
			goto fail;
		} else
			core_dc->hwss.set_bandwidth(core_dc, context, false);
	}

	if (!surface_count)  /* reset */
		core_dc->hwss.apply_ctx_for_surface(core_dc, NULL, context);

	/* Lock pipes for provided surfaces */
	for (i = 0; i < surface_count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(srf_updates[i].surface);

		for (j = 0; j < core_dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];

			if (pipe_ctx->surface != surface)
				continue;
			if (!pipe_ctx->tg->funcs->is_blanked(pipe_ctx->tg)) {
				core_dc->hwss.pipe_control_lock(
						core_dc,
						pipe_ctx,
						true);
			}
		}
	}

	/* Perform requested Updates */
	for (i = 0; i < surface_count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(srf_updates[i].surface);

		if (update_type >= UPDATE_TYPE_MED) {
				core_dc->hwss.apply_ctx_for_surface(
						core_dc, surface, context);
				context_timing_trace(dc, &context->res_ctx);
		}

		for (j = 0; j < core_dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[j];
			struct pipe_ctx *cur_pipe_ctx;
			bool is_new_pipe_surface = true;

			if (pipe_ctx->surface != surface)
				continue;

			if (srf_updates[i].flip_addr)
				core_dc->hwss.update_plane_addr(core_dc, pipe_ctx);

			if (update_type == UPDATE_TYPE_FAST)
				continue;

			cur_pipe_ctx = &core_dc->current_context->res_ctx.pipe_ctx[j];
			if (cur_pipe_ctx->surface == pipe_ctx->surface)
				is_new_pipe_surface = false;

			if (is_new_pipe_surface ||
					srf_updates[i].in_transfer_func)
				core_dc->hwss.set_input_transfer_func(
						pipe_ctx, pipe_ctx->surface);

			if (is_new_pipe_surface ||
				(stream_update != NULL &&
					stream_update->out_transfer_func !=
							NULL)) {
				core_dc->hwss.set_output_transfer_func(
						pipe_ctx, pipe_ctx->stream);
			}

			if (srf_updates[i].hdr_static_metadata) {
				resource_build_info_frame(pipe_ctx);
				core_dc->hwss.update_info_frame(pipe_ctx);
			}
		}
	}

	/* Unlock pipes */
	for (i = core_dc->res_pool->pipe_count - 1; i >= 0; i--) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		for (j = 0; j < surface_count; j++) {
			if (srf_updates[j].surface == &pipe_ctx->surface->public) {
				if (!pipe_ctx->tg->funcs->is_blanked(pipe_ctx->tg)) {
					core_dc->hwss.pipe_control_lock(
							core_dc,
							pipe_ctx,
							false);
				}
				break;
			}
		}
	}

	if (core_dc->current_context != context) {
		dc_resource_validate_ctx_destruct(core_dc->current_context);
		dm_free(core_dc->current_context);

		core_dc->current_context = context;
	}
	return;

fail:
	dc_resource_validate_ctx_destruct(context);
	dm_free(context);
}

uint8_t dc_get_current_stream_count(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->current_context->stream_count;
}

struct dc_stream *dc_get_stream_at_index(const struct dc *dc, uint8_t i)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	if (i < core_dc->current_context->stream_count)
		return &(core_dc->current_context->streams[i]->public);
	return NULL;
}

const struct dc_link *dc_get_link_at_index(const struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return &core_dc->links[link_index]->public;
}

const struct graphics_object_id dc_get_link_id_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->link_id;
}

enum dc_irq_source dc_get_hpd_irq_source_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->public.irq_source_hpd;
}

const struct audio **dc_get_audios(struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return (const struct audio **)core_dc->res_pool->audios;
}

void dc_flip_surface_addrs(
		struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i, j;

	for (i = 0; i < count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(surfaces[i]);

		surface->public.address = flip_addrs[i].address;
		surface->public.flip_immediate = flip_addrs[i].flip_immediate;

		for (j = 0; j < core_dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &core_dc->current_context->res_ctx.pipe_ctx[j];

			if (pipe_ctx->surface != surface)
				continue;

			core_dc->hwss.update_plane_addr(core_dc, pipe_ctx);
		}
	}
}

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return dal_irq_service_to_irq_source(core_dc->res_pool->irqs, src_id, ext_id);
}

void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_set(core_dc->res_pool->irqs, src, enable);
}

void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_ack(core_dc->res_pool->irqs, src);
}

void dc_set_power_state(
	struct dc *dc,
	enum dc_acpi_cm_power_state power_state)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		core_dc->hwss.init_hw(core_dc);
		break;
	default:

		core_dc->hwss.power_down(core_dc);

		/* Zero out the current context so that on resume we start with
		 * clean state, and dc hw programming optimizations will not
		 * cause any trouble.
		 */
		memset(core_dc->current_context, 0,
				sizeof(*core_dc->current_context));

		break;
	}

}

void dc_resume(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	uint32_t i;

	for (i = 0; i < core_dc->link_count; i++)
		core_link_resume(core_dc->links[i]);
}

bool dc_read_aux_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	enum ddc_result r = dal_ddc_service_read_dpcd_data(
			link->public.ddc,
			false,
			I2C_MOT_UNDEF,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_write_aux_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		const uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_link *link = core_dc->links[link_index];

	enum ddc_result r = dal_ddc_service_write_dpcd_data(
			link->public.ddc,
			false,
			I2C_MOT_UNDEF,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_read_aux_i2c(
		struct dc *dc,
		uint32_t link_index,
		enum i2c_mot_mode mot,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

		struct core_link *link = core_dc->links[link_index];
		enum ddc_result r = dal_ddc_service_read_dpcd_data(
			link->public.ddc,
			true,
			mot,
			address,
			data,
			size);
		return r == DDC_RESULT_SUCESSFULL;
}

bool dc_write_aux_i2c(
		struct dc *dc,
		uint32_t link_index,
		enum i2c_mot_mode mot,
		uint32_t address,
		const uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_link *link = core_dc->links[link_index];

	enum ddc_result r = dal_ddc_service_write_dpcd_data(
			link->public.ddc,
			true,
			mot,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_query_ddc_data(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *write_buf,
		uint32_t write_size,
		uint8_t *read_buf,
		uint32_t read_size) {

	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];

	bool result = dal_ddc_service_query_ddc_data(
			link->public.ddc,
			address,
			write_buf,
			write_size,
			read_buf,
			read_size);

	return result;
}

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	struct ddc_service *ddc = link->public.ddc;

	return dal_i2caux_submit_i2c_command(
		ddc->ctx->i2caux,
		ddc->ddc_pin,
		cmd);
}

static bool link_add_remote_sink_helper(struct core_link *core_link, struct dc_sink *sink)
{
	struct dc_link *dc_link = &core_link->public;

	if (dc_link->sink_count >= MAX_SINKS_PER_LINK) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	dc_sink_retain(sink);

	dc_link->remote_sinks[dc_link->sink_count] = sink;
	dc_link->sink_count++;

	return true;
}

struct dc_sink *dc_link_add_remote_sink(
		const struct dc_link *link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data)
{
	struct dc_sink *dc_sink;
	enum dc_edid_status edid_status;
	struct core_link *core_link = DC_LINK_TO_LINK(link);

	if (len > MAX_EDID_BUFFER_SIZE) {
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
			core_link,
			dc_sink))
		goto fail_add_sink;

	edid_status = dm_helpers_parse_edid_caps(
			core_link->ctx,
			&dc_sink->dc_edid,
			&dc_sink->edid_caps);

	if (edid_status != EDID_OK)
		goto fail;

	return dc_sink;
fail:
	dc_link_remove_remote_sink(link, dc_sink);
fail_add_sink:
	dc_sink_release(dc_sink);
	return NULL;
}

void dc_link_set_sink(const struct dc_link *link, struct dc_sink *sink)
{
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	dc_link->local_sink = sink;

	if (sink == NULL) {
		dc_link->type = dc_connection_none;
	} else {
		dc_link->type = dc_connection_single;
	}
}

void dc_link_remove_remote_sink(const struct dc_link *link, const struct dc_sink *sink)
{
	int i;
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	if (!link->sink_count) {
		BREAK_TO_DEBUGGER();
		return;
	}

	for (i = 0; i < dc_link->sink_count; i++) {
		if (dc_link->remote_sinks[i] == sink) {
			dc_sink_release(sink);
			dc_link->remote_sinks[i] = NULL;

			/* shrink array to remove empty place */
			while (i < dc_link->sink_count - 1) {
				dc_link->remote_sinks[i] = dc_link->remote_sinks[i+1];
				i++;
			}
			dc_link->remote_sinks[i] = NULL;
			dc_link->sink_count--;
			return;
		}
	}
}

bool dc_init_dchub(struct dc *dc, struct dchub_init_data *dh_data)
{
	int i;
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct mem_input *mi = NULL;

	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		if (core_dc->res_pool->mis[i] != NULL) {
			mi = core_dc->res_pool->mis[i];
			break;
		}
	}
	if (mi == NULL) {
		dm_error("no mem_input!\n");
		return false;
	}

	if (mi->funcs->mem_input_update_dchub)
		mi->funcs->mem_input_update_dchub(mi, dh_data);
	else
		ASSERT(mi->funcs->mem_input_update_dchub);


	return true;

}


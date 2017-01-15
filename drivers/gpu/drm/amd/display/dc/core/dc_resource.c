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

#include "resource.h"
#include "include/irq_service_interface.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "opp.h"
#include "timing_generator.h"
#include "transform.h"
#include "set_mode_types.h"
#include "virtual/virtual_stream_encoder.h"

#include "dce80/dce80_resource.h"
#include "dce100/dce100_resource.h"
#include "dce110/dce110_resource.h"
#include "dce112/dce112_resource.h"

enum dce_version resource_parse_asic_id(struct hw_asic_id asic_id)
{
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;
	switch (asic_id.chip_family) {

	case FAMILY_CI:
	case FAMILY_KV:
		dc_version = DCE_VERSION_8_0;
		break;
	case FAMILY_CZ:
		dc_version = DCE_VERSION_11_0;
		break;

	case FAMILY_VI:
		if (ASIC_REV_IS_TONGA_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(asic_id.hw_internal_rev)) {
			dc_version = DCE_VERSION_10_0;
			break;
		}
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev)) {
			dc_version = DCE_VERSION_11_2;
		}
		break;
	default:
		dc_version = DCE_VERSION_UNKNOWN;
		break;
	}
	return dc_version;
}

struct resource_pool *dc_create_resource_pool(
				struct core_dc *dc,
				int num_virtual_links,
				enum dce_version dc_version,
				struct hw_asic_id asic_id)
{

	switch (dc_version) {
	case DCE_VERSION_8_0:
		return dce80_create_resource_pool(
			num_virtual_links, dc);
	case DCE_VERSION_10_0:
		return dce100_create_resource_pool(
				num_virtual_links, dc);
	case DCE_VERSION_11_0:
		return dce110_create_resource_pool(
			num_virtual_links, dc, asic_id);
	case DCE_VERSION_11_2:
		return dce112_create_resource_pool(
			num_virtual_links, dc);
	default:
		break;
	}

	return false;
}

void dc_destroy_resource_pool(struct core_dc *dc)
{
	if (dc) {
		if (dc->res_pool)
			dc->res_pool->funcs->destroy(&dc->res_pool);

		if (dc->hwseq)
			dm_free(dc->hwseq);
	}
}

static void update_num_audio(
	const struct resource_straps *straps,
	unsigned int *num_audio,
	struct audio_support *aud_support)
{
	if (straps->hdmi_disable == 0) {
		aud_support->hdmi_audio_native = true;
		aud_support->hdmi_audio_on_dongle = true;
		aud_support->dp_audio = true;
	} else {
		if (straps->dc_pinstraps_audio & 0x2) {
			aud_support->hdmi_audio_on_dongle = true;
			aud_support->dp_audio = true;
		} else {
			aud_support->dp_audio = true;
		}
	}

	switch (straps->audio_stream_number) {
	case 0: /* multi streams supported */
		break;
	case 1: /* multi streams not supported */
		*num_audio = 1;
		break;
	default:
		DC_ERR("DC: unexpected audio fuse!\n");
	};
}

bool resource_construct(
	unsigned int num_virtual_links,
	struct core_dc *dc,
	struct resource_pool *pool,
	const struct resource_create_funcs *create_funcs)
{
	struct dc_context *ctx = dc->ctx;
	const struct resource_caps *caps = pool->res_cap;
	int i;
	unsigned int num_audio = caps->num_audio;
	struct resource_straps straps = {0};

	if (create_funcs->read_dce_straps)
		create_funcs->read_dce_straps(dc->ctx, &straps);

	pool->audio_count = 0;
	if (create_funcs->create_audio) {
		/* find the total number of streams available via the
		 * AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT
		 * registers (one for each pin) starting from pin 1
		 * up to the max number of audio pins.
		 * We stop on the first pin where
		 * PORT_CONNECTIVITY == 1 (as instructed by HW team).
		 */
		update_num_audio(&straps, &num_audio, &pool->audio_support);
		for (i = 0; i < pool->pipe_count && i < num_audio; i++) {
			struct audio *aud = create_funcs->create_audio(ctx, i);

			if (aud == NULL) {
				DC_ERR("DC: failed to create audio!\n");
				return false;
			}

			if (!aud->funcs->endpoint_valid(aud)) {
				aud->funcs->destroy(&aud);
				break;
			}

			pool->audios[i] = aud;
			pool->audio_count++;
		}
	}

	pool->stream_enc_count = 0;
	if (create_funcs->create_stream_encoder) {
		for (i = 0; i < caps->num_stream_encoder; i++) {
			pool->stream_enc[i] = create_funcs->create_stream_encoder(i, ctx);
			if (pool->stream_enc[i] == NULL)
				DC_ERR("DC: failed to create stream_encoder!\n");
			pool->stream_enc_count++;
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		pool->stream_enc[pool->stream_enc_count] =
			virtual_stream_encoder_create(
					ctx, ctx->dc_bios);
		if (pool->stream_enc[pool->stream_enc_count] == NULL) {
			DC_ERR("DC: failed to create stream_encoder!\n");
			return false;
		}
		pool->stream_enc_count++;
	}

	dc->hwseq = create_funcs->create_hwseq(ctx);

	return true;
}


void resource_unreference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source **clock_source)
{
	int i;
	for (i = 0; i < res_ctx->pool->clk_src_count; i++) {
		if (res_ctx->pool->clock_sources[i] != *clock_source)
			continue;

		res_ctx->clock_source_ref_count[i]--;

		if (res_ctx->clock_source_ref_count[i] == 0)
			(*clock_source)->funcs->cs_power_down(*clock_source);

		break;
	}

	if (res_ctx->pool->dp_clock_source == *clock_source) {
		res_ctx->dp_clock_source_ref_count--;

		if (res_ctx->dp_clock_source_ref_count == 0)
			(*clock_source)->funcs->cs_power_down(*clock_source);
	}
	*clock_source = NULL;
}

void resource_reference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source)
{
	int i;
	for (i = 0; i < res_ctx->pool->clk_src_count; i++) {
		if (res_ctx->pool->clock_sources[i] != clock_source)
			continue;

		res_ctx->clock_source_ref_count[i]++;
		break;
	}

	if (res_ctx->pool->dp_clock_source == clock_source)
		res_ctx->dp_clock_source_ref_count++;
}

bool resource_are_streams_timing_synchronizable(
	const struct core_stream *stream1,
	const struct core_stream *stream2)
{
	if (stream1->public.timing.h_total != stream2->public.timing.h_total)
		return false;

	if (stream1->public.timing.v_total != stream2->public.timing.v_total)
		return false;

	if (stream1->public.timing.h_addressable
				!= stream2->public.timing.h_addressable)
		return false;

	if (stream1->public.timing.v_addressable
				!= stream2->public.timing.v_addressable)
		return false;

	if (stream1->public.timing.pix_clk_khz
				!= stream2->public.timing.pix_clk_khz)
		return false;

	if (stream1->phy_pix_clk != stream2->phy_pix_clk
			&& !dc_is_dp_signal(stream1->signal)
			&& !dc_is_dp_signal(stream2->signal))
		return false;

	return true;
}

static bool is_sharable_clk_src(
	const struct pipe_ctx *pipe_with_clk_src,
	const struct pipe_ctx *pipe)
{
	if (pipe_with_clk_src->clock_source == NULL)
		return false;

	if (pipe_with_clk_src->stream->signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	if (dc_is_dp_signal(pipe_with_clk_src->stream->signal))
		return false;

	if (dc_is_hdmi_signal(pipe_with_clk_src->stream->signal)
			&& dc_is_dvi_signal(pipe->stream->signal))
		return false;

	if (dc_is_hdmi_signal(pipe->stream->signal)
			&& dc_is_dvi_signal(pipe_with_clk_src->stream->signal))
		return false;

	if (!resource_are_streams_timing_synchronizable(
			pipe_with_clk_src->stream, pipe->stream))
		return false;

	return true;
}

struct clock_source *resource_find_used_clk_src_for_sharing(
					struct resource_context *res_ctx,
					struct pipe_ctx *pipe_ctx)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (is_sharable_clk_src(&res_ctx->pipe_ctx[i], pipe_ctx))
			return res_ctx->pipe_ctx[i].clock_source;
	}

	return NULL;
}

static enum pixel_format convert_pixel_format_to_dalsurface(
		enum surface_pixel_format surface_pixel_format)
{
	enum pixel_format dal_pixel_format = PIXEL_FORMAT_UNKNOWN;

	switch (surface_pixel_format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		dal_pixel_format = PIXEL_FORMAT_INDEX8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010_XRBIAS;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

static void rect_swap_helper(struct rect *rect)
{
	uint32_t temp = 0;

	temp = rect->height;
	rect->height = rect->width;
	rect->width = temp;

	temp = rect->x;
	rect->x = rect->y;
	rect->y = temp;
}

static void calculate_viewport(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	struct rect stream_src = pipe_ctx->stream->public.src;
	struct rect src = surface->src_rect;
	struct rect dst = surface->dst_rect;
	struct rect surface_clip = surface->clip_rect;
	struct rect clip = {0};


	if (surface->rotation == ROTATION_ANGLE_90 ||
	    surface->rotation == ROTATION_ANGLE_270) {
		rect_swap_helper(&src);
		rect_swap_helper(&dst);
		rect_swap_helper(&surface_clip);
		rect_swap_helper(&stream_src);
	}

	/* The actual clip is an intersection between stream
	 * source and surface clip
	 */
	clip.x = stream_src.x > surface_clip.x ?
			stream_src.x : surface_clip.x;

	clip.width = stream_src.x + stream_src.width <
			surface_clip.x + surface_clip.width ?
			stream_src.x + stream_src.width - clip.x :
			surface_clip.x + surface_clip.width - clip.x ;

	clip.y = stream_src.y > surface_clip.y ?
			stream_src.y : surface_clip.y;

	clip.height = stream_src.y + stream_src.height <
			surface_clip.y + surface_clip.height ?
			stream_src.y + stream_src.height - clip.y :
			surface_clip.y + surface_clip.height - clip.y ;

	/* offset = src.ofs + (clip.ofs - dst.ofs) * scl_ratio
	 * num_pixels = clip.num_pix * scl_ratio
	 */
	pipe_ctx->scl_data.viewport.x = src.x + (clip.x - dst.x) *
			src.width / dst.width;
	pipe_ctx->scl_data.viewport.width = clip.width *
			src.width / dst.width;

	pipe_ctx->scl_data.viewport.y = src.y + (clip.y - dst.y) *
			src.height / dst.height;
	pipe_ctx->scl_data.viewport.height = clip.height *
			src.height / dst.height;

	/* Minimum viewport such that 420/422 chroma vp is non 0 */
	if (pipe_ctx->scl_data.viewport.width < 2)
		pipe_ctx->scl_data.viewport.width = 2;
	if (pipe_ctx->scl_data.viewport.height < 2)
		pipe_ctx->scl_data.viewport.height = 2;
}

static void calculate_recout(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct rect clip = surface->clip_rect;

	pipe_ctx->scl_data.recout.x = stream->public.dst.x;
	if (stream->public.src.x < clip.x)
		pipe_ctx->scl_data.recout.x += (clip.x
			- stream->public.src.x) * stream->public.dst.width
						/ stream->public.src.width;

	pipe_ctx->scl_data.recout.width = clip.width *
			stream->public.dst.width / stream->public.src.width;
	if (pipe_ctx->scl_data.recout.width + pipe_ctx->scl_data.recout.x >
			stream->public.dst.x + stream->public.dst.width)
		pipe_ctx->scl_data.recout.width =
			stream->public.dst.x + stream->public.dst.width
						- pipe_ctx->scl_data.recout.x;

	pipe_ctx->scl_data.recout.y = stream->public.dst.y;
	if (stream->public.src.y < clip.y)
		pipe_ctx->scl_data.recout.y += (clip.y
			- stream->public.src.y) * stream->public.dst.height
						/ stream->public.src.height;

	pipe_ctx->scl_data.recout.height = clip.height *
			stream->public.dst.height / stream->public.src.height;
	if (pipe_ctx->scl_data.recout.height + pipe_ctx->scl_data.recout.y >
			stream->public.dst.y + stream->public.dst.height)
		pipe_ctx->scl_data.recout.height =
			stream->public.dst.y + stream->public.dst.height
						- pipe_ctx->scl_data.recout.y;
}

static void calculate_scaling_ratios(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	const uint32_t in_w = stream->public.src.width;
	const uint32_t in_h = stream->public.src.height;
	const uint32_t out_w = stream->public.dst.width;
	const uint32_t out_h = stream->public.dst.height;

	pipe_ctx->scl_data.ratios.horz = dal_fixed31_32_from_fraction(
					surface->src_rect.width,
					surface->dst_rect.width);
	pipe_ctx->scl_data.ratios.vert = dal_fixed31_32_from_fraction(
					surface->src_rect.height,
					surface->dst_rect.height);

	if (surface->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE)
		pipe_ctx->scl_data.ratios.horz.value *= 2;
	else if (surface->stereo_format == PLANE_STEREO_FORMAT_TOP_AND_BOTTOM)
		pipe_ctx->scl_data.ratios.vert.value *= 2;

	pipe_ctx->scl_data.ratios.vert.value = div64_s64(
		pipe_ctx->scl_data.ratios.vert.value * in_h, out_h);
	pipe_ctx->scl_data.ratios.horz.value = div64_s64(
		pipe_ctx->scl_data.ratios.horz.value * in_w, out_w);

	pipe_ctx->scl_data.ratios.horz_c = pipe_ctx->scl_data.ratios.horz;
	pipe_ctx->scl_data.ratios.vert_c = pipe_ctx->scl_data.ratios.vert;

	if (pipe_ctx->scl_data.format == PIXEL_FORMAT_420BPP12) {
		pipe_ctx->scl_data.ratios.horz_c.value /= 2;
		pipe_ctx->scl_data.ratios.vert_c.value /= 2;
	}
}

bool resource_build_scaling_params(
	const struct dc_surface *surface,
	struct pipe_ctx *pipe_ctx)
{
	bool res;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->public.timing;
	/* Important: scaling ratio calculation requires pixel format,
	 * lb depth calculation requires recout and taps require scaling ratios.
	 */
	pipe_ctx->scl_data.format = convert_pixel_format_to_dalsurface(surface->format);

	calculate_viewport(surface, pipe_ctx);

	if (pipe_ctx->scl_data.viewport.height < 16 || pipe_ctx->scl_data.viewport.width < 16)
		return false;

	calculate_scaling_ratios(surface, pipe_ctx);

	calculate_recout(surface, pipe_ctx);

	/**
	 * Setting line buffer pixel depth to 24bpp yields banding
	 * on certain displays, such as the Sharp 4k
	 */
	pipe_ctx->scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;

	pipe_ctx->scl_data.h_active = timing->h_addressable;
	pipe_ctx->scl_data.v_active = timing->v_addressable;

	/* Taps calculations */
	res = pipe_ctx->xfm->funcs->transform_get_optimal_number_of_taps(
		pipe_ctx->xfm, &pipe_ctx->scl_data, &surface->scaling_quality);

	if (!res) {
		/* Try 24 bpp linebuffer */
		pipe_ctx->scl_data.lb_params.depth = LB_PIXEL_DEPTH_24BPP;

		res = pipe_ctx->xfm->funcs->transform_get_optimal_number_of_taps(
			pipe_ctx->xfm, &pipe_ctx->scl_data, &surface->scaling_quality);
	}

	dm_logger_write(pipe_ctx->stream->ctx->logger, LOG_SCALER,
				"%s: Viewport:\nheight:%d width:%d x:%d "
				"y:%d\n dst_rect:\nheight:%d width:%d x:%d "
				"y:%d\n",
				__func__,
				pipe_ctx->scl_data.viewport.height,
				pipe_ctx->scl_data.viewport.width,
				pipe_ctx->scl_data.viewport.x,
				pipe_ctx->scl_data.viewport.y,
				surface->dst_rect.height,
				surface->dst_rect.width,
				surface->dst_rect.x,
				surface->dst_rect.y);

	return res;
}


enum dc_status resource_build_scaling_params_for_context(
	const struct core_dc *dc,
	struct validate_context *context)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context->res_ctx.pipe_ctx[i].surface != NULL &&
				context->res_ctx.pipe_ctx[i].stream != NULL)
			if (!resource_build_scaling_params(
				&context->res_ctx.pipe_ctx[i].surface->public,
				&context->res_ctx.pipe_ctx[i]))
				return DC_FAIL_SCALING;
	}

	return DC_OK;
}

static void detach_surfaces_for_stream(
		struct validate_context *context,
		const struct dc_stream *dc_stream)
{
	int i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	for (i = 0; i < context->res_ctx.pool->pipe_count; i++) {
		struct pipe_ctx *cur_pipe = &context->res_ctx.pipe_ctx[i];
		if (cur_pipe->stream == stream) {
			cur_pipe->surface = NULL;
			cur_pipe->top_pipe = NULL;
			cur_pipe->bottom_pipe = NULL;
		}
	}
}

struct pipe_ctx *find_idle_secondary_pipe(struct resource_context *res_ctx)
{
	int i;
	struct pipe_ctx *secondary_pipe = NULL;

	/*
	 * search backwards for the second pipe to keep pipe
	 * assignment more consistent
	 */

	for (i = res_ctx->pool->pipe_count - 1; i >= 0; i--) {
		if (res_ctx->pipe_ctx[i].stream == NULL) {
			secondary_pipe = &res_ctx->pipe_ctx[i];
			secondary_pipe->pipe_idx = i;
			break;
		}
	}


	return secondary_pipe;
}

struct pipe_ctx *resource_get_head_pipe_for_stream(
		struct resource_context *res_ctx,
		const struct core_stream *stream)
{
	int i;
	for (i = 0; i < res_ctx->pool->pipe_count; i++) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				!res_ctx->pipe_ctx[i].top_pipe) {
			return &res_ctx->pipe_ctx[i];
			break;
		}
	}
	return NULL;
}

/*
 * A free_pipe for a stream is defined here as a pipe
 * that has no surface attached yet
 */
static struct pipe_ctx *acquire_free_pipe_for_stream(
		struct resource_context *res_ctx,
		const struct dc_stream *dc_stream)
{
	int i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	struct pipe_ctx *head_pipe = NULL;

	/* Find head pipe, which has the back end set up*/

	head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);

	if (!head_pipe)
		ASSERT(0);

	if (!head_pipe->surface)
		return head_pipe;

	/* Re-use pipe already acquired for this stream if available*/
	for (i = res_ctx->pool->pipe_count - 1; i >= 0; i--) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				!res_ctx->pipe_ctx[i].surface) {
			return &res_ctx->pipe_ctx[i];
		}
	}

	/*
	 * At this point we have no re-useable pipe for this stream and we need
	 * to acquire an idle one to satisfy the request
	 */

	if(!res_ctx->pool->funcs->acquire_idle_pipe_for_layer)
		return NULL;

	return res_ctx->pool->funcs->acquire_idle_pipe_for_layer(res_ctx, stream);

}

static void release_free_pipes_for_stream(
		struct resource_context *res_ctx,
		const struct dc_stream *dc_stream)
{
	int i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	for (i = res_ctx->pool->pipe_count - 1; i >= 0; i--) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				!res_ctx->pipe_ctx[i].surface) {
			res_ctx->pipe_ctx[i].stream = NULL;
		}
	}
}

bool resource_attach_surfaces_to_context(
		const struct dc_surface * const *surfaces,
		int surface_count,
		const struct dc_stream *dc_stream,
		struct validate_context *context)
{
	int i;
	struct pipe_ctx *tail_pipe;
	struct dc_stream_status *stream_status = NULL;


	if (surface_count > MAX_SURFACE_NUM) {
		dm_error("Surface: can not attach %d surfaces! Maximum is: %d\n",
			surface_count, MAX_SURFACE_NUM);
		return false;
	}

	for (i = 0; i < context->stream_count; i++)
		if (&context->streams[i]->public == dc_stream) {
			stream_status = &context->stream_status[i];
			break;
		}
	if (stream_status == NULL) {
		dm_error("Existing stream not found; failed to attach surfaces\n");
		return false;
	}

	/* retain new surfaces */
	for (i = 0; i < surface_count; i++)
		dc_surface_retain(surfaces[i]);

	detach_surfaces_for_stream(context, dc_stream);

	/* release existing surfaces*/
	for (i = 0; i < stream_status->surface_count; i++)
		dc_surface_release(stream_status->surfaces[i]);

	for (i = surface_count; i < stream_status->surface_count; i++)
		stream_status->surfaces[i] = NULL;

	stream_status->surface_count = 0;

	if (surface_count == 0)
		return true;

	tail_pipe = NULL;
	for (i = 0; i < surface_count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(surfaces[i]);
		struct pipe_ctx *free_pipe = acquire_free_pipe_for_stream(
				&context->res_ctx, dc_stream);

		if (!free_pipe) {
			stream_status->surfaces[i] = NULL;
			return false;
		}

		free_pipe->surface = surface;

		if (tail_pipe) {
			free_pipe->top_pipe = tail_pipe;
			tail_pipe->bottom_pipe = free_pipe;
		}

		tail_pipe = free_pipe;
	}

	release_free_pipes_for_stream(&context->res_ctx, dc_stream);

	/* assign new surfaces*/
	for (i = 0; i < surface_count; i++)
		stream_status->surfaces[i] = surfaces[i];

	stream_status->surface_count = surface_count;

	return true;
}


static bool is_timing_changed(const struct core_stream *cur_stream,
		const struct core_stream *new_stream)
{
	if (cur_stream == NULL)
		return true;

	/* If sink pointer changed, it means this is a hotplug, we should do
	 * full hw setting.
	 */
	if (cur_stream->sink != new_stream->sink)
		return true;

	/* If output color space is changed, need to reprogram info frames */
	if (cur_stream->public.output_color_space !=
			new_stream->public.output_color_space)
		return true;

	return memcmp(
		&cur_stream->public.timing,
		&new_stream->public.timing,
		sizeof(struct dc_crtc_timing)) != 0;
}

static bool are_stream_backends_same(
	const struct core_stream *stream_a, const struct core_stream *stream_b)
{
	if (stream_a == stream_b)
		return true;

	if (stream_a == NULL || stream_b == NULL)
		return false;

	if (is_timing_changed(stream_a, stream_b))
		return false;

	return true;
}

bool is_stream_unchanged(
	const struct core_stream *old_stream, const struct core_stream *stream)
{
	if (old_stream == stream)
		return true;

	if (!are_stream_backends_same(old_stream, stream))
		return false;

	return true;
}

bool resource_validate_attach_surfaces(
		const struct dc_validation_set set[],
		int set_count,
		const struct validate_context *old_context,
		struct validate_context *context)
{
	int i, j;

	for (i = 0; i < set_count; i++) {
		for (j = 0; j < old_context->stream_count; j++)
			if (is_stream_unchanged(
					old_context->streams[j],
					context->streams[i])) {
				if (!resource_attach_surfaces_to_context(
						old_context->stream_status[j].surfaces,
						old_context->stream_status[j].surface_count,
						&context->streams[i]->public,
						context))
					return false;
				context->stream_status[i] = old_context->stream_status[j];
			}
		if (set[i].surface_count != 0)
			if (!resource_attach_surfaces_to_context(
					set[i].surfaces,
					set[i].surface_count,
					&context->streams[i]->public,
					context))
				return false;

	}

	return true;
}

/* Maximum TMDS single link pixel clock 165MHz */
#define TMDS_MAX_PIXEL_CLOCK_IN_KHZ 165000

static void set_stream_engine_in_use(
		struct resource_context *res_ctx,
		struct stream_encoder *stream_enc)
{
	int i;

	for (i = 0; i < res_ctx->pool->stream_enc_count; i++) {
		if (res_ctx->pool->stream_enc[i] == stream_enc)
			res_ctx->is_stream_enc_acquired[i] = true;
	}
}

/* TODO: release audio object */
static void set_audio_in_use(
		struct resource_context *res_ctx,
		struct audio *audio)
{
	int i;
	for (i = 0; i < res_ctx->pool->audio_count; i++) {
		if (res_ctx->pool->audios[i] == audio) {
			res_ctx->is_audio_acquired[i] = true;
		}
	}
}

static int acquire_first_free_pipe(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	int i;

	for (i = 0; i < res_ctx->pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream) {
			struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

			pipe_ctx->tg = res_ctx->pool->timing_generators[i];
			pipe_ctx->mi = res_ctx->pool->mis[i];
			pipe_ctx->ipp = res_ctx->pool->ipps[i];
			pipe_ctx->xfm = res_ctx->pool->transforms[i];
			pipe_ctx->opp = res_ctx->pool->opps[i];
			pipe_ctx->dis_clk = res_ctx->pool->display_clock;
			pipe_ctx->pipe_idx = i;

			pipe_ctx->stream = stream;
			return i;
		}
	}
	return -1;
}

static struct stream_encoder *find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	int i;
	int j = -1;
	struct core_link *link = stream->sink->link;

	for (i = 0; i < res_ctx->pool->stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
					res_ctx->pool->stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case */
			j = i;
			if (res_ctx->pool->stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return res_ctx->pool->stream_enc[i];
		}
	}

	/*
	 * below can happen in cases when stream encoder is acquired:
	 * 1) for second MST display in chain, so preferred engine already
	 * acquired;
	 * 2) for another link, which preferred engine already acquired by any
	 * MST configuration.
	 *
	 * If signal is of DP type and preferred engine not found, return last available
	 *
	 * TODO - This is just a patch up and a generic solution is
	 * required for non DP connectors.
	 */

	if (j >= 0 && dc_is_dp_signal(stream->signal))
		return res_ctx->pool->stream_enc[j];

	return NULL;
}

static struct audio *find_first_free_audio(struct resource_context *res_ctx)
{
	int i;
	for (i = 0; i < res_ctx->pool->audio_count; i++) {
		if (res_ctx->is_audio_acquired[i] == false) {
			return res_ctx->pool->audios[i];
		}
	}

	return 0;
}

static void update_stream_signal(struct core_stream *stream)
{
	const struct dc_sink *dc_sink = stream->public.sink;

	if (dc_sink->sink_signal == SIGNAL_TYPE_NONE)
		stream->signal = stream->sink->link->public.connector_signal;
	else if (dc_sink->sink_signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
			dc_sink->sink_signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		/* For asic supports dual link DVI, we should adjust signal type
		 * based on timing pixel clock. If pixel clock more than 165Mhz,
		 * signal is dual link, otherwise, single link.
		 */
		if (stream->public.timing.pix_clk_khz > TMDS_MAX_PIXEL_CLOCK_IN_KHZ)
			stream->signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		else
			stream->signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	else
		stream->signal = dc_sink->sink_signal;
}

bool resource_is_stream_unchanged(
	const struct validate_context *old_context, const struct core_stream *stream)
{
	int i;

	for (i = 0; i < old_context->stream_count; i++) {
		const struct core_stream *old_stream = old_context->streams[i];

		if (are_stream_backends_same(old_stream, stream))
				return true;
	}

	return false;
}

static void copy_pipe_ctx(
	const struct pipe_ctx *from_pipe_ctx, struct pipe_ctx *to_pipe_ctx)
{
	struct core_surface *surface = to_pipe_ctx->surface;
	struct core_stream *stream = to_pipe_ctx->stream;

	*to_pipe_ctx = *from_pipe_ctx;
	to_pipe_ctx->stream = stream;
	if (surface != NULL)
		to_pipe_ctx->surface = surface;
}

static struct core_stream *find_pll_sharable_stream(
		const struct core_stream *stream_needs_pll,
		struct validate_context *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream_has_pll = context->streams[i];

		/* We are looking for non dp, non virtual stream */
		if (resource_are_streams_timing_synchronizable(
			stream_needs_pll, stream_has_pll)
			&& !dc_is_dp_signal(stream_has_pll->signal)
			&& stream_has_pll->sink->link->public.connector_signal
			!= SIGNAL_TYPE_VIRTUAL)
			return stream_has_pll;

	}

	return NULL;
}

static int get_norm_pix_clk(const struct dc_crtc_timing *timing)
{
	uint32_t pix_clk = timing->pix_clk_khz;
	uint32_t normalized_pix_clk = pix_clk;

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pix_clk /= 2;

	switch (timing->display_color_depth) {
	case COLOR_DEPTH_888:
		normalized_pix_clk = pix_clk;
		break;
	case COLOR_DEPTH_101010:
		normalized_pix_clk = (pix_clk * 30) / 24;
		break;
	case COLOR_DEPTH_121212:
		normalized_pix_clk = (pix_clk * 36) / 24;
		break;
	case COLOR_DEPTH_161616:
		normalized_pix_clk = (pix_clk * 48) / 24;
		break;
	default:
		ASSERT(0);
		break;
	}

	return normalized_pix_clk;
}

static void calculate_phy_pix_clks(
		const struct core_dc *dc,
		struct validate_context *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream = context->streams[i];

		update_stream_signal(stream);

		/* update actual pixel clock on all streams */
		if (dc_is_hdmi_signal(stream->signal))
			stream->phy_pix_clk = get_norm_pix_clk(
				&stream->public.timing);
		else
			stream->phy_pix_clk =
				stream->public.timing.pix_clk_khz;
	}
}

enum dc_status resource_map_pool_resources(
		const struct core_dc *dc,
		struct validate_context *context)
{
	int i, j;

	calculate_phy_pix_clks(dc, context);

	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream = context->streams[i];

		if (!resource_is_stream_unchanged(dc->current_context, stream))
			continue;

		/* mark resources used for stream that is already active */
		for (j = 0; j < MAX_PIPES; j++) {
			struct pipe_ctx *pipe_ctx =
				&context->res_ctx.pipe_ctx[j];
			const struct pipe_ctx *old_pipe_ctx =
				&dc->current_context->res_ctx.pipe_ctx[j];

			if (!are_stream_backends_same(old_pipe_ctx->stream, stream))
				continue;

			pipe_ctx->stream = stream;
			copy_pipe_ctx(old_pipe_ctx, pipe_ctx);

			/* Split pipe resource, do not acquire back end */
			if (!pipe_ctx->stream_enc)
				continue;

			set_stream_engine_in_use(
				&context->res_ctx,
				pipe_ctx->stream_enc);

			/* Switch to dp clock source only if there is
			 * no non dp stream that shares the same timing
			 * with the dp stream.
			 */
			if (dc_is_dp_signal(pipe_ctx->stream->signal) &&
				!find_pll_sharable_stream(stream, context))
				pipe_ctx->clock_source =
					context->res_ctx.pool->dp_clock_source;

			resource_reference_clock_source(
				&context->res_ctx,
				pipe_ctx->clock_source);

			set_audio_in_use(&context->res_ctx,
					 pipe_ctx->audio);
		}
	}

	for (i = 0; i < context->stream_count; i++) {
		struct core_stream *stream = context->streams[i];
		struct pipe_ctx *pipe_ctx = NULL;
		int pipe_idx = -1;

		if (resource_is_stream_unchanged(dc->current_context, stream))
			continue;
		/* acquire new resources */
		pipe_idx = acquire_first_free_pipe(&context->res_ctx, stream);
		if (pipe_idx < 0)
			return DC_NO_CONTROLLER_RESOURCE;


		pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];

		pipe_ctx->stream_enc =
			find_first_free_match_stream_enc_for_link(
				&context->res_ctx, stream);

		if (!pipe_ctx->stream_enc)
			return DC_NO_STREAM_ENG_RESOURCE;

		set_stream_engine_in_use(
			&context->res_ctx,
			pipe_ctx->stream_enc);

		/* TODO: Add check if ASIC support and EDID audio */
		if (!stream->sink->converter_disable_audio &&
			dc_is_audio_capable_signal(pipe_ctx->stream->signal) &&
			stream->public.audio_info.mode_count) {
			pipe_ctx->audio = find_first_free_audio(
				&context->res_ctx);

			/*
			 * Audio assigned in order first come first get.
			 * There are asics which has number of audio
			 * resources less then number of pipes
			 */
			if (pipe_ctx->audio)
				set_audio_in_use(
					&context->res_ctx,
					pipe_ctx->audio);
		}

		context->stream_status[i].primary_otg_inst = pipe_ctx->tg->inst;
	}

	return DC_OK;
}

/* first stream in the context is used to populate the rest */
void validate_guaranteed_copy_streams(
		struct validate_context *context,
		int max_streams)
{
	int i;

	for (i = 1; i < max_streams; i++) {
		context->streams[i] = context->streams[0];

		copy_pipe_ctx(&context->res_ctx.pipe_ctx[0],
			      &context->res_ctx.pipe_ctx[i]);
		context->res_ctx.pipe_ctx[i].stream =
				context->res_ctx.pipe_ctx[0].stream;

		dc_stream_retain(&context->streams[i]->public);
		context->stream_count++;
	}
}

static void patch_gamut_packet_checksum(
		struct encoder_info_packet *gamut_packet)
{
	/* For gamut we recalc checksum */
	if (gamut_packet->valid) {
		uint8_t chk_sum = 0;
		uint8_t *ptr;
		uint8_t i;

		/*start of the Gamut data. */
		ptr = &gamut_packet->sb[3];

		for (i = 0; i <= gamut_packet->sb[1]; i++)
			chk_sum += ptr[i];

		gamut_packet->sb[2] = (uint8_t) (0x100 - chk_sum);
	}
}

static void set_avi_info_frame(
		struct encoder_info_packet *info_packet,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	struct info_frame info_frame = { {0} };
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum dc_aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	bool itc = false;
	uint8_t cn0_cn1 = 0;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;

	color_space = pipe_ctx->stream->public.output_color_space;

	/* Initialize header */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.
			info_frame_type = HDMI_INFOFRAME_TYPE_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.version = 2;
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.length =
			HDMI_AVI_INFOFRAME_SIZE;

	/*
	 * IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	 * according to HDMI 2.0 spec (Section 10.1)
	 */

	switch (stream->public.timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		pixel_encoding = 1;
		break;

	case PIXEL_ENCODING_YCBCR444:
		pixel_encoding = 2;
		break;
	case PIXEL_ENCODING_YCBCR420:
		pixel_encoding = 3;
		break;

	case PIXEL_ENCODING_RGB:
	default:
		pixel_encoding = 0;
	}

	/* Y0_Y1_Y2 : The pixel encoding */
	/* H14b AVI InfoFrame has extension on Y-field from 2 bits to 3 bits */
	info_frame.avi_info_packet.info_packet_hdmi.bits.Y0_Y1_Y2 =
		pixel_encoding;

	/* A0 = 1 Active Format Information valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.A0 =
		ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.B0_B1 =
		BAR_INFO_BOTH_VALID;

	info_frame.avi_info_packet.info_packet_hdmi.bits.SC0_SC1 =
			PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */
	/* TODO: un-hardcode scan type */
	scan_type = SCANNING_TYPE_UNDERSCAN;
	info_frame.avi_info_packet.info_packet_hdmi.bits.S0_S1 = scan_type;

	/* C0, C1 : Colorimetry */
	if (color_space == COLOR_SPACE_YCBCR709)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU709;
	else if (color_space == COLOR_SPACE_YCBCR601)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU601;
	else
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_NO_DATA;

	/* TODO: un-hardcode aspect ratio */
	aspect = stream->public.timing.aspect_ratio;

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	case ASPECT_RATIO_64_27:
	case ASPECT_RATIO_256_135:
	default:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = 0;
	}

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.R0_R3 =
			ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	/* TODO: un-hardcode cn0_cn1 and itc */
	cn0_cn1 = 0;
	itc = false;

	if (itc) {
		info_frame.avi_info_packet.info_packet_hdmi.bits.ITC = 1;
		info_frame.avi_info_packet.info_packet_hdmi.bits.CN0_CN1 =
			cn0_cn1;
	}

	/* TODO : We should handle YCC quantization */
	/* but we do not have matrix calculation */
	if (color_space == COLOR_SPACE_SRGB) {
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_FULL_RANGE;
		info_frame.avi_info_packet.info_packet_hdmi.bits.YQ0_YQ1 =
						YYC_QUANTIZATION_FULL_RANGE;
	} else if (color_space == COLOR_SPACE_SRGB_LIMITED) {
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_LIMITED_RANGE;
		info_frame.avi_info_packet.info_packet_hdmi.bits.YQ0_YQ1 =
						YYC_QUANTIZATION_LIMITED_RANGE;
	} else {
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_DEFAULT_RANGE;
		info_frame.avi_info_packet.info_packet_hdmi.bits.YQ0_YQ1 =
						YYC_QUANTIZATION_LIMITED_RANGE;
	}

	info_frame.avi_info_packet.info_packet_hdmi.bits.VIC0_VIC7 =
					stream->public.timing.vic;

	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	info_frame.avi_info_packet.info_packet_hdmi.bits.PR0_PR3 = 0;

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_top =
			stream->public.timing.v_border_top;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_bottom =
		(stream->public.timing.v_border_top
			- stream->public.timing.v_border_bottom + 1);
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_left =
			stream->public.timing.h_border_left;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_right =
		(stream->public.timing.h_total
			- stream->public.timing.h_border_right + 1);

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum =
		&info_frame.
		avi_info_packet.info_packet_hdmi.packet_raw_data.sb[0];
	*check_sum = HDMI_INFOFRAME_TYPE_AVI + HDMI_AVI_INFOFRAME_SIZE + 2;

	for (byte_index = 1; byte_index <= HDMI_AVI_INFOFRAME_SIZE; byte_index++)
		*check_sum += info_frame.avi_info_packet.info_packet_hdmi.
				packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb0;
	info_packet->hb1 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb1;
	info_packet->hb2 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(info_frame.avi_info_packet.
				info_packet_hdmi.packet_raw_data.sb); byte_index++)
		info_packet->sb[byte_index] = info_frame.avi_info_packet.
				info_packet_hdmi.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

static void set_vendor_info_packet(
		struct encoder_info_packet *info_packet,
		struct core_stream *stream)
{
	uint32_t length = 0;
	bool hdmi_vic_mode = false;
	uint8_t checksum = 0;
	uint32_t i = 0;
	enum dc_timing_3d_format format;

	format = stream->public.timing.timing_3d_format;

	/* Can be different depending on packet content */
	length = 5;

	if (stream->public.timing.hdmi_vic != 0
			&& stream->public.timing.h_total >= 3840
			&& stream->public.timing.v_total >= 2160)
		hdmi_vic_mode = true;

	/* According to HDMI 1.4a CTS, VSIF should be sent
	 * for both 3D stereo and HDMI VIC modes.
	 * For all other modes, there is no VSIF sent.  */

	if (format == TIMING_3D_FORMAT_NONE && !hdmi_vic_mode)
		return;

	/* 24bit IEEE Registration identifier (0x000c03). LSB first. */
	info_packet->sb[1] = 0x03;
	info_packet->sb[2] = 0x0C;
	info_packet->sb[3] = 0x00;

	/*PB4: 5 lower bytes = 0 (reserved). 3 higher bits = HDMI_Video_Format.
	 * The value for HDMI_Video_Format are:
	 * 0x0 (0b000) - No additional HDMI video format is presented in this
	 * packet
	 * 0x1 (0b001) - Extended resolution format present. 1 byte of HDMI_VIC
	 * parameter follows
	 * 0x2 (0b010) - 3D format indication present. 3D_Structure and
	 * potentially 3D_Ext_Data follows
	 * 0x3..0x7 (0b011..0b111) - reserved for future use */
	if (format != TIMING_3D_FORMAT_NONE)
		info_packet->sb[4] = (2 << 5);
	else if (hdmi_vic_mode)
		info_packet->sb[4] = (1 << 5);

	/* PB5: If PB4 claims 3D timing (HDMI_Video_Format = 0x2):
	 * 4 lower bites = 0 (reserved). 4 higher bits = 3D_Structure.
	 * The value for 3D_Structure are:
	 * 0x0 - Frame Packing
	 * 0x1 - Field Alternative
	 * 0x2 - Line Alternative
	 * 0x3 - Side-by-Side (full)
	 * 0x4 - L + depth
	 * 0x5 - L + depth + graphics + graphics-depth
	 * 0x6 - Top-and-Bottom
	 * 0x7 - Reserved for future use
	 * 0x8 - Side-by-Side (Half)
	 * 0x9..0xE - Reserved for future use
	 * 0xF - Not used */
	switch (format) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		info_packet->sb[5] = (0x0 << 4);
		break;

	case TIMING_3D_FORMAT_SIDE_BY_SIDE:
	case TIMING_3D_FORMAT_SBS_SW_PACKED:
		info_packet->sb[5] = (0x8 << 4);
		length = 6;
		break;

	case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		info_packet->sb[5] = (0x6 << 4);
		break;

	default:
		break;
	}

	/*PB5: If PB4 is set to 0x1 (extended resolution format)
	 * fill PB5 with the correct HDMI VIC code */
	if (hdmi_vic_mode)
		info_packet->sb[5] = stream->public.timing.hdmi_vic;

	/* Header */
	info_packet->hb0 = HDMI_INFOFRAME_TYPE_VENDOR; /* VSIF packet type. */
	info_packet->hb1 = 0x01; /* Version */

	/* 4 lower bits = Length, 4 higher bits = 0 (reserved) */
	info_packet->hb2 = (uint8_t) (length);

	/* Calculate checksum */
	checksum = 0;
	checksum += info_packet->hb0;
	checksum += info_packet->hb1;
	checksum += info_packet->hb2;

	for (i = 1; i <= length; i++)
		checksum += info_packet->sb[i];

	info_packet->sb[0] = (uint8_t) (0x100 - checksum);

	info_packet->valid = true;
}

static void set_spd_info_packet(
		struct encoder_info_packet *info_packet,
		struct core_stream *stream)
{
	/* SPD info packet for FreeSync */

	unsigned char checksum = 0;
	unsigned int idx, payload_size = 0;

	/* Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (stream->public.freesync_ctx.supported == false)
		return;

	if (dc_is_hdmi_signal(stream->signal)) {

		/* HEADER */

		/* HB0  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		info_packet->hb0 = HDMI_INFOFRAME_TYPE_SPD;

		/* HB1  = Version = 0x01 */
		info_packet->hb1 = 0x01;

		/* HB2  = [Bits 7:5 = 0] [Bits 4:0 = Length = 0x08] */
		info_packet->hb2 = 0x08;

		payload_size = 0x08;

	} else if (dc_is_dp_signal(stream->signal)) {

		/* HEADER */

		/* HB0  = Secondary-data Packet ID = 0 - Only non-zero
		 *	  when used to associate audio related info packets
		 */
		info_packet->hb0 = 0x00;

		/* HB1  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		info_packet->hb1 = HDMI_INFOFRAME_TYPE_SPD;

		/* HB2  = [Bits 7:0 = Least significant eight bits -
		 *	  For INFOFRAME, the value must be 1Bh]
		 */
		info_packet->hb2 = 0x1B;

		/* HB3  = [Bits 7:2 = INFOFRAME SDP Version Number = 0x1]
		 *	  [Bits 1:0 = Most significant two bits = 0x00]
		 */
		info_packet->hb3 = 0x04;

		payload_size = 0x1B;
	}

	/* PB1 = 0x1A (24bit AMD IEEE OUI (0x00001A) - Byte 0) */
	info_packet->sb[1] = 0x1A;

	/* PB2 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 1) */
	info_packet->sb[2] = 0x00;

	/* PB3 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 2) */
	info_packet->sb[3] = 0x00;

	/* PB4 = Reserved */
	info_packet->sb[4] = 0x00;

	/* PB5 = Reserved */
	info_packet->sb[5] = 0x00;

	/* PB6 = [Bits 7:3 = Reserved] */
	info_packet->sb[6] = 0x00;

	if (stream->public.freesync_ctx.supported == true)
		/* PB6 = [Bit 0 = FreeSync Supported] */
		info_packet->sb[6] |= 0x01;

	if (stream->public.freesync_ctx.enabled == true)
		/* PB6 = [Bit 1 = FreeSync Enabled] */
		info_packet->sb[6] |= 0x02;

	if (stream->public.freesync_ctx.active == true)
		/* PB6 = [Bit 2 = FreeSync Active] */
		info_packet->sb[6] |= 0x04;

	/* PB7 = FreeSync Minimum refresh rate (Hz) */
	info_packet->sb[7] = (unsigned char) (stream->public.freesync_ctx.
			min_refresh_in_micro_hz / 1000000);

	/* PB8 = FreeSync Maximum refresh rate (Hz)
	 *
	 * Note: We do not use the maximum capable refresh rate
	 * of the panel, because we should never go above the field
	 * rate of the mode timing set.
	 */
	info_packet->sb[8] = (unsigned char) (stream->public.freesync_ctx.
			nominal_refresh_in_micro_hz / 1000000);

	/* PB9 - PB27  = Reserved */
	for (idx = 9; idx <= 27; idx++)
		info_packet->sb[idx] = 0x00;

	/* Calculate checksum */
	checksum += info_packet->hb0;
	checksum += info_packet->hb1;
	checksum += info_packet->hb2;
	checksum += info_packet->hb3;

	for (idx = 1; idx <= payload_size; idx++)
		checksum += info_packet->sb[idx];

	/* PB0 = Checksum (one byte complement) */
	info_packet->sb[0] = (unsigned char) (0x100 - checksum);

	info_packet->valid = true;
}

static void set_hdr_static_info_packet(
		struct encoder_info_packet *info_packet,
		struct core_surface *surface,
		struct core_stream *stream)
{
	uint16_t i = 0;
	enum signal_type signal = stream->signal;
	struct dc_hdr_static_metadata hdr_metadata;
	uint32_t data;

	if (!surface)
		return;

	hdr_metadata = surface->public.hdr_static_ctx;

	if (!hdr_metadata.is_hdr)
		return;

	if (dc_is_hdmi_signal(signal)) {
		info_packet->valid = true;

		info_packet->hb0 = 0x87;
		info_packet->hb1 = 0x01;
		info_packet->hb2 = 0x1A;
		i = 1;
	} else if (dc_is_dp_signal(signal)) {
		info_packet->valid = true;

		info_packet->hb0 = 0x00;
		info_packet->hb1 = 0x87;
		info_packet->hb2 = 0x1D;
		info_packet->hb3 = (0x13 << 2);
		i = 2;
	}

	data = hdr_metadata.is_hdr;
	info_packet->sb[i++] = data ? 0x02 : 0x00;
	info_packet->sb[i++] = 0x00;

	data = hdr_metadata.chromaticity_green_x / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_green_y / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_blue_x / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_blue_y / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_red_x / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_red_y / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_white_point_x / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.chromaticity_white_point_y / 2;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.max_luminance;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.min_luminance;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.maximum_content_light_level;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	data = hdr_metadata.maximum_frame_average_light_level;
	info_packet->sb[i++] = data & 0xFF;
	info_packet->sb[i++] = (data & 0xFF00) >> 8;

	if (dc_is_hdmi_signal(signal)) {
		uint32_t checksum = 0;

		checksum += info_packet->hb0;
		checksum += info_packet->hb1;
		checksum += info_packet->hb2;

		for (i = 1; i <= info_packet->hb2; i++)
			checksum += info_packet->sb[i];

		info_packet->sb[0] = 0x100 - checksum;
	} else if (dc_is_dp_signal(signal)) {
		info_packet->sb[0] = 0x01;
		info_packet->sb[1] = 0x1A;
	}
}

static void set_vsc_info_packet(
		struct encoder_info_packet *info_packet,
		struct core_stream *stream)
{
	unsigned int vscPacketRevision = 0;
	unsigned int i;

	if (stream->sink->link->public.psr_caps.psr_version != 0) {
		vscPacketRevision = 2;
	}

	/* VSC packet not needed based on the features
	 * supported by this DP display
	 */
	if (vscPacketRevision == 0)
		return;

	if (vscPacketRevision == 0x2) {
		/* Secondary-data Packet ID = 0*/
		info_packet->hb0 = 0x00;
		/* 07h - Packet Type Value indicating Video
		 * Stream Configuration packet
		 */
		info_packet->hb1 = 0x07;
		/* 02h = VSC SDP supporting 3D stereo and PSR
		 * (applies to eDP v1.3 or higher).
		 */
		info_packet->hb2 = 0x02;
		/* 08h = VSC packet supporting 3D stereo + PSR
		 * (HB2 = 02h).
		 */
		info_packet->hb3 = 0x08;

		for (i = 0; i < 28; i++)
			info_packet->sb[i] = 0;

		info_packet->valid = true;
	}

	/*TODO: stereo 3D support and extend pixel encoding colorimetry*/
}

void resource_validate_ctx_destruct(struct validate_context *context)
{
	int i, j;

	for (i = 0; i < context->stream_count; i++) {
		for (j = 0; j < context->stream_status[i].surface_count; j++)
			dc_surface_release(
				context->stream_status[i].surfaces[j]);

		context->stream_status[i].surface_count = 0;
		dc_stream_release(&context->streams[i]->public);
		context->streams[i] = NULL;
	}
}

/*
 * Copy src_ctx into dst_ctx and retain all surfaces and streams referenced
 * by the src_ctx
 */
void resource_validate_ctx_copy_construct(
		const struct validate_context *src_ctx,
		struct validate_context *dst_ctx)
{
	int i, j;

	*dst_ctx = *src_ctx;

	for (i = 0; i < dst_ctx->res_ctx.pool->pipe_count; i++) {
		struct pipe_ctx *cur_pipe = &dst_ctx->res_ctx.pipe_ctx[i];

		if (cur_pipe->top_pipe)
			cur_pipe->top_pipe =  &dst_ctx->res_ctx.pipe_ctx[cur_pipe->top_pipe->pipe_idx];

		if (cur_pipe->bottom_pipe)
			cur_pipe->bottom_pipe = &dst_ctx->res_ctx.pipe_ctx[cur_pipe->bottom_pipe->pipe_idx];

	}

	for (i = 0; i < dst_ctx->stream_count; i++) {
		dc_stream_retain(&dst_ctx->streams[i]->public);
		for (j = 0; j < dst_ctx->stream_status[i].surface_count; j++)
			dc_surface_retain(
				dst_ctx->stream_status[i].surfaces[j]);
	}
}

struct clock_source *dc_resource_find_first_free_pll(
		struct resource_context *res_ctx)
{
	int i;

	for (i = 0; i < res_ctx->pool->clk_src_count; ++i) {
		if (res_ctx->clock_source_ref_count[i] == 0)
			return res_ctx->pool->clock_sources[i];
	}

	return NULL;
}

void resource_build_info_frame(struct pipe_ctx *pipe_ctx)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct encoder_info_frame *info = &pipe_ctx->encoder_info_frame;

	/* default all packets to invalid */
	info->avi.valid = false;
	info->gamut.valid = false;
	info->vendor.valid = false;
	info->hdrsmd.valid = false;
	info->vsc.valid = false;

	signal = pipe_ctx->stream->signal;

	/* HDMi and DP have different info packets*/
	if (dc_is_hdmi_signal(signal)) {
		set_avi_info_frame(&info->avi, pipe_ctx);

		set_vendor_info_packet(&info->vendor, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd,
				pipe_ctx->surface, pipe_ctx->stream);

	} else if (dc_is_dp_signal(signal)) {
		set_vsc_info_packet(&info->vsc, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd,
				pipe_ctx->surface, pipe_ctx->stream);
	}

	patch_gamut_packet_checksum(&info->gamut);
}

enum dc_status resource_map_clock_resources(
		const struct core_dc *dc,
		struct validate_context *context)
{
	int i, j;

	/* acquire new resources */
	for (i = 0; i < context->stream_count; i++) {
		const struct core_stream *stream = context->streams[i];

		if (resource_is_stream_unchanged(dc->current_context, stream))
			continue;

		for (j = 0; j < MAX_PIPES; j++) {
			struct pipe_ctx *pipe_ctx =
				&context->res_ctx.pipe_ctx[j];

			if (context->res_ctx.pipe_ctx[j].stream != stream)
				continue;

			if (dc_is_dp_signal(pipe_ctx->stream->signal)
				|| pipe_ctx->stream->signal == SIGNAL_TYPE_VIRTUAL)
				pipe_ctx->clock_source =
					context->res_ctx.pool->dp_clock_source;
			else {
				pipe_ctx->clock_source = NULL;

				if (!dc->public.config.disable_disp_pll_sharing)
					resource_find_used_clk_src_for_sharing(
						&context->res_ctx,
						pipe_ctx);

				if (pipe_ctx->clock_source == NULL)
					pipe_ctx->clock_source =
						dc_resource_find_first_free_pll(&context->res_ctx);
			}

			if (pipe_ctx->clock_source == NULL)
				return DC_NO_CLOCK_SOURCE_RESOURCE;

			resource_reference_clock_source(
				&context->res_ctx,
				pipe_ctx->clock_source);

			/* only one cs per stream regardless of mpo */
			break;
		}
	}

	return DC_OK;
}

/*
 * Note: We need to disable output if clock sources change,
 * since bios does optimization and doesn't apply if changing
 * PHY when not already disabled.
 */
bool pipe_need_reprogram(
		struct pipe_ctx *pipe_ctx_old,
		struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx_old->stream->sink != pipe_ctx->stream->sink)
		return true;

	if (pipe_ctx_old->stream->signal != pipe_ctx->stream->signal)
		return true;

	if (pipe_ctx_old->audio != pipe_ctx->audio)
		return true;

	if (pipe_ctx_old->clock_source != pipe_ctx->clock_source
			&& pipe_ctx_old->stream != pipe_ctx->stream)
		return true;

	if (pipe_ctx_old->stream_enc != pipe_ctx->stream_enc)
		return true;

	if (is_timing_changed(pipe_ctx_old->stream, pipe_ctx->stream))
		return true;


	return false;
}

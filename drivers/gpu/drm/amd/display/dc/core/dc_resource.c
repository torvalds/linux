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
#include "dccg.h"
#include "dchubbub.h"
#include "dpp.h"
#include "core_types.h"
#include "set_mode_types.h"
#include "virtual/virtual_stream_encoder.h"
#include "dpcd_defs.h"

#include "dce80/dce80_resource.h"
#include "dce100/dce100_resource.h"
#include "dce110/dce110_resource.h"
#include "dce112/dce112_resource.h"
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "dcn10/dcn10_resource.h"
#endif
#include "dce120/dce120_resource.h"

#define DC_LOGGER_INIT(logger)

enum dce_version resource_parse_asic_id(struct hw_asic_id asic_id)
{
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;
	switch (asic_id.chip_family) {

	case FAMILY_CI:
		dc_version = DCE_VERSION_8_0;
		break;
	case FAMILY_KV:
		if (ASIC_REV_IS_KALINDI(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_BHAVANI(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_GODAVARI(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_8_3;
		else
			dc_version = DCE_VERSION_8_1;
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
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_11_22;
		break;
	case FAMILY_AI:
		if (ASICREV_IS_VEGA20_P(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_12_1;
		else
			dc_version = DCE_VERSION_12_0;
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case FAMILY_RV:
		dc_version = DCN_VERSION_1_0;
#if defined(CONFIG_DRM_AMD_DC_DCN1_01)
		if (ASICREV_IS_RAVEN2(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_1_01;
#endif
		break;
#endif
	default:
		dc_version = DCE_VERSION_UNKNOWN;
		break;
	}
	return dc_version;
}

struct resource_pool *dc_create_resource_pool(struct dc  *dc,
					      const struct dc_init_data *init_data,
					      enum dce_version dc_version)
{
	struct resource_pool *res_pool = NULL;

	switch (dc_version) {
	case DCE_VERSION_8_0:
		res_pool = dce80_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_8_1:
		res_pool = dce81_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_8_3:
		res_pool = dce83_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_10_0:
		res_pool = dce100_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_11_0:
		res_pool = dce110_create_resource_pool(
				init_data->num_virtual_links, dc,
				init_data->asic_id);
		break;
	case DCE_VERSION_11_2:
	case DCE_VERSION_11_22:
		res_pool = dce112_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_12_0:
	case DCE_VERSION_12_1:
		res_pool = dce120_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case DCN_VERSION_1_0:
#if defined(CONFIG_DRM_AMD_DC_DCN1_01)
	case DCN_VERSION_1_01:
#endif
		res_pool = dcn10_create_resource_pool(init_data, dc);
		break;
#endif


	default:
		break;
	}
	if (res_pool != NULL) {
		struct dc_firmware_info fw_info = { { 0 } };

		if (dc->ctx->dc_bios->funcs->get_firmware_info(
				dc->ctx->dc_bios, &fw_info) == BP_RESULT_OK) {
				res_pool->ref_clocks.xtalin_clock_inKhz = fw_info.pll_info.crystal_frequency;

				if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
					// On FPGA these dividers are currently not configured by GDB
					res_pool->ref_clocks.dccg_ref_clock_inKhz = res_pool->ref_clocks.xtalin_clock_inKhz;
					res_pool->ref_clocks.dchub_ref_clock_inKhz = res_pool->ref_clocks.xtalin_clock_inKhz;
				} else if (res_pool->dccg && res_pool->hubbub) {
					// If DCCG reference frequency cannot be determined (usually means not set to xtalin) then this is a critical error
					// as this value must be known for DCHUB programming
					(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
							fw_info.pll_info.crystal_frequency,
							&res_pool->ref_clocks.dccg_ref_clock_inKhz);

					// Similarly, if DCHUB reference frequency cannot be determined, then it is also a critical error
					(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
							res_pool->ref_clocks.dccg_ref_clock_inKhz,
							&res_pool->ref_clocks.dchub_ref_clock_inKhz);
				} else {
					// Not all ASICs have DCCG sw component
					res_pool->ref_clocks.dccg_ref_clock_inKhz = res_pool->ref_clocks.xtalin_clock_inKhz;
					res_pool->ref_clocks.dchub_ref_clock_inKhz = res_pool->ref_clocks.xtalin_clock_inKhz;
				}
			} else
				ASSERT_CRITICAL(false);
	}

	return res_pool;
}

void dc_destroy_resource_pool(struct dc  *dc)
{
	if (dc) {
		if (dc->res_pool)
			dc->res_pool->funcs->destroy(&dc->res_pool);

		kfree(dc->hwseq);
	}
}

static void update_num_audio(
	const struct resource_straps *straps,
	unsigned int *num_audio,
	struct audio_support *aud_support)
{
	aud_support->dp_audio = true;
	aud_support->hdmi_audio_native = false;
	aud_support->hdmi_audio_on_dongle = false;

	if (straps->hdmi_disable == 0) {
		if (straps->dc_pinstraps_audio & 0x2) {
			aud_support->hdmi_audio_on_dongle = true;
			aud_support->hdmi_audio_native = true;
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
	}
}

bool resource_construct(
	unsigned int num_virtual_links,
	struct dc  *dc,
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

	dc->caps.dynamic_audio = false;
	if (pool->audio_count < pool->stream_enc_count) {
		dc->caps.dynamic_audio = true;
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
static int find_matching_clock_source(
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{

	int i;

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == clock_source)
			return i;
	}
	return -1;
}

void resource_unreference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		res_ctx->clock_source_ref_count[i]--;

	if (pool->dp_clock_source == clock_source)
		res_ctx->dp_clock_source_ref_count--;
}

void resource_reference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		res_ctx->clock_source_ref_count[i]++;

	if (pool->dp_clock_source == clock_source)
		res_ctx->dp_clock_source_ref_count++;
}

int resource_get_clock_source_reference(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		return res_ctx->clock_source_ref_count[i];

	if (pool->dp_clock_source == clock_source)
		return res_ctx->dp_clock_source_ref_count;

	return -1;
}

bool resource_are_streams_timing_synchronizable(
	struct dc_stream_state *stream1,
	struct dc_stream_state *stream2)
{
	if (stream1->timing.h_total != stream2->timing.h_total)
		return false;

	if (stream1->timing.v_total != stream2->timing.v_total)
		return false;

	if (stream1->timing.h_addressable
				!= stream2->timing.h_addressable)
		return false;

	if (stream1->timing.v_addressable
				!= stream2->timing.v_addressable)
		return false;

	if (stream1->timing.pix_clk_100hz
				!= stream2->timing.pix_clk_100hz)
		return false;

	if (stream1->clamping.c_depth != stream2->clamping.c_depth)
		return false;

	if (stream1->phy_pix_clk != stream2->phy_pix_clk
			&& (!dc_is_dp_signal(stream1->signal)
			|| !dc_is_dp_signal(stream2->signal)))
		return false;

	if (stream1->view_format != stream2->view_format)
		return false;

	return true;
}
static bool is_dp_and_hdmi_sharable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2)
{
	if (stream1->ctx->dc->caps.disable_dp_clk_share)
		return false;

	if (stream1->clamping.c_depth != COLOR_DEPTH_888 ||
		stream2->clamping.c_depth != COLOR_DEPTH_888)
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

	if (dc_is_dp_signal(pipe_with_clk_src->stream->signal) ||
		(dc_is_dp_signal(pipe->stream->signal) &&
		!is_dp_and_hdmi_sharable(pipe_with_clk_src->stream,
				     pipe->stream)))
		return false;

	if (dc_is_hdmi_signal(pipe_with_clk_src->stream->signal)
			&& dc_is_dual_link_signal(pipe->stream->signal))
		return false;

	if (dc_is_hdmi_signal(pipe->stream->signal)
			&& dc_is_dual_link_signal(pipe_with_clk_src->stream->signal))
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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
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
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP8;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP10;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

static inline void get_vp_scan_direction(
	enum dc_rotation_angle rotation,
	bool horizontal_mirror,
	bool *orthogonal_rotation,
	bool *flip_vert_scan_dir,
	bool *flip_horz_scan_dir)
{
	*orthogonal_rotation = false;
	*flip_vert_scan_dir = false;
	*flip_horz_scan_dir = false;
	if (rotation == ROTATION_ANGLE_180) {
		*flip_vert_scan_dir = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == ROTATION_ANGLE_90) {
		*orthogonal_rotation = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == ROTATION_ANGLE_270) {
		*orthogonal_rotation = true;
		*flip_vert_scan_dir = true;
	}

	if (horizontal_mirror)
		*flip_horz_scan_dir = !*flip_horz_scan_dir;
}

static void calculate_viewport(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct scaler_data *data = &pipe_ctx->plane_res.scl_data;
	struct rect surf_src = plane_state->src_rect;
	struct rect clip, dest;
	int vpc_div = (data->format == PIXEL_FORMAT_420BPP8
			|| data->format == PIXEL_FORMAT_420BPP10) ? 2 : 1;
	bool pri_split = pipe_ctx->bottom_pipe &&
			pipe_ctx->bottom_pipe->plane_state == pipe_ctx->plane_state;
	bool sec_split = pipe_ctx->top_pipe &&
			pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state;
	bool orthogonal_rotation, flip_y_start, flip_x_start;

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE ||
		stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM) {
		pri_split = false;
		sec_split = false;
	}

	/* The actual clip is an intersection between stream
	 * source and surface clip
	 */
	dest = plane_state->dst_rect;
	clip.x = stream->src.x > plane_state->clip_rect.x ?
			stream->src.x : plane_state->clip_rect.x;

	clip.width = stream->src.x + stream->src.width <
			plane_state->clip_rect.x + plane_state->clip_rect.width ?
			stream->src.x + stream->src.width - clip.x :
			plane_state->clip_rect.x + plane_state->clip_rect.width - clip.x ;

	clip.y = stream->src.y > plane_state->clip_rect.y ?
			stream->src.y : plane_state->clip_rect.y;

	clip.height = stream->src.y + stream->src.height <
			plane_state->clip_rect.y + plane_state->clip_rect.height ?
			stream->src.y + stream->src.height - clip.y :
			plane_state->clip_rect.y + plane_state->clip_rect.height - clip.y ;

	/*
	 * Need to calculate how scan origin is shifted in vp space
	 * to correctly rotate clip and dst
	 */
	get_vp_scan_direction(
			plane_state->rotation,
			plane_state->horizontal_mirror,
			&orthogonal_rotation,
			&flip_y_start,
			&flip_x_start);

	if (orthogonal_rotation) {
		swap(clip.x, clip.y);
		swap(clip.width, clip.height);
		swap(dest.x, dest.y);
		swap(dest.width, dest.height);
	}
	if (flip_x_start) {
		clip.x = dest.x + dest.width - clip.x - clip.width;
		dest.x = 0;
	}
	if (flip_y_start) {
		clip.y = dest.y + dest.height - clip.y - clip.height;
		dest.y = 0;
	}

	/* offset = surf_src.ofs + (clip.ofs - surface->dst_rect.ofs) * scl_ratio
	 * num_pixels = clip.num_pix * scl_ratio
	 */
	data->viewport.x = surf_src.x + (clip.x - dest.x) * surf_src.width / dest.width;
	data->viewport.width = clip.width * surf_src.width / dest.width;

	data->viewport.y = surf_src.y + (clip.y - dest.y) * surf_src.height / dest.height;
	data->viewport.height = clip.height * surf_src.height / dest.height;

	/* Handle split */
	if (pri_split || sec_split) {
		if (orthogonal_rotation) {
			if (flip_y_start != pri_split)
				data->viewport.height /= 2;
			else {
				data->viewport.y +=  data->viewport.height / 2;
				/* Ceil offset pipe */
				data->viewport.height = (data->viewport.height + 1) / 2;
			}
		} else {
			if (flip_x_start != pri_split)
				data->viewport.width /= 2;
			else {
				data->viewport.x +=  data->viewport.width / 2;
				/* Ceil offset pipe */
				data->viewport.width = (data->viewport.width + 1) / 2;
			}
		}
	}

	/* Round down, compensate in init */
	data->viewport_c.x = data->viewport.x / vpc_div;
	data->viewport_c.y = data->viewport.y / vpc_div;
	data->inits.h_c = (data->viewport.x % vpc_div) != 0 ? dc_fixpt_half : dc_fixpt_zero;
	data->inits.v_c = (data->viewport.y % vpc_div) != 0 ? dc_fixpt_half : dc_fixpt_zero;

	/* Round up, assume original video size always even dimensions */
	data->viewport_c.width = (data->viewport.width + vpc_div - 1) / vpc_div;
	data->viewport_c.height = (data->viewport.height + vpc_div - 1) / vpc_div;
}

static void calculate_recout(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct rect surf_clip = plane_state->clip_rect;
	bool pri_split = pipe_ctx->bottom_pipe &&
			pipe_ctx->bottom_pipe->plane_state == pipe_ctx->plane_state;
	bool sec_split = pipe_ctx->top_pipe &&
			pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state;
	bool top_bottom_split = stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM;

	pipe_ctx->plane_res.scl_data.recout.x = stream->dst.x;
	if (stream->src.x < surf_clip.x)
		pipe_ctx->plane_res.scl_data.recout.x += (surf_clip.x
			- stream->src.x) * stream->dst.width
						/ stream->src.width;

	pipe_ctx->plane_res.scl_data.recout.width = surf_clip.width *
			stream->dst.width / stream->src.width;
	if (pipe_ctx->plane_res.scl_data.recout.width + pipe_ctx->plane_res.scl_data.recout.x >
			stream->dst.x + stream->dst.width)
		pipe_ctx->plane_res.scl_data.recout.width =
			stream->dst.x + stream->dst.width
						- pipe_ctx->plane_res.scl_data.recout.x;

	pipe_ctx->plane_res.scl_data.recout.y = stream->dst.y;
	if (stream->src.y < surf_clip.y)
		pipe_ctx->plane_res.scl_data.recout.y += (surf_clip.y
			- stream->src.y) * stream->dst.height
						/ stream->src.height;

	pipe_ctx->plane_res.scl_data.recout.height = surf_clip.height *
			stream->dst.height / stream->src.height;
	if (pipe_ctx->plane_res.scl_data.recout.height + pipe_ctx->plane_res.scl_data.recout.y >
			stream->dst.y + stream->dst.height)
		pipe_ctx->plane_res.scl_data.recout.height =
			stream->dst.y + stream->dst.height
						- pipe_ctx->plane_res.scl_data.recout.y;

	/* Handle h & v split, handle rotation using viewport */
	if (sec_split && top_bottom_split) {
		pipe_ctx->plane_res.scl_data.recout.y +=
				pipe_ctx->plane_res.scl_data.recout.height / 2;
		/* Floor primary pipe, ceil 2ndary pipe */
		pipe_ctx->plane_res.scl_data.recout.height =
				(pipe_ctx->plane_res.scl_data.recout.height + 1) / 2;
	} else if (pri_split && top_bottom_split)
		pipe_ctx->plane_res.scl_data.recout.height /= 2;
	else if (sec_split) {
		pipe_ctx->plane_res.scl_data.recout.x +=
				pipe_ctx->plane_res.scl_data.recout.width / 2;
		/* Ceil offset pipe */
		pipe_ctx->plane_res.scl_data.recout.width =
				(pipe_ctx->plane_res.scl_data.recout.width + 1) / 2;
	} else if (pri_split)
		pipe_ctx->plane_res.scl_data.recout.width /= 2;
}

static void calculate_scaling_ratios(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct rect surf_src = plane_state->src_rect;
	const int in_w = stream->src.width;
	const int in_h = stream->src.height;
	const int out_w = stream->dst.width;
	const int out_h = stream->dst.height;

	/*Swap surf_src height and width since scaling ratios are in recout rotation*/
	if (pipe_ctx->plane_state->rotation == ROTATION_ANGLE_90 ||
			pipe_ctx->plane_state->rotation == ROTATION_ANGLE_270)
		swap(surf_src.height, surf_src.width);

	pipe_ctx->plane_res.scl_data.ratios.horz = dc_fixpt_from_fraction(
					surf_src.width,
					plane_state->dst_rect.width);
	pipe_ctx->plane_res.scl_data.ratios.vert = dc_fixpt_from_fraction(
					surf_src.height,
					plane_state->dst_rect.height);

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
		pipe_ctx->plane_res.scl_data.ratios.horz.value *= 2;
	else if (stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM)
		pipe_ctx->plane_res.scl_data.ratios.vert.value *= 2;

	pipe_ctx->plane_res.scl_data.ratios.vert.value = div64_s64(
		pipe_ctx->plane_res.scl_data.ratios.vert.value * in_h, out_h);
	pipe_ctx->plane_res.scl_data.ratios.horz.value = div64_s64(
		pipe_ctx->plane_res.scl_data.ratios.horz.value * in_w, out_w);

	pipe_ctx->plane_res.scl_data.ratios.horz_c = pipe_ctx->plane_res.scl_data.ratios.horz;
	pipe_ctx->plane_res.scl_data.ratios.vert_c = pipe_ctx->plane_res.scl_data.ratios.vert;

	if (pipe_ctx->plane_res.scl_data.format == PIXEL_FORMAT_420BPP8
			|| pipe_ctx->plane_res.scl_data.format == PIXEL_FORMAT_420BPP10) {
		pipe_ctx->plane_res.scl_data.ratios.horz_c.value /= 2;
		pipe_ctx->plane_res.scl_data.ratios.vert_c.value /= 2;
	}
	pipe_ctx->plane_res.scl_data.ratios.horz = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.horz, 19);
	pipe_ctx->plane_res.scl_data.ratios.vert = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.vert, 19);
	pipe_ctx->plane_res.scl_data.ratios.horz_c = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.horz_c, 19);
	pipe_ctx->plane_res.scl_data.ratios.vert_c = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.vert_c, 19);
}

static inline void adjust_vp_and_init_for_seamless_clip(
		bool flip_scan_dir,
		int recout_skip,
		int src_size,
		int taps,
		struct fixed31_32 ratio,
		struct fixed31_32 *init,
		int *vp_offset,
		int *vp_size)
{
	if (!flip_scan_dir) {
		/* Adjust for viewport end clip-off */
		if ((*vp_offset + *vp_size) < src_size) {
			int vp_clip = src_size - *vp_size - *vp_offset;
			int int_part = dc_fixpt_floor(dc_fixpt_sub(*init, ratio));

			int_part = int_part > 0 ? int_part : 0;
			*vp_size += int_part < vp_clip ? int_part : vp_clip;
		}

		/* Adjust for non-0 viewport offset */
		if (*vp_offset) {
			int int_part;

			*init = dc_fixpt_add(*init, dc_fixpt_mul_int(ratio, recout_skip));
			int_part = dc_fixpt_floor(*init) - *vp_offset;
			if (int_part < taps) {
				int int_adj = *vp_offset >= (taps - int_part) ?
							(taps - int_part) : *vp_offset;
				*vp_offset -= int_adj;
				*vp_size += int_adj;
				int_part += int_adj;
			} else if (int_part > taps) {
				*vp_offset += int_part - taps;
				*vp_size -= int_part - taps;
				int_part = taps;
			}
			init->value &= 0xffffffff;
			*init = dc_fixpt_add_int(*init, int_part);
		}
	} else {
		/* Adjust for non-0 viewport offset */
		if (*vp_offset) {
			int int_part = dc_fixpt_floor(dc_fixpt_sub(*init, ratio));

			int_part = int_part > 0 ? int_part : 0;
			*vp_size += int_part < *vp_offset ? int_part : *vp_offset;
			*vp_offset -= int_part < *vp_offset ? int_part : *vp_offset;
		}

		/* Adjust for viewport end clip-off */
		if ((*vp_offset + *vp_size) < src_size) {
			int int_part;
			int end_offset = src_size - *vp_offset - *vp_size;

			/*
			 * this is init if vp had no offset, keep in mind this is from the
			 * right side of vp due to scan direction
			 */
			*init = dc_fixpt_add(*init, dc_fixpt_mul_int(ratio, recout_skip));
			/*
			 * this is the difference between first pixel of viewport available to read
			 * and init position, takning into account scan direction
			 */
			int_part = dc_fixpt_floor(*init) - end_offset;
			if (int_part < taps) {
				int int_adj = end_offset >= (taps - int_part) ?
							(taps - int_part) : end_offset;
				*vp_size += int_adj;
				int_part += int_adj;
			} else if (int_part > taps) {
				*vp_size += int_part - taps;
				int_part = taps;
			}
			init->value &= 0xffffffff;
			*init = dc_fixpt_add_int(*init, int_part);
		}
	}
}

static void calculate_inits_and_adj_vp(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct scaler_data *data = &pipe_ctx->plane_res.scl_data;
	struct rect src = pipe_ctx->plane_state->src_rect;
	int recout_skip_h, recout_skip_v, surf_size_h, surf_size_v;
	int vpc_div = (data->format == PIXEL_FORMAT_420BPP8
			|| data->format == PIXEL_FORMAT_420BPP10) ? 2 : 1;
	bool orthogonal_rotation, flip_vert_scan_dir, flip_horz_scan_dir;

	/*
	 * Need to calculate the scan direction for viewport to make adjustments
	 */
	get_vp_scan_direction(
			plane_state->rotation,
			plane_state->horizontal_mirror,
			&orthogonal_rotation,
			&flip_vert_scan_dir,
			&flip_horz_scan_dir);

	/* Calculate src rect rotation adjusted to recout space */
	surf_size_h = src.x + src.width;
	surf_size_v = src.y + src.height;
	if (flip_horz_scan_dir)
		src.x = 0;
	if (flip_vert_scan_dir)
		src.y = 0;
	if (orthogonal_rotation) {
		swap(src.x, src.y);
		swap(src.width, src.height);
	}

	/* Recout matching initial vp offset = recout_offset - (stream dst offset +
	 *			((surf dst offset - stream src offset) * 1/ stream scaling ratio)
	 *			- (surf surf_src offset * 1/ full scl ratio))
	 */
	recout_skip_h = data->recout.x - (stream->dst.x + (plane_state->dst_rect.x - stream->src.x)
					* stream->dst.width / stream->src.width -
					src.x * plane_state->dst_rect.width / src.width
					* stream->dst.width / stream->src.width);
	recout_skip_v = data->recout.y - (stream->dst.y + (plane_state->dst_rect.y - stream->src.y)
					* stream->dst.height / stream->src.height -
					src.y * plane_state->dst_rect.height / src.height
					* stream->dst.height / stream->src.height);
	if (orthogonal_rotation)
		swap(recout_skip_h, recout_skip_v);
	/*
	 * Init calculated according to formula:
	 * 	init = (scaling_ratio + number_of_taps + 1) / 2
	 * 	init_bot = init + scaling_ratio
	 * 	init_c = init + truncated_vp_c_offset(from calculate viewport)
	 */
	data->inits.h = dc_fixpt_truncate(dc_fixpt_div_int(
			dc_fixpt_add_int(data->ratios.horz, data->taps.h_taps + 1), 2), 19);

	data->inits.h_c = dc_fixpt_truncate(dc_fixpt_add(data->inits.h_c, dc_fixpt_div_int(
			dc_fixpt_add_int(data->ratios.horz_c, data->taps.h_taps_c + 1), 2)), 19);

	data->inits.v = dc_fixpt_truncate(dc_fixpt_div_int(
			dc_fixpt_add_int(data->ratios.vert, data->taps.v_taps + 1), 2), 19);

	data->inits.v_c = dc_fixpt_truncate(dc_fixpt_add(data->inits.v_c, dc_fixpt_div_int(
			dc_fixpt_add_int(data->ratios.vert_c, data->taps.v_taps_c + 1), 2)), 19);

	/*
	 * Taps, inits and scaling ratios are in recout space need to rotate
	 * to viewport rotation before adjustment
	 */
	adjust_vp_and_init_for_seamless_clip(
			flip_horz_scan_dir,
			recout_skip_h,
			surf_size_h,
			orthogonal_rotation ? data->taps.v_taps : data->taps.h_taps,
			orthogonal_rotation ? data->ratios.vert : data->ratios.horz,
			orthogonal_rotation ? &data->inits.v : &data->inits.h,
			&data->viewport.x,
			&data->viewport.width);
	adjust_vp_and_init_for_seamless_clip(
			flip_horz_scan_dir,
			recout_skip_h,
			surf_size_h / vpc_div,
			orthogonal_rotation ? data->taps.v_taps_c : data->taps.h_taps_c,
			orthogonal_rotation ? data->ratios.vert_c : data->ratios.horz_c,
			orthogonal_rotation ? &data->inits.v_c : &data->inits.h_c,
			&data->viewport_c.x,
			&data->viewport_c.width);
	adjust_vp_and_init_for_seamless_clip(
			flip_vert_scan_dir,
			recout_skip_v,
			surf_size_v,
			orthogonal_rotation ? data->taps.h_taps : data->taps.v_taps,
			orthogonal_rotation ? data->ratios.horz : data->ratios.vert,
			orthogonal_rotation ? &data->inits.h : &data->inits.v,
			&data->viewport.y,
			&data->viewport.height);
	adjust_vp_and_init_for_seamless_clip(
			flip_vert_scan_dir,
			recout_skip_v,
			surf_size_v / vpc_div,
			orthogonal_rotation ? data->taps.h_taps_c : data->taps.v_taps_c,
			orthogonal_rotation ? data->ratios.horz_c : data->ratios.vert_c,
			orthogonal_rotation ? &data->inits.h_c : &data->inits.v_c,
			&data->viewport_c.y,
			&data->viewport_c.height);

	/* Interlaced inits based on final vert inits */
	data->inits.v_bot = dc_fixpt_add(data->inits.v, data->ratios.vert);
	data->inits.v_c_bot = dc_fixpt_add(data->inits.v_c, data->ratios.vert_c);

}

bool resource_build_scaling_params(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	bool res = false;
	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);
	/* Important: scaling ratio calculation requires pixel format,
	 * lb depth calculation requires recout and taps require scaling ratios.
	 * Inits require viewport, taps, ratios and recout of split pipe
	 */
	pipe_ctx->plane_res.scl_data.format = convert_pixel_format_to_dalsurface(
			pipe_ctx->plane_state->format);

	calculate_scaling_ratios(pipe_ctx);

	calculate_viewport(pipe_ctx);

	if (pipe_ctx->plane_res.scl_data.viewport.height < 16 || pipe_ctx->plane_res.scl_data.viewport.width < 16)
		return false;

	calculate_recout(pipe_ctx);

	/**
	 * Setting line buffer pixel depth to 24bpp yields banding
	 * on certain displays, such as the Sharp 4k
	 */
	pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;

	pipe_ctx->plane_res.scl_data.recout.x += timing->h_border_left;
	pipe_ctx->plane_res.scl_data.recout.y += timing->v_border_top;

	pipe_ctx->plane_res.scl_data.h_active = timing->h_addressable + timing->h_border_left + timing->h_border_right;
	pipe_ctx->plane_res.scl_data.v_active = timing->v_addressable + timing->v_border_top + timing->v_border_bottom;

	/* Taps calculations */
	if (pipe_ctx->plane_res.xfm != NULL)
		res = pipe_ctx->plane_res.xfm->funcs->transform_get_optimal_number_of_taps(
				pipe_ctx->plane_res.xfm, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);

	if (pipe_ctx->plane_res.dpp != NULL)
		res = pipe_ctx->plane_res.dpp->funcs->dpp_get_optimal_number_of_taps(
				pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);
	if (!res) {
		/* Try 24 bpp linebuffer */
		pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_24BPP;

		if (pipe_ctx->plane_res.xfm != NULL)
			res = pipe_ctx->plane_res.xfm->funcs->transform_get_optimal_number_of_taps(
					pipe_ctx->plane_res.xfm,
					&pipe_ctx->plane_res.scl_data,
					&plane_state->scaling_quality);

		if (pipe_ctx->plane_res.dpp != NULL)
			res = pipe_ctx->plane_res.dpp->funcs->dpp_get_optimal_number_of_taps(
					pipe_ctx->plane_res.dpp,
					&pipe_ctx->plane_res.scl_data,
					&plane_state->scaling_quality);
	}

	if (res)
		/* May need to re-check lb size after this in some obscure scenario */
		calculate_inits_and_adj_vp(pipe_ctx);

	DC_LOG_SCALER(
				"%s: Viewport:\nheight:%d width:%d x:%d "
				"y:%d\n dst_rect:\nheight:%d width:%d x:%d "
				"y:%d\n",
				__func__,
				pipe_ctx->plane_res.scl_data.viewport.height,
				pipe_ctx->plane_res.scl_data.viewport.width,
				pipe_ctx->plane_res.scl_data.viewport.x,
				pipe_ctx->plane_res.scl_data.viewport.y,
				plane_state->dst_rect.height,
				plane_state->dst_rect.width,
				plane_state->dst_rect.x,
				plane_state->dst_rect.y);

	return res;
}


enum dc_status resource_build_scaling_params_for_context(
	const struct dc  *dc,
	struct dc_state *context)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context->res_ctx.pipe_ctx[i].plane_state != NULL &&
				context->res_ctx.pipe_ctx[i].stream != NULL)
			if (!resource_build_scaling_params(&context->res_ctx.pipe_ctx[i]))
				return DC_FAIL_SCALING;
	}

	return DC_OK;
}

struct pipe_ctx *find_idle_secondary_pipe(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe)
{
	int i;
	struct pipe_ctx *secondary_pipe = NULL;

	/*
	 * We add a preferred pipe mapping to avoid the chance that
	 * MPCCs already in use will need to be reassigned to other trees.
	 * For example, if we went with the strict, assign backwards logic:
	 *
	 * (State 1)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, no surface, top pipe = 1
	 *
	 * (State 2)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 5
	 *
	 * (State 3)
	 * Display A on, surface enable, top pipe = 0, bottom pipe = 5
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 4
	 *
	 * The state 2->3 transition requires remapping MPCC 5 from display B
	 * to display A.
	 *
	 * However, with the preferred pipe logic, state 2 would look like:
	 *
	 * (State 2)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 4
	 *
	 * This would then cause 2->3 to not require remapping any MPCCs.
	 */
	if (primary_pipe) {
		int preferred_pipe_idx = (pool->pipe_count - 1) - primary_pipe->pipe_idx;
		if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
			secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
			secondary_pipe->pipe_idx = preferred_pipe_idx;
		}
	}

	/*
	 * search backwards for the second pipe to keep pipe
	 * assignment more consistent
	 */
	if (!secondary_pipe)
		for (i = pool->pipe_count - 1; i >= 0; i--) {
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
		struct dc_stream_state *stream)
{
	int i;
	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				!res_ctx->pipe_ctx[i].top_pipe) {
			return &res_ctx->pipe_ctx[i];
			break;
		}
	}
	return NULL;
}

static struct pipe_ctx *resource_get_tail_pipe_for_stream(
		struct resource_context *res_ctx,
		struct dc_stream_state *stream)
{
	struct pipe_ctx *head_pipe, *tail_pipe;
	head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);

	if (!head_pipe)
		return NULL;

	tail_pipe = head_pipe->bottom_pipe;

	while (tail_pipe) {
		head_pipe = tail_pipe;
		tail_pipe = tail_pipe->bottom_pipe;
	}

	return head_pipe;
}

/*
 * A free_pipe for a stream is defined here as a pipe
 * that has no surface attached yet
 */
static struct pipe_ctx *acquire_free_pipe_for_stream(
		struct dc_state *context,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;
	struct resource_context *res_ctx = &context->res_ctx;

	struct pipe_ctx *head_pipe = NULL;

	/* Find head pipe, which has the back end set up*/

	head_pipe = resource_get_head_pipe_for_stream(res_ctx, stream);

	if (!head_pipe) {
		ASSERT(0);
		return NULL;
	}

	if (!head_pipe->plane_state)
		return head_pipe;

	/* Re-use pipe already acquired for this stream if available*/
	for (i = pool->pipe_count - 1; i >= 0; i--) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				!res_ctx->pipe_ctx[i].plane_state) {
			return &res_ctx->pipe_ctx[i];
		}
	}

	/*
	 * At this point we have no re-useable pipe for this stream and we need
	 * to acquire an idle one to satisfy the request
	 */

	if (!pool->funcs->acquire_idle_pipe_for_layer)
		return NULL;

	return pool->funcs->acquire_idle_pipe_for_layer(context, pool, stream);

}

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
static int acquire_first_split_pipe(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->top_pipe &&
				pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state) {
			pipe_ctx->top_pipe->bottom_pipe = pipe_ctx->bottom_pipe;
			if (pipe_ctx->bottom_pipe)
				pipe_ctx->bottom_pipe->top_pipe = pipe_ctx->top_pipe;

			memset(pipe_ctx, 0, sizeof(*pipe_ctx));
			pipe_ctx->stream_res.tg = pool->timing_generators[i];
			pipe_ctx->plane_res.hubp = pool->hubps[i];
			pipe_ctx->plane_res.ipp = pool->ipps[i];
			pipe_ctx->plane_res.dpp = pool->dpps[i];
			pipe_ctx->stream_res.opp = pool->opps[i];
			pipe_ctx->plane_res.mpcc_inst = pool->dpps[i]->inst;
			pipe_ctx->pipe_idx = i;

			pipe_ctx->stream = stream;
			return i;
		}
	}
	return -1;
}
#endif

bool dc_add_plane_to_context(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *context)
{
	int i;
	struct resource_pool *pool = dc->res_pool;
	struct pipe_ctx *head_pipe, *tail_pipe, *free_pipe;
	struct dc_stream_status *stream_status = NULL;

	for (i = 0; i < context->stream_count; i++)
		if (context->streams[i] == stream) {
			stream_status = &context->stream_status[i];
			break;
		}
	if (stream_status == NULL) {
		dm_error("Existing stream not found; failed to attach surface!\n");
		return false;
	}


	if (stream_status->plane_count == MAX_SURFACE_NUM) {
		dm_error("Surface: can not attach plane_state %p! Maximum is: %d\n",
				plane_state, MAX_SURFACE_NUM);
		return false;
	}

	head_pipe = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	if (!head_pipe) {
		dm_error("Head pipe not found for stream_state %p !\n", stream);
		return false;
	}

	tail_pipe = resource_get_tail_pipe_for_stream(&context->res_ctx, stream);
	ASSERT(tail_pipe);

	free_pipe = acquire_free_pipe_for_stream(context, pool, stream);

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	if (!free_pipe) {
		int pipe_idx = acquire_first_split_pipe(&context->res_ctx, pool, stream);
		if (pipe_idx >= 0)
			free_pipe = &context->res_ctx.pipe_ctx[pipe_idx];
	}
#endif
	if (!free_pipe)
		return false;

	/* retain new surfaces */
	dc_plane_state_retain(plane_state);
	free_pipe->plane_state = plane_state;

	if (head_pipe != free_pipe) {
		free_pipe->stream_res.tg = tail_pipe->stream_res.tg;
		free_pipe->stream_res.abm = tail_pipe->stream_res.abm;
		free_pipe->stream_res.opp = tail_pipe->stream_res.opp;
		free_pipe->stream_res.stream_enc = tail_pipe->stream_res.stream_enc;
		free_pipe->stream_res.audio = tail_pipe->stream_res.audio;
		free_pipe->clock_source = tail_pipe->clock_source;
		free_pipe->top_pipe = tail_pipe;
		tail_pipe->bottom_pipe = free_pipe;
	} else if (free_pipe->bottom_pipe && free_pipe->bottom_pipe->plane_state == NULL) {
		ASSERT(free_pipe->bottom_pipe->stream_res.opp != free_pipe->stream_res.opp);
		free_pipe->bottom_pipe->plane_state = plane_state;
	}

	/* assign new surfaces*/
	stream_status->plane_states[stream_status->plane_count] = plane_state;

	stream_status->plane_count++;

	return true;
}

struct pipe_ctx *dc_res_get_odm_bottom_pipe(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *bottom_pipe = pipe_ctx->bottom_pipe;

	/* ODM should only be updated once per otg */
	if (pipe_ctx->top_pipe)
		return NULL;

	while (bottom_pipe) {
		if (bottom_pipe->stream_res.opp != pipe_ctx->stream_res.opp)
			break;
		bottom_pipe = bottom_pipe->bottom_pipe;
	}

	return bottom_pipe;
}

bool dc_res_is_odm_head_pipe(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *top_pipe = pipe_ctx->top_pipe;

	if (!top_pipe)
		return false;
	if (top_pipe && top_pipe->stream_res.opp == pipe_ctx->stream_res.opp)
		return false;

	return true;
}

bool dc_remove_plane_from_context(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *context)
{
	int i;
	struct dc_stream_status *stream_status = NULL;
	struct resource_pool *pool = dc->res_pool;

	for (i = 0; i < context->stream_count; i++)
		if (context->streams[i] == stream) {
			stream_status = &context->stream_status[i];
			break;
		}

	if (stream_status == NULL) {
		dm_error("Existing stream not found; failed to remove plane.\n");
		return false;
	}

	/* release pipe for plane*/
	for (i = pool->pipe_count - 1; i >= 0; i--) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state == plane_state) {
			if (dc_res_is_odm_head_pipe(pipe_ctx)) {
				pipe_ctx->plane_state = NULL;
				pipe_ctx->bottom_pipe = NULL;
				continue;
			}

			if (pipe_ctx->top_pipe)
				pipe_ctx->top_pipe->bottom_pipe = pipe_ctx->bottom_pipe;

			/* Second condition is to avoid setting NULL to top pipe
			 * of tail pipe making it look like head pipe in subsequent
			 * deletes
			 */
			if (pipe_ctx->bottom_pipe && pipe_ctx->top_pipe)
				pipe_ctx->bottom_pipe->top_pipe = pipe_ctx->top_pipe;

			/*
			 * For head pipe detach surfaces from pipe for tail
			 * pipe just zero it out
			 */
			if (!pipe_ctx->top_pipe) {
				pipe_ctx->plane_state = NULL;
				if (!dc_res_get_odm_bottom_pipe(pipe_ctx))
					pipe_ctx->bottom_pipe = NULL;
			} else {
				memset(pipe_ctx, 0, sizeof(*pipe_ctx));
			}
		}
	}


	for (i = 0; i < stream_status->plane_count; i++) {
		if (stream_status->plane_states[i] == plane_state) {

			dc_plane_state_release(stream_status->plane_states[i]);
			break;
		}
	}

	if (i == stream_status->plane_count) {
		dm_error("Existing plane_state not found; failed to detach it!\n");
		return false;
	}

	stream_status->plane_count--;

	/* Start at the plane we've just released, and move all the planes one index forward to "trim" the array */
	for (; i < stream_status->plane_count; i++)
		stream_status->plane_states[i] = stream_status->plane_states[i + 1];

	stream_status->plane_states[stream_status->plane_count] = NULL;

	return true;
}

bool dc_rem_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *context)
{
	int i, old_plane_count;
	struct dc_stream_status *stream_status = NULL;
	struct dc_plane_state *del_planes[MAX_SURFACE_NUM] = { 0 };

	for (i = 0; i < context->stream_count; i++)
			if (context->streams[i] == stream) {
				stream_status = &context->stream_status[i];
				break;
			}

	if (stream_status == NULL) {
		dm_error("Existing stream %p not found!\n", stream);
		return false;
	}

	old_plane_count = stream_status->plane_count;

	for (i = 0; i < old_plane_count; i++)
		del_planes[i] = stream_status->plane_states[i];

	for (i = 0; i < old_plane_count; i++)
		if (!dc_remove_plane_from_context(dc, stream, del_planes[i], context))
			return false;

	return true;
}

static bool add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		const struct dc_validation_set set[],
		int set_count,
		struct dc_state *context)
{
	int i, j;

	for (i = 0; i < set_count; i++)
		if (set[i].stream == stream)
			break;

	if (i == set_count) {
		dm_error("Stream %p not found in set!\n", stream);
		return false;
	}

	for (j = 0; j < set[i].plane_count; j++)
		if (!dc_add_plane_to_context(dc, stream, set[i].plane_states[j], context))
			return false;

	return true;
}

bool dc_add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state * const *plane_states,
		int plane_count,
		struct dc_state *context)
{
	struct dc_validation_set set;
	int i;

	set.stream = stream;
	set.plane_count = plane_count;

	for (i = 0; i < plane_count; i++)
		set.plane_states[i] = plane_states[i];

	return add_all_planes_for_stream(dc, stream, &set, 1, context);
}


static bool is_hdr_static_meta_changed(struct dc_stream_state *cur_stream,
	struct dc_stream_state *new_stream)
{
	if (cur_stream == NULL)
		return true;

	if (memcmp(&cur_stream->hdr_static_metadata,
			&new_stream->hdr_static_metadata,
			sizeof(struct dc_info_packet)) != 0)
		return true;

	return false;
}

static bool is_vsc_info_packet_changed(struct dc_stream_state *cur_stream,
		struct dc_stream_state *new_stream)
{
	if (cur_stream == NULL)
		return true;

	if (memcmp(&cur_stream->vsc_infopacket,
			&new_stream->vsc_infopacket,
			sizeof(struct dc_info_packet)) != 0)
		return true;

	return false;
}

static bool is_timing_changed(struct dc_stream_state *cur_stream,
		struct dc_stream_state *new_stream)
{
	if (cur_stream == NULL)
		return true;

	/* If sink pointer changed, it means this is a hotplug, we should do
	 * full hw setting.
	 */
	if (cur_stream->sink != new_stream->sink)
		return true;

	/* If output color space is changed, need to reprogram info frames */
	if (cur_stream->output_color_space != new_stream->output_color_space)
		return true;

	return memcmp(
		&cur_stream->timing,
		&new_stream->timing,
		sizeof(struct dc_crtc_timing)) != 0;
}

static bool are_stream_backends_same(
	struct dc_stream_state *stream_a, struct dc_stream_state *stream_b)
{
	if (stream_a == stream_b)
		return true;

	if (stream_a == NULL || stream_b == NULL)
		return false;

	if (is_timing_changed(stream_a, stream_b))
		return false;

	if (is_hdr_static_meta_changed(stream_a, stream_b))
		return false;

	if (stream_a->dpms_off != stream_b->dpms_off)
		return false;

	if (is_vsc_info_packet_changed(stream_a, stream_b))
		return false;

	return true;
}

/**
 * dc_is_stream_unchanged() - Compare two stream states for equivalence.
 *
 * Checks if there a difference between the two states
 * that would require a mode change.
 *
 * Does not compare cursor position or attributes.
 */
bool dc_is_stream_unchanged(
	struct dc_stream_state *old_stream, struct dc_stream_state *stream)
{

	if (!are_stream_backends_same(old_stream, stream))
		return false;

	return true;
}

/**
 * dc_is_stream_scaling_unchanged() - Compare scaling rectangles of two streams.
 */
bool dc_is_stream_scaling_unchanged(
	struct dc_stream_state *old_stream, struct dc_stream_state *stream)
{
	if (old_stream == stream)
		return true;

	if (old_stream == NULL || stream == NULL)
		return false;

	if (memcmp(&old_stream->src,
			&stream->src,
			sizeof(struct rect)) != 0)
		return false;

	if (memcmp(&old_stream->dst,
			&stream->dst,
			sizeof(struct rect)) != 0)
		return false;

	return true;
}

static void update_stream_engine_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct stream_encoder *stream_enc,
		bool acquired)
{
	int i;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] == stream_enc)
			res_ctx->is_stream_enc_acquired[i] = acquired;
	}
}

/* TODO: release audio object */
void update_audio_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct audio *audio,
		bool acquired)
{
	int i;
	for (i = 0; i < pool->audio_count; i++) {
		if (pool->audios[i] == audio)
			res_ctx->is_audio_acquired[i] = acquired;
	}
}

static int acquire_first_free_pipe(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream) {
			struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

			pipe_ctx->stream_res.tg = pool->timing_generators[i];
			pipe_ctx->plane_res.mi = pool->mis[i];
			pipe_ctx->plane_res.hubp = pool->hubps[i];
			pipe_ctx->plane_res.ipp = pool->ipps[i];
			pipe_ctx->plane_res.xfm = pool->transforms[i];
			pipe_ctx->plane_res.dpp = pool->dpps[i];
			pipe_ctx->stream_res.opp = pool->opps[i];
			if (pool->dpps[i])
				pipe_ctx->plane_res.mpcc_inst = pool->dpps[i]->inst;
			pipe_ctx->pipe_idx = i;


			pipe_ctx->stream = stream;
			return i;
		}
	}
	return -1;
}

static struct stream_encoder *find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;
	int j = -1;
	struct dc_link *link = stream->link;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
				pool->stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case */
			j = i;
			if (pool->stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return pool->stream_enc[i];
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

	if (j >= 0 && link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT)
		return pool->stream_enc[j];

	return NULL;
}

static struct audio *find_first_free_audio(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		enum engine_id id)
{
	int i;
	for (i = 0; i < pool->audio_count; i++) {
		if ((res_ctx->is_audio_acquired[i] == false) && (res_ctx->is_stream_enc_acquired[i] == true)) {
			/*we have enough audio endpoint, find the matching inst*/
			if (id != i)
				continue;

			return pool->audios[i];
		}
	}
	/*not found the matching one, first come first serve*/
	for (i = 0; i < pool->audio_count; i++) {
		if (res_ctx->is_audio_acquired[i] == false) {
			return pool->audios[i];
		}
	}
	return 0;
}

bool resource_is_stream_unchanged(
	struct dc_state *old_context, struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < old_context->stream_count; i++) {
		struct dc_stream_state *old_stream = old_context->streams[i];

		if (are_stream_backends_same(old_stream, stream))
				return true;
	}

	return false;
}

/**
 * dc_add_stream_to_ctx() - Add a new dc_stream_state to a dc_state.
 */
enum dc_status dc_add_stream_to_ctx(
		struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *stream)
{
	enum dc_status res;
	DC_LOGGER_INIT(dc->ctx->logger);

	if (new_ctx->stream_count >= dc->res_pool->timing_generator_count) {
		DC_LOG_WARNING("Max streams reached, can't add stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	new_ctx->streams[new_ctx->stream_count] = stream;
	dc_stream_retain(stream);
	new_ctx->stream_count++;

	res = dc->res_pool->funcs->add_stream_to_ctx(dc, new_ctx, stream);
	if (res != DC_OK)
		DC_LOG_WARNING("Adding stream %p to context failed with err %d!\n", stream, res);

	return res;
}

/**
 * dc_remove_stream_from_ctx() - Remove a stream from a dc_state.
 */
enum dc_status dc_remove_stream_from_ctx(
			struct dc *dc,
			struct dc_state *new_ctx,
			struct dc_stream_state *stream)
{
	int i;
	struct dc_context *dc_ctx = dc->ctx;
	struct pipe_ctx *del_pipe = NULL;

	/* Release primary pipe */
	for (i = 0; i < MAX_PIPES; i++) {
		if (new_ctx->res_ctx.pipe_ctx[i].stream == stream &&
				!new_ctx->res_ctx.pipe_ctx[i].top_pipe) {
			struct pipe_ctx *odm_pipe =
					dc_res_get_odm_bottom_pipe(&new_ctx->res_ctx.pipe_ctx[i]);

			del_pipe = &new_ctx->res_ctx.pipe_ctx[i];

			ASSERT(del_pipe->stream_res.stream_enc);
			update_stream_engine_usage(
					&new_ctx->res_ctx,
						dc->res_pool,
					del_pipe->stream_res.stream_enc,
					false);

			if (del_pipe->stream_res.audio)
				update_audio_usage(
					&new_ctx->res_ctx,
					dc->res_pool,
					del_pipe->stream_res.audio,
					false);

			resource_unreference_clock_source(&new_ctx->res_ctx,
							  dc->res_pool,
							  del_pipe->clock_source);

			if (dc->res_pool->funcs->remove_stream_from_ctx)
				dc->res_pool->funcs->remove_stream_from_ctx(dc, new_ctx, stream);

			memset(del_pipe, 0, sizeof(*del_pipe));
			if (odm_pipe)
				memset(odm_pipe, 0, sizeof(*odm_pipe));

			break;
		}
	}

	if (!del_pipe) {
		DC_ERROR("Pipe not found for stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	for (i = 0; i < new_ctx->stream_count; i++)
		if (new_ctx->streams[i] == stream)
			break;

	if (new_ctx->streams[i] != stream) {
		DC_ERROR("Context doesn't have stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	dc_stream_release(new_ctx->streams[i]);
	new_ctx->stream_count--;

	/* Trim back arrays */
	for (; i < new_ctx->stream_count; i++) {
		new_ctx->streams[i] = new_ctx->streams[i + 1];
		new_ctx->stream_status[i] = new_ctx->stream_status[i + 1];
	}

	new_ctx->streams[new_ctx->stream_count] = NULL;
	memset(
			&new_ctx->stream_status[new_ctx->stream_count],
			0,
			sizeof(new_ctx->stream_status[0]));

	return DC_OK;
}

static struct dc_stream_state *find_pll_sharable_stream(
		struct dc_stream_state *stream_needs_pll,
		struct dc_state *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream_has_pll = context->streams[i];

		/* We are looking for non dp, non virtual stream */
		if (resource_are_streams_timing_synchronizable(
			stream_needs_pll, stream_has_pll)
			&& !dc_is_dp_signal(stream_has_pll->signal)
			&& stream_has_pll->link->connector_signal
			!= SIGNAL_TYPE_VIRTUAL)
			return stream_has_pll;

	}

	return NULL;
}

static int get_norm_pix_clk(const struct dc_crtc_timing *timing)
{
	uint32_t pix_clk = timing->pix_clk_100hz;
	uint32_t normalized_pix_clk = pix_clk;

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pix_clk /= 2;
	if (timing->pixel_encoding != PIXEL_ENCODING_YCBCR422) {
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
	}
	return normalized_pix_clk;
}

static void calculate_phy_pix_clks(struct dc_stream_state *stream)
{
	/* update actual pixel clock on all streams */
	if (dc_is_hdmi_signal(stream->signal))
		stream->phy_pix_clk = get_norm_pix_clk(
			&stream->timing) / 10;
	else
		stream->phy_pix_clk =
			stream->timing.pix_clk_100hz / 10;

	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		stream->phy_pix_clk *= 2;
}

static int acquire_resource_from_hw_enabled_state(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct dc_link *link = stream->link;
	unsigned int inst;

	/* Check for enabled DIG to identify enabled display */
	if (!link->link_enc->funcs->is_dig_enabled(link->link_enc))
		return -1;

	/* Check for which front end is used by this encoder.
	 * Note the inst is 1 indexed, where 0 is undefined.
	 * Note that DIG_FE can source from different OTG but our
	 * current implementation always map 1-to-1, so this code makes
	 * the same assumption and doesn't check OTG source.
	 */
	inst = link->link_enc->funcs->get_dig_frontend(link->link_enc) - 1;

	/* Instance should be within the range of the pool */
	if (inst >= pool->pipe_count)
		return -1;

	if (!res_ctx->pipe_ctx[inst].stream) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[inst];

		pipe_ctx->stream_res.tg = pool->timing_generators[inst];
		pipe_ctx->plane_res.mi = pool->mis[inst];
		pipe_ctx->plane_res.hubp = pool->hubps[inst];
		pipe_ctx->plane_res.ipp = pool->ipps[inst];
		pipe_ctx->plane_res.xfm = pool->transforms[inst];
		pipe_ctx->plane_res.dpp = pool->dpps[inst];
		pipe_ctx->stream_res.opp = pool->opps[inst];
		if (pool->dpps[inst])
			pipe_ctx->plane_res.mpcc_inst = pool->dpps[inst]->inst;
		pipe_ctx->pipe_idx = inst;

		pipe_ctx->stream = stream;
		return inst;
	}

	return -1;
}

enum dc_status resource_map_pool_resources(
		const struct dc  *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	const struct resource_pool *pool = dc->res_pool;
	int i;
	struct dc_context *dc_ctx = dc->ctx;
	struct pipe_ctx *pipe_ctx = NULL;
	int pipe_idx = -1;
	struct dc_bios *dcb = dc->ctx->dc_bios;

	/* TODO Check if this is needed */
	/*if (!resource_is_stream_unchanged(old_context, stream)) {
			if (stream != NULL && old_context->streams[i] != NULL) {
				stream->bit_depth_params =
						old_context->streams[i]->bit_depth_params;
				stream->clamping = old_context->streams[i]->clamping;
				continue;
			}
		}
	*/

	calculate_phy_pix_clks(stream);

	/* TODO: Check Linux */
	if (dc->config.allow_seamless_boot_optimization &&
			!dcb->funcs->is_accelerated_mode(dcb)) {
		if (dc_validate_seamless_boot_timing(dc, stream->sink, &stream->timing))
			stream->apply_seamless_boot_optimization = true;
	}

	if (stream->apply_seamless_boot_optimization)
		pipe_idx = acquire_resource_from_hw_enabled_state(
				&context->res_ctx,
				pool,
				stream);

	if (pipe_idx < 0)
		/* acquire new resources */
		pipe_idx = acquire_first_free_pipe(&context->res_ctx, pool, stream);

#ifdef CONFIG_DRM_AMD_DC_DCN1_0
	if (pipe_idx < 0)
		pipe_idx = acquire_first_split_pipe(&context->res_ctx, pool, stream);
#endif

	if (pipe_idx < 0 || context->res_ctx.pipe_ctx[pipe_idx].stream_res.tg == NULL)
		return DC_NO_CONTROLLER_RESOURCE;

	pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];

	pipe_ctx->stream_res.stream_enc =
		find_first_free_match_stream_enc_for_link(
			&context->res_ctx, pool, stream);

	if (!pipe_ctx->stream_res.stream_enc)
		return DC_NO_STREAM_ENC_RESOURCE;

	update_stream_engine_usage(
		&context->res_ctx, pool,
		pipe_ctx->stream_res.stream_enc,
		true);

	/* TODO: Add check if ASIC support and EDID audio */
	if (!stream->converter_disable_audio &&
	    dc_is_audio_capable_signal(pipe_ctx->stream->signal) &&
	    stream->audio_info.mode_count) {
		pipe_ctx->stream_res.audio = find_first_free_audio(
		&context->res_ctx, pool, pipe_ctx->stream_res.stream_enc->id);

		/*
		 * Audio assigned in order first come first get.
		 * There are asics which has number of audio
		 * resources less then number of pipes
		 */
		if (pipe_ctx->stream_res.audio)
			update_audio_usage(&context->res_ctx, pool,
					   pipe_ctx->stream_res.audio, true);
	}

	/* Add ABM to the resource if on EDP */
	if (pipe_ctx->stream && dc_is_embedded_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.abm = pool->abm;

	for (i = 0; i < context->stream_count; i++)
		if (context->streams[i] == stream) {
			context->stream_status[i].primary_otg_inst = pipe_ctx->stream_res.tg->inst;
			context->stream_status[i].stream_enc_inst = pipe_ctx->stream_res.stream_enc->id;
			context->stream_status[i].audio_inst =
				pipe_ctx->stream_res.audio ? pipe_ctx->stream_res.audio->inst : -1;

			return DC_OK;
		}

	DC_ERROR("Stream %p not found in new ctx!\n", stream);
	return DC_ERROR_UNEXPECTED;
}

/**
 * dc_resource_state_copy_construct_current() - Creates a new dc_state from existing state
 * Is a shallow copy.  Increments refcounts on existing streams and planes.
 * @dc: copy out of dc->current_state
 * @dst_ctx: copy into this
 */
void dc_resource_state_copy_construct_current(
		const struct dc *dc,
		struct dc_state *dst_ctx)
{
	dc_resource_state_copy_construct(dc->current_state, dst_ctx);
}


void dc_resource_state_construct(
		const struct dc *dc,
		struct dc_state *dst_ctx)
{
	dst_ctx->clk_mgr = dc->res_pool->clk_mgr;
}

/**
 * dc_validate_global_state() - Determine if HW can support a given state
 * Checks HW resource availability and bandwidth requirement.
 * @dc: dc struct for this driver
 * @new_ctx: state to be validated
 * @fast_validate: set to true if only yes/no to support matters
 *
 * Return: DC_OK if the result can be programmed.  Otherwise, an error code.
 */
enum dc_status dc_validate_global_state(
		struct dc *dc,
		struct dc_state *new_ctx,
		bool fast_validate)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	int i, j;

	if (!new_ctx)
		return DC_ERROR_UNEXPECTED;

	if (dc->res_pool->funcs->validate_global) {
		result = dc->res_pool->funcs->validate_global(dc, new_ctx);
		if (result != DC_OK)
			return result;
	}

	for (i = 0; i < new_ctx->stream_count; i++) {
		struct dc_stream_state *stream = new_ctx->streams[i];

		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &new_ctx->res_ctx.pipe_ctx[j];

			if (pipe_ctx->stream != stream)
				continue;

			if (dc->res_pool->funcs->get_default_swizzle_mode &&
					pipe_ctx->plane_state &&
					pipe_ctx->plane_state->tiling_info.gfx9.swizzle == DC_SW_UNKNOWN) {
				result = dc->res_pool->funcs->get_default_swizzle_mode(pipe_ctx->plane_state);
				if (result != DC_OK)
					return result;
			}

			/* Switch to dp clock source only if there is
			 * no non dp stream that shares the same timing
			 * with the dp stream.
			 */
			if (dc_is_dp_signal(pipe_ctx->stream->signal) &&
				!find_pll_sharable_stream(stream, new_ctx)) {

				resource_unreference_clock_source(
						&new_ctx->res_ctx,
						dc->res_pool,
						pipe_ctx->clock_source);

				pipe_ctx->clock_source = dc->res_pool->dp_clock_source;
				resource_reference_clock_source(
						&new_ctx->res_ctx,
						dc->res_pool,
						 pipe_ctx->clock_source);
			}
		}
	}

	result = resource_build_scaling_params_for_context(dc, new_ctx);

	if (result == DC_OK)
		if (!dc->res_pool->funcs->validate_bandwidth(dc, new_ctx, fast_validate))
			result = DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

static void patch_gamut_packet_checksum(
		struct dc_info_packet *gamut_packet)
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
		struct dc_info_packet *info_packet,
		struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum dc_aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	bool itc = false;
	uint8_t itc_value = 0;
	uint8_t cn0_cn1 = 0;
	unsigned int cn0_cn1_value = 0;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;
	union hdmi_info_packet hdmi_info;
	union display_content_support support = {0};
	unsigned int vic = pipe_ctx->stream->timing.vic;
	enum dc_timing_3d_format format;

	memset(&hdmi_info, 0, sizeof(union hdmi_info_packet));

	color_space = pipe_ctx->stream->output_color_space;
	if (color_space == COLOR_SPACE_UNKNOWN)
		color_space = (stream->timing.pixel_encoding == PIXEL_ENCODING_RGB) ?
			COLOR_SPACE_SRGB:COLOR_SPACE_YCBCR709;

	/* Initialize header */
	hdmi_info.bits.header.info_frame_type = HDMI_INFOFRAME_TYPE_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	hdmi_info.bits.header.version = 2;
	hdmi_info.bits.header.length = HDMI_AVI_INFOFRAME_SIZE;

	/*
	 * IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	 * according to HDMI 2.0 spec (Section 10.1)
	 */

	switch (stream->timing.pixel_encoding) {
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
	hdmi_info.bits.Y0_Y1_Y2 = pixel_encoding;

	/* A0 = 1 Active Format Information valid */
	hdmi_info.bits.A0 = ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	hdmi_info.bits.B0_B1 = BAR_INFO_BOTH_VALID;

	hdmi_info.bits.SC0_SC1 = PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */
	/* TODO: un-hardcode scan type */
	scan_type = SCANNING_TYPE_UNDERSCAN;
	hdmi_info.bits.S0_S1 = scan_type;

	/* C0, C1 : Colorimetry */
	if (color_space == COLOR_SPACE_YCBCR709 ||
			color_space == COLOR_SPACE_YCBCR709_LIMITED)
		hdmi_info.bits.C0_C1 = COLORIMETRY_ITU709;
	else if (color_space == COLOR_SPACE_YCBCR601 ||
			color_space == COLOR_SPACE_YCBCR601_LIMITED)
		hdmi_info.bits.C0_C1 = COLORIMETRY_ITU601;
	else {
		hdmi_info.bits.C0_C1 = COLORIMETRY_NO_DATA;
	}
	if (color_space == COLOR_SPACE_2020_RGB_FULLRANGE ||
			color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE ||
			color_space == COLOR_SPACE_2020_YCBCR) {
		hdmi_info.bits.EC0_EC2 = COLORIMETRYEX_BT2020RGBYCBCR;
		hdmi_info.bits.C0_C1   = COLORIMETRY_EXTENDED;
	} else if (color_space == COLOR_SPACE_ADOBERGB) {
		hdmi_info.bits.EC0_EC2 = COLORIMETRYEX_ADOBERGB;
		hdmi_info.bits.C0_C1   = COLORIMETRY_EXTENDED;
	}

	/* TODO: un-hardcode aspect ratio */
	aspect = stream->timing.aspect_ratio;

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		hdmi_info.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	case ASPECT_RATIO_64_27:
	case ASPECT_RATIO_256_135:
	default:
		hdmi_info.bits.M0_M1 = 0;
	}

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	hdmi_info.bits.R0_R3 = ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	/* TODO: un-hardcode cn0_cn1 and itc */

	cn0_cn1 = 0;
	cn0_cn1_value = 0;

	itc = true;
	itc_value = 1;

	support = stream->content_support;

	if (itc) {
		if (!support.bits.valid_content_type) {
			cn0_cn1_value = 0;
		} else {
			if (cn0_cn1 == DISPLAY_CONTENT_TYPE_GRAPHICS) {
				if (support.bits.graphics_content == 1) {
					cn0_cn1_value = 0;
				}
			} else if (cn0_cn1 == DISPLAY_CONTENT_TYPE_PHOTO) {
				if (support.bits.photo_content == 1) {
					cn0_cn1_value = 1;
				} else {
					cn0_cn1_value = 0;
					itc_value = 0;
				}
			} else if (cn0_cn1 == DISPLAY_CONTENT_TYPE_CINEMA) {
				if (support.bits.cinema_content == 1) {
					cn0_cn1_value = 2;
				} else {
					cn0_cn1_value = 0;
					itc_value = 0;
				}
			} else if (cn0_cn1 == DISPLAY_CONTENT_TYPE_GAME) {
				if (support.bits.game_content == 1) {
					cn0_cn1_value = 3;
				} else {
					cn0_cn1_value = 0;
					itc_value = 0;
				}
			}
		}
		hdmi_info.bits.CN0_CN1 = cn0_cn1_value;
		hdmi_info.bits.ITC = itc_value;
	}

	/* TODO : We should handle YCC quantization */
	/* but we do not have matrix calculation */
	if (stream->qs_bit == 1 &&
			stream->qy_bit == 1) {
		if (color_space == COLOR_SPACE_SRGB ||
			color_space == COLOR_SPACE_2020_RGB_FULLRANGE) {
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_FULL_RANGE;
			hdmi_info.bits.YQ0_YQ1 = YYC_QUANTIZATION_FULL_RANGE;
		} else if (color_space == COLOR_SPACE_SRGB_LIMITED ||
					color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE) {
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_LIMITED_RANGE;
			hdmi_info.bits.YQ0_YQ1 = YYC_QUANTIZATION_LIMITED_RANGE;
		} else {
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_DEFAULT_RANGE;
			hdmi_info.bits.YQ0_YQ1 = YYC_QUANTIZATION_LIMITED_RANGE;
		}
	} else {
		hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_DEFAULT_RANGE;
		hdmi_info.bits.YQ0_YQ1   = YYC_QUANTIZATION_LIMITED_RANGE;
	}

	///VIC
	format = stream->timing.timing_3d_format;
	/*todo, add 3DStereo support*/
	if (format != TIMING_3D_FORMAT_NONE) {
		// Based on HDMI specs hdmi vic needs to be converted to cea vic when 3D is enabled
		switch (pipe_ctx->stream->timing.hdmi_vic) {
		case 1:
			vic = 95;
			break;
		case 2:
			vic = 94;
			break;
		case 3:
			vic = 93;
			break;
		case 4:
			vic = 98;
			break;
		default:
			break;
		}
	}
	hdmi_info.bits.VIC0_VIC7 = vic;

	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	hdmi_info.bits.PR0_PR3 = 0;

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	hdmi_info.bits.bar_top = stream->timing.v_border_top;
	hdmi_info.bits.bar_bottom = (stream->timing.v_total
			- stream->timing.v_border_bottom + 1);
	hdmi_info.bits.bar_left  = stream->timing.h_border_left;
	hdmi_info.bits.bar_right = (stream->timing.h_total
			- stream->timing.h_border_right + 1);

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum = &hdmi_info.packet_raw_data.sb[0];

	*check_sum = HDMI_INFOFRAME_TYPE_AVI + HDMI_AVI_INFOFRAME_SIZE + 2;

	for (byte_index = 1; byte_index <= HDMI_AVI_INFOFRAME_SIZE; byte_index++)
		*check_sum += hdmi_info.packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 = hdmi_info.packet_raw_data.hb0;
	info_packet->hb1 = hdmi_info.packet_raw_data.hb1;
	info_packet->hb2 = hdmi_info.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(hdmi_info.packet_raw_data.sb); byte_index++)
		info_packet->sb[byte_index] = hdmi_info.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

static void set_vendor_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* SPD info packet for FreeSync */

	/* Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!stream->vsp_infopacket.valid)
		return;

	*info_packet = stream->vsp_infopacket;
}

static void set_spd_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* SPD info packet for FreeSync */

	/* Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!stream->vrr_infopacket.valid)
		return;

	*info_packet = stream->vrr_infopacket;
}

static void set_dp_sdp_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* SPD info packet for custom sdp message */

	/* Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!stream->dpsdp_infopacket.valid)
		return;

	*info_packet = stream->dpsdp_infopacket;
}

static void set_hdr_static_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* HDR Static Metadata info packet for HDR10 */

	if (!stream->hdr_static_metadata.valid ||
			stream->use_dynamic_meta)
		return;

	*info_packet = stream->hdr_static_metadata;
}

static void set_vsc_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	if (!stream->vsc_infopacket.valid)
		return;

	*info_packet = stream->vsc_infopacket;
}

void dc_resource_state_destruct(struct dc_state *context)
{
	int i, j;

	for (i = 0; i < context->stream_count; i++) {
		for (j = 0; j < context->stream_status[i].plane_count; j++)
			dc_plane_state_release(
				context->stream_status[i].plane_states[j]);

		context->stream_status[i].plane_count = 0;
		dc_stream_release(context->streams[i]);
		context->streams[i] = NULL;
	}
}

void dc_resource_state_copy_construct(
		const struct dc_state *src_ctx,
		struct dc_state *dst_ctx)
{
	int i, j;
	struct kref refcount = dst_ctx->refcount;

	*dst_ctx = *src_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *cur_pipe = &dst_ctx->res_ctx.pipe_ctx[i];

		if (cur_pipe->top_pipe)
			cur_pipe->top_pipe =  &dst_ctx->res_ctx.pipe_ctx[cur_pipe->top_pipe->pipe_idx];

		if (cur_pipe->bottom_pipe)
			cur_pipe->bottom_pipe = &dst_ctx->res_ctx.pipe_ctx[cur_pipe->bottom_pipe->pipe_idx];

	}

	for (i = 0; i < dst_ctx->stream_count; i++) {
		dc_stream_retain(dst_ctx->streams[i]);
		for (j = 0; j < dst_ctx->stream_status[i].plane_count; j++)
			dc_plane_state_retain(
				dst_ctx->stream_status[i].plane_states[j]);
	}

	/* context refcount should not be overridden */
	dst_ctx->refcount = refcount;

}

struct clock_source *dc_resource_find_first_free_pll(
		struct resource_context *res_ctx,
		const struct resource_pool *pool)
{
	int i;

	for (i = 0; i < pool->clk_src_count; ++i) {
		if (res_ctx->clock_source_ref_count[i] == 0)
			return pool->clock_sources[i];
	}

	return NULL;
}

void resource_build_info_frame(struct pipe_ctx *pipe_ctx)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct encoder_info_frame *info = &pipe_ctx->stream_res.encoder_info_frame;

	/* default all packets to invalid */
	info->avi.valid = false;
	info->gamut.valid = false;
	info->vendor.valid = false;
	info->spd.valid = false;
	info->hdrsmd.valid = false;
	info->vsc.valid = false;
	info->dpsdp.valid = false;

	signal = pipe_ctx->stream->signal;

	/* HDMi and DP have different info packets*/
	if (dc_is_hdmi_signal(signal)) {
		set_avi_info_frame(&info->avi, pipe_ctx);

		set_vendor_info_packet(&info->vendor, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd, pipe_ctx->stream);

	} else if (dc_is_dp_signal(signal)) {
		set_vsc_info_packet(&info->vsc, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd, pipe_ctx->stream);

		set_dp_sdp_info_packet(&info->dpsdp, pipe_ctx->stream);
	}

	patch_gamut_packet_checksum(&info->gamut);
}

enum dc_status resource_map_clock_resources(
		const struct dc  *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	/* acquire new resources */
	const struct resource_pool *pool = dc->res_pool;
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(
				&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;

	if (dc_is_dp_signal(pipe_ctx->stream->signal)
		|| pipe_ctx->stream->signal == SIGNAL_TYPE_VIRTUAL)
		pipe_ctx->clock_source = pool->dp_clock_source;
	else {
		pipe_ctx->clock_source = NULL;

		if (!dc->config.disable_disp_pll_sharing)
			pipe_ctx->clock_source = resource_find_used_clk_src_for_sharing(
				&context->res_ctx,
				pipe_ctx);

		if (pipe_ctx->clock_source == NULL)
			pipe_ctx->clock_source =
				dc_resource_find_first_free_pll(
					&context->res_ctx,
					pool);
	}

	if (pipe_ctx->clock_source == NULL)
		return DC_NO_CLOCK_SOURCE_RESOURCE;

	resource_reference_clock_source(
		&context->res_ctx, pool,
		pipe_ctx->clock_source);

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
	if (!pipe_ctx_old->stream)
		return false;

	if (pipe_ctx_old->stream->sink != pipe_ctx->stream->sink)
		return true;

	if (pipe_ctx_old->stream->signal != pipe_ctx->stream->signal)
		return true;

	if (pipe_ctx_old->stream_res.audio != pipe_ctx->stream_res.audio)
		return true;

	if (pipe_ctx_old->clock_source != pipe_ctx->clock_source
			&& pipe_ctx_old->stream != pipe_ctx->stream)
		return true;

	if (pipe_ctx_old->stream_res.stream_enc != pipe_ctx->stream_res.stream_enc)
		return true;

	if (is_timing_changed(pipe_ctx_old->stream, pipe_ctx->stream))
		return true;

	if (is_hdr_static_meta_changed(pipe_ctx_old->stream, pipe_ctx->stream))
		return true;

	if (pipe_ctx_old->stream->dpms_off != pipe_ctx->stream->dpms_off)
		return true;

	if (is_vsc_info_packet_changed(pipe_ctx_old->stream, pipe_ctx->stream))
		return true;

	return false;
}

void resource_build_bit_depth_reduction_params(struct dc_stream_state *stream,
		struct bit_depth_reduction_params *fmt_bit_depth)
{
	enum dc_dither_option option = stream->dither_option;
	enum dc_pixel_encoding pixel_encoding =
			stream->timing.pixel_encoding;

	memset(fmt_bit_depth, 0, sizeof(*fmt_bit_depth));

	if (option == DITHER_OPTION_DEFAULT) {
		switch (stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			option = DITHER_OPTION_SPATIAL6;
			break;
		case COLOR_DEPTH_888:
			option = DITHER_OPTION_SPATIAL8;
			break;
		case COLOR_DEPTH_101010:
			option = DITHER_OPTION_SPATIAL10;
			break;
		default:
			option = DITHER_OPTION_DISABLE;
		}
	}

	if (option == DITHER_OPTION_DISABLE)
		return;

	if (option == DITHER_OPTION_TRUN6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 0;
	} else if (option == DITHER_OPTION_TRUN8 ||
			option == DITHER_OPTION_TRUN8_SPATIAL6 ||
			option == DITHER_OPTION_TRUN8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 1;
	} else if (option == DITHER_OPTION_TRUN10        ||
			option == DITHER_OPTION_TRUN10_SPATIAL6   ||
			option == DITHER_OPTION_TRUN10_SPATIAL8   ||
			option == DITHER_OPTION_TRUN10_FM8     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
	}

	/* special case - Formatter can only reduce by 4 bits at most.
	 * When reducing from 12 to 6 bits,
	 * HW recommends we use trunc with round mode
	 * (if we did nothing, trunc to 10 bits would be used)
	 * note that any 12->10 bit reduction is ignored prior to DCE8,
	 * as the input was 10 bits.
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
		fmt_bit_depth->flags.TRUNCATE_MODE = 1;
	}

	/* spatial dither
	 * note that spatial modes 1-3 are never used
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_TRUN10_SPATIAL6      ||
			option == DITHER_OPTION_TRUN8_SPATIAL6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 0;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL8_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL8_FM6        ||
			option == DITHER_OPTION_TRUN10_SPATIAL8      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 1;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL10_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL10 ||
			option == DITHER_OPTION_SPATIAL10_FM8 ||
			option == DITHER_OPTION_SPATIAL10_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 2;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	}

	if (option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL10) {
		fmt_bit_depth->flags.FRAME_RANDOM = 0;
	} else {
		fmt_bit_depth->flags.FRAME_RANDOM = 1;
	}

	//////////////////////
	//// temporal dither
	//////////////////////
	if (option == DITHER_OPTION_FM6           ||
			option == DITHER_OPTION_SPATIAL8_FM6     ||
			option == DITHER_OPTION_SPATIAL10_FM6     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN8_FM6      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 0;
	} else if (option == DITHER_OPTION_FM8        ||
			option == DITHER_OPTION_SPATIAL10_FM8  ||
			option == DITHER_OPTION_TRUN10_FM8) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 1;
	} else if (option == DITHER_OPTION_FM10) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 2;
	}

	fmt_bit_depth->pixel_encoding = pixel_encoding;
}

enum dc_status dc_validate_stream(struct dc *dc, struct dc_stream_state *stream)
{
	struct dc  *core_dc = dc;
	struct dc_link *link = stream->link;
	struct timing_generator *tg = core_dc->res_pool->timing_generators[0];
	enum dc_status res = DC_OK;

	calculate_phy_pix_clks(stream);

	if (!tg->funcs->validate_timing(tg, &stream->timing))
		res = DC_FAIL_CONTROLLER_VALIDATE;

	if (res == DC_OK) {
		if (!link->link_enc->funcs->validate_output_with_stream(
						link->link_enc, stream))
			res = DC_FAIL_ENC_VALIDATE;
	}

	/* TODO: validate audio ASIC caps, encoder */

	if (res == DC_OK)
		res = dc_link_validate_mode_timing(stream,
		      link,
		      &stream->timing);

	return res;
}

enum dc_status dc_validate_plane(struct dc *dc, const struct dc_plane_state *plane_state)
{
	enum dc_status res = DC_OK;

	/* TODO For now validates pixel format only */
	if (dc->res_pool->funcs->validate_plane)
		return dc->res_pool->funcs->validate_plane(plane_state, &dc->caps);

	return res;
}

unsigned int resource_pixel_format_to_bpp(enum surface_pixel_format format)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		return 8;
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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
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

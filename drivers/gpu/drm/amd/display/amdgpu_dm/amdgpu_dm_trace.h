/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdgpu_dm

#if !defined(_AMDGPU_DM_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDGPU_DM_TRACE_H_

#include <linux/tracepoint.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_atomic.h>

#include "dc/inc/core_types.h"

DECLARE_EVENT_CLASS(amdgpu_dc_reg_template,
		    TP_PROTO(unsigned long *count, uint32_t reg, uint32_t value),
		    TP_ARGS(count, reg, value),

		    TP_STRUCT__entry(
				     __field(uint32_t, reg)
				     __field(uint32_t, value)
		    ),

		    TP_fast_assign(
				   __entry->reg = reg;
				   __entry->value = value;
				   *count = *count + 1;
		    ),

		    TP_printk("reg=0x%08lx, value=0x%08lx",
			      (unsigned long)__entry->reg,
			      (unsigned long)__entry->value)
);

DEFINE_EVENT(amdgpu_dc_reg_template, amdgpu_dc_rreg,
	     TP_PROTO(unsigned long *count, uint32_t reg, uint32_t value),
	     TP_ARGS(count, reg, value));

DEFINE_EVENT(amdgpu_dc_reg_template, amdgpu_dc_wreg,
	     TP_PROTO(unsigned long *count, uint32_t reg, uint32_t value),
	     TP_ARGS(count, reg, value));

TRACE_EVENT(amdgpu_dc_performance,
	TP_PROTO(unsigned long read_count, unsigned long write_count,
		unsigned long *last_read, unsigned long *last_write,
		const char *func, unsigned int line),
	TP_ARGS(read_count, write_count, last_read, last_write, func, line),
	TP_STRUCT__entry(
			__field(uint32_t, reads)
			__field(uint32_t, writes)
			__field(uint32_t, read_delta)
			__field(uint32_t, write_delta)
			__string(func, func)
			__field(uint32_t, line)
			),
	TP_fast_assign(
			__entry->reads = read_count;
			__entry->writes = write_count;
			__entry->read_delta = read_count - *last_read;
			__entry->write_delta = write_count - *last_write;
			__assign_str(func, func);
			__entry->line = line;
			*last_read = read_count;
			*last_write = write_count;
			),
	TP_printk("%s:%d reads=%08ld (%08ld total), writes=%08ld (%08ld total)",
			__get_str(func), __entry->line,
			(unsigned long)__entry->read_delta,
			(unsigned long)__entry->reads,
			(unsigned long)__entry->write_delta,
			(unsigned long)__entry->writes)
);

TRACE_EVENT(amdgpu_dm_connector_atomic_check,
	    TP_PROTO(const struct drm_connector_state *state),
	    TP_ARGS(state),

	    TP_STRUCT__entry(
			     __field(uint32_t, conn_id)
			     __field(const struct drm_connector_state *, conn_state)
			     __field(const struct drm_atomic_state *, state)
			     __field(const struct drm_crtc_commit *, commit)
			     __field(uint32_t, crtc_id)
			     __field(uint32_t, best_encoder_id)
			     __field(enum drm_link_status, link_status)
			     __field(bool, self_refresh_aware)
			     __field(enum hdmi_picture_aspect, picture_aspect_ratio)
			     __field(unsigned int, content_type)
			     __field(unsigned int, hdcp_content_type)
			     __field(unsigned int, content_protection)
			     __field(unsigned int, scaling_mode)
			     __field(u32, colorspace)
			     __field(u8, max_requested_bpc)
			     __field(u8, max_bpc)
	    ),

	    TP_fast_assign(
			   __entry->conn_id = state->connector->base.id;
			   __entry->conn_state = state;
			   __entry->state = state->state;
			   __entry->commit = state->commit;
			   __entry->crtc_id = state->crtc ? state->crtc->base.id : 0;
			   __entry->best_encoder_id = state->best_encoder ?
						      state->best_encoder->base.id : 0;
			   __entry->link_status = state->link_status;
			   __entry->self_refresh_aware = state->self_refresh_aware;
			   __entry->picture_aspect_ratio = state->picture_aspect_ratio;
			   __entry->content_type = state->content_type;
			   __entry->hdcp_content_type = state->hdcp_content_type;
			   __entry->content_protection = state->content_protection;
			   __entry->scaling_mode = state->scaling_mode;
			   __entry->colorspace = state->colorspace;
			   __entry->max_requested_bpc = state->max_requested_bpc;
			   __entry->max_bpc = state->max_bpc;
	    ),

	    TP_printk("conn_id=%u conn_state=%p state=%p commit=%p crtc_id=%u "
		      "best_encoder_id=%u link_status=%d self_refresh_aware=%d "
		      "picture_aspect_ratio=%d content_type=%u "
		      "hdcp_content_type=%u content_protection=%u scaling_mode=%u "
		      "colorspace=%u max_requested_bpc=%u max_bpc=%u",
		      __entry->conn_id, __entry->conn_state, __entry->state,
		      __entry->commit, __entry->crtc_id, __entry->best_encoder_id,
		      __entry->link_status, __entry->self_refresh_aware,
		      __entry->picture_aspect_ratio, __entry->content_type,
		      __entry->hdcp_content_type, __entry->content_protection,
		      __entry->scaling_mode, __entry->colorspace,
		      __entry->max_requested_bpc, __entry->max_bpc)
);

TRACE_EVENT(amdgpu_dm_crtc_atomic_check,
	    TP_PROTO(const struct drm_crtc_state *state),
	    TP_ARGS(state),

	    TP_STRUCT__entry(
			     __field(const struct drm_atomic_state *, state)
			     __field(const struct drm_crtc_state *, crtc_state)
			     __field(const struct drm_crtc_commit *, commit)
			     __field(uint32_t, crtc_id)
			     __field(bool, enable)
			     __field(bool, active)
			     __field(bool, planes_changed)
			     __field(bool, mode_changed)
			     __field(bool, active_changed)
			     __field(bool, connectors_changed)
			     __field(bool, zpos_changed)
			     __field(bool, color_mgmt_changed)
			     __field(bool, no_vblank)
			     __field(bool, async_flip)
			     __field(bool, vrr_enabled)
			     __field(bool, self_refresh_active)
			     __field(u32, plane_mask)
			     __field(u32, connector_mask)
			     __field(u32, encoder_mask)
	    ),

	    TP_fast_assign(
			   __entry->state = state->state;
			   __entry->crtc_state = state;
			   __entry->crtc_id = state->crtc->base.id;
			   __entry->commit = state->commit;
			   __entry->enable = state->enable;
			   __entry->active = state->active;
			   __entry->planes_changed = state->planes_changed;
			   __entry->mode_changed = state->mode_changed;
			   __entry->active_changed = state->active_changed;
			   __entry->connectors_changed = state->connectors_changed;
			   __entry->zpos_changed = state->zpos_changed;
			   __entry->color_mgmt_changed = state->color_mgmt_changed;
			   __entry->no_vblank = state->no_vblank;
			   __entry->async_flip = state->async_flip;
			   __entry->vrr_enabled = state->vrr_enabled;
			   __entry->self_refresh_active = state->self_refresh_active;
			   __entry->plane_mask = state->plane_mask;
			   __entry->connector_mask = state->connector_mask;
			   __entry->encoder_mask = state->encoder_mask;
	    ),

	    TP_printk("crtc_id=%u crtc_state=%p state=%p commit=%p changed("
		      "planes=%d mode=%d active=%d conn=%d zpos=%d color_mgmt=%d) "
		      "state(enable=%d active=%d async_flip=%d vrr_enabled=%d "
		      "self_refresh_active=%d no_vblank=%d) mask(plane=%x conn=%x "
		      "enc=%x)",
		      __entry->crtc_id, __entry->crtc_state, __entry->state,
		      __entry->commit, __entry->planes_changed,
		      __entry->mode_changed, __entry->active_changed,
		      __entry->connectors_changed, __entry->zpos_changed,
		      __entry->color_mgmt_changed, __entry->enable, __entry->active,
		      __entry->async_flip, __entry->vrr_enabled,
		      __entry->self_refresh_active, __entry->no_vblank,
		      __entry->plane_mask, __entry->connector_mask,
		      __entry->encoder_mask)
);

DECLARE_EVENT_CLASS(amdgpu_dm_plane_state_template,
	    TP_PROTO(const struct drm_plane_state *state),
	    TP_ARGS(state),
	    TP_STRUCT__entry(
			     __field(uint32_t, plane_id)
			     __field(enum drm_plane_type, plane_type)
			     __field(const struct drm_plane_state *, plane_state)
			     __field(const struct drm_atomic_state *, state)
			     __field(uint32_t, crtc_id)
			     __field(uint32_t, fb_id)
			     __field(uint32_t, fb_format)
			     __field(uint8_t, fb_planes)
			     __field(uint64_t, fb_modifier)
			     __field(const struct dma_fence *, fence)
			     __field(int32_t, crtc_x)
			     __field(int32_t, crtc_y)
			     __field(uint32_t, crtc_w)
			     __field(uint32_t, crtc_h)
			     __field(uint32_t, src_x)
			     __field(uint32_t, src_y)
			     __field(uint32_t, src_w)
			     __field(uint32_t, src_h)
			     __field(u32, alpha)
			     __field(uint32_t, pixel_blend_mode)
			     __field(unsigned int, rotation)
			     __field(unsigned int, zpos)
			     __field(unsigned int, normalized_zpos)
			     __field(enum drm_color_encoding, color_encoding)
			     __field(enum drm_color_range, color_range)
			     __field(bool, visible)
	    ),

	    TP_fast_assign(
			   __entry->plane_id = state->plane->base.id;
			   __entry->plane_type = state->plane->type;
			   __entry->plane_state = state;
			   __entry->state = state->state;
			   __entry->crtc_id = state->crtc ? state->crtc->base.id : 0;
			   __entry->fb_id = state->fb ? state->fb->base.id : 0;
			   __entry->fb_format = state->fb ? state->fb->format->format : 0;
			   __entry->fb_planes = state->fb ? state->fb->format->num_planes : 0;
			   __entry->fb_modifier = state->fb ? state->fb->modifier : 0;
			   __entry->fence = state->fence;
			   __entry->crtc_x = state->crtc_x;
			   __entry->crtc_y = state->crtc_y;
			   __entry->crtc_w = state->crtc_w;
			   __entry->crtc_h = state->crtc_h;
			   __entry->src_x = state->src_x >> 16;
			   __entry->src_y = state->src_y >> 16;
			   __entry->src_w = state->src_w >> 16;
			   __entry->src_h = state->src_h >> 16;
			   __entry->alpha = state->alpha;
			   __entry->pixel_blend_mode = state->pixel_blend_mode;
			   __entry->rotation = state->rotation;
			   __entry->zpos = state->zpos;
			   __entry->normalized_zpos = state->normalized_zpos;
			   __entry->color_encoding = state->color_encoding;
			   __entry->color_range = state->color_range;
			   __entry->visible = state->visible;
	    ),

	    TP_printk("plane_id=%u plane_type=%d plane_state=%p state=%p "
		      "crtc_id=%u fb(id=%u fmt=%c%c%c%c planes=%u mod=%llu) "
		      "fence=%p crtc_x=%d crtc_y=%d crtc_w=%u crtc_h=%u "
		      "src_x=%u src_y=%u src_w=%u src_h=%u alpha=%u "
		      "pixel_blend_mode=%u rotation=%u zpos=%u "
		      "normalized_zpos=%u color_encoding=%d color_range=%d "
		      "visible=%d",
		      __entry->plane_id, __entry->plane_type, __entry->plane_state,
		      __entry->state, __entry->crtc_id, __entry->fb_id,
		      (__entry->fb_format & 0xff) ? (__entry->fb_format & 0xff) : 'N',
		      ((__entry->fb_format >> 8) & 0xff) ? ((__entry->fb_format >> 8) & 0xff) : 'O',
		      ((__entry->fb_format >> 16) & 0xff) ? ((__entry->fb_format >> 16) & 0xff) : 'N',
		      ((__entry->fb_format >> 24) & 0x7f) ? ((__entry->fb_format >> 24) & 0x7f) : 'E',
		      __entry->fb_planes,
		      __entry->fb_modifier, __entry->fence, __entry->crtc_x,
		      __entry->crtc_y, __entry->crtc_w, __entry->crtc_h,
		      __entry->src_x, __entry->src_y, __entry->src_w, __entry->src_h,
		      __entry->alpha, __entry->pixel_blend_mode, __entry->rotation,
		      __entry->zpos, __entry->normalized_zpos,
		      __entry->color_encoding, __entry->color_range,
		      __entry->visible)
);

DEFINE_EVENT(amdgpu_dm_plane_state_template, amdgpu_dm_plane_atomic_check,
	     TP_PROTO(const struct drm_plane_state *state),
	     TP_ARGS(state));

DEFINE_EVENT(amdgpu_dm_plane_state_template, amdgpu_dm_atomic_update_cursor,
	     TP_PROTO(const struct drm_plane_state *state),
	     TP_ARGS(state));

TRACE_EVENT(amdgpu_dm_atomic_state_template,
	    TP_PROTO(const struct drm_atomic_state *state),
	    TP_ARGS(state),

	    TP_STRUCT__entry(
			     __field(const struct drm_atomic_state *, state)
			     __field(bool, allow_modeset)
			     __field(bool, legacy_cursor_update)
			     __field(bool, async_update)
			     __field(bool, duplicated)
			     __field(int, num_connector)
			     __field(int, num_private_objs)
	    ),

	    TP_fast_assign(
			   __entry->state = state;
			   __entry->allow_modeset = state->allow_modeset;
			   __entry->legacy_cursor_update = state->legacy_cursor_update;
			   __entry->async_update = state->async_update;
			   __entry->duplicated = state->duplicated;
			   __entry->num_connector = state->num_connector;
			   __entry->num_private_objs = state->num_private_objs;
	    ),

	    TP_printk("state=%p allow_modeset=%d legacy_cursor_update=%d "
		      "async_update=%d duplicated=%d num_connector=%d "
		      "num_private_objs=%d",
		      __entry->state, __entry->allow_modeset, __entry->legacy_cursor_update,
		      __entry->async_update, __entry->duplicated, __entry->num_connector,
		      __entry->num_private_objs)
);

DEFINE_EVENT(amdgpu_dm_atomic_state_template, amdgpu_dm_atomic_commit_tail_begin,
	     TP_PROTO(const struct drm_atomic_state *state),
	     TP_ARGS(state));

DEFINE_EVENT(amdgpu_dm_atomic_state_template, amdgpu_dm_atomic_commit_tail_finish,
	     TP_PROTO(const struct drm_atomic_state *state),
	     TP_ARGS(state));

DEFINE_EVENT(amdgpu_dm_atomic_state_template, amdgpu_dm_atomic_check_begin,
	     TP_PROTO(const struct drm_atomic_state *state),
	     TP_ARGS(state));

TRACE_EVENT(amdgpu_dm_atomic_check_finish,
	    TP_PROTO(const struct drm_atomic_state *state, int res),
	    TP_ARGS(state, res),

	    TP_STRUCT__entry(
			     __field(const struct drm_atomic_state *, state)
			     __field(int, res)
			     __field(bool, async_update)
			     __field(bool, allow_modeset)
	    ),

	    TP_fast_assign(
			   __entry->state = state;
			   __entry->res = res;
			   __entry->async_update = state->async_update;
			   __entry->allow_modeset = state->allow_modeset;
	    ),

	    TP_printk("state=%p res=%d async_update=%d allow_modeset=%d",
		      __entry->state, __entry->res,
		      __entry->async_update, __entry->allow_modeset)
);

TRACE_EVENT(amdgpu_dm_dc_pipe_state,
	    TP_PROTO(int pipe_idx, const struct dc_plane_state *plane_state,
		     const struct dc_stream_state *stream,
		     const struct plane_resource *plane_res,
		     int update_flags),
	    TP_ARGS(pipe_idx, plane_state, stream, plane_res, update_flags),

	    TP_STRUCT__entry(
			     __field(int, pipe_idx)
			     __field(const void *, stream)
			     __field(int, stream_w)
			     __field(int, stream_h)
			     __field(int, dst_x)
			     __field(int, dst_y)
			     __field(int, dst_w)
			     __field(int, dst_h)
			     __field(int, src_x)
			     __field(int, src_y)
			     __field(int, src_w)
			     __field(int, src_h)
			     __field(int, clip_x)
			     __field(int, clip_y)
			     __field(int, clip_w)
			     __field(int, clip_h)
			     __field(int, recout_x)
			     __field(int, recout_y)
			     __field(int, recout_w)
			     __field(int, recout_h)
			     __field(int, viewport_x)
			     __field(int, viewport_y)
			     __field(int, viewport_w)
			     __field(int, viewport_h)
			     __field(int, flip_immediate)
			     __field(int, surface_pitch)
			     __field(int, format)
			     __field(int, swizzle)
			     __field(unsigned int, update_flags)
	),

	TP_fast_assign(
		       __entry->pipe_idx = pipe_idx;
		       __entry->stream = stream;
		       __entry->stream_w = stream->timing.h_addressable;
		       __entry->stream_h = stream->timing.v_addressable;
		       __entry->dst_x = plane_state->dst_rect.x;
		       __entry->dst_y = plane_state->dst_rect.y;
		       __entry->dst_w = plane_state->dst_rect.width;
		       __entry->dst_h = plane_state->dst_rect.height;
		       __entry->src_x = plane_state->src_rect.x;
		       __entry->src_y = plane_state->src_rect.y;
		       __entry->src_w = plane_state->src_rect.width;
		       __entry->src_h = plane_state->src_rect.height;
		       __entry->clip_x = plane_state->clip_rect.x;
		       __entry->clip_y = plane_state->clip_rect.y;
		       __entry->clip_w = plane_state->clip_rect.width;
		       __entry->clip_h = plane_state->clip_rect.height;
		       __entry->recout_x = plane_res->scl_data.recout.x;
		       __entry->recout_y = plane_res->scl_data.recout.y;
		       __entry->recout_w = plane_res->scl_data.recout.width;
		       __entry->recout_h = plane_res->scl_data.recout.height;
		       __entry->viewport_x = plane_res->scl_data.viewport.x;
		       __entry->viewport_y = plane_res->scl_data.viewport.y;
		       __entry->viewport_w = plane_res->scl_data.viewport.width;
		       __entry->viewport_h = plane_res->scl_data.viewport.height;
		       __entry->flip_immediate = plane_state->flip_immediate;
		       __entry->surface_pitch = plane_state->plane_size.surface_pitch;
		       __entry->format = plane_state->format;
		       __entry->swizzle = plane_state->tiling_info.gfx9.swizzle;
		       __entry->update_flags = update_flags;
	),
	TP_printk("pipe_idx=%d stream=%p rct(%d,%d) dst=(%d,%d,%d,%d) "
		  "src=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) recout=(%d,%d,%d,%d) "
		  "viewport=(%d,%d,%d,%d) flip_immediate=%d pitch=%d "
		  "format=%d swizzle=%d update_flags=%x",
		  __entry->pipe_idx,
		  __entry->stream,
		  __entry->stream_w,
		  __entry->stream_h,
		  __entry->dst_x,
		  __entry->dst_y,
		  __entry->dst_w,
		  __entry->dst_h,
		  __entry->src_x,
		  __entry->src_y,
		  __entry->src_w,
		  __entry->src_h,
		  __entry->clip_x,
		  __entry->clip_y,
		  __entry->clip_w,
		  __entry->clip_h,
		  __entry->recout_x,
		  __entry->recout_y,
		  __entry->recout_w,
		  __entry->recout_h,
		  __entry->viewport_x,
		  __entry->viewport_y,
		  __entry->viewport_w,
		  __entry->viewport_h,
		  __entry->flip_immediate,
		  __entry->surface_pitch,
		  __entry->format,
		  __entry->swizzle,
		  __entry->update_flags
	)
);

TRACE_EVENT(amdgpu_dm_dc_clocks_state,
	    TP_PROTO(const struct dc_clocks *clk),
	    TP_ARGS(clk),

	    TP_STRUCT__entry(
			     __field(int, dispclk_khz)
			     __field(int, dppclk_khz)
			     __field(int, disp_dpp_voltage_level_khz)
			     __field(int, dcfclk_khz)
			     __field(int, socclk_khz)
			     __field(int, dcfclk_deep_sleep_khz)
			     __field(int, fclk_khz)
			     __field(int, phyclk_khz)
			     __field(int, dramclk_khz)
			     __field(int, p_state_change_support)
			     __field(int, prev_p_state_change_support)
			     __field(int, pwr_state)
			     __field(int, dtm_level)
			     __field(int, max_supported_dppclk_khz)
			     __field(int, max_supported_dispclk_khz)
			     __field(int, bw_dppclk_khz)
			     __field(int, bw_dispclk_khz)
	    ),
	    TP_fast_assign(
			   __entry->dispclk_khz = clk->dispclk_khz;
			   __entry->dppclk_khz = clk->dppclk_khz;
			   __entry->dcfclk_khz = clk->dcfclk_khz;
			   __entry->socclk_khz = clk->socclk_khz;
			   __entry->dcfclk_deep_sleep_khz = clk->dcfclk_deep_sleep_khz;
			   __entry->fclk_khz = clk->fclk_khz;
			   __entry->phyclk_khz = clk->phyclk_khz;
			   __entry->dramclk_khz = clk->dramclk_khz;
			   __entry->p_state_change_support = clk->p_state_change_support;
			   __entry->prev_p_state_change_support = clk->prev_p_state_change_support;
			   __entry->pwr_state = clk->pwr_state;
			   __entry->prev_p_state_change_support = clk->prev_p_state_change_support;
			   __entry->dtm_level = clk->dtm_level;
			   __entry->max_supported_dppclk_khz = clk->max_supported_dppclk_khz;
			   __entry->max_supported_dispclk_khz = clk->max_supported_dispclk_khz;
			   __entry->bw_dppclk_khz = clk->bw_dppclk_khz;
			   __entry->bw_dispclk_khz = clk->bw_dispclk_khz;
	    ),
	    TP_printk("dispclk_khz=%d dppclk_khz=%d disp_dpp_voltage_level_khz=%d dcfclk_khz=%d socclk_khz=%d "
		      "dcfclk_deep_sleep_khz=%d fclk_khz=%d phyclk_khz=%d "
		      "dramclk_khz=%d p_state_change_support=%d "
		      "prev_p_state_change_support=%d pwr_state=%d prev_p_state_change_support=%d "
		      "dtm_level=%d max_supported_dppclk_khz=%d max_supported_dispclk_khz=%d "
		      "bw_dppclk_khz=%d bw_dispclk_khz=%d ",
		      __entry->dispclk_khz,
		      __entry->dppclk_khz,
		      __entry->disp_dpp_voltage_level_khz,
		      __entry->dcfclk_khz,
		      __entry->socclk_khz,
		      __entry->dcfclk_deep_sleep_khz,
		      __entry->fclk_khz,
		      __entry->phyclk_khz,
		      __entry->dramclk_khz,
		      __entry->p_state_change_support,
		      __entry->prev_p_state_change_support,
		      __entry->pwr_state,
		      __entry->prev_p_state_change_support,
		      __entry->dtm_level,
		      __entry->max_supported_dppclk_khz,
		      __entry->max_supported_dispclk_khz,
		      __entry->bw_dppclk_khz,
		      __entry->bw_dispclk_khz
	    )
);

TRACE_EVENT(amdgpu_dm_dce_clocks_state,
	    TP_PROTO(const struct dce_bw_output *clk),
	    TP_ARGS(clk),

	    TP_STRUCT__entry(
			     __field(bool, cpuc_state_change_enable)
			     __field(bool, cpup_state_change_enable)
			     __field(bool, stutter_mode_enable)
			     __field(bool, nbp_state_change_enable)
			     __field(bool, all_displays_in_sync)
			     __field(int, sclk_khz)
			     __field(int, sclk_deep_sleep_khz)
			     __field(int, yclk_khz)
			     __field(int, dispclk_khz)
			     __field(int, blackout_recovery_time_us)
	    ),
	    TP_fast_assign(
			   __entry->cpuc_state_change_enable = clk->cpuc_state_change_enable;
			   __entry->cpup_state_change_enable = clk->cpup_state_change_enable;
			   __entry->stutter_mode_enable = clk->stutter_mode_enable;
			   __entry->nbp_state_change_enable = clk->nbp_state_change_enable;
			   __entry->all_displays_in_sync = clk->all_displays_in_sync;
			   __entry->sclk_khz = clk->sclk_khz;
			   __entry->sclk_deep_sleep_khz = clk->sclk_deep_sleep_khz;
			   __entry->yclk_khz = clk->yclk_khz;
			   __entry->dispclk_khz = clk->dispclk_khz;
			   __entry->blackout_recovery_time_us = clk->blackout_recovery_time_us;
	    ),
	    TP_printk("cpuc_state_change_enable=%d cpup_state_change_enable=%d stutter_mode_enable=%d "
		      "nbp_state_change_enable=%d all_displays_in_sync=%d sclk_khz=%d sclk_deep_sleep_khz=%d "
		      "yclk_khz=%d dispclk_khz=%d blackout_recovery_time_us=%d",
		      __entry->cpuc_state_change_enable,
		      __entry->cpup_state_change_enable,
		      __entry->stutter_mode_enable,
		      __entry->nbp_state_change_enable,
		      __entry->all_displays_in_sync,
		      __entry->sclk_khz,
		      __entry->sclk_deep_sleep_khz,
		      __entry->yclk_khz,
		      __entry->dispclk_khz,
		      __entry->blackout_recovery_time_us
	    )
);

TRACE_EVENT(amdgpu_dmub_trace_high_irq,
	TP_PROTO(uint32_t trace_code, uint32_t tick_count, uint32_t param0,
		 uint32_t param1),
	TP_ARGS(trace_code, tick_count, param0, param1),
	TP_STRUCT__entry(
		__field(uint32_t, trace_code)
		__field(uint32_t, tick_count)
		__field(uint32_t, param0)
		__field(uint32_t, param1)
		),
	TP_fast_assign(
		__entry->trace_code = trace_code;
		__entry->tick_count = tick_count;
		__entry->param0 = param0;
		__entry->param1 = param1;
	),
	TP_printk("trace_code=%u tick_count=%u param0=%u param1=%u",
		  __entry->trace_code, __entry->tick_count,
		  __entry->param0, __entry->param1)
);

TRACE_EVENT(amdgpu_refresh_rate_track,
	TP_PROTO(int crtc_index, ktime_t refresh_rate_ns, uint32_t refresh_rate_hz),
	TP_ARGS(crtc_index, refresh_rate_ns, refresh_rate_hz),
	TP_STRUCT__entry(
		__field(int, crtc_index)
		__field(ktime_t, refresh_rate_ns)
		__field(uint32_t, refresh_rate_hz)
		),
	TP_fast_assign(
		__entry->crtc_index = crtc_index;
		__entry->refresh_rate_ns = refresh_rate_ns;
		__entry->refresh_rate_hz = refresh_rate_hz;
	),
	TP_printk("crtc_index=%d refresh_rate=%dHz (%lld)",
		  __entry->crtc_index,
		  __entry->refresh_rate_hz,
		  __entry->refresh_rate_ns)
);

TRACE_EVENT(dcn_fpu,
	    TP_PROTO(bool begin, const char *function, const int line, const int recursion_depth),
	    TP_ARGS(begin, function, line, recursion_depth),

	    TP_STRUCT__entry(
			     __field(bool, begin)
			     __field(const char *, function)
			     __field(int, line)
			     __field(int, recursion_depth)
	    ),
	    TP_fast_assign(
			   __entry->begin = begin;
			   __entry->function = function;
			   __entry->line = line;
			   __entry->recursion_depth = recursion_depth;
	    ),
	    TP_printk("%s: recursion_depth: %d: %s()+%d:",
		      __entry->begin ? "begin" : "end",
		      __entry->recursion_depth,
		      __entry->function,
		      __entry->line
	    )
);

#endif /* _AMDGPU_DM_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE amdgpu_dm_trace
#include <trace/define_trace.h>

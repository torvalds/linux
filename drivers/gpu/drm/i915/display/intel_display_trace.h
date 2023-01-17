/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2021 Intel Corporation
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i915

#if !defined(__INTEL_DISPLAY_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __INTEL_DISPLAY_TRACE_H__

#include <linux/string_helpers.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_crtc.h"
#include "intel_display_types.h"

#define __dev_name_i915(i915) dev_name((i915)->drm.dev)
#define __dev_name_kms(obj) dev_name((obj)->base.dev->dev)

TRACE_EVENT(intel_pipe_enable,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(enum pipe, pipe)
			     ),
	    TP_fast_assign(
			   struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
			   struct intel_crtc *it__;
			   __assign_str(dev, __dev_name_kms(crtc));
			   for_each_intel_crtc(&dev_priv->drm, it__) {
				   __entry->frame[it__->pipe] = intel_crtc_get_vblank_counter(it__);
				   __entry->scanline[it__->pipe] = intel_get_crtc_scanline(it__);
			   }
			   __entry->pipe = crtc->pipe;
			   ),

	    TP_printk("dev %s, pipe %c enable, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(intel_pipe_disable,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(enum pipe, pipe)
			     ),

	    TP_fast_assign(
			   struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
			   struct intel_crtc *it__;
			   __assign_str(dev, __dev_name_kms(crtc));
			   for_each_intel_crtc(&dev_priv->drm, it__) {
				   __entry->frame[it__->pipe] = intel_crtc_get_vblank_counter(it__);
				   __entry->scanline[it__->pipe] = intel_get_crtc_scanline(it__);
			   }
			   __entry->pipe = crtc->pipe;
			   ),

	    TP_printk("dev %s, pipe %c disable, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(intel_pipe_crc,
	    TP_PROTO(struct intel_crtc *crtc, const u32 *crcs),
	    TP_ARGS(crtc, crcs),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __array(u32, crcs, 5)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   memcpy(__entry->crcs, crcs, sizeof(__entry->crcs));
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u crc=%08x %08x %08x %08x %08x",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->crcs[0], __entry->crcs[1],
		      __entry->crcs[2], __entry->crcs[3],
		      __entry->crcs[4])
);

TRACE_EVENT(intel_cpu_fifo_underrun,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pipe),
	    TP_ARGS(dev_priv, pipe),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_i915(dev_priv))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			    struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_pch_fifo_underrun,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pch_transcoder),
	    TP_ARGS(dev_priv, pch_transcoder),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_i915(dev_priv))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   enum pipe pipe = pch_transcoder;
			   struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
			   __assign_str(dev, __dev_name_i915(dev_priv));
			   __entry->pipe = pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pch transcoder %c, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_memory_cxsr,
	    TP_PROTO(struct drm_i915_private *dev_priv, bool old, bool new),
	    TP_ARGS(dev_priv, old, new),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_i915(dev_priv))
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(bool, old)
			     __field(bool, new)
			     ),

	    TP_fast_assign(
			   struct intel_crtc *crtc;
			   __assign_str(dev, __dev_name_i915(dev_priv));
			   for_each_intel_crtc(&dev_priv->drm, crtc) {
				   __entry->frame[crtc->pipe] = intel_crtc_get_vblank_counter(crtc);
				   __entry->scanline[crtc->pipe] = intel_get_crtc_scanline(crtc);
			   }
			   __entry->old = old;
			   __entry->new = new;
			   ),

	    TP_printk("dev %s, cxsr %s->%s, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      __get_str(dev), str_on_off(__entry->old), str_on_off(__entry->new),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(g4x_wm,
	    TP_PROTO(struct intel_crtc *crtc, const struct g4x_wm_values *wm),
	    TP_ARGS(crtc, wm),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u16, primary)
			     __field(u16, sprite)
			     __field(u16, cursor)
			     __field(u16, sr_plane)
			     __field(u16, sr_cursor)
			     __field(u16, sr_fbc)
			     __field(u16, hpll_plane)
			     __field(u16, hpll_cursor)
			     __field(u16, hpll_fbc)
			     __field(bool, cxsr)
			     __field(bool, hpll)
			     __field(bool, fbc)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->primary = wm->pipe[crtc->pipe].plane[PLANE_PRIMARY];
			   __entry->sprite = wm->pipe[crtc->pipe].plane[PLANE_SPRITE0];
			   __entry->cursor = wm->pipe[crtc->pipe].plane[PLANE_CURSOR];
			   __entry->sr_plane = wm->sr.plane;
			   __entry->sr_cursor = wm->sr.cursor;
			   __entry->sr_fbc = wm->sr.fbc;
			   __entry->hpll_plane = wm->hpll.plane;
			   __entry->hpll_cursor = wm->hpll.cursor;
			   __entry->hpll_fbc = wm->hpll.fbc;
			   __entry->cxsr = wm->cxsr;
			   __entry->hpll = wm->hpll_en;
			   __entry->fbc = wm->fbc_en;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u, wm %d/%d/%d, sr %s/%d/%d/%d, hpll %s/%d/%d/%d, fbc %s",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->primary, __entry->sprite, __entry->cursor,
		      str_yes_no(__entry->cxsr), __entry->sr_plane, __entry->sr_cursor, __entry->sr_fbc,
		      str_yes_no(__entry->hpll), __entry->hpll_plane, __entry->hpll_cursor, __entry->hpll_fbc,
		      str_yes_no(__entry->fbc))
);

TRACE_EVENT(vlv_wm,
	    TP_PROTO(struct intel_crtc *crtc, const struct vlv_wm_values *wm),
	    TP_ARGS(crtc, wm),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, level)
			     __field(u32, cxsr)
			     __field(u32, primary)
			     __field(u32, sprite0)
			     __field(u32, sprite1)
			     __field(u32, cursor)
			     __field(u32, sr_plane)
			     __field(u32, sr_cursor)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->level = wm->level;
			   __entry->cxsr = wm->cxsr;
			   __entry->primary = wm->pipe[crtc->pipe].plane[PLANE_PRIMARY];
			   __entry->sprite0 = wm->pipe[crtc->pipe].plane[PLANE_SPRITE0];
			   __entry->sprite1 = wm->pipe[crtc->pipe].plane[PLANE_SPRITE1];
			   __entry->cursor = wm->pipe[crtc->pipe].plane[PLANE_CURSOR];
			   __entry->sr_plane = wm->sr.plane;
			   __entry->sr_cursor = wm->sr.cursor;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u, level=%d, cxsr=%d, wm %d/%d/%d/%d, sr %d/%d",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->level, __entry->cxsr,
		      __entry->primary, __entry->sprite0, __entry->sprite1, __entry->cursor,
		      __entry->sr_plane, __entry->sr_cursor)
);

TRACE_EVENT(vlv_fifo_size,
	    TP_PROTO(struct intel_crtc *crtc, u32 sprite0_start, u32 sprite1_start, u32 fifo_size),
	    TP_ARGS(crtc, sprite0_start, sprite1_start, fifo_size),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, sprite0_start)
			     __field(u32, sprite1_start)
			     __field(u32, fifo_size)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->sprite0_start = sprite0_start;
			   __entry->sprite1_start = sprite1_start;
			   __entry->fifo_size = fifo_size;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u, %d/%d/%d",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->sprite0_start, __entry->sprite1_start, __entry->fifo_size)
);

TRACE_EVENT(intel_plane_update_noarm,
	    TP_PROTO(struct intel_plane *plane, struct intel_crtc *crtc),
	    TP_ARGS(plane, crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __array(int, src, 4)
			     __array(int, dst, 4)
			     __string(name, plane->base.name)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name);
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   memcpy(__entry->src, &plane->base.state->src, sizeof(__entry->src));
			   memcpy(__entry->dst, &plane->base.state->dst, sizeof(__entry->dst));
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u, " DRM_RECT_FP_FMT " -> " DRM_RECT_FMT,
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline,
		      DRM_RECT_FP_ARG((const struct drm_rect *)__entry->src),
		      DRM_RECT_ARG((const struct drm_rect *)__entry->dst))
);

TRACE_EVENT(intel_plane_update_arm,
	    TP_PROTO(struct intel_plane *plane, struct intel_crtc *crtc),
	    TP_ARGS(plane, crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __array(int, src, 4)
			     __array(int, dst, 4)
			     __string(name, plane->base.name)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name);
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   memcpy(__entry->src, &plane->base.state->src, sizeof(__entry->src));
			   memcpy(__entry->dst, &plane->base.state->dst, sizeof(__entry->dst));
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u, " DRM_RECT_FP_FMT " -> " DRM_RECT_FMT,
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline,
		      DRM_RECT_FP_ARG((const struct drm_rect *)__entry->src),
		      DRM_RECT_ARG((const struct drm_rect *)__entry->dst))
);

TRACE_EVENT(intel_plane_disable_arm,
	    TP_PROTO(struct intel_plane *plane, struct intel_crtc *crtc),
	    TP_ARGS(plane, crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __string(name, plane->base.name)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name);
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_fbc_activate,
	    TP_PROTO(struct intel_plane *plane),
	    TP_ARGS(plane),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __string(name, plane->base.name)
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   struct intel_crtc *crtc = intel_crtc_for_pipe(to_i915(plane->base.dev),
									 plane->pipe);
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name)
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_fbc_deactivate,
	    TP_PROTO(struct intel_plane *plane),
	    TP_ARGS(plane),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __string(name, plane->base.name)
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   struct intel_crtc *crtc = intel_crtc_for_pipe(to_i915(plane->base.dev),
									 plane->pipe);
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name)
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_fbc_nuke,
	    TP_PROTO(struct intel_plane *plane),
	    TP_ARGS(plane),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(plane))
			     __string(name, plane->base.name)
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   struct intel_crtc *crtc = intel_crtc_for_pipe(to_i915(plane->base.dev),
									 plane->pipe);
			   __assign_str(dev, __dev_name_kms(plane));
			   __assign_str(name, plane->base.name)
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, plane %s, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe), __get_str(name),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_crtc_vblank_work_start,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_crtc_vblank_work_end,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_pipe_update_start,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, min)
			     __field(u32, max)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = intel_crtc_get_vblank_counter(crtc);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->min = crtc->debug.min_vbl;
			   __entry->max = crtc->debug.max_vbl;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->min, __entry->max)
);

TRACE_EVENT(intel_pipe_update_vblank_evaded,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, min)
			     __field(u32, max)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->debug.start_vbl_count;
			   __entry->scanline = crtc->debug.scanline_start;
			   __entry->min = crtc->debug.min_vbl;
			   __entry->max = crtc->debug.max_vbl;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline,
		      __entry->min, __entry->max)
);

TRACE_EVENT(intel_pipe_update_end,
	    TP_PROTO(struct intel_crtc *crtc, u32 frame, int scanline_end),
	    TP_ARGS(crtc, frame, scanline_end),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_kms(crtc))
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_kms(crtc));
			   __entry->pipe = crtc->pipe;
			   __entry->frame = frame;
			   __entry->scanline = scanline_end;
			   ),

	    TP_printk("dev %s, pipe %c, frame=%u, scanline=%u",
		      __get_str(dev), pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_frontbuffer_invalidate,
	    TP_PROTO(struct drm_i915_private *i915,
		     unsigned int frontbuffer_bits, unsigned int origin),
	    TP_ARGS(i915, frontbuffer_bits, origin),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_i915(i915))
			     __field(unsigned int, frontbuffer_bits)
			     __field(unsigned int, origin)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_i915(i915));
			   __entry->frontbuffer_bits = frontbuffer_bits;
			   __entry->origin = origin;
			   ),

	    TP_printk("dev %s, frontbuffer_bits=0x%08x, origin=%u",
		      __get_str(dev), __entry->frontbuffer_bits, __entry->origin)
);

TRACE_EVENT(intel_frontbuffer_flush,
	    TP_PROTO(struct drm_i915_private *i915,
		     unsigned int frontbuffer_bits, unsigned int origin),
	    TP_ARGS(i915, frontbuffer_bits, origin),

	    TP_STRUCT__entry(
			     __string(dev, __dev_name_i915(i915))
			     __field(unsigned int, frontbuffer_bits)
			     __field(unsigned int, origin)
			     ),

	    TP_fast_assign(
			   __assign_str(dev, __dev_name_i915(i915));
			   __entry->frontbuffer_bits = frontbuffer_bits;
			   __entry->origin = origin;
			   ),

	    TP_printk("dev %s, frontbuffer_bits=0x%08x, origin=%u",
		      __get_str(dev), __entry->frontbuffer_bits, __entry->origin)
);

#endif /* __INTEL_DISPLAY_TRACE_H__ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/i915/display
#define TRACE_INCLUDE_FILE intel_display_trace
#include <trace/define_trace.h>

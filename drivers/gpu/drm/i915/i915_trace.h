/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_I915_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _I915_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drm_drv.h>

#include "gt/intel_engine.h"

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_drv.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i915
#define TRACE_INCLUDE_FILE i915_trace

/* watermark/fifo updates */

TRACE_EVENT(intel_pipe_enable,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pipe),
	    TP_ARGS(dev_priv, pipe),

	    TP_STRUCT__entry(
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(enum pipe, pipe)
			     ),

	    TP_fast_assign(
			   enum pipe _pipe;
			   for_each_pipe(dev_priv, _pipe) {
				   __entry->frame[_pipe] =
					   dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm, _pipe);
				   __entry->scanline[_pipe] =
					   intel_get_crtc_scanline(intel_get_crtc_for_pipe(dev_priv, _pipe));
			   }
			   __entry->pipe = pipe;
			   ),

	    TP_printk("pipe %c enable, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      pipe_name(__entry->pipe),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(intel_pipe_disable,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pipe),
	    TP_ARGS(dev_priv, pipe),

	    TP_STRUCT__entry(
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(enum pipe, pipe)
			     ),

	    TP_fast_assign(
			   enum pipe _pipe;
			   for_each_pipe(dev_priv, _pipe) {
				   __entry->frame[_pipe] =
					   dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm, _pipe);
				   __entry->scanline[_pipe] =
					   intel_get_crtc_scanline(intel_get_crtc_for_pipe(dev_priv, _pipe));
			   }
			   __entry->pipe = pipe;
			   ),

	    TP_printk("pipe %c disable, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      pipe_name(__entry->pipe),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(intel_pipe_crc,
	    TP_PROTO(struct intel_crtc *crtc, const u32 *crcs),
	    TP_ARGS(crtc, crcs),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __array(u32, crcs, 5)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   memcpy(__entry->crcs, crcs, sizeof(__entry->crcs));
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u crc=%08x %08x %08x %08x %08x",
		      pipe_name(__entry->pipe), __entry->frame, __entry->scanline,
		      __entry->crcs[0], __entry->crcs[1], __entry->crcs[2],
		      __entry->crcs[3], __entry->crcs[4])
);

TRACE_EVENT(intel_cpu_fifo_underrun,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pipe),
	    TP_ARGS(dev_priv, pipe),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __entry->pipe = pipe;
			   __entry->frame = dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm, pipe);
			   __entry->scanline = intel_get_crtc_scanline(intel_get_crtc_for_pipe(dev_priv, pipe));
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u",
		      pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_pch_fifo_underrun,
	    TP_PROTO(struct drm_i915_private *dev_priv, enum pipe pch_transcoder),
	    TP_ARGS(dev_priv, pch_transcoder),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   enum pipe pipe = pch_transcoder;
			   __entry->pipe = pipe;
			   __entry->frame = dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm, pipe);
			   __entry->scanline = intel_get_crtc_scanline(intel_get_crtc_for_pipe(dev_priv, pipe));
			   ),

	    TP_printk("pch transcoder %c, frame=%u, scanline=%u",
		      pipe_name(__entry->pipe),
		      __entry->frame, __entry->scanline)
);

TRACE_EVENT(intel_memory_cxsr,
	    TP_PROTO(struct drm_i915_private *dev_priv, bool old, bool new),
	    TP_ARGS(dev_priv, old, new),

	    TP_STRUCT__entry(
			     __array(u32, frame, 3)
			     __array(u32, scanline, 3)
			     __field(bool, old)
			     __field(bool, new)
			     ),

	    TP_fast_assign(
			   enum pipe pipe;
			   for_each_pipe(dev_priv, pipe) {
				   __entry->frame[pipe] =
					   dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm, pipe);
				   __entry->scanline[pipe] =
					   intel_get_crtc_scanline(intel_get_crtc_for_pipe(dev_priv, pipe));
			   }
			   __entry->old = old;
			   __entry->new = new;
			   ),

	    TP_printk("%s->%s, pipe A: frame=%u, scanline=%u, pipe B: frame=%u, scanline=%u, pipe C: frame=%u, scanline=%u",
		      onoff(__entry->old), onoff(__entry->new),
		      __entry->frame[PIPE_A], __entry->scanline[PIPE_A],
		      __entry->frame[PIPE_B], __entry->scanline[PIPE_B],
		      __entry->frame[PIPE_C], __entry->scanline[PIPE_C])
);

TRACE_EVENT(g4x_wm,
	    TP_PROTO(struct intel_crtc *crtc, const struct g4x_wm_values *wm),
	    TP_ARGS(crtc, wm),

	    TP_STRUCT__entry(
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
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
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

	    TP_printk("pipe %c, frame=%u, scanline=%u, wm %d/%d/%d, sr %s/%d/%d/%d, hpll %s/%d/%d/%d, fbc %s",
		      pipe_name(__entry->pipe), __entry->frame, __entry->scanline,
		      __entry->primary, __entry->sprite, __entry->cursor,
		      yesno(__entry->cxsr), __entry->sr_plane, __entry->sr_cursor, __entry->sr_fbc,
		      yesno(__entry->hpll), __entry->hpll_plane, __entry->hpll_cursor, __entry->hpll_fbc,
		      yesno(__entry->fbc))
);

TRACE_EVENT(vlv_wm,
	    TP_PROTO(struct intel_crtc *crtc, const struct vlv_wm_values *wm),
	    TP_ARGS(crtc, wm),

	    TP_STRUCT__entry(
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
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
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

	    TP_printk("pipe %c, frame=%u, scanline=%u, level=%d, cxsr=%d, wm %d/%d/%d/%d, sr %d/%d",
		      pipe_name(__entry->pipe), __entry->frame,
		      __entry->scanline, __entry->level, __entry->cxsr,
		      __entry->primary, __entry->sprite0, __entry->sprite1, __entry->cursor,
		      __entry->sr_plane, __entry->sr_cursor)
);

TRACE_EVENT(vlv_fifo_size,
	    TP_PROTO(struct intel_crtc *crtc, u32 sprite0_start, u32 sprite1_start, u32 fifo_size),
	    TP_ARGS(crtc, sprite0_start, sprite1_start, fifo_size),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, sprite0_start)
			     __field(u32, sprite1_start)
			     __field(u32, fifo_size)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->sprite0_start = sprite0_start;
			   __entry->sprite1_start = sprite1_start;
			   __entry->fifo_size = fifo_size;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u, %d/%d/%d",
		      pipe_name(__entry->pipe), __entry->frame,
		      __entry->scanline, __entry->sprite0_start,
		      __entry->sprite1_start, __entry->fifo_size)
);

/* plane updates */

TRACE_EVENT(intel_update_plane,
	    TP_PROTO(struct drm_plane *plane, struct intel_crtc *crtc),
	    TP_ARGS(plane, crtc),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(const char *, name)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __array(int, src, 4)
			     __array(int, dst, 4)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->name = plane->name;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   memcpy(__entry->src, &plane->state->src, sizeof(__entry->src));
			   memcpy(__entry->dst, &plane->state->dst, sizeof(__entry->dst));
			   ),

	    TP_printk("pipe %c, plane %s, frame=%u, scanline=%u, " DRM_RECT_FP_FMT " -> " DRM_RECT_FMT,
		      pipe_name(__entry->pipe), __entry->name,
		      __entry->frame, __entry->scanline,
		      DRM_RECT_FP_ARG((const struct drm_rect *)__entry->src),
		      DRM_RECT_ARG((const struct drm_rect *)__entry->dst))
);

TRACE_EVENT(intel_disable_plane,
	    TP_PROTO(struct drm_plane *plane, struct intel_crtc *crtc),
	    TP_ARGS(plane, crtc),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(const char *, name)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->name = plane->name;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   ),

	    TP_printk("pipe %c, plane %s, frame=%u, scanline=%u",
		      pipe_name(__entry->pipe), __entry->name,
		      __entry->frame, __entry->scanline)
);

/* pipe updates */

TRACE_EVENT(i915_pipe_update_start,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, min)
			     __field(u32, max)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->base.dev->driver->get_vblank_counter(crtc->base.dev,
										       crtc->pipe);
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->min = crtc->debug.min_vbl;
			   __entry->max = crtc->debug.max_vbl;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      pipe_name(__entry->pipe), __entry->frame,
		       __entry->scanline, __entry->min, __entry->max)
);

TRACE_EVENT(i915_pipe_update_vblank_evaded,
	    TP_PROTO(struct intel_crtc *crtc),
	    TP_ARGS(crtc),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, min)
			     __field(u32, max)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = crtc->debug.start_vbl_count;
			   __entry->scanline = crtc->debug.scanline_start;
			   __entry->min = crtc->debug.min_vbl;
			   __entry->max = crtc->debug.max_vbl;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      pipe_name(__entry->pipe), __entry->frame,
		       __entry->scanline, __entry->min, __entry->max)
);

TRACE_EVENT(i915_pipe_update_end,
	    TP_PROTO(struct intel_crtc *crtc, u32 frame, int scanline_end),
	    TP_ARGS(crtc, frame, scanline_end),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = frame;
			   __entry->scanline = scanline_end;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u",
		      pipe_name(__entry->pipe), __entry->frame,
		      __entry->scanline)
);

/* object tracking */

TRACE_EVENT(i915_gem_object_create,
	    TP_PROTO(struct drm_i915_gem_object *obj),
	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u64, size)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->size = obj->base.size;
			   ),

	    TP_printk("obj=%p, size=0x%llx", __entry->obj, __entry->size)
);

TRACE_EVENT(i915_gem_shrink,
	    TP_PROTO(struct drm_i915_private *i915, unsigned long target, unsigned flags),
	    TP_ARGS(i915, target, flags),

	    TP_STRUCT__entry(
			     __field(int, dev)
			     __field(unsigned long, target)
			     __field(unsigned, flags)
			     ),

	    TP_fast_assign(
			   __entry->dev = i915->drm.primary->index;
			   __entry->target = target;
			   __entry->flags = flags;
			   ),

	    TP_printk("dev=%d, target=%lu, flags=%x",
		      __entry->dev, __entry->target, __entry->flags)
);

TRACE_EVENT(i915_vma_bind,
	    TP_PROTO(struct i915_vma *vma, unsigned flags),
	    TP_ARGS(vma, flags),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(struct i915_address_space *, vm)
			     __field(u64, offset)
			     __field(u64, size)
			     __field(unsigned, flags)
			     ),

	    TP_fast_assign(
			   __entry->obj = vma->obj;
			   __entry->vm = vma->vm;
			   __entry->offset = vma->node.start;
			   __entry->size = vma->node.size;
			   __entry->flags = flags;
			   ),

	    TP_printk("obj=%p, offset=0x%016llx size=0x%llx%s vm=%p",
		      __entry->obj, __entry->offset, __entry->size,
		      __entry->flags & PIN_MAPPABLE ? ", mappable" : "",
		      __entry->vm)
);

TRACE_EVENT(i915_vma_unbind,
	    TP_PROTO(struct i915_vma *vma),
	    TP_ARGS(vma),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(struct i915_address_space *, vm)
			     __field(u64, offset)
			     __field(u64, size)
			     ),

	    TP_fast_assign(
			   __entry->obj = vma->obj;
			   __entry->vm = vma->vm;
			   __entry->offset = vma->node.start;
			   __entry->size = vma->node.size;
			   ),

	    TP_printk("obj=%p, offset=0x%016llx size=0x%llx vm=%p",
		      __entry->obj, __entry->offset, __entry->size, __entry->vm)
);

TRACE_EVENT(i915_gem_object_pwrite,
	    TP_PROTO(struct drm_i915_gem_object *obj, u64 offset, u64 len),
	    TP_ARGS(obj, offset, len),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u64, offset)
			     __field(u64, len)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->offset = offset;
			   __entry->len = len;
			   ),

	    TP_printk("obj=%p, offset=0x%llx, len=0x%llx",
		      __entry->obj, __entry->offset, __entry->len)
);

TRACE_EVENT(i915_gem_object_pread,
	    TP_PROTO(struct drm_i915_gem_object *obj, u64 offset, u64 len),
	    TP_ARGS(obj, offset, len),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u64, offset)
			     __field(u64, len)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->offset = offset;
			   __entry->len = len;
			   ),

	    TP_printk("obj=%p, offset=0x%llx, len=0x%llx",
		      __entry->obj, __entry->offset, __entry->len)
);

TRACE_EVENT(i915_gem_object_fault,
	    TP_PROTO(struct drm_i915_gem_object *obj, u64 index, bool gtt, bool write),
	    TP_ARGS(obj, index, gtt, write),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u64, index)
			     __field(bool, gtt)
			     __field(bool, write)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->index = index;
			   __entry->gtt = gtt;
			   __entry->write = write;
			   ),

	    TP_printk("obj=%p, %s index=%llu %s",
		      __entry->obj,
		      __entry->gtt ? "GTT" : "CPU",
		      __entry->index,
		      __entry->write ? ", writable" : "")
);

DECLARE_EVENT_CLASS(i915_gem_object,
	    TP_PROTO(struct drm_i915_gem_object *obj),
	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   ),

	    TP_printk("obj=%p", __entry->obj)
);

DEFINE_EVENT(i915_gem_object, i915_gem_object_clflush,
	     TP_PROTO(struct drm_i915_gem_object *obj),
	     TP_ARGS(obj)
);

DEFINE_EVENT(i915_gem_object, i915_gem_object_destroy,
	    TP_PROTO(struct drm_i915_gem_object *obj),
	    TP_ARGS(obj)
);

TRACE_EVENT(i915_gem_evict,
	    TP_PROTO(struct i915_address_space *vm, u64 size, u64 align, unsigned int flags),
	    TP_ARGS(vm, size, align, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(struct i915_address_space *, vm)
			     __field(u64, size)
			     __field(u64, align)
			     __field(unsigned int, flags)
			    ),

	    TP_fast_assign(
			   __entry->dev = vm->i915->drm.primary->index;
			   __entry->vm = vm;
			   __entry->size = size;
			   __entry->align = align;
			   __entry->flags = flags;
			  ),

	    TP_printk("dev=%d, vm=%p, size=0x%llx, align=0x%llx %s",
		      __entry->dev, __entry->vm, __entry->size, __entry->align,
		      __entry->flags & PIN_MAPPABLE ? ", mappable" : "")
);

TRACE_EVENT(i915_gem_evict_node,
	    TP_PROTO(struct i915_address_space *vm, struct drm_mm_node *node, unsigned int flags),
	    TP_ARGS(vm, node, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(struct i915_address_space *, vm)
			     __field(u64, start)
			     __field(u64, size)
			     __field(unsigned long, color)
			     __field(unsigned int, flags)
			    ),

	    TP_fast_assign(
			   __entry->dev = vm->i915->drm.primary->index;
			   __entry->vm = vm;
			   __entry->start = node->start;
			   __entry->size = node->size;
			   __entry->color = node->color;
			   __entry->flags = flags;
			  ),

	    TP_printk("dev=%d, vm=%p, start=0x%llx size=0x%llx, color=0x%lx, flags=%x",
		      __entry->dev, __entry->vm,
		      __entry->start, __entry->size,
		      __entry->color, __entry->flags)
);

TRACE_EVENT(i915_gem_evict_vm,
	    TP_PROTO(struct i915_address_space *vm),
	    TP_ARGS(vm),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(struct i915_address_space *, vm)
			    ),

	    TP_fast_assign(
			   __entry->dev = vm->i915->drm.primary->index;
			   __entry->vm = vm;
			  ),

	    TP_printk("dev=%d, vm=%p", __entry->dev, __entry->vm)
);

TRACE_EVENT(i915_request_queue,
	    TP_PROTO(struct i915_request *rq, u32 flags),
	    TP_ARGS(rq, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, hw_id)
			     __field(u64, ctx)
			     __field(u16, class)
			     __field(u16, instance)
			     __field(u32, seqno)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->dev = rq->i915->drm.primary->index;
			   __entry->hw_id = rq->gem_context->hw_id;
			   __entry->class = rq->engine->uabi_class;
			   __entry->instance = rq->engine->instance;
			   __entry->ctx = rq->fence.context;
			   __entry->seqno = rq->fence.seqno;
			   __entry->flags = flags;
			   ),

	    TP_printk("dev=%u, engine=%u:%u, hw_id=%u, ctx=%llu, seqno=%u, flags=0x%x",
		      __entry->dev, __entry->class, __entry->instance,
		      __entry->hw_id, __entry->ctx, __entry->seqno,
		      __entry->flags)
);

DECLARE_EVENT_CLASS(i915_request,
	    TP_PROTO(struct i915_request *rq),
	    TP_ARGS(rq),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, hw_id)
			     __field(u64, ctx)
			     __field(u16, class)
			     __field(u16, instance)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = rq->i915->drm.primary->index;
			   __entry->hw_id = rq->gem_context->hw_id;
			   __entry->class = rq->engine->uabi_class;
			   __entry->instance = rq->engine->instance;
			   __entry->ctx = rq->fence.context;
			   __entry->seqno = rq->fence.seqno;
			   ),

	    TP_printk("dev=%u, engine=%u:%u, hw_id=%u, ctx=%llu, seqno=%u",
		      __entry->dev, __entry->class, __entry->instance,
		      __entry->hw_id, __entry->ctx, __entry->seqno)
);

DEFINE_EVENT(i915_request, i915_request_add,
	    TP_PROTO(struct i915_request *rq),
	    TP_ARGS(rq)
);

#if defined(CONFIG_DRM_I915_LOW_LEVEL_TRACEPOINTS)
DEFINE_EVENT(i915_request, i915_request_submit,
	     TP_PROTO(struct i915_request *rq),
	     TP_ARGS(rq)
);

DEFINE_EVENT(i915_request, i915_request_execute,
	     TP_PROTO(struct i915_request *rq),
	     TP_ARGS(rq)
);

TRACE_EVENT(i915_request_in,
	    TP_PROTO(struct i915_request *rq, unsigned int port),
	    TP_ARGS(rq, port),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, hw_id)
			     __field(u64, ctx)
			     __field(u16, class)
			     __field(u16, instance)
			     __field(u32, seqno)
			     __field(u32, port)
			     __field(u32, prio)
			    ),

	    TP_fast_assign(
			   __entry->dev = rq->i915->drm.primary->index;
			   __entry->hw_id = rq->gem_context->hw_id;
			   __entry->class = rq->engine->uabi_class;
			   __entry->instance = rq->engine->instance;
			   __entry->ctx = rq->fence.context;
			   __entry->seqno = rq->fence.seqno;
			   __entry->prio = rq->sched.attr.priority;
			   __entry->port = port;
			   ),

	    TP_printk("dev=%u, engine=%u:%u, hw_id=%u, ctx=%llu, seqno=%u, prio=%u, port=%u",
		      __entry->dev, __entry->class, __entry->instance,
		      __entry->hw_id, __entry->ctx, __entry->seqno,
		      __entry->prio, __entry->port)
);

TRACE_EVENT(i915_request_out,
	    TP_PROTO(struct i915_request *rq),
	    TP_ARGS(rq),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, hw_id)
			     __field(u64, ctx)
			     __field(u16, class)
			     __field(u16, instance)
			     __field(u32, seqno)
			     __field(u32, completed)
			    ),

	    TP_fast_assign(
			   __entry->dev = rq->i915->drm.primary->index;
			   __entry->hw_id = rq->gem_context->hw_id;
			   __entry->class = rq->engine->uabi_class;
			   __entry->instance = rq->engine->instance;
			   __entry->ctx = rq->fence.context;
			   __entry->seqno = rq->fence.seqno;
			   __entry->completed = i915_request_completed(rq);
			   ),

		    TP_printk("dev=%u, engine=%u:%u, hw_id=%u, ctx=%llu, seqno=%u, completed?=%u",
			      __entry->dev, __entry->class, __entry->instance,
			      __entry->hw_id, __entry->ctx, __entry->seqno,
			      __entry->completed)
);

#else
#if !defined(TRACE_HEADER_MULTI_READ)
static inline void
trace_i915_request_submit(struct i915_request *rq)
{
}

static inline void
trace_i915_request_execute(struct i915_request *rq)
{
}

static inline void
trace_i915_request_in(struct i915_request *rq, unsigned int port)
{
}

static inline void
trace_i915_request_out(struct i915_request *rq)
{
}
#endif
#endif

DEFINE_EVENT(i915_request, i915_request_retire,
	    TP_PROTO(struct i915_request *rq),
	    TP_ARGS(rq)
);

TRACE_EVENT(i915_request_wait_begin,
	    TP_PROTO(struct i915_request *rq, unsigned int flags),
	    TP_ARGS(rq, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, hw_id)
			     __field(u64, ctx)
			     __field(u16, class)
			     __field(u16, instance)
			     __field(u32, seqno)
			     __field(unsigned int, flags)
			     ),

	    /* NB: the blocking information is racy since mutex_is_locked
	     * doesn't check that the current thread holds the lock. The only
	     * other option would be to pass the boolean information of whether
	     * or not the class was blocking down through the stack which is
	     * less desirable.
	     */
	    TP_fast_assign(
			   __entry->dev = rq->i915->drm.primary->index;
			   __entry->hw_id = rq->gem_context->hw_id;
			   __entry->class = rq->engine->uabi_class;
			   __entry->instance = rq->engine->instance;
			   __entry->ctx = rq->fence.context;
			   __entry->seqno = rq->fence.seqno;
			   __entry->flags = flags;
			   ),

	    TP_printk("dev=%u, engine=%u:%u, hw_id=%u, ctx=%llu, seqno=%u, blocking=%u, flags=0x%x",
		      __entry->dev, __entry->class, __entry->instance,
		      __entry->hw_id, __entry->ctx, __entry->seqno,
		      !!(__entry->flags & I915_WAIT_LOCKED),
		      __entry->flags)
);

DEFINE_EVENT(i915_request, i915_request_wait_end,
	    TP_PROTO(struct i915_request *rq),
	    TP_ARGS(rq)
);

TRACE_EVENT_CONDITION(i915_reg_rw,
	TP_PROTO(bool write, i915_reg_t reg, u64 val, int len, bool trace),

	TP_ARGS(write, reg, val, len, trace),

	TP_CONDITION(trace),

	TP_STRUCT__entry(
		__field(u64, val)
		__field(u32, reg)
		__field(u16, write)
		__field(u16, len)
		),

	TP_fast_assign(
		__entry->val = (u64)val;
		__entry->reg = i915_mmio_reg_offset(reg);
		__entry->write = write;
		__entry->len = len;
		),

	TP_printk("%s reg=0x%x, len=%d, val=(0x%x, 0x%x)",
		__entry->write ? "write" : "read",
		__entry->reg, __entry->len,
		(u32)(__entry->val & 0xffffffff),
		(u32)(__entry->val >> 32))
);

TRACE_EVENT(intel_gpu_freq_change,
	    TP_PROTO(u32 freq),
	    TP_ARGS(freq),

	    TP_STRUCT__entry(
			     __field(u32, freq)
			     ),

	    TP_fast_assign(
			   __entry->freq = freq;
			   ),

	    TP_printk("new_freq=%u", __entry->freq)
);

/**
 * DOC: i915_ppgtt_create and i915_ppgtt_release tracepoints
 *
 * With full ppgtt enabled each process using drm will allocate at least one
 * translation table. With these traces it is possible to keep track of the
 * allocation and of the lifetime of the tables; this can be used during
 * testing/debug to verify that we are not leaking ppgtts.
 * These traces identify the ppgtt through the vm pointer, which is also printed
 * by the i915_vma_bind and i915_vma_unbind tracepoints.
 */
DECLARE_EVENT_CLASS(i915_ppgtt,
	TP_PROTO(struct i915_address_space *vm),
	TP_ARGS(vm),

	TP_STRUCT__entry(
			__field(struct i915_address_space *, vm)
			__field(u32, dev)
	),

	TP_fast_assign(
			__entry->vm = vm;
			__entry->dev = vm->i915->drm.primary->index;
	),

	TP_printk("dev=%u, vm=%p", __entry->dev, __entry->vm)
)

DEFINE_EVENT(i915_ppgtt, i915_ppgtt_create,
	TP_PROTO(struct i915_address_space *vm),
	TP_ARGS(vm)
);

DEFINE_EVENT(i915_ppgtt, i915_ppgtt_release,
	TP_PROTO(struct i915_address_space *vm),
	TP_ARGS(vm)
);

/**
 * DOC: i915_context_create and i915_context_free tracepoints
 *
 * These tracepoints are used to track creation and deletion of contexts.
 * If full ppgtt is enabled, they also print the address of the vm assigned to
 * the context.
 */
DECLARE_EVENT_CLASS(i915_context,
	TP_PROTO(struct i915_gem_context *ctx),
	TP_ARGS(ctx),

	TP_STRUCT__entry(
			__field(u32, dev)
			__field(struct i915_gem_context *, ctx)
			__field(u32, hw_id)
			__field(struct i915_address_space *, vm)
	),

	TP_fast_assign(
			__entry->dev = ctx->i915->drm.primary->index;
			__entry->ctx = ctx;
			__entry->hw_id = ctx->hw_id;
			__entry->vm = ctx->vm;
	),

	TP_printk("dev=%u, ctx=%p, ctx_vm=%p, hw_id=%u",
		  __entry->dev, __entry->ctx, __entry->vm, __entry->hw_id)
)

DEFINE_EVENT(i915_context, i915_context_create,
	TP_PROTO(struct i915_gem_context *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(i915_context, i915_context_free,
	TP_PROTO(struct i915_gem_context *ctx),
	TP_ARGS(ctx)
);

#endif /* _I915_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/i915
#include <trace/define_trace.h>

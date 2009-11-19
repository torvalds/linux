#if !defined(_I915_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _I915_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i915
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE i915_trace

/* object tracking */

TRACE_EVENT(i915_gem_object_create,

	    TP_PROTO(struct drm_gem_object *obj),

	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     __field(u32, size)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->size = obj->size;
			   ),

	    TP_printk("obj=%p, size=%u", __entry->obj, __entry->size)
);

TRACE_EVENT(i915_gem_object_bind,

	    TP_PROTO(struct drm_gem_object *obj, u32 gtt_offset),

	    TP_ARGS(obj, gtt_offset),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     __field(u32, gtt_offset)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->gtt_offset = gtt_offset;
			   ),

	    TP_printk("obj=%p, gtt_offset=%08x",
		      __entry->obj, __entry->gtt_offset)
);

TRACE_EVENT(i915_gem_object_clflush,

	    TP_PROTO(struct drm_gem_object *obj),

	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   ),

	    TP_printk("obj=%p", __entry->obj)
);

TRACE_EVENT(i915_gem_object_change_domain,

	    TP_PROTO(struct drm_gem_object *obj, uint32_t old_read_domains, uint32_t old_write_domain),

	    TP_ARGS(obj, old_read_domains, old_write_domain),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     __field(u32, read_domains)
			     __field(u32, write_domain)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->read_domains = obj->read_domains | (old_read_domains << 16);
			   __entry->write_domain = obj->write_domain | (old_write_domain << 16);
			   ),

	    TP_printk("obj=%p, read=%04x, write=%04x",
		      __entry->obj,
		      __entry->read_domains, __entry->write_domain)
);

TRACE_EVENT(i915_gem_object_get_fence,

	    TP_PROTO(struct drm_gem_object *obj, int fence, int tiling_mode),

	    TP_ARGS(obj, fence, tiling_mode),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     __field(int, fence)
			     __field(int, tiling_mode)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->fence = fence;
			   __entry->tiling_mode = tiling_mode;
			   ),

	    TP_printk("obj=%p, fence=%d, tiling=%d",
		      __entry->obj, __entry->fence, __entry->tiling_mode)
);

TRACE_EVENT(i915_gem_object_unbind,

	    TP_PROTO(struct drm_gem_object *obj),

	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   ),

	    TP_printk("obj=%p", __entry->obj)
);

TRACE_EVENT(i915_gem_object_destroy,

	    TP_PROTO(struct drm_gem_object *obj),

	    TP_ARGS(obj),

	    TP_STRUCT__entry(
			     __field(struct drm_gem_object *, obj)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   ),

	    TP_printk("obj=%p", __entry->obj)
);

/* batch tracing */

TRACE_EVENT(i915_gem_request_submit,

	    TP_PROTO(struct drm_device *dev, u32 seqno),

	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   i915_trace_irq_get(dev, seqno);
			   ),

	    TP_printk("dev=%u, seqno=%u", __entry->dev, __entry->seqno)
);

TRACE_EVENT(i915_gem_request_flush,

	    TP_PROTO(struct drm_device *dev, u32 seqno,
		     u32 flush_domains, u32 invalidate_domains),

	    TP_ARGS(dev, seqno, flush_domains, invalidate_domains),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     __field(u32, flush_domains)
			     __field(u32, invalidate_domains)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   __entry->flush_domains = flush_domains;
			   __entry->invalidate_domains = invalidate_domains;
			   ),

	    TP_printk("dev=%u, seqno=%u, flush=%04x, invalidate=%04x",
		      __entry->dev, __entry->seqno,
		      __entry->flush_domains, __entry->invalidate_domains)
);


TRACE_EVENT(i915_gem_request_complete,

	    TP_PROTO(struct drm_device *dev, u32 seqno),

	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%u", __entry->dev, __entry->seqno)
);

TRACE_EVENT(i915_gem_request_retire,

	    TP_PROTO(struct drm_device *dev, u32 seqno),

	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%u", __entry->dev, __entry->seqno)
);

TRACE_EVENT(i915_gem_request_wait_begin,

	    TP_PROTO(struct drm_device *dev, u32 seqno),

	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%u", __entry->dev, __entry->seqno)
);

TRACE_EVENT(i915_gem_request_wait_end,

	    TP_PROTO(struct drm_device *dev, u32 seqno),

	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%u", __entry->dev, __entry->seqno)
);

TRACE_EVENT(i915_ring_wait_begin,

	    TP_PROTO(struct drm_device *dev),

	    TP_ARGS(dev),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   ),

	    TP_printk("dev=%u", __entry->dev)
);

TRACE_EVENT(i915_ring_wait_end,

	    TP_PROTO(struct drm_device *dev),

	    TP_ARGS(dev),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   ),

	    TP_printk("dev=%u", __entry->dev)
);

#endif /* _I915_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/i915
#include <trace/define_trace.h>

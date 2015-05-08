#if !defined(_I915_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _I915_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_ringbuffer.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i915
#define TRACE_INCLUDE_FILE i915_trace

/* pipe updates */

TRACE_EVENT(i915_pipe_update_start,
	    TP_PROTO(struct intel_crtc *crtc, u32 min, u32 max),
	    TP_ARGS(crtc, min, max),

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
			   __entry->min = min;
			   __entry->max = max;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      pipe_name(__entry->pipe), __entry->frame,
		       __entry->scanline, __entry->min, __entry->max)
);

TRACE_EVENT(i915_pipe_update_vblank_evaded,
	    TP_PROTO(struct intel_crtc *crtc, u32 min, u32 max, u32 frame),
	    TP_ARGS(crtc, min, max, frame),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     __field(u32, min)
			     __field(u32, max)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = frame;
			   __entry->scanline = intel_get_crtc_scanline(crtc);
			   __entry->min = min;
			   __entry->max = max;
			   ),

	    TP_printk("pipe %c, frame=%u, scanline=%u, min=%u, max=%u",
		      pipe_name(__entry->pipe), __entry->frame,
		       __entry->scanline, __entry->min, __entry->max)
);

TRACE_EVENT(i915_pipe_update_end,
	    TP_PROTO(struct intel_crtc *crtc, u32 frame),
	    TP_ARGS(crtc, frame),

	    TP_STRUCT__entry(
			     __field(enum pipe, pipe)
			     __field(u32, frame)
			     __field(u32, scanline)
			     ),

	    TP_fast_assign(
			   __entry->pipe = crtc->pipe;
			   __entry->frame = frame;
			   __entry->scanline = intel_get_crtc_scanline(crtc);
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
			     __field(u32, size)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->size = obj->base.size;
			   ),

	    TP_printk("obj=%p, size=%u", __entry->obj, __entry->size)
);

TRACE_EVENT(i915_vma_bind,
	    TP_PROTO(struct i915_vma *vma, unsigned flags),
	    TP_ARGS(vma, flags),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(struct i915_address_space *, vm)
			     __field(u64, offset)
			     __field(u32, size)
			     __field(unsigned, flags)
			     ),

	    TP_fast_assign(
			   __entry->obj = vma->obj;
			   __entry->vm = vma->vm;
			   __entry->offset = vma->node.start;
			   __entry->size = vma->node.size;
			   __entry->flags = flags;
			   ),

	    TP_printk("obj=%p, offset=%016llx size=%x%s vm=%p",
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
			     __field(u32, size)
			     ),

	    TP_fast_assign(
			   __entry->obj = vma->obj;
			   __entry->vm = vma->vm;
			   __entry->offset = vma->node.start;
			   __entry->size = vma->node.size;
			   ),

	    TP_printk("obj=%p, offset=%016llx size=%x vm=%p",
		      __entry->obj, __entry->offset, __entry->size, __entry->vm)
);

#define VM_TO_TRACE_NAME(vm) \
	(i915_is_ggtt(vm) ? "G" : \
		      "P")

DECLARE_EVENT_CLASS(i915_va,
	TP_PROTO(struct i915_address_space *vm, u64 start, u64 length, const char *name),
	TP_ARGS(vm, start, length, name),

	TP_STRUCT__entry(
		__field(struct i915_address_space *, vm)
		__field(u64, start)
		__field(u64, end)
		__string(name, name)
	),

	TP_fast_assign(
		__entry->vm = vm;
		__entry->start = start;
		__entry->end = start + length - 1;
		__assign_str(name, name);
	),

	TP_printk("vm=%p (%s), 0x%llx-0x%llx",
		  __entry->vm, __get_str(name),  __entry->start, __entry->end)
);

DEFINE_EVENT(i915_va, i915_va_alloc,
	     TP_PROTO(struct i915_address_space *vm, u64 start, u64 length, const char *name),
	     TP_ARGS(vm, start, length, name)
);

DECLARE_EVENT_CLASS(i915_page_table_entry,
	TP_PROTO(struct i915_address_space *vm, u32 pde, u64 start, u64 pde_shift),
	TP_ARGS(vm, pde, start, pde_shift),

	TP_STRUCT__entry(
		__field(struct i915_address_space *, vm)
		__field(u32, pde)
		__field(u64, start)
		__field(u64, end)
	),

	TP_fast_assign(
		__entry->vm = vm;
		__entry->pde = pde;
		__entry->start = start;
		__entry->end = ((start + (1ULL << pde_shift)) & ~((1ULL << pde_shift)-1)) - 1;
	),

	TP_printk("vm=%p, pde=%d (0x%llx-0x%llx)",
		  __entry->vm, __entry->pde, __entry->start, __entry->end)
);

DEFINE_EVENT(i915_page_table_entry, i915_page_table_entry_alloc,
	     TP_PROTO(struct i915_address_space *vm, u32 pde, u64 start, u64 pde_shift),
	     TP_ARGS(vm, pde, start, pde_shift)
);

/* Avoid extra math because we only support two sizes. The format is defined by
 * bitmap_scnprintf. Each 32 bits is 8 HEX digits followed by comma */
#define TRACE_PT_SIZE(bits) \
	((((bits) == 1024) ? 288 : 144) + 1)

DECLARE_EVENT_CLASS(i915_page_table_entry_update,
	TP_PROTO(struct i915_address_space *vm, u32 pde,
		 struct i915_page_table *pt, u32 first, u32 count, u32 bits),
	TP_ARGS(vm, pde, pt, first, count, bits),

	TP_STRUCT__entry(
		__field(struct i915_address_space *, vm)
		__field(u32, pde)
		__field(u32, first)
		__field(u32, last)
		__dynamic_array(char, cur_ptes, TRACE_PT_SIZE(bits))
	),

	TP_fast_assign(
		__entry->vm = vm;
		__entry->pde = pde;
		__entry->first = first;
		__entry->last = first + count - 1;
		scnprintf(__get_str(cur_ptes),
			  TRACE_PT_SIZE(bits),
			  "%*pb",
			  bits,
			  pt->used_ptes);
	),

	TP_printk("vm=%p, pde=%d, updating %u:%u\t%s",
		  __entry->vm, __entry->pde, __entry->last, __entry->first,
		  __get_str(cur_ptes))
);

DEFINE_EVENT(i915_page_table_entry_update, i915_page_table_entry_map,
	TP_PROTO(struct i915_address_space *vm, u32 pde,
		 struct i915_page_table *pt, u32 first, u32 count, u32 bits),
	TP_ARGS(vm, pde, pt, first, count, bits)
);

TRACE_EVENT(i915_gem_object_change_domain,
	    TP_PROTO(struct drm_i915_gem_object *obj, u32 old_read, u32 old_write),
	    TP_ARGS(obj, old_read, old_write),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u32, read_domains)
			     __field(u32, write_domain)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->read_domains = obj->base.read_domains | (old_read << 16);
			   __entry->write_domain = obj->base.write_domain | (old_write << 16);
			   ),

	    TP_printk("obj=%p, read=%02x=>%02x, write=%02x=>%02x",
		      __entry->obj,
		      __entry->read_domains >> 16,
		      __entry->read_domains & 0xffff,
		      __entry->write_domain >> 16,
		      __entry->write_domain & 0xffff)
);

TRACE_EVENT(i915_gem_object_pwrite,
	    TP_PROTO(struct drm_i915_gem_object *obj, u32 offset, u32 len),
	    TP_ARGS(obj, offset, len),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u32, offset)
			     __field(u32, len)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->offset = offset;
			   __entry->len = len;
			   ),

	    TP_printk("obj=%p, offset=%u, len=%u",
		      __entry->obj, __entry->offset, __entry->len)
);

TRACE_EVENT(i915_gem_object_pread,
	    TP_PROTO(struct drm_i915_gem_object *obj, u32 offset, u32 len),
	    TP_ARGS(obj, offset, len),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u32, offset)
			     __field(u32, len)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->offset = offset;
			   __entry->len = len;
			   ),

	    TP_printk("obj=%p, offset=%u, len=%u",
		      __entry->obj, __entry->offset, __entry->len)
);

TRACE_EVENT(i915_gem_object_fault,
	    TP_PROTO(struct drm_i915_gem_object *obj, u32 index, bool gtt, bool write),
	    TP_ARGS(obj, index, gtt, write),

	    TP_STRUCT__entry(
			     __field(struct drm_i915_gem_object *, obj)
			     __field(u32, index)
			     __field(bool, gtt)
			     __field(bool, write)
			     ),

	    TP_fast_assign(
			   __entry->obj = obj;
			   __entry->index = index;
			   __entry->gtt = gtt;
			   __entry->write = write;
			   ),

	    TP_printk("obj=%p, %s index=%u %s",
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
	    TP_PROTO(struct drm_device *dev, u32 size, u32 align, unsigned flags),
	    TP_ARGS(dev, size, align, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, size)
			     __field(u32, align)
			     __field(unsigned, flags)
			    ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->size = size;
			   __entry->align = align;
			   __entry->flags = flags;
			  ),

	    TP_printk("dev=%d, size=%d, align=%d %s",
		      __entry->dev, __entry->size, __entry->align,
		      __entry->flags & PIN_MAPPABLE ? ", mappable" : "")
);

TRACE_EVENT(i915_gem_evict_everything,
	    TP_PROTO(struct drm_device *dev),
	    TP_ARGS(dev),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			    ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			  ),

	    TP_printk("dev=%d", __entry->dev)
);

TRACE_EVENT(i915_gem_evict_vm,
	    TP_PROTO(struct i915_address_space *vm),
	    TP_ARGS(vm),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(struct i915_address_space *, vm)
			    ),

	    TP_fast_assign(
			   __entry->dev = vm->dev->primary->index;
			   __entry->vm = vm;
			  ),

	    TP_printk("dev=%d, vm=%p", __entry->dev, __entry->vm)
);

TRACE_EVENT(i915_gem_ring_sync_to,
	    TP_PROTO(struct intel_engine_cs *from,
		     struct intel_engine_cs *to,
		     struct drm_i915_gem_request *req),
	    TP_ARGS(from, to, req),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, sync_from)
			     __field(u32, sync_to)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = from->dev->primary->index;
			   __entry->sync_from = from->id;
			   __entry->sync_to = to->id;
			   __entry->seqno = i915_gem_request_get_seqno(req);
			   ),

	    TP_printk("dev=%u, sync-from=%u, sync-to=%u, seqno=%u",
		      __entry->dev,
		      __entry->sync_from, __entry->sync_to,
		      __entry->seqno)
);

TRACE_EVENT(i915_gem_ring_dispatch,
	    TP_PROTO(struct drm_i915_gem_request *req, u32 flags),
	    TP_ARGS(req, flags),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, ring)
			     __field(u32, seqno)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   struct intel_engine_cs *ring =
						i915_gem_request_get_ring(req);
			   __entry->dev = ring->dev->primary->index;
			   __entry->ring = ring->id;
			   __entry->seqno = i915_gem_request_get_seqno(req);
			   __entry->flags = flags;
			   i915_trace_irq_get(ring, req);
			   ),

	    TP_printk("dev=%u, ring=%u, seqno=%u, flags=%x",
		      __entry->dev, __entry->ring, __entry->seqno, __entry->flags)
);

TRACE_EVENT(i915_gem_ring_flush,
	    TP_PROTO(struct intel_engine_cs *ring, u32 invalidate, u32 flush),
	    TP_ARGS(ring, invalidate, flush),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, ring)
			     __field(u32, invalidate)
			     __field(u32, flush)
			     ),

	    TP_fast_assign(
			   __entry->dev = ring->dev->primary->index;
			   __entry->ring = ring->id;
			   __entry->invalidate = invalidate;
			   __entry->flush = flush;
			   ),

	    TP_printk("dev=%u, ring=%x, invalidate=%04x, flush=%04x",
		      __entry->dev, __entry->ring,
		      __entry->invalidate, __entry->flush)
);

DECLARE_EVENT_CLASS(i915_gem_request,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, ring)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   struct intel_engine_cs *ring =
						i915_gem_request_get_ring(req);
			   __entry->dev = ring->dev->primary->index;
			   __entry->ring = ring->id;
			   __entry->seqno = i915_gem_request_get_seqno(req);
			   ),

	    TP_printk("dev=%u, ring=%u, seqno=%u",
		      __entry->dev, __entry->ring, __entry->seqno)
);

DEFINE_EVENT(i915_gem_request, i915_gem_request_add,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req)
);

TRACE_EVENT(i915_gem_request_notify,
	    TP_PROTO(struct intel_engine_cs *ring),
	    TP_ARGS(ring),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, ring)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = ring->dev->primary->index;
			   __entry->ring = ring->id;
			   __entry->seqno = ring->get_seqno(ring, false);
			   ),

	    TP_printk("dev=%u, ring=%u, seqno=%u",
		      __entry->dev, __entry->ring, __entry->seqno)
);

DEFINE_EVENT(i915_gem_request, i915_gem_request_retire,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req)
);

DEFINE_EVENT(i915_gem_request, i915_gem_request_complete,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req)
);

TRACE_EVENT(i915_gem_request_wait_begin,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, ring)
			     __field(u32, seqno)
			     __field(bool, blocking)
			     ),

	    /* NB: the blocking information is racy since mutex_is_locked
	     * doesn't check that the current thread holds the lock. The only
	     * other option would be to pass the boolean information of whether
	     * or not the class was blocking down through the stack which is
	     * less desirable.
	     */
	    TP_fast_assign(
			   struct intel_engine_cs *ring =
						i915_gem_request_get_ring(req);
			   __entry->dev = ring->dev->primary->index;
			   __entry->ring = ring->id;
			   __entry->seqno = i915_gem_request_get_seqno(req);
			   __entry->blocking =
				     mutex_is_locked(&ring->dev->struct_mutex);
			   ),

	    TP_printk("dev=%u, ring=%u, seqno=%u, blocking=%s",
		      __entry->dev, __entry->ring,
		      __entry->seqno, __entry->blocking ?  "yes (NB)" : "no")
);

DEFINE_EVENT(i915_gem_request, i915_gem_request_wait_end,
	    TP_PROTO(struct drm_i915_gem_request *req),
	    TP_ARGS(req)
);

TRACE_EVENT(i915_flip_request,
	    TP_PROTO(int plane, struct drm_i915_gem_object *obj),

	    TP_ARGS(plane, obj),

	    TP_STRUCT__entry(
		    __field(int, plane)
		    __field(struct drm_i915_gem_object *, obj)
		    ),

	    TP_fast_assign(
		    __entry->plane = plane;
		    __entry->obj = obj;
		    ),

	    TP_printk("plane=%d, obj=%p", __entry->plane, __entry->obj)
);

TRACE_EVENT(i915_flip_complete,
	    TP_PROTO(int plane, struct drm_i915_gem_object *obj),

	    TP_ARGS(plane, obj),

	    TP_STRUCT__entry(
		    __field(int, plane)
		    __field(struct drm_i915_gem_object *, obj)
		    ),

	    TP_fast_assign(
		    __entry->plane = plane;
		    __entry->obj = obj;
		    ),

	    TP_printk("plane=%d, obj=%p", __entry->plane, __entry->obj)
);

TRACE_EVENT_CONDITION(i915_reg_rw,
	TP_PROTO(bool write, u32 reg, u64 val, int len, bool trace),

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
		__entry->reg = reg;
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
			__entry->dev = vm->dev->primary->index;
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
	TP_PROTO(struct intel_context *ctx),
	TP_ARGS(ctx),

	TP_STRUCT__entry(
			__field(u32, dev)
			__field(struct intel_context *, ctx)
			__field(struct i915_address_space *, vm)
	),

	TP_fast_assign(
			__entry->ctx = ctx;
			__entry->vm = ctx->ppgtt ? &ctx->ppgtt->base : NULL;
			__entry->dev = ctx->file_priv->dev_priv->dev->primary->index;
	),

	TP_printk("dev=%u, ctx=%p, ctx_vm=%p",
		  __entry->dev, __entry->ctx, __entry->vm)
)

DEFINE_EVENT(i915_context, i915_context_create,
	TP_PROTO(struct intel_context *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(i915_context, i915_context_free,
	TP_PROTO(struct intel_context *ctx),
	TP_ARGS(ctx)
);

/**
 * DOC: switch_mm tracepoint
 *
 * This tracepoint allows tracking of the mm switch, which is an important point
 * in the lifetime of the vm in the legacy submission path. This tracepoint is
 * called only if full ppgtt is enabled.
 */
TRACE_EVENT(switch_mm,
	TP_PROTO(struct intel_engine_cs *ring, struct intel_context *to),

	TP_ARGS(ring, to),

	TP_STRUCT__entry(
			__field(u32, ring)
			__field(struct intel_context *, to)
			__field(struct i915_address_space *, vm)
			__field(u32, dev)
	),

	TP_fast_assign(
			__entry->ring = ring->id;
			__entry->to = to;
			__entry->vm = to->ppgtt? &to->ppgtt->base : NULL;
			__entry->dev = ring->dev->primary->index;
	),

	TP_printk("dev=%u, ring=%u, ctx=%p, ctx_vm=%p",
		  __entry->dev, __entry->ring, __entry->to, __entry->vm)
);

#endif /* _I915_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

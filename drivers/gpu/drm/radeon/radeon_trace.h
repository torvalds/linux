#if !defined(_RADEON_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _RADEON_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM radeon
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE radeon_trace

TRACE_EVENT(radeon_bo_create,
	    TP_PROTO(struct radeon_bo *bo),
	    TP_ARGS(bo),
	    TP_STRUCT__entry(
			     __field(struct radeon_bo *, bo)
			     __field(u32, pages)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo;
			   __entry->pages = bo->tbo.num_pages;
			   ),
	    TP_printk("bo=%p, pages=%u", __entry->bo, __entry->pages)
);

TRACE_EVENT(radeon_cs,
	    TP_PROTO(struct radeon_cs_parser *p),
	    TP_ARGS(p),
	    TP_STRUCT__entry(
			     __field(u32, ring)
			     __field(u32, dw)
			     __field(u32, fences)
			     ),

	    TP_fast_assign(
			   __entry->ring = p->ring;
			   __entry->dw = p->chunks[p->chunk_ib_idx].length_dw;
			   __entry->fences = radeon_fence_count_emitted(
				p->rdev, p->ring);
			   ),
	    TP_printk("ring=%u, dw=%u, fences=%u",
		      __entry->ring, __entry->dw,
		      __entry->fences)
);

TRACE_EVENT(radeon_vm_grab_id,
	    TP_PROTO(unsigned vmid, int ring),
	    TP_ARGS(vmid, ring),
	    TP_STRUCT__entry(
			     __field(u32, vmid)
			     __field(u32, ring)
			     ),

	    TP_fast_assign(
			   __entry->vmid = vmid;
			   __entry->ring = ring;
			   ),
	    TP_printk("vmid=%u, ring=%u", __entry->vmid, __entry->ring)
);

TRACE_EVENT(radeon_vm_bo_update,
	    TP_PROTO(struct radeon_bo_va *bo_va),
	    TP_ARGS(bo_va),
	    TP_STRUCT__entry(
			     __field(u64, soffset)
			     __field(u64, eoffset)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->soffset = bo_va->it.start;
			   __entry->eoffset = bo_va->it.last + 1;
			   __entry->flags = bo_va->flags;
			   ),
	    TP_printk("soffs=%010llx, eoffs=%010llx, flags=%08x",
		      __entry->soffset, __entry->eoffset, __entry->flags)
);

TRACE_EVENT(radeon_vm_set_page,
	    TP_PROTO(uint64_t pe, uint64_t addr, unsigned count,
		     uint32_t incr, uint32_t flags),
	    TP_ARGS(pe, addr, count, incr, flags),
	    TP_STRUCT__entry(
			     __field(u64, pe)
			     __field(u64, addr)
			     __field(u32, count)
			     __field(u32, incr)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->pe = pe;
			   __entry->addr = addr;
			   __entry->count = count;
			   __entry->incr = incr;
			   __entry->flags = flags;
			   ),
	    TP_printk("pe=%010Lx, addr=%010Lx, incr=%u, flags=%08x, count=%u",
		      __entry->pe, __entry->addr, __entry->incr,
		      __entry->flags, __entry->count)
);

TRACE_EVENT(radeon_vm_flush,
	    TP_PROTO(uint64_t pd_addr, unsigned ring, unsigned id),
	    TP_ARGS(pd_addr, ring, id),
	    TP_STRUCT__entry(
			     __field(u64, pd_addr)
			     __field(u32, ring)
			     __field(u32, id)
			     ),

	    TP_fast_assign(
			   __entry->pd_addr = pd_addr;
			   __entry->ring = ring;
			   __entry->id = id;
			   ),
	    TP_printk("pd_addr=%010Lx, ring=%u, id=%u",
		      __entry->pd_addr, __entry->ring, __entry->id)
);

DECLARE_EVENT_CLASS(radeon_fence_request,

	    TP_PROTO(struct drm_device *dev, int ring, u32 seqno),

	    TP_ARGS(dev, ring, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(int, ring)
			     __field(u32, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->ring = ring;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, ring=%d, seqno=%u",
		      __entry->dev, __entry->ring, __entry->seqno)
);

DEFINE_EVENT(radeon_fence_request, radeon_fence_emit,

	    TP_PROTO(struct drm_device *dev, int ring, u32 seqno),

	    TP_ARGS(dev, ring, seqno)
);

DEFINE_EVENT(radeon_fence_request, radeon_fence_wait_begin,

	    TP_PROTO(struct drm_device *dev, int ring, u32 seqno),

	    TP_ARGS(dev, ring, seqno)
);

DEFINE_EVENT(radeon_fence_request, radeon_fence_wait_end,

	    TP_PROTO(struct drm_device *dev, int ring, u32 seqno),

	    TP_ARGS(dev, ring, seqno)
);

DECLARE_EVENT_CLASS(radeon_semaphore_request,

	    TP_PROTO(int ring, struct radeon_semaphore *sem),

	    TP_ARGS(ring, sem),

	    TP_STRUCT__entry(
			     __field(int, ring)
			     __field(signed, waiters)
			     __field(uint64_t, gpu_addr)
			     ),

	    TP_fast_assign(
			   __entry->ring = ring;
			   __entry->waiters = sem->waiters;
			   __entry->gpu_addr = sem->gpu_addr;
			   ),

	    TP_printk("ring=%u, waiters=%d, addr=%010Lx", __entry->ring,
		      __entry->waiters, __entry->gpu_addr)
);

DEFINE_EVENT(radeon_semaphore_request, radeon_semaphore_signale,

	    TP_PROTO(int ring, struct radeon_semaphore *sem),

	    TP_ARGS(ring, sem)
);

DEFINE_EVENT(radeon_semaphore_request, radeon_semaphore_wait,

	    TP_PROTO(int ring, struct radeon_semaphore *sem),

	    TP_ARGS(ring, sem)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

#if !defined(_AMDGPU_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDGPU_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdgpu
#define TRACE_INCLUDE_FILE amdgpu_trace

TRACE_EVENT(amdgpu_bo_create,
	    TP_PROTO(struct amdgpu_bo *bo),
	    TP_ARGS(bo),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo *, bo)
			     __field(u32, pages)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo;
			   __entry->pages = bo->tbo.num_pages;
			   ),
	    TP_printk("bo=%p, pages=%u", __entry->bo, __entry->pages)
);

TRACE_EVENT(amdgpu_cs,
	    TP_PROTO(struct amdgpu_cs_parser *p, int i),
	    TP_ARGS(p, i),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo_list *, bo_list)
			     __field(u32, ring)
			     __field(u32, dw)
			     __field(u32, fences)
			     ),

	    TP_fast_assign(
			   __entry->bo_list = p->bo_list;
			   __entry->ring = p->job->ring->idx;
			   __entry->dw = p->job->ibs[i].length_dw;
			   __entry->fences = amdgpu_fence_count_emitted(
				p->job->ring);
			   ),
	    TP_printk("bo_list=%p, ring=%u, dw=%u, fences=%u",
		      __entry->bo_list, __entry->ring, __entry->dw,
		      __entry->fences)
);

TRACE_EVENT(amdgpu_cs_ioctl,
	    TP_PROTO(struct amdgpu_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_device *, adev)
			     __field(struct amd_sched_job *, sched_job)
			     __field(struct amdgpu_ib *, ib)
			     __field(struct fence *, fence)
			     __field(char *, ring_name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->adev = job->adev;
			   __entry->sched_job = &job->base;
			   __entry->ib = job->ibs;
			   __entry->fence = &job->base.s_fence->base;
			   __entry->ring_name = job->ring->name;
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("adev=%p, sched_job=%p, first ib=%p, sched fence=%p, ring name:%s, num_ibs:%u",
		      __entry->adev, __entry->sched_job, __entry->ib,
		      __entry->fence, __entry->ring_name, __entry->num_ibs)
);

TRACE_EVENT(amdgpu_sched_run_job,
	    TP_PROTO(struct amdgpu_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_device *, adev)
			     __field(struct amd_sched_job *, sched_job)
			     __field(struct amdgpu_ib *, ib)
			     __field(struct fence *, fence)
			     __field(char *, ring_name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->adev = job->adev;
			   __entry->sched_job = &job->base;
			   __entry->ib = job->ibs;
			   __entry->fence = &job->base.s_fence->base;
			   __entry->ring_name = job->ring->name;
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("adev=%p, sched_job=%p, first ib=%p, sched fence=%p, ring name:%s, num_ibs:%u",
		      __entry->adev, __entry->sched_job, __entry->ib,
		      __entry->fence, __entry->ring_name, __entry->num_ibs)
);


TRACE_EVENT(amdgpu_vm_grab_id,
	    TP_PROTO(struct amdgpu_vm *vm, int ring, unsigned vmid,
		     uint64_t pd_addr),
	    TP_ARGS(vm, ring, vmid, pd_addr),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_vm *, vm)
			     __field(u32, ring)
			     __field(u32, vmid)
			     __field(u64, pd_addr)
			     ),

	    TP_fast_assign(
			   __entry->vm = vm;
			   __entry->ring = ring;
			   __entry->vmid = vmid;
			   __entry->pd_addr = pd_addr;
			   ),
	    TP_printk("vm=%p, ring=%u, id=%u, pd_addr=%010Lx", __entry->vm,
		      __entry->ring, __entry->vmid, __entry->pd_addr)
);

TRACE_EVENT(amdgpu_vm_bo_map,
	    TP_PROTO(struct amdgpu_bo_va *bo_va,
		     struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(bo_va, mapping),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo *, bo)
			     __field(long, start)
			     __field(long, last)
			     __field(u64, offset)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo_va->bo;
			   __entry->start = mapping->it.start;
			   __entry->last = mapping->it.last;
			   __entry->offset = mapping->offset;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("bo=%p, start=%lx, last=%lx, offset=%010llx, flags=%08x",
		      __entry->bo, __entry->start, __entry->last,
		      __entry->offset, __entry->flags)
);

TRACE_EVENT(amdgpu_vm_bo_unmap,
	    TP_PROTO(struct amdgpu_bo_va *bo_va,
		     struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(bo_va, mapping),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo *, bo)
			     __field(long, start)
			     __field(long, last)
			     __field(u64, offset)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo_va->bo;
			   __entry->start = mapping->it.start;
			   __entry->last = mapping->it.last;
			   __entry->offset = mapping->offset;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("bo=%p, start=%lx, last=%lx, offset=%010llx, flags=%08x",
		      __entry->bo, __entry->start, __entry->last,
		      __entry->offset, __entry->flags)
);

DECLARE_EVENT_CLASS(amdgpu_vm_mapping,
	    TP_PROTO(struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(mapping),
	    TP_STRUCT__entry(
			     __field(u64, soffset)
			     __field(u64, eoffset)
			     __field(u32, flags)
			     ),

	    TP_fast_assign(
			   __entry->soffset = mapping->it.start;
			   __entry->eoffset = mapping->it.last + 1;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("soffs=%010llx, eoffs=%010llx, flags=%08x",
		      __entry->soffset, __entry->eoffset, __entry->flags)
);

DEFINE_EVENT(amdgpu_vm_mapping, amdgpu_vm_bo_update,
	    TP_PROTO(struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(mapping)
);

DEFINE_EVENT(amdgpu_vm_mapping, amdgpu_vm_bo_mapping,
	    TP_PROTO(struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(mapping)
);

TRACE_EVENT(amdgpu_vm_set_page,
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

TRACE_EVENT(amdgpu_vm_flush,
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
	    TP_printk("ring=%u, id=%u, pd_addr=%010Lx",
		      __entry->ring, __entry->id, __entry->pd_addr)
);

TRACE_EVENT(amdgpu_bo_list_set,
	    TP_PROTO(struct amdgpu_bo_list *list, struct amdgpu_bo *bo),
	    TP_ARGS(list, bo),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo_list *, list)
			     __field(struct amdgpu_bo *, bo)
			     ),

	    TP_fast_assign(
			   __entry->list = list;
			   __entry->bo = bo;
			   ),
	    TP_printk("list=%p, bo=%p", __entry->list, __entry->bo)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

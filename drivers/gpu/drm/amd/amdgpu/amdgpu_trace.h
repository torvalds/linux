/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 */

#if !defined(_AMDGPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDGPU_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdgpu
#define TRACE_INCLUDE_FILE amdgpu_trace

#define AMDGPU_JOB_GET_TIMELINE_NAME(job) \
	 job->base.s_fence->finished.ops->get_timeline_name(&job->base.s_fence->finished)

TRACE_EVENT(amdgpu_device_rreg,
	    TP_PROTO(unsigned did, uint32_t reg, uint32_t value),
	    TP_ARGS(did, reg, value),
	    TP_STRUCT__entry(
				__field(unsigned, did)
				__field(uint32_t, reg)
				__field(uint32_t, value)
			    ),
	    TP_fast_assign(
			   __entry->did = did;
			   __entry->reg = reg;
			   __entry->value = value;
			   ),
	    TP_printk("0x%04lx, 0x%08lx, 0x%08lx",
		      (unsigned long)__entry->did,
		      (unsigned long)__entry->reg,
		      (unsigned long)__entry->value)
);

TRACE_EVENT(amdgpu_device_wreg,
	    TP_PROTO(unsigned did, uint32_t reg, uint32_t value),
	    TP_ARGS(did, reg, value),
	    TP_STRUCT__entry(
				__field(unsigned, did)
				__field(uint32_t, reg)
				__field(uint32_t, value)
			    ),
	    TP_fast_assign(
			   __entry->did = did;
			   __entry->reg = reg;
			   __entry->value = value;
			   ),
	    TP_printk("0x%04lx, 0x%08lx, 0x%08lx",
		      (unsigned long)__entry->did,
		      (unsigned long)__entry->reg,
		      (unsigned long)__entry->value)
);

TRACE_EVENT(amdgpu_iv,
	    TP_PROTO(unsigned ih, struct amdgpu_iv_entry *iv),
	    TP_ARGS(ih, iv),
	    TP_STRUCT__entry(
			     __field(unsigned, ih)
			     __field(unsigned, client_id)
			     __field(unsigned, src_id)
			     __field(unsigned, ring_id)
			     __field(unsigned, vmid)
			     __field(unsigned, vmid_src)
			     __field(uint64_t, timestamp)
			     __field(unsigned, timestamp_src)
			     __field(unsigned, pasid)
			     __array(unsigned, src_data, 4)
			    ),
	    TP_fast_assign(
			   __entry->ih = ih;
			   __entry->client_id = iv->client_id;
			   __entry->src_id = iv->src_id;
			   __entry->ring_id = iv->ring_id;
			   __entry->vmid = iv->vmid;
			   __entry->vmid_src = iv->vmid_src;
			   __entry->timestamp = iv->timestamp;
			   __entry->timestamp_src = iv->timestamp_src;
			   __entry->pasid = iv->pasid;
			   __entry->src_data[0] = iv->src_data[0];
			   __entry->src_data[1] = iv->src_data[1];
			   __entry->src_data[2] = iv->src_data[2];
			   __entry->src_data[3] = iv->src_data[3];
			   ),
	    TP_printk("ih:%u client_id:%u src_id:%u ring:%u vmid:%u "
		      "timestamp: %llu pasid:%u src_data: %08x %08x %08x %08x",
		      __entry->ih, __entry->client_id, __entry->src_id,
		      __entry->ring_id, __entry->vmid,
		      __entry->timestamp, __entry->pasid,
		      __entry->src_data[0], __entry->src_data[1],
		      __entry->src_data[2], __entry->src_data[3])
);


TRACE_EVENT(amdgpu_bo_create,
	    TP_PROTO(struct amdgpu_bo *bo),
	    TP_ARGS(bo),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo *, bo)
			     __field(u32, pages)
			     __field(u32, type)
			     __field(u32, prefer)
			     __field(u32, allow)
			     __field(u32, visible)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo;
			   __entry->pages = PFN_UP(bo->tbo.resource->size);
			   __entry->type = bo->tbo.resource->mem_type;
			   __entry->prefer = bo->preferred_domains;
			   __entry->allow = bo->allowed_domains;
			   __entry->visible = bo->flags;
			   ),

	    TP_printk("bo=%p, pages=%u, type=%d, preferred=%d, allowed=%d, visible=%d",
		       __entry->bo, __entry->pages, __entry->type,
		       __entry->prefer, __entry->allow, __entry->visible)
);

TRACE_EVENT(amdgpu_cs,
	    TP_PROTO(struct amdgpu_cs_parser *p,
		     struct amdgpu_job *job,
		     struct amdgpu_ib *ib),
	    TP_ARGS(p, job, ib),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo_list *, bo_list)
			     __field(u32, ring)
			     __field(u32, dw)
			     __field(u32, fences)
			     ),

	    TP_fast_assign(
			   __entry->bo_list = p->bo_list;
			   __entry->ring = to_amdgpu_ring(job->base.entity->rq->sched)->idx;
			   __entry->dw = ib->length_dw;
			   __entry->fences = amdgpu_fence_count_emitted(
				to_amdgpu_ring(job->base.entity->rq->sched));
			   ),
	    TP_printk("bo_list=%p, ring=%u, dw=%u, fences=%u",
		      __entry->bo_list, __entry->ring, __entry->dw,
		      __entry->fences)
);

TRACE_EVENT(amdgpu_cs_ioctl,
	    TP_PROTO(struct amdgpu_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(
			     __field(uint64_t, sched_job_id)
			     __string(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			     __field(unsigned int, context)
			     __field(unsigned int, seqno)
			     __field(struct dma_fence *, fence)
			     __string(ring, to_amdgpu_ring(job->base.sched)->name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->sched_job_id = job->base.id;
			   __assign_str(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job));
			   __entry->context = job->base.s_fence->finished.context;
			   __entry->seqno = job->base.s_fence->finished.seqno;
			   __assign_str(ring, to_amdgpu_ring(job->base.sched)->name);
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("sched_job=%llu, timeline=%s, context=%u, seqno=%u, ring_name=%s, num_ibs=%u",
		      __entry->sched_job_id, __get_str(timeline), __entry->context,
		      __entry->seqno, __get_str(ring), __entry->num_ibs)
);

TRACE_EVENT(amdgpu_sched_run_job,
	    TP_PROTO(struct amdgpu_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(
			     __field(uint64_t, sched_job_id)
			     __string(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			     __field(unsigned int, context)
			     __field(unsigned int, seqno)
			     __string(ring, to_amdgpu_ring(job->base.sched)->name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->sched_job_id = job->base.id;
			   __assign_str(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job));
			   __entry->context = job->base.s_fence->finished.context;
			   __entry->seqno = job->base.s_fence->finished.seqno;
			   __assign_str(ring, to_amdgpu_ring(job->base.sched)->name);
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("sched_job=%llu, timeline=%s, context=%u, seqno=%u, ring_name=%s, num_ibs=%u",
		      __entry->sched_job_id, __get_str(timeline), __entry->context,
		      __entry->seqno, __get_str(ring), __entry->num_ibs)
);


TRACE_EVENT(amdgpu_vm_grab_id,
	    TP_PROTO(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_job *job),
	    TP_ARGS(vm, ring, job),
	    TP_STRUCT__entry(
			     __field(u32, pasid)
			     __string(ring, ring->name)
			     __field(u32, ring)
			     __field(u32, vmid)
			     __field(u32, vm_hub)
			     __field(u64, pd_addr)
			     __field(u32, needs_flush)
			     ),

	    TP_fast_assign(
			   __entry->pasid = vm->pasid;
			   __assign_str(ring, ring->name);
			   __entry->vmid = job->vmid;
			   __entry->vm_hub = ring->vm_hub,
			   __entry->pd_addr = job->vm_pd_addr;
			   __entry->needs_flush = job->vm_needs_flush;
			   ),
	    TP_printk("pasid=%d, ring=%s, id=%u, hub=%u, pd_addr=%010Lx needs_flush=%u",
		      __entry->pasid, __get_str(ring), __entry->vmid,
		      __entry->vm_hub, __entry->pd_addr, __entry->needs_flush)
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
			     __field(u64, flags)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo_va ? bo_va->base.bo : NULL;
			   __entry->start = mapping->start;
			   __entry->last = mapping->last;
			   __entry->offset = mapping->offset;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("bo=%p, start=%lx, last=%lx, offset=%010llx, flags=%llx",
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
			     __field(u64, flags)
			     ),

	    TP_fast_assign(
			   __entry->bo = bo_va ? bo_va->base.bo : NULL;
			   __entry->start = mapping->start;
			   __entry->last = mapping->last;
			   __entry->offset = mapping->offset;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("bo=%p, start=%lx, last=%lx, offset=%010llx, flags=%llx",
		      __entry->bo, __entry->start, __entry->last,
		      __entry->offset, __entry->flags)
);

DECLARE_EVENT_CLASS(amdgpu_vm_mapping,
	    TP_PROTO(struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(mapping),
	    TP_STRUCT__entry(
			     __field(u64, soffset)
			     __field(u64, eoffset)
			     __field(u64, flags)
			     ),

	    TP_fast_assign(
			   __entry->soffset = mapping->start;
			   __entry->eoffset = mapping->last + 1;
			   __entry->flags = mapping->flags;
			   ),
	    TP_printk("soffs=%010llx, eoffs=%010llx, flags=%llx",
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

DEFINE_EVENT(amdgpu_vm_mapping, amdgpu_vm_bo_cs,
	    TP_PROTO(struct amdgpu_bo_va_mapping *mapping),
	    TP_ARGS(mapping)
);

TRACE_EVENT(amdgpu_vm_update_ptes,
	    TP_PROTO(struct amdgpu_vm_update_params *p,
		     uint64_t start, uint64_t end,
		     unsigned int nptes, uint64_t dst,
		     uint64_t incr, uint64_t flags,
		     pid_t pid, uint64_t vm_ctx),
	TP_ARGS(p, start, end, nptes, dst, incr, flags, pid, vm_ctx),
	TP_STRUCT__entry(
			 __field(u64, start)
			 __field(u64, end)
			 __field(u64, flags)
			 __field(unsigned int, nptes)
			 __field(u64, incr)
			 __field(pid_t, pid)
			 __field(u64, vm_ctx)
			 __dynamic_array(u64, dst, nptes)
	),

	TP_fast_assign(
			unsigned int i;

			__entry->start = start;
			__entry->end = end;
			__entry->flags = flags;
			__entry->incr = incr;
			__entry->nptes = nptes;
			__entry->pid = pid;
			__entry->vm_ctx = vm_ctx;
			for (i = 0; i < nptes; ++i) {
				u64 addr = p->pages_addr ? amdgpu_vm_map_gart(
					p->pages_addr, dst) : dst;

				((u64 *)__get_dynamic_array(dst))[i] = addr;
				dst += incr;
			}
	),
	TP_printk("pid:%u vm_ctx:0x%llx start:0x%010llx end:0x%010llx,"
		  " flags:0x%llx, incr:%llu, dst:\n%s", __entry->pid,
		  __entry->vm_ctx, __entry->start, __entry->end,
		  __entry->flags, __entry->incr,  __print_array(
		  __get_dynamic_array(dst), __entry->nptes, 8))
);

TRACE_EVENT(amdgpu_vm_set_ptes,
	    TP_PROTO(uint64_t pe, uint64_t addr, unsigned count,
		     uint32_t incr, uint64_t flags, bool immediate),
	    TP_ARGS(pe, addr, count, incr, flags, immediate),
	    TP_STRUCT__entry(
			     __field(u64, pe)
			     __field(u64, addr)
			     __field(u32, count)
			     __field(u32, incr)
			     __field(u64, flags)
			     __field(bool, immediate)
			     ),

	    TP_fast_assign(
			   __entry->pe = pe;
			   __entry->addr = addr;
			   __entry->count = count;
			   __entry->incr = incr;
			   __entry->flags = flags;
			   __entry->immediate = immediate;
			   ),
	    TP_printk("pe=%010Lx, addr=%010Lx, incr=%u, flags=%llx, count=%u, "
		      "immediate=%d", __entry->pe, __entry->addr, __entry->incr,
		      __entry->flags, __entry->count, __entry->immediate)
);

TRACE_EVENT(amdgpu_vm_copy_ptes,
	    TP_PROTO(uint64_t pe, uint64_t src, unsigned count, bool immediate),
	    TP_ARGS(pe, src, count, immediate),
	    TP_STRUCT__entry(
			     __field(u64, pe)
			     __field(u64, src)
			     __field(u32, count)
			     __field(bool, immediate)
			     ),

	    TP_fast_assign(
			   __entry->pe = pe;
			   __entry->src = src;
			   __entry->count = count;
			   __entry->immediate = immediate;
			   ),
	    TP_printk("pe=%010Lx, src=%010Lx, count=%u, immediate=%d",
		      __entry->pe, __entry->src, __entry->count,
		      __entry->immediate)
);

TRACE_EVENT(amdgpu_vm_flush,
	    TP_PROTO(struct amdgpu_ring *ring, unsigned vmid,
		     uint64_t pd_addr),
	    TP_ARGS(ring, vmid, pd_addr),
	    TP_STRUCT__entry(
			     __string(ring, ring->name)
			     __field(u32, vmid)
			     __field(u32, vm_hub)
			     __field(u64, pd_addr)
			     ),

	    TP_fast_assign(
			   __assign_str(ring, ring->name);
			   __entry->vmid = vmid;
			   __entry->vm_hub = ring->vm_hub;
			   __entry->pd_addr = pd_addr;
			   ),
	    TP_printk("ring=%s, id=%u, hub=%u, pd_addr=%010Lx",
		      __get_str(ring), __entry->vmid,
		      __entry->vm_hub,__entry->pd_addr)
);

DECLARE_EVENT_CLASS(amdgpu_pasid,
	    TP_PROTO(unsigned pasid),
	    TP_ARGS(pasid),
	    TP_STRUCT__entry(
			     __field(unsigned, pasid)
			     ),
	    TP_fast_assign(
			   __entry->pasid = pasid;
			   ),
	    TP_printk("pasid=%u", __entry->pasid)
);

DEFINE_EVENT(amdgpu_pasid, amdgpu_pasid_allocated,
	    TP_PROTO(unsigned pasid),
	    TP_ARGS(pasid)
);

DEFINE_EVENT(amdgpu_pasid, amdgpu_pasid_freed,
	    TP_PROTO(unsigned pasid),
	    TP_ARGS(pasid)
);

TRACE_EVENT(amdgpu_bo_list_set,
	    TP_PROTO(struct amdgpu_bo_list *list, struct amdgpu_bo *bo),
	    TP_ARGS(list, bo),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_bo_list *, list)
			     __field(struct amdgpu_bo *, bo)
			     __field(u64, bo_size)
			     ),

	    TP_fast_assign(
			   __entry->list = list;
			   __entry->bo = bo;
			   __entry->bo_size = amdgpu_bo_size(bo);
			   ),
	    TP_printk("list=%p, bo=%p, bo_size=%Ld",
		      __entry->list,
		      __entry->bo,
		      __entry->bo_size)
);

TRACE_EVENT(amdgpu_cs_bo_status,
	    TP_PROTO(uint64_t total_bo, uint64_t total_size),
	    TP_ARGS(total_bo, total_size),
	    TP_STRUCT__entry(
			__field(u64, total_bo)
			__field(u64, total_size)
			),

	    TP_fast_assign(
			__entry->total_bo = total_bo;
			__entry->total_size = total_size;
			),
	    TP_printk("total_bo_size=%Ld, total_bo_count=%Ld",
			__entry->total_bo, __entry->total_size)
);

TRACE_EVENT(amdgpu_bo_move,
	    TP_PROTO(struct amdgpu_bo* bo, uint32_t new_placement, uint32_t old_placement),
	    TP_ARGS(bo, new_placement, old_placement),
	    TP_STRUCT__entry(
			__field(struct amdgpu_bo *, bo)
			__field(u64, bo_size)
			__field(u32, new_placement)
			__field(u32, old_placement)
			),

	    TP_fast_assign(
			__entry->bo      = bo;
			__entry->bo_size = amdgpu_bo_size(bo);
			__entry->new_placement = new_placement;
			__entry->old_placement = old_placement;
			),
	    TP_printk("bo=%p, from=%d, to=%d, size=%Ld",
			__entry->bo, __entry->old_placement,
			__entry->new_placement, __entry->bo_size)
);

TRACE_EVENT(amdgpu_ib_pipe_sync,
	    TP_PROTO(struct amdgpu_job *sched_job, struct dma_fence *fence),
	    TP_ARGS(sched_job, fence),
	    TP_STRUCT__entry(
			     __string(ring, sched_job->base.sched->name)
			     __field(uint64_t, id)
			     __field(struct dma_fence *, fence)
			     __field(uint64_t, ctx)
			     __field(unsigned, seqno)
			     ),

	    TP_fast_assign(
			   __assign_str(ring, sched_job->base.sched->name);
			   __entry->id = sched_job->base.id;
			   __entry->fence = fence;
			   __entry->ctx = fence->context;
			   __entry->seqno = fence->seqno;
			   ),
	    TP_printk("job ring=%s, id=%llu, need pipe sync to fence=%p, context=%llu, seq=%u",
		      __get_str(ring), __entry->id,
		      __entry->fence, __entry->ctx,
		      __entry->seqno)
);

TRACE_EVENT(amdgpu_reset_reg_dumps,
	    TP_PROTO(uint32_t address, uint32_t value),
	    TP_ARGS(address, value),
	    TP_STRUCT__entry(
			     __field(uint32_t, address)
			     __field(uint32_t, value)
			     ),
	    TP_fast_assign(
			   __entry->address = address;
			   __entry->value = value;
			   ),
	    TP_printk("amdgpu register dump 0x%x: 0x%x",
		      __entry->address,
		      __entry->value)
);

#undef AMDGPU_JOB_GET_TIMELINE_NAME
#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/amd/amdgpu
#include <trace/define_trace.h>

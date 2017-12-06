/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_AMDGPU_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDGPU_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdgpu
#define TRACE_INCLUDE_FILE amdgpu_trace

#define AMDGPU_JOB_GET_TIMELINE_NAME(job) \
	 job->base.s_fence->finished.ops->get_timeline_name(&job->base.s_fence->finished)

TRACE_EVENT(amdgpu_ttm_tt_populate,
	    TP_PROTO(struct amdgpu_device *adev, uint64_t dma_address, uint64_t phys_address),
	    TP_ARGS(adev, dma_address, phys_address),
	    TP_STRUCT__entry(
				__field(uint16_t, domain)
				__field(uint8_t, bus)
				__field(uint8_t, slot)
				__field(uint8_t, func)
				__field(uint64_t, dma)
				__field(uint64_t, phys)
			    ),
	    TP_fast_assign(
			   __entry->domain = pci_domain_nr(adev->pdev->bus);
			   __entry->bus = adev->pdev->bus->number;
			   __entry->slot = PCI_SLOT(adev->pdev->devfn);
			   __entry->func = PCI_FUNC(adev->pdev->devfn);
			   __entry->dma = dma_address;
			   __entry->phys = phys_address;
			   ),
	    TP_printk("%04x:%02x:%02x.%x: 0x%llx => 0x%llx",
		      (unsigned)__entry->domain,
		      (unsigned)__entry->bus,
		      (unsigned)__entry->slot,
		      (unsigned)__entry->func,
		      (unsigned long long)__entry->dma,
		      (unsigned long long)__entry->phys)
);

TRACE_EVENT(amdgpu_ttm_tt_unpopulate,
	    TP_PROTO(struct amdgpu_device *adev, uint64_t dma_address, uint64_t phys_address),
	    TP_ARGS(adev, dma_address, phys_address),
	    TP_STRUCT__entry(
				__field(uint16_t, domain)
				__field(uint8_t, bus)
				__field(uint8_t, slot)
				__field(uint8_t, func)
				__field(uint64_t, dma)
				__field(uint64_t, phys)
			    ),
	    TP_fast_assign(
			   __entry->domain = pci_domain_nr(adev->pdev->bus);
			   __entry->bus = adev->pdev->bus->number;
			   __entry->slot = PCI_SLOT(adev->pdev->devfn);
			   __entry->func = PCI_FUNC(adev->pdev->devfn);
			   __entry->dma = dma_address;
			   __entry->phys = phys_address;
			   ),
	    TP_printk("%04x:%02x:%02x.%x: 0x%llx => 0x%llx",
		      (unsigned)__entry->domain,
		      (unsigned)__entry->bus,
		      (unsigned)__entry->slot,
		      (unsigned)__entry->func,
		      (unsigned long long)__entry->dma,
		      (unsigned long long)__entry->phys)
);

TRACE_EVENT(amdgpu_mm_rreg,
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

TRACE_EVENT(amdgpu_mm_wreg,
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
	    TP_PROTO(struct amdgpu_iv_entry *iv),
	    TP_ARGS(iv),
	    TP_STRUCT__entry(
			     __field(unsigned, client_id)
			     __field(unsigned, src_id)
			     __field(unsigned, ring_id)
			     __field(unsigned, vm_id)
			     __field(unsigned, vm_id_src)
			     __field(uint64_t, timestamp)
			     __field(unsigned, timestamp_src)
			     __field(unsigned, pas_id)
			     __array(unsigned, src_data, 4)
			    ),
	    TP_fast_assign(
			   __entry->client_id = iv->client_id;
			   __entry->src_id = iv->src_id;
			   __entry->ring_id = iv->ring_id;
			   __entry->vm_id = iv->vm_id;
			   __entry->vm_id_src = iv->vm_id_src;
			   __entry->timestamp = iv->timestamp;
			   __entry->timestamp_src = iv->timestamp_src;
			   __entry->pas_id = iv->pas_id;
			   __entry->src_data[0] = iv->src_data[0];
			   __entry->src_data[1] = iv->src_data[1];
			   __entry->src_data[2] = iv->src_data[2];
			   __entry->src_data[3] = iv->src_data[3];
			   ),
	    TP_printk("client_id:%u src_id:%u ring:%u vm_id:%u timestamp: %llu pas_id:%u src_data: %08x %08x %08x %08x\n",
		      __entry->client_id, __entry->src_id,
		      __entry->ring_id, __entry->vm_id,
		      __entry->timestamp, __entry->pas_id,
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
			   __entry->pages = bo->tbo.num_pages;
			   __entry->type = bo->tbo.mem.mem_type;
			   __entry->prefer = bo->preferred_domains;
			   __entry->allow = bo->allowed_domains;
			   __entry->visible = bo->flags;
			   ),

	    TP_printk("bo=%p, pages=%u, type=%d, preferred=%d, allowed=%d, visible=%d",
		       __entry->bo, __entry->pages, __entry->type,
		       __entry->prefer, __entry->allow, __entry->visible)
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
			     __field(uint64_t, sched_job_id)
			     __string(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			     __field(unsigned int, context)
			     __field(unsigned int, seqno)
			     __field(struct dma_fence *, fence)
			     __field(char *, ring_name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->sched_job_id = job->base.id;
			   __assign_str(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			   __entry->context = job->base.s_fence->finished.context;
			   __entry->seqno = job->base.s_fence->finished.seqno;
			   __entry->ring_name = job->ring->name;
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("sched_job=%llu, timeline=%s, context=%u, seqno=%u, ring_name=%s, num_ibs=%u",
		      __entry->sched_job_id, __get_str(timeline), __entry->context,
		      __entry->seqno, __entry->ring_name, __entry->num_ibs)
);

TRACE_EVENT(amdgpu_sched_run_job,
	    TP_PROTO(struct amdgpu_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(
			     __field(uint64_t, sched_job_id)
			     __string(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			     __field(unsigned int, context)
			     __field(unsigned int, seqno)
			     __field(char *, ring_name)
			     __field(u32, num_ibs)
			     ),

	    TP_fast_assign(
			   __entry->sched_job_id = job->base.id;
			   __assign_str(timeline, AMDGPU_JOB_GET_TIMELINE_NAME(job))
			   __entry->context = job->base.s_fence->finished.context;
			   __entry->seqno = job->base.s_fence->finished.seqno;
			   __entry->ring_name = job->ring->name;
			   __entry->num_ibs = job->num_ibs;
			   ),
	    TP_printk("sched_job=%llu, timeline=%s, context=%u, seqno=%u, ring_name=%s, num_ibs=%u",
		      __entry->sched_job_id, __get_str(timeline), __entry->context,
		      __entry->seqno, __entry->ring_name, __entry->num_ibs)
);


TRACE_EVENT(amdgpu_vm_grab_id,
	    TP_PROTO(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_job *job),
	    TP_ARGS(vm, ring, job),
	    TP_STRUCT__entry(
			     __field(struct amdgpu_vm *, vm)
			     __field(u32, ring)
			     __field(u32, vm_id)
			     __field(u32, vm_hub)
			     __field(u64, pd_addr)
			     __field(u32, needs_flush)
			     ),

	    TP_fast_assign(
			   __entry->vm = vm;
			   __entry->ring = ring->idx;
			   __entry->vm_id = job->vm_id;
			   __entry->vm_hub = ring->funcs->vmhub,
			   __entry->pd_addr = job->vm_pd_addr;
			   __entry->needs_flush = job->vm_needs_flush;
			   ),
	    TP_printk("vm=%p, ring=%u, id=%u, hub=%u, pd_addr=%010Lx needs_flush=%u",
		      __entry->vm, __entry->ring, __entry->vm_id,
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
			   __entry->bo = bo_va->base.bo;
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

TRACE_EVENT(amdgpu_vm_set_ptes,
	    TP_PROTO(uint64_t pe, uint64_t addr, unsigned count,
		     uint32_t incr, uint64_t flags),
	    TP_ARGS(pe, addr, count, incr, flags),
	    TP_STRUCT__entry(
			     __field(u64, pe)
			     __field(u64, addr)
			     __field(u32, count)
			     __field(u32, incr)
			     __field(u64, flags)
			     ),

	    TP_fast_assign(
			   __entry->pe = pe;
			   __entry->addr = addr;
			   __entry->count = count;
			   __entry->incr = incr;
			   __entry->flags = flags;
			   ),
	    TP_printk("pe=%010Lx, addr=%010Lx, incr=%u, flags=%llx, count=%u",
		      __entry->pe, __entry->addr, __entry->incr,
		      __entry->flags, __entry->count)
);

TRACE_EVENT(amdgpu_vm_copy_ptes,
	    TP_PROTO(uint64_t pe, uint64_t src, unsigned count),
	    TP_ARGS(pe, src, count),
	    TP_STRUCT__entry(
			     __field(u64, pe)
			     __field(u64, src)
			     __field(u32, count)
			     ),

	    TP_fast_assign(
			   __entry->pe = pe;
			   __entry->src = src;
			   __entry->count = count;
			   ),
	    TP_printk("pe=%010Lx, src=%010Lx, count=%u",
		      __entry->pe, __entry->src, __entry->count)
);

TRACE_EVENT(amdgpu_vm_flush,
	    TP_PROTO(struct amdgpu_ring *ring, unsigned vm_id,
		     uint64_t pd_addr),
	    TP_ARGS(ring, vm_id, pd_addr),
	    TP_STRUCT__entry(
			     __field(u32, ring)
			     __field(u32, vm_id)
			     __field(u32, vm_hub)
			     __field(u64, pd_addr)
			     ),

	    TP_fast_assign(
			   __entry->ring = ring->idx;
			   __entry->vm_id = vm_id;
			   __entry->vm_hub = ring->funcs->vmhub;
			   __entry->pd_addr = pd_addr;
			   ),
	    TP_printk("ring=%u, id=%u, hub=%u, pd_addr=%010Lx",
		      __entry->ring, __entry->vm_id,
		      __entry->vm_hub,__entry->pd_addr)
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

TRACE_EVENT(amdgpu_ttm_bo_move,
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

#undef AMDGPU_JOB_GET_TIMELINE_NAME
#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2022 Intel Corporation
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xe

#if !defined(_XE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _XE_TRACE_H_

#include <linux/tracepoint.h>
#include <linux/types.h>

#include "xe_bo_types.h"
#include "xe_engine_types.h"
#include "xe_gpu_scheduler_types.h"
#include "xe_gt_tlb_invalidation_types.h"
#include "xe_gt_types.h"
#include "xe_guc_engine_types.h"
#include "xe_sched_job.h"
#include "xe_vm_types.h"

DECLARE_EVENT_CLASS(xe_gt_tlb_invalidation_fence,
		    TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
		    TP_ARGS(fence),

		    TP_STRUCT__entry(
			     __field(u64, fence)
			     __field(int, seqno)
			     ),

		    TP_fast_assign(
			   __entry->fence = (u64)fence;
			   __entry->seqno = fence->seqno;
			   ),

		    TP_printk("fence=0x%016llx, seqno=%d",
			      __entry->fence, __entry->seqno)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_create,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence,
	     xe_gt_tlb_invalidation_fence_work_func,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_cb,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_send,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_recv,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_signal,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_timeout,
	     TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(fence)
);

DECLARE_EVENT_CLASS(xe_bo,
		    TP_PROTO(struct xe_bo *bo),
		    TP_ARGS(bo),

		    TP_STRUCT__entry(
			     __field(size_t, size)
			     __field(u32, flags)
			     __field(u64, vm)
			     ),

		    TP_fast_assign(
			   __entry->size = bo->size;
			   __entry->flags = bo->flags;
			   __entry->vm = (unsigned long)bo->vm;
			   ),

		    TP_printk("size=%zu, flags=0x%02x, vm=0x%016llx",
			      __entry->size, __entry->flags, __entry->vm)
);

DEFINE_EVENT(xe_bo, xe_bo_cpu_fault,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

DEFINE_EVENT(xe_bo, xe_bo_move,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

DECLARE_EVENT_CLASS(xe_engine,
		    TP_PROTO(struct xe_engine *e),
		    TP_ARGS(e),

		    TP_STRUCT__entry(
			     __field(enum xe_engine_class, class)
			     __field(u32, logical_mask)
			     __field(u8, gt_id)
			     __field(u16, width)
			     __field(u16, guc_id)
			     __field(u32, guc_state)
			     __field(u32, flags)
			     ),

		    TP_fast_assign(
			   __entry->class = e->class;
			   __entry->logical_mask = e->logical_mask;
			   __entry->gt_id = e->gt->info.id;
			   __entry->width = e->width;
			   __entry->guc_id = e->guc->id;
			   __entry->guc_state = atomic_read(&e->guc->state);
			   __entry->flags = e->flags;
			   ),

		    TP_printk("%d:0x%x, gt=%d, width=%d, guc_id=%d, guc_state=0x%x, flags=0x%x",
			      __entry->class, __entry->logical_mask,
			      __entry->gt_id, __entry->width, __entry->guc_id,
			      __entry->guc_state, __entry->flags)
);

DEFINE_EVENT(xe_engine, xe_engine_create,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_supress_resume,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_submit,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_scheduling_enable,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_scheduling_disable,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_scheduling_done,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_register,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_deregister,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_deregister_done,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_close,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_kill,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_cleanup_entity,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_destroy,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_reset,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_memory_cat_error,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_stop,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_resubmit,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DEFINE_EVENT(xe_engine, xe_engine_lr_cleanup,
	     TP_PROTO(struct xe_engine *e),
	     TP_ARGS(e)
);

DECLARE_EVENT_CLASS(xe_sched_job,
		    TP_PROTO(struct xe_sched_job *job),
		    TP_ARGS(job),

		    TP_STRUCT__entry(
			     __field(u32, seqno)
			     __field(u16, guc_id)
			     __field(u32, guc_state)
			     __field(u32, flags)
			     __field(int, error)
			     __field(u64, fence)
			     __field(u64, batch_addr)
			     ),

		    TP_fast_assign(
			   __entry->seqno = xe_sched_job_seqno(job);
			   __entry->guc_id = job->engine->guc->id;
			   __entry->guc_state =
			   atomic_read(&job->engine->guc->state);
			   __entry->flags = job->engine->flags;
			   __entry->error = job->fence->error;
			   __entry->fence = (unsigned long)job->fence;
			   __entry->batch_addr = (u64)job->batch_addr[0];
			   ),

		    TP_printk("fence=0x%016llx, seqno=%u, guc_id=%d, batch_addr=0x%012llx, guc_state=0x%x, flags=0x%x, error=%d",
			      __entry->fence, __entry->seqno, __entry->guc_id,
			      __entry->batch_addr, __entry->guc_state,
			      __entry->flags, __entry->error)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_create,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_exec,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_run,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_free,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_timedout,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_set_error,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DEFINE_EVENT(xe_sched_job, xe_sched_job_ban,
	     TP_PROTO(struct xe_sched_job *job),
	     TP_ARGS(job)
);

DECLARE_EVENT_CLASS(xe_sched_msg,
		    TP_PROTO(struct xe_sched_msg *msg),
		    TP_ARGS(msg),

		    TP_STRUCT__entry(
			     __field(u32, opcode)
			     __field(u16, guc_id)
			     ),

		    TP_fast_assign(
			   __entry->opcode = msg->opcode;
			   __entry->guc_id =
			   ((struct xe_engine *)msg->private_data)->guc->id;
			   ),

		    TP_printk("guc_id=%d, opcode=%u", __entry->guc_id,
			      __entry->opcode)
);

DEFINE_EVENT(xe_sched_msg, xe_sched_msg_add,
	     TP_PROTO(struct xe_sched_msg *msg),
	     TP_ARGS(msg)
);

DEFINE_EVENT(xe_sched_msg, xe_sched_msg_recv,
	     TP_PROTO(struct xe_sched_msg *msg),
	     TP_ARGS(msg)
);

DECLARE_EVENT_CLASS(xe_hw_fence,
		    TP_PROTO(struct xe_hw_fence *fence),
		    TP_ARGS(fence),

		    TP_STRUCT__entry(
			     __field(u64, ctx)
			     __field(u32, seqno)
			     __field(u64, fence)
			     ),

		    TP_fast_assign(
			   __entry->ctx = fence->dma.context;
			   __entry->seqno = fence->dma.seqno;
			   __entry->fence = (unsigned long)fence;
			   ),

		    TP_printk("ctx=0x%016llx, fence=0x%016llx, seqno=%u",
			      __entry->ctx, __entry->fence, __entry->seqno)
);

DEFINE_EVENT(xe_hw_fence, xe_hw_fence_create,
	     TP_PROTO(struct xe_hw_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_hw_fence, xe_hw_fence_signal,
	     TP_PROTO(struct xe_hw_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_hw_fence, xe_hw_fence_try_signal,
	     TP_PROTO(struct xe_hw_fence *fence),
	     TP_ARGS(fence)
);

DEFINE_EVENT(xe_hw_fence, xe_hw_fence_free,
	     TP_PROTO(struct xe_hw_fence *fence),
	     TP_ARGS(fence)
);

DECLARE_EVENT_CLASS(xe_vma,
		    TP_PROTO(struct xe_vma *vma),
		    TP_ARGS(vma),

		    TP_STRUCT__entry(
			     __field(u64, vma)
			     __field(u32, asid)
			     __field(u64, start)
			     __field(u64, end)
			     __field(u64, ptr)
			     ),

		    TP_fast_assign(
			   __entry->vma = (unsigned long)vma;
			   __entry->asid = vma->vm->usm.asid;
			   __entry->start = vma->start;
			   __entry->end = vma->end;
			   __entry->ptr = (u64)vma->userptr.ptr;
			   ),

		    TP_printk("vma=0x%016llx, asid=0x%05x, start=0x%012llx, end=0x%012llx, ptr=0x%012llx,",
			      __entry->vma, __entry->asid, __entry->start,
			      __entry->end, __entry->ptr)
)

DEFINE_EVENT(xe_vma, xe_vma_flush,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_pagefault,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_acc,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_fail,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_bind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_pf_bind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_unbind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_rebind_worker,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_rebind_exec,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_rebind_worker,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_rebind_exec,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_invalidate,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_usm_invalidate,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_evict,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_invalidate_complete,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DECLARE_EVENT_CLASS(xe_vm,
		    TP_PROTO(struct xe_vm *vm),
		    TP_ARGS(vm),

		    TP_STRUCT__entry(
			     __field(u64, vm)
			     __field(u32, asid)
			     ),

		    TP_fast_assign(
			   __entry->vm = (unsigned long)vm;
			   __entry->asid = vm->usm.asid;
			   ),

		    TP_printk("vm=0x%016llx, asid=0x%05x",  __entry->vm,
			      __entry->asid)
);

DEFINE_EVENT(xe_vm, xe_vm_create,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_free,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_cpu_bind,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_restart,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_enter,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_retry,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_exit,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

TRACE_EVENT(xe_guc_ct_h2g_flow_control,
	    TP_PROTO(u32 _head, u32 _tail, u32 size, u32 space, u32 len),
	    TP_ARGS(_head, _tail, size, space, len),

	    TP_STRUCT__entry(
		     __field(u32, _head)
		     __field(u32, _tail)
		     __field(u32, size)
		     __field(u32, space)
		     __field(u32, len)
		     ),

	    TP_fast_assign(
		   __entry->_head = _head;
		   __entry->_tail = _tail;
		   __entry->size = size;
		   __entry->space = space;
		   __entry->len = len;
		   ),

	    TP_printk("head=%u, tail=%u, size=%u, space=%u, len=%u",
		      __entry->_head, __entry->_tail, __entry->size,
		      __entry->space, __entry->len)
);

TRACE_EVENT(xe_guc_ct_g2h_flow_control,
	    TP_PROTO(u32 _head, u32 _tail, u32 size, u32 space, u32 len),
	    TP_ARGS(_head, _tail, size, space, len),

	    TP_STRUCT__entry(
		     __field(u32, _head)
		     __field(u32, _tail)
		     __field(u32, size)
		     __field(u32, space)
		     __field(u32, len)
		     ),

	    TP_fast_assign(
		   __entry->_head = _head;
		   __entry->_tail = _tail;
		   __entry->size = size;
		   __entry->space = space;
		   __entry->len = len;
		   ),

	    TP_printk("head=%u, tail=%u, size=%u, space=%u, len=%u",
		      __entry->_head, __entry->_tail, __entry->size,
		      __entry->space, __entry->len)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/xe
#define TRACE_INCLUDE_FILE xe_trace
#include <trace/define_trace.h>

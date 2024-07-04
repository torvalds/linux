/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2022 Intel Corporation
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xe

#if !defined(_XE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _XE_TRACE_H_

#include <linux/tracepoint.h>
#include <linux/types.h>

#include "xe_bo.h"
#include "xe_bo_types.h"
#include "xe_exec_queue_types.h"
#include "xe_gpu_scheduler_types.h"
#include "xe_gt_tlb_invalidation_types.h"
#include "xe_gt_types.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_sched_job.h"
#include "xe_vm.h"

DECLARE_EVENT_CLASS(xe_gt_tlb_invalidation_fence,
		    TP_PROTO(struct xe_gt_tlb_invalidation_fence *fence),
		    TP_ARGS(fence),

		    TP_STRUCT__entry(
			     __field(struct xe_gt_tlb_invalidation_fence *, fence)
			     __field(int, seqno)
			     ),

		    TP_fast_assign(
			   __entry->fence = fence;
			   __entry->seqno = fence->seqno;
			   ),

		    TP_printk("fence=%p, seqno=%d",
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
			     __field(struct xe_vm *, vm)
			     ),

		    TP_fast_assign(
			   __entry->size = bo->size;
			   __entry->flags = bo->flags;
			   __entry->vm = bo->vm;
			   ),

		    TP_printk("size=%zu, flags=0x%02x, vm=%p",
			      __entry->size, __entry->flags, __entry->vm)
);

DEFINE_EVENT(xe_bo, xe_bo_cpu_fault,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

TRACE_EVENT(xe_bo_move,
	    TP_PROTO(struct xe_bo *bo, uint32_t new_placement, uint32_t old_placement,
		     bool move_lacks_source),
	    TP_ARGS(bo, new_placement, old_placement, move_lacks_source),
	    TP_STRUCT__entry(
		     __field(struct xe_bo *, bo)
		     __field(size_t, size)
		     __field(u32, new_placement)
		     __field(u32, old_placement)
		     __array(char, device_id, 12)
		     __field(bool, move_lacks_source)
			),

	    TP_fast_assign(
		   __entry->bo      = bo;
		   __entry->size = bo->size;
		   __entry->new_placement = new_placement;
		   __entry->old_placement = old_placement;
		   strscpy(__entry->device_id, dev_name(xe_bo_device(__entry->bo)->drm.dev), 12);
		   __entry->move_lacks_source = move_lacks_source;
		   ),
	    TP_printk("move_lacks_source:%s, migrate object %p [size %zu] from %s to %s device_id:%s",
		      __entry->move_lacks_source ? "yes" : "no", __entry->bo, __entry->size,
		      xe_mem_type_to_name[__entry->old_placement],
		      xe_mem_type_to_name[__entry->new_placement], __entry->device_id)
);

DECLARE_EVENT_CLASS(xe_exec_queue,
		    TP_PROTO(struct xe_exec_queue *q),
		    TP_ARGS(q),

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
			   __entry->class = q->class;
			   __entry->logical_mask = q->logical_mask;
			   __entry->gt_id = q->gt->info.id;
			   __entry->width = q->width;
			   __entry->guc_id = q->guc->id;
			   __entry->guc_state = atomic_read(&q->guc->state);
			   __entry->flags = q->flags;
			   ),

		    TP_printk("%d:0x%x, gt=%d, width=%d, guc_id=%d, guc_state=0x%x, flags=0x%x",
			      __entry->class, __entry->logical_mask,
			      __entry->gt_id, __entry->width, __entry->guc_id,
			      __entry->guc_state, __entry->flags)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_create,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_supress_resume,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_submit,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_scheduling_enable,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_scheduling_disable,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_scheduling_done,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_register,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_deregister,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_deregister_done,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_close,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_kill,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_cleanup_entity,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_destroy,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_reset,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_memory_cat_error,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_stop,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_resubmit,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
);

DEFINE_EVENT(xe_exec_queue, xe_exec_queue_lr_cleanup,
	     TP_PROTO(struct xe_exec_queue *q),
	     TP_ARGS(q)
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
			     __field(struct dma_fence *, fence)
			     __field(u64, batch_addr)
			     ),

		    TP_fast_assign(
			   __entry->seqno = xe_sched_job_seqno(job);
			   __entry->guc_id = job->q->guc->id;
			   __entry->guc_state =
			   atomic_read(&job->q->guc->state);
			   __entry->flags = job->q->flags;
			   __entry->error = job->fence->error;
			   __entry->fence = job->fence;
			   __entry->batch_addr = (u64)job->batch_addr[0];
			   ),

		    TP_printk("fence=%p, seqno=%u, guc_id=%d, batch_addr=0x%012llx, guc_state=0x%x, flags=0x%x, error=%d",
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
			   ((struct xe_exec_queue *)msg->private_data)->guc->id;
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
			     __field(struct xe_hw_fence *, fence)
			     ),

		    TP_fast_assign(
			   __entry->ctx = fence->dma.context;
			   __entry->seqno = fence->dma.seqno;
			   __entry->fence = fence;
			   ),

		    TP_printk("ctx=0x%016llx, fence=%p, seqno=%u",
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
			     __field(struct xe_vma *, vma)
			     __field(u32, asid)
			     __field(u64, start)
			     __field(u64, end)
			     __field(u64, ptr)
			     ),

		    TP_fast_assign(
			   __entry->vma = vma;
			   __entry->asid = xe_vma_vm(vma)->usm.asid;
			   __entry->start = xe_vma_start(vma);
			   __entry->end = xe_vma_end(vma) - 1;
			   __entry->ptr = xe_vma_userptr(vma);
			   ),

		    TP_printk("vma=%p, asid=0x%05x, start=0x%012llx, end=0x%012llx, userptr=0x%012llx,",
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

DEFINE_EVENT(xe_vma, xe_vma_invalidate,
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
			     __field(struct xe_vm *, vm)
			     __field(u32, asid)
			     ),

		    TP_fast_assign(
			   __entry->vm = vm;
			   __entry->asid = vm->usm.asid;
			   ),

		    TP_printk("vm=%p, asid=0x%05x",  __entry->vm,
			      __entry->asid)
);

DEFINE_EVENT(xe_vm, xe_vm_kill,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
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

/* GuC */
DECLARE_EVENT_CLASS(xe_guc_ct_flow_control,
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

		    TP_printk("h2g flow control: head=%u, tail=%u, size=%u, space=%u, len=%u",
			      __entry->_head, __entry->_tail, __entry->size,
			      __entry->space, __entry->len)
);

DEFINE_EVENT(xe_guc_ct_flow_control, xe_guc_ct_h2g_flow_control,
	     TP_PROTO(u32 _head, u32 _tail, u32 size, u32 space, u32 len),
	     TP_ARGS(_head, _tail, size, space, len)
);

DEFINE_EVENT_PRINT(xe_guc_ct_flow_control, xe_guc_ct_g2h_flow_control,
		   TP_PROTO(u32 _head, u32 _tail, u32 size, u32 space, u32 len),
		   TP_ARGS(_head, _tail, size, space, len),

		   TP_printk("g2h flow control: head=%u, tail=%u, size=%u, space=%u, len=%u",
			     __entry->_head, __entry->_tail, __entry->size,
			     __entry->space, __entry->len)
);

DECLARE_EVENT_CLASS(xe_guc_ctb,
		    TP_PROTO(u8 gt_id, u32 action, u32 len, u32 _head, u32 tail),
		    TP_ARGS(gt_id, action, len, _head, tail),

		    TP_STRUCT__entry(
				__field(u8, gt_id)
				__field(u32, action)
				__field(u32, len)
				__field(u32, tail)
				__field(u32, _head)
		    ),

		    TP_fast_assign(
			    __entry->gt_id = gt_id;
			    __entry->action = action;
			    __entry->len = len;
			    __entry->tail = tail;
			    __entry->_head = _head;
		    ),

		    TP_printk("gt%d: H2G CTB: action=0x%x, len=%d, tail=%d, head=%d\n",
			      __entry->gt_id, __entry->action, __entry->len,
			      __entry->tail, __entry->_head)
);

DEFINE_EVENT(xe_guc_ctb, xe_guc_ctb_h2g,
	     TP_PROTO(u8 gt_id, u32 action, u32 len, u32 _head, u32 tail),
	     TP_ARGS(gt_id, action, len, _head, tail)
);

DEFINE_EVENT_PRINT(xe_guc_ctb, xe_guc_ctb_g2h,
		   TP_PROTO(u8 gt_id, u32 action, u32 len, u32 _head, u32 tail),
		   TP_ARGS(gt_id, action, len, _head, tail),

		   TP_printk("gt%d: G2H CTB: action=0x%x, len=%d, tail=%d, head=%d\n",
			     __entry->gt_id, __entry->action, __entry->len,
			     __entry->tail, __entry->_head)

);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/xe
#define TRACE_INCLUDE_FILE xe_trace
#include <trace/define_trace.h>

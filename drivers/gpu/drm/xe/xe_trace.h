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

#include "xe_exec_queue_types.h"
#include "xe_gpu_scheduler_types.h"
#include "xe_gt_tlb_invalidation_types.h"
#include "xe_gt_types.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_sched_job.h"
#include "xe_vm.h"

#define __dev_name_xe(xe)	dev_name((xe)->drm.dev)
#define __dev_name_gt(gt)	__dev_name_xe(gt_to_xe((gt)))
#define __dev_name_eq(q)	__dev_name_gt((q)->gt)

DECLARE_EVENT_CLASS(xe_gt_tlb_invalidation_fence,
		    TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
		    TP_ARGS(xe, fence),

		    TP_STRUCT__entry(
			     __string(dev, __dev_name_xe(xe))
			     __field(struct xe_gt_tlb_invalidation_fence *, fence)
			     __field(int, seqno)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->fence = fence;
			   __entry->seqno = fence->seqno;
			   ),

		    TP_printk("dev=%s, fence=%p, seqno=%d",
			      __get_str(dev), __entry->fence, __entry->seqno)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_create,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence,
	     xe_gt_tlb_invalidation_fence_work_func,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_cb,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_send,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_recv,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_signal,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DEFINE_EVENT(xe_gt_tlb_invalidation_fence, xe_gt_tlb_invalidation_fence_timeout,
	     TP_PROTO(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence),
	     TP_ARGS(xe, fence)
);

DECLARE_EVENT_CLASS(xe_exec_queue,
		    TP_PROTO(struct xe_exec_queue *q),
		    TP_ARGS(q),

		    TP_STRUCT__entry(
			     __string(dev, __dev_name_eq(q))
			     __field(enum xe_engine_class, class)
			     __field(u32, logical_mask)
			     __field(u8, gt_id)
			     __field(u16, width)
			     __field(u16, guc_id)
			     __field(u32, guc_state)
			     __field(u32, flags)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->class = q->class;
			   __entry->logical_mask = q->logical_mask;
			   __entry->gt_id = q->gt->info.id;
			   __entry->width = q->width;
			   __entry->guc_id = q->guc->id;
			   __entry->guc_state = atomic_read(&q->guc->state);
			   __entry->flags = q->flags;
			   ),

		    TP_printk("dev=%s, %d:0x%x, gt=%d, width=%d, guc_id=%d, guc_state=0x%x, flags=0x%x",
			      __get_str(dev), __entry->class, __entry->logical_mask,
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
			     __string(dev, __dev_name_eq(job->q))
			     __field(u32, seqno)
			     __field(u32, lrc_seqno)
			     __field(u16, guc_id)
			     __field(u32, guc_state)
			     __field(u32, flags)
			     __field(int, error)
			     __field(struct dma_fence *, fence)
			     __field(u64, batch_addr)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->seqno = xe_sched_job_seqno(job);
			   __entry->lrc_seqno = xe_sched_job_lrc_seqno(job);
			   __entry->guc_id = job->q->guc->id;
			   __entry->guc_state =
			   atomic_read(&job->q->guc->state);
			   __entry->flags = job->q->flags;
			   __entry->error = job->fence ? job->fence->error : 0;
			   __entry->fence = job->fence;
			   __entry->batch_addr = (u64)job->ptrs[0].batch_addr;
			   ),

		    TP_printk("dev=%s, fence=%p, seqno=%u, lrc_seqno=%u, guc_id=%d, batch_addr=0x%012llx, guc_state=0x%x, flags=0x%x, error=%d",
			      __get_str(dev), __entry->fence, __entry->seqno,
			      __entry->lrc_seqno, __entry->guc_id,
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
			     __string(dev, __dev_name_eq(((struct xe_exec_queue *)msg->private_data)))
			     __field(u32, opcode)
			     __field(u16, guc_id)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->opcode = msg->opcode;
			   __entry->guc_id =
			   ((struct xe_exec_queue *)msg->private_data)->guc->id;
			   ),

		    TP_printk("dev=%s, guc_id=%d, opcode=%u", __get_str(dev), __entry->guc_id,
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
			     __string(dev, __dev_name_xe(fence->xe))
			     __field(u64, ctx)
			     __field(u32, seqno)
			     __field(struct xe_hw_fence *, fence)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->ctx = fence->dma.context;
			   __entry->seqno = fence->dma.seqno;
			   __entry->fence = fence;
			   ),

		    TP_printk("dev=%s, ctx=0x%016llx, fence=%p, seqno=%u",
			      __get_str(dev), __entry->ctx, __entry->fence, __entry->seqno)
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

TRACE_EVENT(xe_reg_rw,
	TP_PROTO(struct xe_gt *gt, bool write, u32 reg, u64 val, int len),

	TP_ARGS(gt, write, reg, val, len),

	TP_STRUCT__entry(
		__string(dev, __dev_name_gt(gt))
		__field(u64, val)
		__field(u32, reg)
		__field(u16, write)
		__field(u16, len)
		),

	TP_fast_assign(
		__assign_str(dev);
		__entry->val = val;
		__entry->reg = reg;
		__entry->write = write;
		__entry->len = len;
		),

	TP_printk("dev=%s, %s reg=0x%x, len=%d, val=(0x%x, 0x%x)",
		  __get_str(dev), __entry->write ? "write" : "read",
		  __entry->reg, __entry->len,
		  (u32)(__entry->val & 0xffffffff),
		  (u32)(__entry->val >> 32))
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/xe
#define TRACE_INCLUDE_FILE xe_trace
#include <trace/define_trace.h>

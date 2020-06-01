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

#if !defined(_GPU_SCHED_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GPU_SCHED_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_scheduler
#define TRACE_INCLUDE_FILE gpu_scheduler_trace

TRACE_EVENT(drm_sched_job,
	    TP_PROTO(struct drm_sched_job *sched_job, struct drm_sched_entity *entity),
	    TP_ARGS(sched_job, entity),
	    TP_STRUCT__entry(
			     __field(struct drm_sched_entity *, entity)
			     __field(struct dma_fence *, fence)
			     __field(const char *, name)
			     __field(uint64_t, id)
			     __field(u32, job_count)
			     __field(int, hw_job_count)
			     ),

	    TP_fast_assign(
			   __entry->entity = entity;
			   __entry->id = sched_job->id;
			   __entry->fence = &sched_job->s_fence->finished;
			   __entry->name = sched_job->sched->name;
			   __entry->job_count = spsc_queue_count(&entity->job_queue);
			   __entry->hw_job_count = atomic_read(
				   &sched_job->sched->hw_rq_count);
			   ),
	    TP_printk("entity=%p, id=%llu, fence=%p, ring=%s, job count:%u, hw job count:%d",
		      __entry->entity, __entry->id,
		      __entry->fence, __entry->name,
		      __entry->job_count, __entry->hw_job_count)
);

TRACE_EVENT(drm_run_job,
	    TP_PROTO(struct drm_sched_job *sched_job, struct drm_sched_entity *entity),
	    TP_ARGS(sched_job, entity),
	    TP_STRUCT__entry(
			     __field(struct drm_sched_entity *, entity)
			     __field(struct dma_fence *, fence)
			     __field(const char *, name)
			     __field(uint64_t, id)
			     __field(u32, job_count)
			     __field(int, hw_job_count)
			     ),

	    TP_fast_assign(
			   __entry->entity = entity;
			   __entry->id = sched_job->id;
			   __entry->fence = &sched_job->s_fence->finished;
			   __entry->name = sched_job->sched->name;
			   __entry->job_count = spsc_queue_count(&entity->job_queue);
			   __entry->hw_job_count = atomic_read(
				   &sched_job->sched->hw_rq_count);
			   ),
	    TP_printk("entity=%p, id=%llu, fence=%p, ring=%s, job count:%u, hw job count:%d",
		      __entry->entity, __entry->id,
		      __entry->fence, __entry->name,
		      __entry->job_count, __entry->hw_job_count)
);

TRACE_EVENT(drm_sched_process_job,
	    TP_PROTO(struct drm_sched_fence *fence),
	    TP_ARGS(fence),
	    TP_STRUCT__entry(
		    __field(struct dma_fence *, fence)
		    ),

	    TP_fast_assign(
		    __entry->fence = &fence->finished;
		    ),
	    TP_printk("fence=%p signaled", __entry->fence)
);

TRACE_EVENT(drm_sched_job_wait_dep,
	    TP_PROTO(struct drm_sched_job *sched_job, struct dma_fence *fence),
	    TP_ARGS(sched_job, fence),
	    TP_STRUCT__entry(
			     __field(const char *,name)
			     __field(uint64_t, id)
			     __field(struct dma_fence *, fence)
			     __field(uint64_t, ctx)
			     __field(unsigned, seqno)
			     ),

	    TP_fast_assign(
			   __entry->name = sched_job->sched->name;
			   __entry->id = sched_job->id;
			   __entry->fence = fence;
			   __entry->ctx = fence->context;
			   __entry->seqno = fence->seqno;
			   ),
	    TP_printk("job ring=%s, id=%llu, depends fence=%p, context=%llu, seq=%u",
		      __entry->name, __entry->id,
		      __entry->fence, __entry->ctx,
		      __entry->seqno)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/scheduler
#include <trace/define_trace.h>

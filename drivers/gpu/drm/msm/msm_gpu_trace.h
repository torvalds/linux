/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_MSM_GPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _MSM_GPU_TRACE_H_

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm_msm_gpu
#define TRACE_INCLUDE_FILE msm_gpu_trace

TRACE_EVENT(msm_gpu_submit,
	    TP_PROTO(pid_t pid, u32 ringid, u32 id, u32 nr_bos, u32 nr_cmds),
	    TP_ARGS(pid, ringid, id, nr_bos, nr_cmds),
	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(u32, id)
		    __field(u32, ringid)
		    __field(u32, nr_cmds)
		    __field(u32, nr_bos)
		    ),
	    TP_fast_assign(
		    __entry->pid = pid;
		    __entry->id = id;
		    __entry->ringid = ringid;
		    __entry->nr_bos = nr_bos;
		    __entry->nr_cmds = nr_cmds
		    ),
	    TP_printk("id=%d pid=%d ring=%d bos=%d cmds=%d",
		    __entry->id, __entry->pid, __entry->ringid,
		    __entry->nr_bos, __entry->nr_cmds)
);

TRACE_EVENT(msm_gpu_submit_flush,
	    TP_PROTO(struct msm_gem_submit *submit, u64 ticks),
	    TP_ARGS(submit, ticks),
	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(u32, id)
		    __field(u32, ringid)
		    __field(u32, seqno)
		    __field(u64, ticks)
		    ),
	    TP_fast_assign(
		    __entry->pid = pid_nr(submit->pid);
		    __entry->id = submit->ident;
		    __entry->ringid = submit->ring->id;
		    __entry->seqno = submit->seqno;
		    __entry->ticks = ticks;
		    ),
	    TP_printk("id=%d pid=%d ring=%d:%d ticks=%lld",
		    __entry->id, __entry->pid, __entry->ringid, __entry->seqno,
		    __entry->ticks)
);


TRACE_EVENT(msm_gpu_submit_retired,
	    TP_PROTO(struct msm_gem_submit *submit, u64 elapsed, u64 clock,
		    u64 start, u64 end),
	    TP_ARGS(submit, elapsed, clock, start, end),
	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(u32, id)
		    __field(u32, ringid)
		    __field(u32, seqno)
		    __field(u64, elapsed)
		    __field(u64, clock)
		    __field(u64, start_ticks)
		    __field(u64, end_ticks)
		    ),
	    TP_fast_assign(
		    __entry->pid = pid_nr(submit->pid);
		    __entry->id = submit->ident;
		    __entry->ringid = submit->ring->id;
		    __entry->seqno = submit->seqno;
		    __entry->elapsed = elapsed;
		    __entry->clock = clock;
		    __entry->start_ticks = start;
		    __entry->end_ticks = end;
		    ),
	    TP_printk("id=%d pid=%d ring=%d:%d elapsed=%lld ns mhz=%lld start=%lld end=%lld",
		    __entry->id, __entry->pid, __entry->ringid, __entry->seqno,
		    __entry->elapsed, __entry->clock,
		    __entry->start_ticks, __entry->end_ticks)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/msm
#include <trace/define_trace.h>

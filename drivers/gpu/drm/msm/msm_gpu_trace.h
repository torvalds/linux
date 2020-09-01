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


TRACE_EVENT(msm_gpu_freq_change,
		TP_PROTO(u32 freq),
		TP_ARGS(freq),
		TP_STRUCT__entry(
			__field(u32, freq)
			),
		TP_fast_assign(
			/* trace freq in MHz to match intel_gpu_freq_change, to make life easier
			 * for userspace
			 */
			__entry->freq = DIV_ROUND_UP(freq, 1000000);
			),
		TP_printk("new_freq=%u", __entry->freq)
);


TRACE_EVENT(msm_gmu_freq_change,
		TP_PROTO(u32 freq, u32 perf_index),
		TP_ARGS(freq, perf_index),
		TP_STRUCT__entry(
			__field(u32, freq)
			__field(u32, perf_index)
			),
		TP_fast_assign(
			__entry->freq = freq;
			__entry->perf_index = perf_index;
			),
		TP_printk("freq=%u, perf_index=%u", __entry->freq, __entry->perf_index)
);


TRACE_EVENT(msm_gem_purge,
		TP_PROTO(u32 bytes),
		TP_ARGS(bytes),
		TP_STRUCT__entry(
			__field(u32, bytes)
			),
		TP_fast_assign(
			__entry->bytes = bytes;
			),
		TP_printk("Purging %u bytes", __entry->bytes)
);


TRACE_EVENT(msm_gem_purge_vmaps,
		TP_PROTO(u32 unmapped),
		TP_ARGS(unmapped),
		TP_STRUCT__entry(
			__field(u32, unmapped)
			),
		TP_fast_assign(
			__entry->unmapped = unmapped;
			),
		TP_printk("Purging %u vmaps", __entry->unmapped)
);


TRACE_EVENT(msm_gpu_suspend,
		TP_PROTO(int dummy),
		TP_ARGS(dummy),
		TP_STRUCT__entry(
			__field(u32, dummy)
			),
		TP_fast_assign(
			__entry->dummy = dummy;
			),
		TP_printk("%u", __entry->dummy)
);


TRACE_EVENT(msm_gpu_resume,
		TP_PROTO(int dummy),
		TP_ARGS(dummy),
		TP_STRUCT__entry(
			__field(u32, dummy)
			),
		TP_fast_assign(
			__entry->dummy = dummy;
			),
		TP_printk("%u", __entry->dummy)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/msm
#include <trace/define_trace.h>

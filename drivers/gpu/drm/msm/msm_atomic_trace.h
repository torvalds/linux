/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_MSM_GPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _MSM_GPU_TRACE_H_

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm_msm_atomic
#define TRACE_INCLUDE_FILE msm_atomic_trace

TRACE_EVENT(msm_atomic_commit_tail_start,
	    TP_PROTO(bool async, unsigned crtc_mask),
	    TP_ARGS(async, crtc_mask),
	    TP_STRUCT__entry(
		    __field(bool, async)
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->async = async;
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("async=%d crtc_mask=%x",
		    __entry->async, __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_commit_tail_finish,
	    TP_PROTO(bool async, unsigned crtc_mask),
	    TP_ARGS(async, crtc_mask),
	    TP_STRUCT__entry(
		    __field(bool, async)
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->async = async;
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("async=%d crtc_mask=%x",
		    __entry->async, __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_async_commit_start,
	    TP_PROTO(unsigned crtc_mask),
	    TP_ARGS(crtc_mask),
	    TP_STRUCT__entry(
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("crtc_mask=%x",
		    __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_async_commit_finish,
	    TP_PROTO(unsigned crtc_mask),
	    TP_ARGS(crtc_mask),
	    TP_STRUCT__entry(
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("crtc_mask=%x",
		    __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_wait_flush_start,
	    TP_PROTO(unsigned crtc_mask),
	    TP_ARGS(crtc_mask),
	    TP_STRUCT__entry(
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("crtc_mask=%x",
		    __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_wait_flush_finish,
	    TP_PROTO(unsigned crtc_mask),
	    TP_ARGS(crtc_mask),
	    TP_STRUCT__entry(
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("crtc_mask=%x",
		    __entry->crtc_mask)
);

TRACE_EVENT(msm_atomic_flush_commit,
	    TP_PROTO(unsigned crtc_mask),
	    TP_ARGS(crtc_mask),
	    TP_STRUCT__entry(
		    __field(u32, crtc_mask)
		    ),
	    TP_fast_assign(
		    __entry->crtc_mask = crtc_mask;
		    ),
	    TP_printk("crtc_mask=%x",
		    __entry->crtc_mask)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/msm
#include <trace/define_trace.h>

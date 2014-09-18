#if !defined(_DRM_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _DRM_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE drm_trace

TRACE_EVENT(drm_vblank_event,
	    TP_PROTO(int crtc, unsigned int seq),
	    TP_ARGS(crtc, seq),
	    TP_STRUCT__entry(
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    ),
	    TP_fast_assign(
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    ),
	    TP_printk("crtc=%d, seq=%u", __entry->crtc, __entry->seq)
);

TRACE_EVENT(drm_vblank_event_queued,
	    TP_PROTO(pid_t pid, int crtc, unsigned int seq),
	    TP_ARGS(pid, crtc, seq),
	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    ),
	    TP_fast_assign(
		    __entry->pid = pid;
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    ),
	    TP_printk("pid=%d, crtc=%d, seq=%u", __entry->pid, __entry->crtc, \
		      __entry->seq)
);

TRACE_EVENT(drm_vblank_event_delivered,
	    TP_PROTO(pid_t pid, int crtc, unsigned int seq),
	    TP_ARGS(pid, crtc, seq),
	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    ),
	    TP_fast_assign(
		    __entry->pid = pid;
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    ),
	    TP_printk("pid=%d, crtc=%d, seq=%u", __entry->pid, __entry->crtc, \
		      __entry->seq)
);

#endif /* _DRM_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

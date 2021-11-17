/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_DRM_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _DRM_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

struct drm_file;

#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm
#define TRACE_INCLUDE_FILE drm_trace

TRACE_EVENT(drm_vblank_event,
	    TP_PROTO(int crtc, unsigned int seq, ktime_t time, bool high_prec),
	    TP_ARGS(crtc, seq, time, high_prec),
	    TP_STRUCT__entry(
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    __field(ktime_t, time)
		    __field(bool, high_prec)
		    ),
	    TP_fast_assign(
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    __entry->time = time;
		    __entry->high_prec = high_prec;
			),
	    TP_printk("crtc=%d, seq=%u, time=%lld, high-prec=%s",
			__entry->crtc, __entry->seq, __entry->time,
			__entry->high_prec ? "true" : "false")
);

TRACE_EVENT(drm_vblank_event_queued,
	    TP_PROTO(struct drm_file *file, int crtc, unsigned int seq),
	    TP_ARGS(file, crtc, seq),
	    TP_STRUCT__entry(
		    __field(struct drm_file *, file)
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    ),
	    TP_fast_assign(
		    __entry->file = file;
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    ),
	    TP_printk("file=%p, crtc=%d, seq=%u", __entry->file, __entry->crtc, \
		      __entry->seq)
);

TRACE_EVENT(drm_vblank_event_delivered,
	    TP_PROTO(struct drm_file *file, int crtc, unsigned int seq),
	    TP_ARGS(file, crtc, seq),
	    TP_STRUCT__entry(
		    __field(struct drm_file *, file)
		    __field(int, crtc)
		    __field(unsigned int, seq)
		    ),
	    TP_fast_assign(
		    __entry->file = file;
		    __entry->crtc = crtc;
		    __entry->seq = seq;
		    ),
	    TP_printk("file=%p, crtc=%d, seq=%u", __entry->file, __entry->crtc, \
		      __entry->seq)
);

#endif /* _DRM_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm
#include <trace/define_trace.h>

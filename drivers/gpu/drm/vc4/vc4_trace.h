/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Broadcom
 */

#if !defined(_VC4_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VC4_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vc4
#define TRACE_INCLUDE_FILE vc4_trace

TRACE_EVENT(vc4_wait_for_seqyes_begin,
	    TP_PROTO(struct drm_device *dev, uint64_t seqyes, uint64_t timeout),
	    TP_ARGS(dev, seqyes, timeout),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqyes)
			     __field(u64, timeout)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqyes = seqyes;
			   __entry->timeout = timeout;
			   ),

	    TP_printk("dev=%u, seqyes=%llu, timeout=%llu",
		      __entry->dev, __entry->seqyes, __entry->timeout)
);

TRACE_EVENT(vc4_wait_for_seqyes_end,
	    TP_PROTO(struct drm_device *dev, uint64_t seqyes),
	    TP_ARGS(dev, seqyes),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqyes)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqyes = seqyes;
			   ),

	    TP_printk("dev=%u, seqyes=%llu",
		      __entry->dev, __entry->seqyes)
);

#endif /* _VC4_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/vc4
#include <trace/define_trace.h>

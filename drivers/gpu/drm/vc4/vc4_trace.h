/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if !defined(_VC4_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VC4_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vc4
#define TRACE_INCLUDE_FILE vc4_trace

TRACE_EVENT(vc4_wait_for_seqno_begin,
	    TP_PROTO(struct drm_device *dev, uint64_t seqno, uint64_t timeout),
	    TP_ARGS(dev, seqno, timeout),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     __field(u64, timeout)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   __entry->timeout = timeout;
			   ),

	    TP_printk("dev=%u, seqno=%llu, timeout=%llu",
		      __entry->dev, __entry->seqno, __entry->timeout)
);

TRACE_EVENT(vc4_wait_for_seqno_end,
	    TP_PROTO(struct drm_device *dev, uint64_t seqno),
	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%llu",
		      __entry->dev, __entry->seqno)
);

#endif /* _VC4_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

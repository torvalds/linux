/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bam_dma

#if !defined(_TRACE_BAM_DMA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BAM_DMA_TRACE_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#define MAX_MSG_LEN 200

TRACE_EVENT(bam_dma_info,
	    TP_PROTO(const char *name, struct va_format *vaf),
	    TP_ARGS(name, vaf),
	    TP_STRUCT__entry
		(__string(name, name)
		__dynamic_array(char, msg, MAX_MSG_LEN)
	    ),
	    TP_fast_assign
		(__assign_str(name, name);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
			     MAX_MSG_LEN, vaf->fmt, *vaf->va) >= MAX_MSG_LEN);
	    ),
	   TP_printk("%s: %s", __get_str(name),  __get_str(msg))
);

#endif /* _TRACE_BAM_DMA_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE bam_dma_trace
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2024 Intel Corporation
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xe

#if !defined(_XE_TRACE_LRC_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _XE_TRACE_LRC_H_

#include <linux/tracepoint.h>
#include <linux/types.h>

#include "xe_gt_types.h"
#include "xe_lrc.h"
#include "xe_lrc_types.h"

#define __dev_name_lrc(lrc)	dev_name(gt_to_xe((lrc)->fence_ctx.gt)->drm.dev)

TRACE_EVENT(xe_lrc_update_timestamp,
	    TP_PROTO(struct xe_lrc *lrc, uint64_t old),
	    TP_ARGS(lrc, old),
	    TP_STRUCT__entry(
		     __field(struct xe_lrc *, lrc)
		     __field(u64, old)
		     __field(u64, new)
		     __string(name, lrc->fence_ctx.name)
		     __string(device_id, __dev_name_lrc(lrc))
	    ),

	    TP_fast_assign(
		   __entry->lrc	= lrc;
		   __entry->old = old;
		   __entry->new = lrc->ctx_timestamp;
		   __assign_str(name);
		   __assign_str(device_id);
		   ),
	    TP_printk("lrc=:%p lrc->name=%s old=%llu new=%llu device_id:%s",
		      __entry->lrc, __get_str(name),
		      __entry->old, __entry->new,
		      __get_str(device_id))
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/xe
#define TRACE_INCLUDE_FILE xe_trace_lrc
#include <trace/define_trace.h>

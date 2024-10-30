/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#if !defined(__IVPU_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __IVPU_TRACE_H__

#include <linux/tracepoint.h>
#include "ivpu_drv.h"
#include "ivpu_job.h"
#include "vpu_jsm_api.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_ipc.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vpu
#define TRACE_INCLUDE_FILE ivpu_trace

TRACE_EVENT(pm,
	    TP_PROTO(const char *event),
	    TP_ARGS(event),
	    TP_STRUCT__entry(__field(const char *, event)),
	    TP_fast_assign(__entry->event = event;),
	    TP_printk("%s", __entry->event)
);

TRACE_EVENT(job,
	    TP_PROTO(const char *event, struct ivpu_job *job),
	    TP_ARGS(event, job),
	    TP_STRUCT__entry(__field(const char *, event)
		__field(u32, ctx_id)
		__field(u32, engine_id)
		__field(u32, job_id)
		),
	    TP_fast_assign(__entry->event = event;
		__entry->ctx_id = job->file_priv->ctx.id;
		__entry->engine_id = job->engine_idx;
		__entry->job_id = job->job_id;),
	    TP_printk("%s context:%d engine:%d job:%d",
		      __entry->event,
		      __entry->ctx_id,
		      __entry->engine_id,
		      __entry->job_id)
);

TRACE_EVENT(jsm,
	    TP_PROTO(const char *event, struct vpu_jsm_msg *msg),
	    TP_ARGS(event, msg),
	    TP_STRUCT__entry(__field(const char *, event)
		__field(const char *, type)
		__field(enum vpu_ipc_msg_status, status)
		__field(u32, request_id)
		__field(u32, result)
		),
	    TP_fast_assign(__entry->event = event;
		__entry->type = ivpu_jsm_msg_type_to_str(msg->type);
		__entry->status = msg->status;
		__entry->request_id = msg->request_id;
		__entry->result = msg->result;),
	    TP_printk("%s type:%s, status:%#x, id:%#x, result:%#x",
		      __entry->event,
		      __entry->type,
		      __entry->status,
		      __entry->request_id,
		      __entry->result)
);

#endif /* __IVPU_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(__LIB_TRACER_TRACEPOINT_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __LIB_TRACER_TRACEPOINT_H__

#include <linux/tracepoint.h>
#include "fw_tracer.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

/* Tracepoint for FWTracer messages: */
TRACE_EVENT(mlx5_fw,
	TP_PROTO(const struct mlx5_fw_tracer *tracer, u64 trace_timestamp,
		 bool lost, u8 event_id, const char *msg),

	TP_ARGS(tracer, trace_timestamp, lost, event_id, msg),

	TP_STRUCT__entry(
		__string(dev_name, dev_name(&tracer->dev->pdev->dev))
		__field(u64, trace_timestamp)
		__field(bool, lost)
		__field(u8, event_id)
		__string(msg, msg)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name(&tracer->dev->pdev->dev));
		__entry->trace_timestamp = trace_timestamp;
		__entry->lost = lost;
		__entry->event_id = event_id;
		__assign_str(msg, msg);
	),

	TP_printk("%s [0x%llx] %d [0x%x] %s",
		  __get_str(dev_name),
		  __entry->trace_timestamp,
		  __entry->lost, __entry->event_id,
		  __get_str(msg))
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ./diag
#define TRACE_INCLUDE_FILE fw_tracer_tracepoint
#include <trace/define_trace.h>

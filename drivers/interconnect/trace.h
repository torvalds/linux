/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interconnect framework tracepoints
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM interconnect

#if !defined(_TRACE_INTERCONNECT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTERCONNECT_H

#include <linux/interconnect.h>
#include <linux/tracepoint.h>

TRACE_EVENT(icc_set_bw,

	TP_PROTO(struct icc_path *p, struct icc_node *n, int i,
		 u32 avg_bw, u32 peak_bw),

	TP_ARGS(p, n, i, avg_bw, peak_bw),

	TP_STRUCT__entry(
		__string(path_name, p->name)
		__string(dev, dev_name(p->reqs[i].dev))
		__string(node_name, n->name)
		__field(u32, avg_bw)
		__field(u32, peak_bw)
		__field(u32, node_avg_bw)
		__field(u32, node_peak_bw)
	),

	TP_fast_assign(
		__assign_str(path_name);
		__assign_str(dev);
		__assign_str(node_name);
		__entry->avg_bw = avg_bw;
		__entry->peak_bw = peak_bw;
		__entry->node_avg_bw = n->avg_bw;
		__entry->node_peak_bw = n->peak_bw;
	),

	TP_printk("path=%s dev=%s node=%s avg_bw=%u peak_bw=%u agg_avg=%u agg_peak=%u",
		  __get_str(path_name),
		  __get_str(dev),
		  __get_str(node_name),
		  __entry->avg_bw,
		  __entry->peak_bw,
		  __entry->node_avg_bw,
		  __entry->node_peak_bw)
);

TRACE_EVENT(icc_set_bw_end,

	TP_PROTO(struct icc_path *p, int ret),

	TP_ARGS(p, ret),

	TP_STRUCT__entry(
		__string(path_name, p->name)
		__string(dev, dev_name(p->reqs[0].dev))
		__field(int, ret)
	),

	TP_fast_assign(
		__assign_str(path_name);
		__assign_str(dev);
		__entry->ret = ret;
	),

	TP_printk("path=%s dev=%s ret=%d",
		  __get_str(path_name),
		  __get_str(dev),
		  __entry->ret)
);

#endif /* _TRACE_INTERCONNECT_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>

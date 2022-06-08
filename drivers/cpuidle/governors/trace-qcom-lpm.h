/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#if !defined(_TRACE_QCOM_LPM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_LPM_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_lpm

#include <linux/tracepoint.h>

TRACE_EVENT(lpm_gov_select,

	TP_PROTO(int idx, s64 qos, u64 sleep, u64 reason),

	TP_ARGS(idx, qos, sleep, reason),

	TP_STRUCT__entry(
			 __field(int, idx)
			 __field(s64, qos)
			 __field(u64, sleep)
			 __field(u64, reason)
	),

	TP_fast_assign(
		       __entry->idx = idx;
		       __entry->qos = qos;
		       __entry->sleep = sleep;
		       __entry->reason = reason;
	),

	TP_printk("state:%d qos-us:%lld sleep-us:%llu reason:%#x",
		  __entry->idx, __entry->qos, __entry->sleep, __entry->reason)
);

TRACE_EVENT(gov_pred_select,

	TP_PROTO(u32 predtype, u64 predicted, u32 tmr_time),

	TP_ARGS(predtype, predicted, tmr_time),

	TP_STRUCT__entry(
		__field(u32, predtype)
		__field(u64, predicted)
		__field(u32, tmr_time)
	),

	TP_fast_assign(
		__entry->predtype = predtype;
		__entry->predicted = predicted;
		__entry->tmr_time = tmr_time;
	),

	TP_printk("pred:%u time:%lu tmr_time:%u",
		__entry->predtype, __entry->predicted, __entry->tmr_time)
);

TRACE_EVENT(gov_pred_hist,

	TP_PROTO(int idx, int residency, int tmr),

	TP_ARGS(idx, tmr, residency),

	TP_STRUCT__entry(
			 __field(int, idx)
			 __field(int, residency)
			 __field(int, tmr)
	),

	TP_fast_assign(
		       __entry->idx = idx;
		       __entry->residency = residency;
		       __entry->tmr = tmr;
	),

	TP_printk("idx:%d residency=%d, tmr=%d", __entry->idx, __entry->residency, __entry->tmr)
);

#endif /* _TRACE_QCOM_LPM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-qcom-lpm

#include <trace/define_trace.h>

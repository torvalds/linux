/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 *****************************************************************************/

#if !defined(__IWLWIFI_DEVICE_TRACE_UCODE) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_UCODE

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_ucode

TRACE_EVENT(iwlwifi_dev_ucode_cont_event,
	TP_PROTO(const struct device *dev, u32 time, u32 data, u32 ev),
	TP_ARGS(dev, time, data, ev),
	TP_STRUCT__entry(
		DEV_ENTRY

		__field(u32, time)
		__field(u32, data)
		__field(u32, ev)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->time = time;
		__entry->data = data;
		__entry->ev = ev;
	),
	TP_printk("[%s] EVT_LOGT:%010u:0x%08x:%04u",
		  __get_str(dev), __entry->time, __entry->data, __entry->ev)
);

TRACE_EVENT(iwlwifi_dev_ucode_wrap_event,
	TP_PROTO(const struct device *dev, u32 wraps, u32 n_entry, u32 p_entry),
	TP_ARGS(dev, wraps, n_entry, p_entry),
	TP_STRUCT__entry(
		DEV_ENTRY

		__field(u32, wraps)
		__field(u32, n_entry)
		__field(u32, p_entry)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->wraps = wraps;
		__entry->n_entry = n_entry;
		__entry->p_entry = p_entry;
	),
	TP_printk("[%s] wraps=#%02d n=0x%X p=0x%X",
		  __get_str(dev), __entry->wraps, __entry->n_entry,
		  __entry->p_entry)
);
#endif /* __IWLWIFI_DEVICE_TRACE_UCODE */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace-ucode
#include <trace/define_trace.h>

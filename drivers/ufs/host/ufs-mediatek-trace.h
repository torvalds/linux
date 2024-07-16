/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ufs_mtk

#if !defined(_TRACE_EVENT_UFS_MEDIATEK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENT_UFS_MEDIATEK_H

#include <linux/tracepoint.h>

TRACE_EVENT(ufs_mtk_event,
	TP_PROTO(unsigned int type, unsigned int data),
	TP_ARGS(type, data),

	TP_STRUCT__entry(
		__field(unsigned int, type)
		__field(unsigned int, data)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->data = data;
	),

	TP_printk("ufs: event=%u data=%u",
		  __entry->type, __entry->data)
);

TRACE_EVENT(ufs_mtk_clk_scale,
	TP_PROTO(const char *name, bool scale_up, unsigned long clk_rate),
	TP_ARGS(name, scale_up, clk_rate),

	TP_STRUCT__entry(
		__field(const char*, name)
		__field(bool, scale_up)
		__field(unsigned long, clk_rate)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->scale_up = scale_up;
		__entry->clk_rate = clk_rate;
	),

	TP_printk("ufs: clk (%s) scaled %s @ %lu",
		  __entry->name,
		  __entry->scale_up ? "up" : "down",
		  __entry->clk_rate)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/ufs/host
#define TRACE_INCLUDE_FILE ufs-mediatek-trace
#include <trace/define_trace.h>

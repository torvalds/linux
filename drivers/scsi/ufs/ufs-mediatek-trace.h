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

	TP_printk("ufs:event=%u data=%u",
		  __entry->type, __entry->data)
	);
#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/scsi/ufs/
#define TRACE_INCLUDE_FILE ufs-mediatek-trace
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bus_prof

#if !defined(_TRACE_BUS_PROF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BUS_PROF_H
#include "bus_prof.h"
#include <linux/tracepoint.h>

TRACE_EVENT(memory_miss_last_sample,

	TP_PROTO(u64 qtime, struct llcc_miss_buf *master_buf0, struct llcc_miss_buf *master_buf1),

	TP_ARGS(qtime, master_buf0, master_buf1),

	TP_STRUCT__entry(
		__field(u64, qtime)
		__field(u8, master1)
		__field(u16, miss1)
		__field(u8, master2)
		__field(u16, miss2)
	),

	TP_fast_assign(
		__entry->qtime = qtime;
		__entry->master1 = master_buf0->master_id;
		__entry->miss1 = master_buf0->miss_info;
		__entry->master2 = master_buf1->master_id;
		__entry->miss2 = master_buf1->miss_info;
	),

	TP_printk("qtime=%llu master1=%u miss1=%u master2=%u miss2=%u",
		__entry->qtime,
		__entry->master1,
		__entry->miss1,
		__entry->master2,
		__entry->miss2)
);
#endif /* _TRACE_BUS_PROF_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/soc/qcom/dcvs

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-bus-prof

#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM icc_bwmon

#if !defined(_TRACE_ICC_BWMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ICC_BWMON_H
#include <linux/tracepoint.h>

TRACE_EVENT(qcom_bwmon_update,
	    TP_PROTO(const char *name,
		     unsigned int meas_kbps, unsigned int up_kbps, unsigned int down_kbps),

	    TP_ARGS(name, meas_kbps, up_kbps, down_kbps),

	    TP_STRUCT__entry(
			     __string(name, name)
			     __field(unsigned int, meas_kbps)
			     __field(unsigned int, up_kbps)
			     __field(unsigned int, down_kbps)
	    ),

	    TP_fast_assign(
			   __assign_str(name);
			   __entry->meas_kbps = meas_kbps;
			   __entry->up_kbps = up_kbps;
			   __entry->down_kbps = down_kbps;
	    ),

	    TP_printk("name=%s meas_kbps=%u up_kbps=%u down_kbps=%u",
		      __get_str(name),
		      __entry->meas_kbps,
		      __entry->up_kbps,
		      __entry->down_kbps)
);

#endif /* _TRACE_ICC_BWMON_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/soc/qcom/

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_icc-bwmon

#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Linaro Ltd
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pmic_pdcharger_ulog

#if !defined(_TRACE_PMIC_PDCHARGER_ULOG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PMIC_PDCHARGER_ULOG_H

#include <linux/tracepoint.h>

TRACE_EVENT(pmic_pdcharger_ulog_msg,
	TP_PROTO(char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
		__string(msg, msg)
	),
	TP_fast_assign(
		__assign_str(msg);
	),
	TP_printk("%s", __get_str(msg))
);

#endif /* _TRACE_PMIC_PDCHARGER_ULOG_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pmic_pdcharger_ulog

#include <trace/define_trace.h>

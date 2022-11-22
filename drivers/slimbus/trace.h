/* SPDX-License-Identifier: GPL-2.0-only
 *
 * trace.h - Slimbus Controller Trace Support
 *
 * Copyright (C) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM slimbus

#if !defined(__SLIMBUS_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __SLIMBUS_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <asm/byteorder.h>
#define MAX_MSG_LEN             100

TRACE_EVENT(slimbus_dbg,
	TP_PROTO(const char *func, struct va_format *vaf),
	TP_ARGS(func, vaf),
	TP_STRUCT__entry(
		__string(func, func)
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		__assign_str(func, func);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
					MAX_MSG_LEN, vaf->fmt,
					*vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("%s: %s", __get_str(func), __get_str(msg))
);

#endif /* __SLIMBUS_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>

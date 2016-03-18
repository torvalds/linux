/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM adf

#if !defined(__VIDEO_ADF_ADF_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __VIDEO_ADF_ADF_TRACE_H

#include <linux/tracepoint.h>
#include <video/adf.h>

TRACE_EVENT(adf_event,
	TP_PROTO(struct adf_obj *obj, enum adf_event_type type),
	TP_ARGS(obj, type),

	TP_STRUCT__entry(
		__string(name, obj->name)
		__field(enum adf_event_type, type)
		__array(char, type_str, 32)
	),
	TP_fast_assign(
		__assign_str(name, obj->name);
		__entry->type = type;
		strlcpy(__entry->type_str, adf_event_type_str(obj, type),
				sizeof(__entry->type_str));
	),
	TP_printk("obj=%s type=%u (%s)",
			__get_str(name),
			__entry->type,
			__entry->type_str)
);

TRACE_EVENT(adf_event_enable,
	TP_PROTO(struct adf_obj *obj, enum adf_event_type type),
	TP_ARGS(obj, type),

	TP_STRUCT__entry(
		__string(name, obj->name)
		__field(enum adf_event_type, type)
		__array(char, type_str, 32)
	),
	TP_fast_assign(
		__assign_str(name, obj->name);
		__entry->type = type;
		strlcpy(__entry->type_str, adf_event_type_str(obj, type),
				sizeof(__entry->type_str));
	),
	TP_printk("obj=%s type=%u (%s)",
			__get_str(name),
			__entry->type,
			__entry->type_str)
);

TRACE_EVENT(adf_event_disable,
	TP_PROTO(struct adf_obj *obj, enum adf_event_type type),
	TP_ARGS(obj, type),

	TP_STRUCT__entry(
		__string(name, obj->name)
		__field(enum adf_event_type, type)
		__array(char, type_str, 32)
	),
	TP_fast_assign(
		__assign_str(name, obj->name);
		__entry->type = type;
		strlcpy(__entry->type_str, adf_event_type_str(obj, type),
				sizeof(__entry->type_str));
	),
	TP_printk("obj=%s type=%u (%s)",
			__get_str(name),
			__entry->type,
			__entry->type_str)
);

#endif /* __VIDEO_ADF_ADF_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE adf_trace
#include <trace/define_trace.h>

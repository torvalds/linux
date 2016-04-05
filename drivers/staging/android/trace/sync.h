#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../drivers/staging/android/trace
#define TRACE_SYSTEM sync

#if !defined(_TRACE_SYNC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYNC_H

#include "../sync.h"
#include <linux/tracepoint.h>

TRACE_EVENT(sync_timeline,
	TP_PROTO(struct sync_timeline *timeline),

	TP_ARGS(timeline),

	TP_STRUCT__entry(
			__string(name, timeline->name)
			__array(char, value, 32)
	),

	TP_fast_assign(
			__assign_str(name, timeline->name);
			if (timeline->ops->timeline_value_str) {
				timeline->ops->timeline_value_str(timeline,
							__entry->value,
							sizeof(__entry->value));
			} else {
				__entry->value[0] = '\0';
			}
	),

	TP_printk("name=%s value=%s", __get_str(name), __entry->value)
);

#endif /* if !defined(_TRACE_SYNC_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>

#if !defined(_TRACE_MALI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MALI_H

#include <linux/stringify.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mali
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE mali_linux_trace

/**
 * mali_timeline_event - called from the central collection point (_mali_profiling_add_event)
 * @event_id: ORed together bitfields representing a type of event
 * In the future we might add
 * @timestamp
 * @data[5] - this currently includes thread and process id's - we should have EGLConfig or similar too
 *
 * Just make a record of the event_id, we'll decode it elsewhere
 */
TRACE_EVENT(mali_timeline_event,

	TP_PROTO(unsigned int event_id),

	TP_ARGS(event_id),

	TP_STRUCT__entry(
		__field(	int,	event_id	)
	),

	TP_fast_assign(
		__entry->event_id = event_id;
	),

	TP_printk("event=%d", __entry->event_id)
);

/**
 * mali_hw_counter - called from the ????
 * @event_id: event being counted
 * In the future we might add
 * @timestamp ??
 *
 * Just make a record of the event_id and value
 */
TRACE_EVENT(mali_hw_counter, 

	TP_PROTO(unsigned int event_id, unsigned int value),

	TP_ARGS(event_id, value),

	TP_STRUCT__entry(
		__field(	int, 	event_id	)
		__field(	int,	value	)
	),

	TP_fast_assign(
		__entry->event_id = event_id;
	),

	TP_printk("event %d = %d", __entry->event_id, __entry->value)
);

/**
 * mali_sw_counter
 * @event_id: counter id
 */
TRACE_EVENT(mali_sw_counter,

    TP_PROTO(unsigned int event_id, signed long long value),

    TP_ARGS(event_id, value),

    TP_STRUCT__entry(
        __field(    int,    event_id    )
        __field(    long long,  value   )
    ),

    TP_fast_assign(
        __entry->event_id = event_id;
    ),

    TP_printk("event %d = %lld", __entry->event_id, __entry->value)
);

#endif /*  _TRACE_MALI_H */

#undef TRACE_INCLUDE_PATH
#undef linux
#define TRACE_INCLUDE_PATH .

/* This part must be outside protection */
#include <trace/define_trace.h>

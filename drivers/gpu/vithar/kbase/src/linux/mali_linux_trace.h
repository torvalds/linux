/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#if !defined(_TRACE_MALI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MALI_H

#include <linux/stringify.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mali
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE mali_linux_trace

/**
 * mali_timeline_event - called from mali_kbase_core_linux.c
 * @event_id: ORed together bitfields representing a type of event, made with the GATOR_MAKE_EVENT() macro.
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
 * mali_hw_counter - not currently used
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
 * mali_sw_counter - not currently used
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
#define TRACE_INCLUDE_PATH MALI_KBASE_SRC_LINUX_PATH

/* This part must be outside protection */
#include <trace/define_trace.h>

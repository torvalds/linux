/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#if !defined (MALI_LINUX_TRACE_H) || defined (TRACE_HEADER_MULTI_READ)
#define MALI_LINUX_TRACE_H

#include <linux/types.h>

#include <linux/stringify.h>
#include <linux/tracepoint.h>

#undef  TRACE_SYSTEM
#define TRACE_SYSTEM mali
#define TRACE_SYSTEM_STRING __stringfy(TRACE_SYSTEM)

#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE mali_linux_trace

/**
 * Define the tracepoint used to communicate the status of a GPU. Called
 * when a GPU turns on or turns off.
 *
 * @param event_id The type of the event. This parameter is a bitfield
 *  encoding the type of the event.
 *
 * @param d0 First data parameter.
 * @param d1 Second data parameter.
 * @param d2 Third data parameter.
 * @param d3 Fourth data parameter.
 * @param d4 Fifth data parameter.
 */
TRACE_EVENT(mali_timeline_event,

	    TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1,
		     unsigned int d2, unsigned int d3, unsigned int d4),

	    TP_ARGS(event_id, d0, d1, d2, d3, d4),

	    TP_STRUCT__entry(
		    __field(unsigned int, event_id)
		    __field(unsigned int, d0)
		    __field(unsigned int, d1)
		    __field(unsigned int, d2)
		    __field(unsigned int, d3)
		    __field(unsigned int, d4)
	    ),

	    TP_fast_assign(
		    __entry->event_id = event_id;
		    __entry->d0 = d0;
		    __entry->d1 = d1;
		    __entry->d2 = d2;
		    __entry->d3 = d3;
		    __entry->d4 = d4;
	    ),

	    TP_printk("event=%d", __entry->event_id)
	   );

/**
 * Define a tracepoint used to regsiter the value of a hardware counter.
 * Hardware counters belonging to the vertex or fragment processor are
 * reported via this tracepoint each frame, whilst L2 cache hardware
 * counters are reported continuously.
 *
 * @param counter_id The counter ID.
 * @param value The value of the counter.
 */
TRACE_EVENT(mali_hw_counter,

	    TP_PROTO(unsigned int counter_id, unsigned int value),

	    TP_ARGS(counter_id, value),

	    TP_STRUCT__entry(
		    __field(unsigned int, counter_id)
		    __field(unsigned int, value)
	    ),

	    TP_fast_assign(
		    __entry->counter_id = counter_id;
	    ),

	    TP_printk("event %d = %d", __entry->counter_id, __entry->value)
	   );

/**
 * Define a tracepoint used to send a bundle of software counters.
 *
 * @param counters The bundle of counters.
 */
TRACE_EVENT(mali_sw_counters,

	    TP_PROTO(pid_t pid, pid_t tid, void *surface_id, unsigned int *counters),

	    TP_ARGS(pid, tid, surface_id, counters),

	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(pid_t, tid)
		    __field(void *, surface_id)
		    __field(unsigned int *, counters)
	    ),

	    TP_fast_assign(
		    __entry->pid = pid;
		    __entry->tid = tid;
		    __entry->surface_id = surface_id;
		    __entry->counters = counters;
	    ),

	    TP_printk("counters were %s", __entry->counters == NULL ? "NULL" : "not NULL")
	   );

/**
 * Define a tracepoint used to gather core activity for systrace
 * @param pid The process id for which the core activity originates from
 * @param active If the core is active (1) or not (0)
 * @param core_type The type of core active, either GP (1) or PP (0)
 * @param core_id The core id that is active for the core_type
 * @param frame_builder_id The frame builder id associated with this core activity
 * @param flush_id The flush id associated with this core activity
 */
TRACE_EVENT(mali_core_active,

	    TP_PROTO(pid_t pid, unsigned int active, unsigned int core_type, unsigned int core_id, unsigned int frame_builder_id, unsigned int flush_id),

	    TP_ARGS(pid, active, core_type, core_id, frame_builder_id, flush_id),

	    TP_STRUCT__entry(
		    __field(pid_t, pid)
		    __field(unsigned int, active)
		    __field(unsigned int, core_type)
		    __field(unsigned int, core_id)
		    __field(unsigned int, frame_builder_id)
		    __field(unsigned int, flush_id)
	    ),

	    TP_fast_assign(
		    __entry->pid = pid;
		    __entry->active = active;
		    __entry->core_type = core_type;
		    __entry->core_id = core_id;
		    __entry->frame_builder_id = frame_builder_id;
		    __entry->flush_id = flush_id;
	    ),

	    TP_printk("%s|%d|%s%i:%x|%d", __entry->active ? "S" : "F", __entry->pid, __entry->core_type ? "GP" : "PP", __entry->core_id, __entry->flush_id, __entry->frame_builder_id)
	   );

#endif /* MALI_LINUX_TRACE_H */

/* This part must exist outside the header guard. */
#include <trace/define_trace.h>


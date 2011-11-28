#undef TRACE_SYSTEM
#define TRACE_SYSTEM lttng

#if !defined(_TRACE_LTTNG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LTTNG_H

#include <linux/tracepoint.h>

TRACE_EVENT(lttng_metadata,

	TP_PROTO(const char *str),

	TP_ARGS(str),

	/*
	 * Not exactly a string: more a sequence of bytes (dynamic
	 * array) without the length. This is a dummy anyway: we only
	 * use this declaration to generate an event metadata entry.
	 */
	TP_STRUCT__entry(
		__string(	str,		str	)
	),

	TP_fast_assign(
		tp_strcpy(str, str)
	),

	TP_printk("")
)

#endif /*  _TRACE_LTTNG_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

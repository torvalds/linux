#undef TRACE_SYSTEM
#define TRACE_SYSTEM printk

#if !defined(_TRACE_PRINTK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PRINTK_H

#include <linux/tracepoint.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))

TRACE_EVENT(console,
	TP_PROTO(const char *text, size_t len),

	TP_ARGS(text, len),

	TP_STRUCT__entry(
		__dynamic_array_text(char, msg, len)
	),

	TP_fast_assign(
		tp_memcpy_dyn(msg, text)
	),

	TP_printk("%s", __get_str(msg))
)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))

TRACE_EVENT_CONDITION(console,
	TP_PROTO(const char *log_buf, unsigned start, unsigned end,
		 unsigned log_buf_len),

	TP_ARGS(log_buf, start, end, log_buf_len),

	TP_CONDITION(start != end),

	TP_STRUCT__entry(
		__dynamic_array_text(char, msg, end - start)
	),

	TP_fast_assign(
		tp_memcpy_dyn(msg, log_buf + start)
	),

	TP_printk("%s", __get_str(msg))
)

#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)) */

TRACE_EVENT_CONDITION(console,
	TP_PROTO(const char *log_buf, unsigned start, unsigned end,
		 unsigned log_buf_len),

	TP_ARGS(log_buf, start, end, log_buf_len),

	TP_CONDITION(start != end),

	TP_STRUCT__entry(
		__dynamic_array_text_2(char, msg,
			(start & (log_buf_len - 1)) > (end & (log_buf_len - 1))
				? log_buf_len - (start & (log_buf_len - 1))
				: end - start,
			(start & (log_buf_len - 1)) > (end & (log_buf_len - 1))
				? end & (log_buf_len - 1)
				: 0)
	),

	TP_fast_assign(
		tp_memcpy_dyn_2(msg,
			log_buf + (start & (log_buf_len - 1)),
			log_buf)
	),

	TP_printk("%s", __get_str(msg))
)

#endif

#endif /* _TRACE_PRINTK_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

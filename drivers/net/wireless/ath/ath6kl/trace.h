#if !defined(_ATH6KL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)

#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>
#include "wmi.h"

#if !defined(_ATH6KL_TRACE_H)
static inline unsigned int ath6kl_get_wmi_id(void *buf, size_t buf_len)
{
	struct wmi_cmd_hdr *hdr = buf;

	if (buf_len < sizeof(*hdr))
		return 0;

	return le16_to_cpu(hdr->cmd_id);
}
#endif /* __ATH6KL_TRACE_H */

#define _ATH6KL_TRACE_H

/* create empty functions when tracing is disabled */
#if !defined(CONFIG_ATH6KL_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif /* !CONFIG_ATH6KL_TRACING || __CHECKER__ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ath6kl

TRACE_EVENT(ath6kl_wmi_cmd,
	TP_PROTO(void *buf, size_t buf_len),

	TP_ARGS(buf, buf_len),

	TP_STRUCT__entry(
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->id = ath6kl_get_wmi_id(buf, buf_len);
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"id %d len %d",
		__entry->id, __entry->buf_len
	)
);

TRACE_EVENT(ath6kl_wmi_event,
	TP_PROTO(void *buf, size_t buf_len),

	TP_ARGS(buf, buf_len),

	TP_STRUCT__entry(
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->id = ath6kl_get_wmi_id(buf, buf_len);
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"id %d len %d",
		__entry->id, __entry->buf_len
	)
);

#endif /* _ ATH6KL_TRACE_H || TRACE_HEADER_MULTI_READ*/

/* we don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>

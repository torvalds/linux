/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_ATH6KL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)

#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>
#include "wmi.h"
#include "hif.h"

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
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
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
		"id %d len %zd",
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
		"id %d len %zd",
		__entry->id, __entry->buf_len
	)
);

TRACE_EVENT(ath6kl_sdio,
	TP_PROTO(unsigned int addr, int flags,
		 void *buf, size_t buf_len),

	TP_ARGS(addr, flags, buf, buf_len),

	TP_STRUCT__entry(
		__field(unsigned int, tx)
		__field(unsigned int, addr)
		__field(int, flags)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->flags = flags;
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);

		if (flags & HIF_WRITE)
			__entry->tx = 1;
		else
			__entry->tx = 0;
	),

	TP_printk(
		"%s addr 0x%x flags 0x%x len %zd\n",
		__entry->tx ? "tx" : "rx",
		__entry->addr,
		__entry->flags,
		__entry->buf_len
	)
);

TRACE_EVENT(ath6kl_sdio_scat,
	TP_PROTO(unsigned int addr, int flags, unsigned int total_len,
		 unsigned int entries, struct hif_scatter_item *list),

	TP_ARGS(addr, flags, total_len, entries, list),

	TP_STRUCT__entry(
		__field(unsigned int, tx)
		__field(unsigned int, addr)
		__field(int, flags)
		__field(unsigned int, entries)
		__field(size_t, total_len)
		__dynamic_array(unsigned int, len_array, entries)
		__dynamic_array(u8, data, total_len)
	),

	TP_fast_assign(
		unsigned int *len_array;
		int i, offset = 0;
		size_t len;

		__entry->addr = addr;
		__entry->flags = flags;
		__entry->entries = entries;
		__entry->total_len = total_len;

		if (flags & HIF_WRITE)
			__entry->tx = 1;
		else
			__entry->tx = 0;

		len_array = __get_dynamic_array(len_array);

		for (i = 0; i < entries; i++) {
			len = list[i].len;

			memcpy((u8 *) __get_dynamic_array(data) + offset,
			       list[i].buf, len);

			len_array[i] = len;
			offset += len;
		}
	),

	TP_printk(
		"%s addr 0x%x flags 0x%x entries %d total_len %zd\n",
		__entry->tx ? "tx" : "rx",
		__entry->addr,
		__entry->flags,
		__entry->entries,
		__entry->total_len
	)
);

TRACE_EVENT(ath6kl_sdio_irq,
	TP_PROTO(void *buf, size_t buf_len),

	TP_ARGS(buf, buf_len),

	TP_STRUCT__entry(
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"irq len %zd\n", __entry->buf_len
	)
);

TRACE_EVENT(ath6kl_htc_rx,
	TP_PROTO(int status, int endpoint, void *buf,
		 size_t buf_len),

	TP_ARGS(status, endpoint, buf, buf_len),

	TP_STRUCT__entry(
		__field(int, status)
		__field(int, endpoint)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->endpoint = endpoint;
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"status %d endpoint %d len %zd\n",
		__entry->status,
		__entry->endpoint,
		__entry->buf_len
	)
);

TRACE_EVENT(ath6kl_htc_tx,
	TP_PROTO(int status, int endpoint, void *buf,
		 size_t buf_len),

	TP_ARGS(status, endpoint, buf, buf_len),

	TP_STRUCT__entry(
		__field(int, status)
		__field(int, endpoint)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->endpoint = endpoint;
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"status %d endpoint %d len %zd\n",
		__entry->status,
		__entry->endpoint,
		__entry->buf_len
	)
);

#define ATH6KL_MSG_MAX 200

DECLARE_EVENT_CLASS(ath6kl_log_event,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(
		__dynamic_array(char, msg, ATH6KL_MSG_MAX)
	),
	TP_fast_assign(
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       ATH6KL_MSG_MAX,
				       vaf->fmt,
				       *vaf->va) >= ATH6KL_MSG_MAX);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(ath6kl_log_event, ath6kl_log_err,
	     TP_PROTO(struct va_format *vaf),
	     TP_ARGS(vaf)
);

DEFINE_EVENT(ath6kl_log_event, ath6kl_log_warn,
	     TP_PROTO(struct va_format *vaf),
	     TP_ARGS(vaf)
);

DEFINE_EVENT(ath6kl_log_event, ath6kl_log_info,
	     TP_PROTO(struct va_format *vaf),
	     TP_ARGS(vaf)
);

TRACE_EVENT(ath6kl_log_dbg,
	TP_PROTO(unsigned int level, struct va_format *vaf),
	TP_ARGS(level, vaf),
	TP_STRUCT__entry(
		__field(unsigned int, level)
		__dynamic_array(char, msg, ATH6KL_MSG_MAX)
	),
	TP_fast_assign(
		__entry->level = level;
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       ATH6KL_MSG_MAX,
				       vaf->fmt,
				       *vaf->va) >= ATH6KL_MSG_MAX);
	),
	TP_printk("%s", __get_str(msg))
);

TRACE_EVENT(ath6kl_log_dbg_dump,
	TP_PROTO(const char *msg, const char *prefix,
		 const void *buf, size_t buf_len),

	TP_ARGS(msg, prefix, buf, buf_len),

	TP_STRUCT__entry(
		__string(msg, msg)
		__string(prefix, prefix)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(msg, msg);
		__assign_str(prefix, prefix);
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s/%s\n", __get_str(prefix), __get_str(msg)
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

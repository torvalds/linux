/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)

#include <linux/tracepoint.h>
#include "core.h"

#if !defined(_TRACE_H_)
static inline u32 ath10k_frm_hdr_len(const void *buf, size_t len)
{
	const struct ieee80211_hdr *hdr = buf;

	/* In some rare cases (e.g. fcs error) device reports frame buffer
	 * shorter than what frame header implies (e.g. len = 0). The buffer
	 * can still be accessed so do a simple min() to guarantee caller
	 * doesn't get value greater than len.
	 */
	return min_t(u32, len, ieee80211_hdrlen(hdr->frame_control));
}
#endif

#define _TRACE_H_

/* create empty functions when tracing is disabled */
#if !defined(CONFIG_ATH10K_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif /* !CONFIG_ATH10K_TRACING || __CHECKER__ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ath10k

#define ATH10K_MSG_MAX 400

DECLARE_EVENT_CLASS(ath10k_log_event,
	TP_PROTO(struct ath10k *ar, struct va_format *vaf),
	TP_ARGS(ar, vaf),
	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__dynamic_array(char, msg, ATH10K_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       ATH10K_MSG_MAX,
				       vaf->fmt,
				       *vaf->va) >= ATH10K_MSG_MAX);
	),
	TP_printk(
		"%s %s %s",
		__get_str(driver),
		__get_str(device),
		__get_str(msg)
	)
);

DEFINE_EVENT(ath10k_log_event, ath10k_log_err,
	     TP_PROTO(struct ath10k *ar, struct va_format *vaf),
	     TP_ARGS(ar, vaf)
);

DEFINE_EVENT(ath10k_log_event, ath10k_log_warn,
	     TP_PROTO(struct ath10k *ar, struct va_format *vaf),
	     TP_ARGS(ar, vaf)
);

DEFINE_EVENT(ath10k_log_event, ath10k_log_info,
	     TP_PROTO(struct ath10k *ar, struct va_format *vaf),
	     TP_ARGS(ar, vaf)
);

TRACE_EVENT(ath10k_log_dbg,
	TP_PROTO(struct ath10k *ar, unsigned int level, struct va_format *vaf),
	TP_ARGS(ar, level, vaf),
	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(unsigned int, level)
		__dynamic_array(char, msg, ATH10K_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->level = level;
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       ATH10K_MSG_MAX,
				       vaf->fmt,
				       *vaf->va) >= ATH10K_MSG_MAX);
	),
	TP_printk(
		"%s %s %s",
		__get_str(driver),
		__get_str(device),
		__get_str(msg)
	)
);

TRACE_EVENT(ath10k_log_dbg_dump,
	TP_PROTO(struct ath10k *ar, const char *msg, const char *prefix,
		 const void *buf, size_t buf_len),

	TP_ARGS(ar, msg, prefix, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__string(msg, msg)
		__string(prefix, prefix)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__assign_str(msg, msg);
		__assign_str(prefix, prefix);
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s %s %s/%s\n",
		__get_str(driver),
		__get_str(device),
		__get_str(prefix),
		__get_str(msg)
	)
);

TRACE_EVENT(ath10k_wmi_cmd,
	TP_PROTO(struct ath10k *ar, int id, const void *buf, size_t buf_len,
		 int ret),

	TP_ARGS(ar, id, buf, buf_len, ret),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
		__field(int, ret)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->id = id;
		__entry->buf_len = buf_len;
		__entry->ret = ret;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s %s id %d len %zu ret %d",
		__get_str(driver),
		__get_str(device),
		__entry->id,
		__entry->buf_len,
		__entry->ret
	)
);

TRACE_EVENT(ath10k_wmi_event,
	TP_PROTO(struct ath10k *ar, int id, const void *buf, size_t buf_len),

	TP_ARGS(ar, id, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->id = id;
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s %s id %d len %zu",
		__get_str(driver),
		__get_str(device),
		__entry->id,
		__entry->buf_len
	)
);

TRACE_EVENT(ath10k_htt_stats,
	TP_PROTO(struct ath10k *ar, const void *buf, size_t buf_len),

	TP_ARGS(ar, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s %s len %zu",
		__get_str(driver),
		__get_str(device),
		__entry->buf_len
	)
);

TRACE_EVENT(ath10k_wmi_dbglog,
	TP_PROTO(struct ath10k *ar, const void *buf, size_t buf_len),

	TP_ARGS(ar, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk(
		"%s %s len %zu",
		__get_str(driver),
		__get_str(device),
		__entry->buf_len
	)
);

TRACE_EVENT(ath10k_htt_pktlog,
	    TP_PROTO(struct ath10k *ar, const void *buf, u16 buf_len),

	TP_ARGS(ar, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u16, buf_len)
		__dynamic_array(u8, pktlog, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(pktlog), buf, buf_len);
	),

	TP_printk(
		"%s %s size %hu",
		__get_str(driver),
		__get_str(device),
		__entry->buf_len
	 )
);

TRACE_EVENT(ath10k_htt_tx,
	    TP_PROTO(struct ath10k *ar, u16 msdu_id, u16 msdu_len,
		     u8 vdev_id, u8 tid),

	TP_ARGS(ar, msdu_id, msdu_len, vdev_id, tid),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u16, msdu_id)
		__field(u16, msdu_len)
		__field(u8, vdev_id)
		__field(u8, tid)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->msdu_id = msdu_id;
		__entry->msdu_len = msdu_len;
		__entry->vdev_id = vdev_id;
		__entry->tid = tid;
	),

	TP_printk(
		"%s %s msdu_id %d msdu_len %d vdev_id %d tid %d",
		__get_str(driver),
		__get_str(device),
		__entry->msdu_id,
		__entry->msdu_len,
		__entry->vdev_id,
		__entry->tid
	 )
);

TRACE_EVENT(ath10k_txrx_tx_unref,
	    TP_PROTO(struct ath10k *ar, u16 msdu_id),

	TP_ARGS(ar, msdu_id),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u16, msdu_id)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->msdu_id = msdu_id;
	),

	TP_printk(
		"%s %s msdu_id %d",
		__get_str(driver),
		__get_str(device),
		__entry->msdu_id
	 )
);

DECLARE_EVENT_CLASS(ath10k_hdr_event,
		    TP_PROTO(struct ath10k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(size_t, len)
		__dynamic_array(u8, data, ath10k_frm_hdr_len(data, len))
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->len = ath10k_frm_hdr_len(data, len);
		memcpy(__get_dynamic_array(data), data, __entry->len);
	),

	TP_printk(
		"%s %s len %zu\n",
		__get_str(driver),
		__get_str(device),
		__entry->len
	)
);

DECLARE_EVENT_CLASS(ath10k_payload_event,
		    TP_PROTO(struct ath10k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(size_t, len)
		__dynamic_array(u8, payload, (len -
					      ath10k_frm_hdr_len(data, len)))
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->len = len - ath10k_frm_hdr_len(data, len);
		memcpy(__get_dynamic_array(payload),
		       data + ath10k_frm_hdr_len(data, len), __entry->len);
	),

	TP_printk(
		"%s %s len %zu\n",
		__get_str(driver),
		__get_str(device),
		__entry->len
	)
);

DEFINE_EVENT(ath10k_hdr_event, ath10k_tx_hdr,
	     TP_PROTO(struct ath10k *ar, const void *data, size_t len),
	     TP_ARGS(ar, data, len)
);

DEFINE_EVENT(ath10k_payload_event, ath10k_tx_payload,
	     TP_PROTO(struct ath10k *ar, const void *data, size_t len),
	     TP_ARGS(ar, data, len)
);

DEFINE_EVENT(ath10k_hdr_event, ath10k_rx_hdr,
	     TP_PROTO(struct ath10k *ar, const void *data, size_t len),
	     TP_ARGS(ar, data, len)
);

DEFINE_EVENT(ath10k_payload_event, ath10k_rx_payload,
	     TP_PROTO(struct ath10k *ar, const void *data, size_t len),
	     TP_ARGS(ar, data, len)
);

TRACE_EVENT(ath10k_htt_rx_desc,
	    TP_PROTO(struct ath10k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u16, len)
		__dynamic_array(u8, rxdesc, len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->len = len;
		memcpy(__get_dynamic_array(rxdesc), data, len);
	),

	TP_printk(
		"%s %s rxdesc len %d",
		__get_str(driver),
		__get_str(device),
		__entry->len
	 )
);

TRACE_EVENT(ath10k_wmi_diag_container,
	    TP_PROTO(struct ath10k *ar,
		     u8 type,
		     u32 timestamp,
		     u32 code,
		     u16 len,
		     const void *data),

	TP_ARGS(ar, type, timestamp, code, len, data),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u8, type)
		__field(u32, timestamp)
		__field(u32, code)
		__field(u16, len)
		__dynamic_array(u8, data, len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->type = type;
		__entry->timestamp = timestamp;
		__entry->code = code;
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
	),

	TP_printk(
		"%s %s diag container type %hhu timestamp %u code %u len %d",
		__get_str(driver),
		__get_str(device),
		__entry->type,
		__entry->timestamp,
		__entry->code,
		__entry->len
	)
);

TRACE_EVENT(ath10k_wmi_diag,
	    TP_PROTO(struct ath10k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->dev))
		__string(driver, dev_driver_string(ar->dev))
		__field(u16, len)
		__dynamic_array(u8, data, len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->dev));
		__assign_str(driver, dev_driver_string(ar->dev));
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
	),

	TP_printk(
		"%s %s tlv diag len %d",
		__get_str(driver),
		__get_str(device),
		__entry->len
	)
);

#endif /* _TRACE_H_ || TRACE_HEADER_MULTI_READ*/

/* we don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>

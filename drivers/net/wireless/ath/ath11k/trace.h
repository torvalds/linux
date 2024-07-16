/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#if !defined(_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)

#include <linux/tracepoint.h>
#include "core.h"

#define _TRACE_H_

/* create empty functions when tracing is disabled */
#if !defined(CONFIG_ATH11K_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {} \
static inline bool trace_##name##_enabled(void) \
{						\
	return false;				\
}

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif /* !CONFIG_ATH11K_TRACING || __CHECKER__ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ath11k

#define ATH11K_MSG_MAX 400

TRACE_EVENT(ath11k_htt_pktlog,
	    TP_PROTO(struct ath11k *ar, const void *buf, u16 buf_len,
		     u32 pktlog_checksum),

	TP_ARGS(ar, buf, buf_len, pktlog_checksum),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, buf_len)
		__field(u32, pktlog_checksum)
		__dynamic_array(u8, pktlog, buf_len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__entry->buf_len = buf_len;
		__entry->pktlog_checksum = pktlog_checksum;
		memcpy(__get_dynamic_array(pktlog), buf, buf_len);
	),

	TP_printk(
		"%s %s size %u pktlog_checksum %d",
		__get_str(driver),
		__get_str(device),
		__entry->buf_len,
		__entry->pktlog_checksum
	 )
);

TRACE_EVENT(ath11k_htt_ppdu_stats,
	    TP_PROTO(struct ath11k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, len)
		__dynamic_array(u8, ppdu, len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__entry->len = len;
		memcpy(__get_dynamic_array(ppdu), data, len);
	),

	TP_printk(
		"%s %s ppdu len %d",
		__get_str(driver),
		__get_str(device),
		__entry->len
	 )
);

TRACE_EVENT(ath11k_htt_rxdesc,
	    TP_PROTO(struct ath11k *ar, const void *data, size_t log_type, size_t len),

	TP_ARGS(ar, data, log_type, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, len)
		__field(u16, log_type)
		__dynamic_array(u8, rxdesc, len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__entry->len = len;
		__entry->log_type = log_type;
		memcpy(__get_dynamic_array(rxdesc), data, len);
	),

	TP_printk(
		"%s %s rxdesc len %d type %d",
		__get_str(driver),
		__get_str(device),
		__entry->len,
		__entry->log_type
	 )
);

DECLARE_EVENT_CLASS(ath11k_log_event,
		    TP_PROTO(struct ath11k_base *ab, struct va_format *vaf),
	TP_ARGS(ab, vaf),
	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__vstring(msg, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__assign_vstr(msg, vaf->fmt, vaf->va);
	),
	TP_printk(
		"%s %s %s",
		__get_str(driver),
		__get_str(device),
		__get_str(msg)
	)
);

DEFINE_EVENT(ath11k_log_event, ath11k_log_err,
	     TP_PROTO(struct ath11k_base *ab, struct va_format *vaf),
	     TP_ARGS(ab, vaf)
);

DEFINE_EVENT(ath11k_log_event, ath11k_log_warn,
	     TP_PROTO(struct ath11k_base *ab, struct va_format *vaf),
	     TP_ARGS(ab, vaf)
);

DEFINE_EVENT(ath11k_log_event, ath11k_log_info,
	     TP_PROTO(struct ath11k_base *ab, struct va_format *vaf),
	     TP_ARGS(ab, vaf)
);

TRACE_EVENT(ath11k_wmi_cmd,
	    TP_PROTO(struct ath11k_base *ab, int id, const void *buf, size_t buf_len),

	TP_ARGS(ab, id, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
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

TRACE_EVENT(ath11k_wmi_event,
	    TP_PROTO(struct ath11k_base *ab, int id, const void *buf, size_t buf_len),

	TP_ARGS(ab, id, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__field(unsigned int, id)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
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

TRACE_EVENT(ath11k_log_dbg,
	    TP_PROTO(struct ath11k_base *ab, unsigned int level, struct va_format *vaf),

	TP_ARGS(ab, level, vaf),

	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__field(unsigned int, level)
		__dynamic_array(char, msg, ATH11K_MSG_MAX)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__entry->level = level;
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       ATH11K_MSG_MAX, vaf->fmt,
				       *vaf->va) >= ATH11K_MSG_MAX);
	),

	TP_printk(
		"%s %s %s",
		__get_str(driver),
		__get_str(device),
		__get_str(msg)
	)
);

TRACE_EVENT(ath11k_log_dbg_dump,
	    TP_PROTO(struct ath11k_base *ab, const char *msg, const char *prefix,
		     const void *buf, size_t buf_len),

	TP_ARGS(ab, msg, prefix, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__string(msg, msg)
		__string(prefix, prefix)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
		__assign_str(msg);
		__assign_str(prefix);
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

TRACE_EVENT(ath11k_wmi_diag,
	    TP_PROTO(struct ath11k_base *ab, const void *data, size_t len),

	TP_ARGS(ab, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ab->dev))
		__string(driver, dev_driver_string(ab->dev))
		__field(u16, len)
		__dynamic_array(u8, data, len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(driver);
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

TRACE_EVENT(ath11k_ps_timekeeper,
	    TP_PROTO(struct ath11k *ar, const void *peer_addr,
		     u32 peer_ps_timestamp, u8 peer_ps_state),
	TP_ARGS(ar, peer_addr, peer_ps_timestamp, peer_ps_state),

	TP_STRUCT__entry(__string(device, dev_name(ar->ab->dev))
			 __string(driver, dev_driver_string(ar->ab->dev))
			 __dynamic_array(u8, peer_addr, ETH_ALEN)
			 __field(u8, peer_ps_state)
			 __field(u32, peer_ps_timestamp)
	),

	TP_fast_assign(__assign_str(device);
		       __assign_str(driver);
		       memcpy(__get_dynamic_array(peer_addr), peer_addr,
			      ETH_ALEN);
		       __entry->peer_ps_state = peer_ps_state;
		       __entry->peer_ps_timestamp = peer_ps_timestamp;
	),

	TP_printk("%s %s %u %u",
		  __get_str(driver),
		  __get_str(device),
		  __entry->peer_ps_state,
		  __entry->peer_ps_timestamp
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

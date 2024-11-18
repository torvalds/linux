/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_glink

#if !defined(__QCOM_GLINK_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __QCOM_GLINK_TRACE_H__

#include <linux/tracepoint.h>
#include "qcom_glink_native.h"


TRACE_EVENT(qcom_glink_cmd_version,
	TP_PROTO(const char *remote, unsigned int version, unsigned int features, bool tx),
	TP_ARGS(remote, version, features, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__field(u32, version)
		__field(u32, features)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__entry->version = version;
		__entry->features = features;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s version: %u features: %#x",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __entry->version,
		  __entry->features
	)
);
#define trace_qcom_glink_cmd_version_tx(...) trace_qcom_glink_cmd_version(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_version_rx(...) trace_qcom_glink_cmd_version(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_version_ack,
	TP_PROTO(const char *remote, unsigned int version, unsigned int features, bool tx),
	TP_ARGS(remote, version, features, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__field(u32, version)
		__field(u32, features)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__entry->version = version;
		__entry->features = features;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s version: %u features: %#x",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __entry->version,
		  __entry->features
	)
);
#define trace_qcom_glink_cmd_version_ack_tx(...) trace_qcom_glink_cmd_version_ack(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_version_ack_rx(...) trace_qcom_glink_cmd_version_ack(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_open,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u]",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid
	)
);
#define trace_qcom_glink_cmd_open_tx(...) trace_qcom_glink_cmd_open(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_open_rx(...) trace_qcom_glink_cmd_open(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_close,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u]",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid
	)
);
#define trace_qcom_glink_cmd_close_tx(...) trace_qcom_glink_cmd_close(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_close_rx(...) trace_qcom_glink_cmd_close(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_open_ack,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u]",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid
	)
);
#define trace_qcom_glink_cmd_open_ack_tx(...) trace_qcom_glink_cmd_open_ack(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_open_ack_rx(...) trace_qcom_glink_cmd_open_ack(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_intent,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, size_t count, size_t size, u32 liid, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, count, size, liid, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(u32, count)
		__field(u32, size)
		__field(u32, liid)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->count = count;
		__entry->size = size;
		__entry->liid = liid;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] count: %d [size: %d liid: %d]",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->count,
		  __entry->size,
		  __entry->liid
	)
);
#define trace_qcom_glink_cmd_intent_tx(...) trace_qcom_glink_cmd_intent(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_intent_rx(...) trace_qcom_glink_cmd_intent(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_rx_done,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, u32 iid, bool reuse, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, iid, reuse, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(u32, iid)
		__field(bool, reuse)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->iid = iid;
		__entry->reuse = reuse;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] iid: %d reuse: %d",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->iid,
		  __entry->reuse
	)
);
#define trace_qcom_glink_cmd_rx_done_tx(...) trace_qcom_glink_cmd_rx_done(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_rx_done_rx(...) trace_qcom_glink_cmd_rx_done(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_rx_intent_req,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, size_t size, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, size, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(u32, size)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->size = size;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] size: %d",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->size
	)
);
#define trace_qcom_glink_cmd_rx_intent_req_tx(...) trace_qcom_glink_cmd_rx_intent_req(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_rx_intent_req_rx(...) trace_qcom_glink_cmd_rx_intent_req(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_rx_intent_req_ack,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, bool granted, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, granted, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(bool, granted)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->granted = granted;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] granted: %d",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->granted
	)
);
#define trace_qcom_glink_cmd_rx_intent_req_ack_tx(...) trace_qcom_glink_cmd_rx_intent_req_ack(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_rx_intent_req_ack_rx(...) trace_qcom_glink_cmd_rx_intent_req_ack(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_tx_data,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, u32 iid, u32 chunk_size, u32 left_size, bool cont, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, iid, chunk_size, left_size, cont, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(u32, iid)
		__field(u32, chunk_size)
		__field(u32, left_size)
		__field(bool, cont)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->iid = iid;
		__entry->chunk_size = chunk_size;
		__entry->left_size = left_size;
		__entry->cont = cont;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] iid: %d chunk_size: %d left_size: %d cont: %d",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->iid,
		  __entry->chunk_size,
		  __entry->left_size,
		  __entry->cont
	)
);
#define trace_qcom_glink_cmd_tx_data_tx(...) trace_qcom_glink_cmd_tx_data(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_tx_data_rx(...) trace_qcom_glink_cmd_tx_data(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_close_ack,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u]",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid
	)
);
#define trace_qcom_glink_cmd_close_ack_tx(...) trace_qcom_glink_cmd_close_ack(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_close_ack_rx(...) trace_qcom_glink_cmd_close_ack(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_read_notif,
	TP_PROTO(const char *remote, bool tx),
	TP_ARGS(remote, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote)
	)
);
#define trace_qcom_glink_cmd_read_notif_tx(...) trace_qcom_glink_cmd_read_notif(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_read_notif_rx(...) trace_qcom_glink_cmd_read_notif(__VA_ARGS__, false)

TRACE_EVENT(qcom_glink_cmd_signal,
	TP_PROTO(const char *remote, const char *channel, u16 lcid, u16 rcid, unsigned int signals, bool tx),
	TP_ARGS(remote, channel, lcid, rcid, signals, tx),
	TP_STRUCT__entry(
		__string(remote, remote)
		__string(channel, channel)
		__field(u16, lcid)
		__field(u16, rcid)
		__field(u32, signals)
		__field(bool, tx)
	),
	TP_fast_assign(
		__assign_str(remote);
		__assign_str(channel);
		__entry->lcid = lcid;
		__entry->rcid = rcid;
		__entry->signals = signals;
		__entry->tx = tx;
	),
	TP_printk("%s remote: %s channel: %s[%u/%u] signals: %#x",
		  __entry->tx ? "tx" : "rx",
		  __get_str(remote),
		  __get_str(channel),
		  __entry->lcid,
		  __entry->rcid,
		  __entry->signals
	)
);
#define trace_qcom_glink_cmd_signal_tx(...) trace_qcom_glink_cmd_signal(__VA_ARGS__, true)
#define trace_qcom_glink_cmd_signal_rx(...) trace_qcom_glink_cmd_signal(__VA_ARGS__, false)

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE qcom_glink_trace

#include <trace/define_trace.h>

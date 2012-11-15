/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(__TRACE_BRCMSMAC_H) || defined(TRACE_HEADER_MULTI_READ)

#define __TRACE_BRCMSMAC_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/tracepoint.h>
#include "mac80211_if.h"

#ifndef CONFIG_BRCM_TRACING
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmsmac

/*
 * We define a tracepoint, its arguments, its printk format and its
 * 'fast binary record' layout.
 */
TRACE_EVENT(brcms_timer,
	/* TPPROTO is the prototype of the function called by this tracepoint */
	TP_PROTO(struct brcms_timer *t),
	/*
	 * TPARGS(firstarg, p) are the parameters names, same as found in the
	 * prototype.
	 */
	TP_ARGS(t),
	/*
	 * Fast binary tracing: define the trace record via TP_STRUCT__entry().
	 * You can think about it like a regular C structure local variable
	 * definition.
	 */
	TP_STRUCT__entry(
		__field(uint, ms)
		__field(uint, set)
		__field(uint, periodic)
	),
	TP_fast_assign(
		__entry->ms = t->ms;
		__entry->set = t->set;
		__entry->periodic = t->periodic;
	),
	TP_printk(
		"ms=%u set=%u periodic=%u",
		__entry->ms, __entry->set, __entry->periodic
	)
);

TRACE_EVENT(brcms_dpc,
	TP_PROTO(unsigned long data),
	TP_ARGS(data),
	TP_STRUCT__entry(
		__field(unsigned long, data)
	),
	TP_fast_assign(
		__entry->data = data;
	),
	TP_printk(
		"data=%p",
		(void *)__entry->data
	)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmsmac_tx

TRACE_EVENT(brcms_txdesc,
	TP_PROTO(const struct device *dev,
		 void *txh, size_t txh_len),
	TP_ARGS(dev, txh, txh_len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__dynamic_array(u8, txh, txh_len)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		memcpy(__get_dynamic_array(txh), txh, txh_len);
	),
	TP_printk("[%s] txdesc", __get_str(dev))
);

TRACE_EVENT(brcms_txstatus,
	TP_PROTO(const struct device *dev, u16 framelen, u16 frameid,
		 u16 status, u16 lasttxtime, u16 sequence, u16 phyerr,
		 u16 ackphyrxsh),
	TP_ARGS(dev, framelen, frameid, status, lasttxtime, sequence, phyerr,
		ackphyrxsh),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(u16, framelen)
		__field(u16, frameid)
		__field(u16, status)
		__field(u16, lasttxtime)
		__field(u16, sequence)
		__field(u16, phyerr)
		__field(u16, ackphyrxsh)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->framelen = framelen;
		__entry->frameid = frameid;
		__entry->status = status;
		__entry->lasttxtime = lasttxtime;
		__entry->sequence = sequence;
		__entry->phyerr = phyerr;
		__entry->ackphyrxsh = ackphyrxsh;
	),
	TP_printk("[%s] FrameId %#04x TxStatus %#04x LastTxTime %#04x "
		  "Seq %#04x PHYTxStatus %#04x RxAck %#04x",
		  __get_str(dev), __entry->frameid, __entry->status,
		  __entry->lasttxtime, __entry->sequence, __entry->phyerr,
		  __entry->ackphyrxsh)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmsmac_msg

#define MAX_MSG_LEN	100

DECLARE_EVENT_CLASS(brcms_msg_event,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       MAX_MSG_LEN, vaf->fmt,
				       *vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(brcms_msg_event, brcms_info,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(brcms_msg_event, brcms_warn,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(brcms_msg_event, brcms_err,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(brcms_msg_event, brcms_crit,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

TRACE_EVENT(brcms_dbg,
	TP_PROTO(u32 level, const char *func, struct va_format *vaf),
	TP_ARGS(level, func, vaf),
	TP_STRUCT__entry(
		__field(u32, level)
		__string(func, func)
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		__entry->level = level;
		__assign_str(func, func);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       MAX_MSG_LEN, vaf->fmt,
				       *vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("%s: %s", __get_str(func), __get_str(msg))
);

#endif /* __TRACE_BRCMSMAC_H */

#ifdef CONFIG_BRCM_TRACING

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE brcms_trace_events

#include <trace/define_trace.h>

#endif /* CONFIG_BRCM_TRACING */

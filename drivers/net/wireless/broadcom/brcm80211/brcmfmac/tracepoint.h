// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013 Broadcom Corporation
 */
#if !defined(BRCMF_TRACEPOINT_H_) || defined(TRACE_HEADER_MULTI_READ)
#define BRCMF_TRACEPOINT_H_

#include <linux/types.h>
#include <linux/tracepoint.h>

#ifndef CONFIG_BRCM_TRACING

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)

#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}

#endif /* CONFIG_BRCM_TRACING */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	brcmfmac

#define MAX_MSG_LEN		100

TRACE_EVENT(brcmf_err,
	TP_PROTO(const char *func, struct va_format *vaf),
	TP_ARGS(func, vaf),
	TP_STRUCT__entry(
		__string(func, func)
		__vstring(msg, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__assign_str(func, func);
		__assign_vstr(msg, vaf->fmt, vaf->va);
	),
	TP_printk("%s: %s", __get_str(func), __get_str(msg))
);

TRACE_EVENT(brcmf_dbg,
	TP_PROTO(u32 level, const char *func, struct va_format *vaf),
	TP_ARGS(level, func, vaf),
	TP_STRUCT__entry(
		__field(u32, level)
		__string(func, func)
		__vstring(msg, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__entry->level = level;
		__assign_str(func, func);
		__assign_vstr(msg, vaf->fmt, vaf->va);
	),
	TP_printk("%s: %s", __get_str(func), __get_str(msg))
);

TRACE_EVENT(brcmf_hexdump,
	TP_PROTO(void *data, size_t len),
	TP_ARGS(data, len),
	TP_STRUCT__entry(
		__field(unsigned long, len)
		__field(unsigned long, addr)
		__dynamic_array(u8, hdata, len)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->addr = (unsigned long)data;
		memcpy(__get_dynamic_array(hdata), data, len);
	),
	TP_printk("hexdump [addr=%lx, length=%lu]", __entry->addr, __entry->len)
);

TRACE_EVENT(brcmf_bcdchdr,
	TP_PROTO(void *data),
	TP_ARGS(data),
	TP_STRUCT__entry(
		__field(u8, flags)
		__field(u8, prio)
		__field(u8, flags2)
		__field(u32, siglen)
		__dynamic_array(u8, signal, *((u8 *)data + 3) * 4)
	),
	TP_fast_assign(
		__entry->flags = *(u8 *)data;
		__entry->prio = *((u8 *)data + 1);
		__entry->flags2 = *((u8 *)data + 2);
		__entry->siglen = *((u8 *)data + 3) * 4;
		memcpy(__get_dynamic_array(signal),
		       (u8 *)data + 4, __entry->siglen);
	),
	TP_printk("bcdc: prio=%d siglen=%d", __entry->prio, __entry->siglen)
);

#ifndef SDPCM_RX
#define SDPCM_RX	0
#endif
#ifndef SDPCM_TX
#define SDPCM_TX	1
#endif
#ifndef SDPCM_GLOM
#define SDPCM_GLOM	2
#endif

TRACE_EVENT(brcmf_sdpcm_hdr,
	TP_PROTO(u8 dir, void *data),
	TP_ARGS(dir, data),
	TP_STRUCT__entry(
		__field(u8, dir)
		__field(u16, len)
		__dynamic_array(u8, hdr, dir == SDPCM_GLOM ? 20 : 12)
	),
	TP_fast_assign(
		memcpy(__get_dynamic_array(hdr), data, dir == SDPCM_GLOM ? 20 : 12);
		__entry->len = *(u8 *)data | (*((u8 *)data + 1) << 8);
		__entry->dir = dir;
	),
	TP_printk("sdpcm: %s len %u, seq %d",
		  __entry->dir == SDPCM_RX ? "RX" : "TX",
		  __entry->len, ((u8 *)__get_dynamic_array(hdr))[4])
);

#ifdef CONFIG_BRCM_TRACING

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE tracepoint

#include <trace/define_trace.h>

#endif /* CONFIG_BRCM_TRACING */

#endif /* BRCMF_TRACEPOINT_H_ */

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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmsmac

#if !defined(__TRACE_BRCMSMAC_H) || defined(TRACE_HEADER_MULTI_READ)

#define __TRACE_BRCMSMAC_H

#include <linux/tracepoint.h>
#include "mac80211_if.h"

#ifndef CONFIG_BRCM_TRACING
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif

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

#endif /* __TRACE_BRCMSMAC_H */

#ifdef CONFIG_BRCM_TRACING

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE brcms_trace_events

#include <trace/define_trace.h>

#endif /* CONFIG_BRCM_TRACING */

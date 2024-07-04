/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#if !defined(_TRACE_RPMH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPMH_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpmh

#include <linux/tracepoint.h>
#include "rpmh-internal.h"

TRACE_EVENT(rpmh_tx_done,

	TP_PROTO(struct rsc_drv *d, int m, const struct tcs_request *r),

	TP_ARGS(d, m, r),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(int, m)
			 __field(u32, addr)
			 __field(u32, data)
	),

	TP_fast_assign(
		       __assign_str(name);
		       __entry->m = m;
		       __entry->addr = r->cmds[0].addr;
		       __entry->data = r->cmds[0].data;
	),

	TP_printk("%s: ack: tcs-m: %d addr: %#x data: %#x",
		  __get_str(name), __entry->m, __entry->addr, __entry->data)
);

TRACE_EVENT(rpmh_send_msg,

	TP_PROTO(struct rsc_drv *d, int m, enum rpmh_state state, int n, u32 h,
		 const struct tcs_cmd *c),

	TP_ARGS(d, m, state, n, h, c),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(int, m)
			 __field(u32, state)
			 __field(int, n)
			 __field(u32, hdr)
			 __field(u32, addr)
			 __field(u32, data)
			 __field(bool, wait)
	),

	TP_fast_assign(
		       __assign_str(name);
		       __entry->m = m;
		       __entry->state = state;
		       __entry->n = n;
		       __entry->hdr = h;
		       __entry->addr = c->addr;
		       __entry->data = c->data;
		       __entry->wait = c->wait;
	),

	TP_printk("%s: tcs(m): %d [%s] cmd(n): %d msgid: %#x addr: %#x data: %#x complete: %d",
		  __get_str(name), __entry->m,
		  __print_symbolic(__entry->state,
				   { RPMH_SLEEP_STATE, "sleep" },
				   { RPMH_WAKE_ONLY_STATE, "wake" },
				   { RPMH_ACTIVE_ONLY_STATE, "active" }),
		  __entry->n,
		  __entry->hdr,
		  __entry->addr, __entry->data, __entry->wait)
);

#endif /* _TRACE_RPMH_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-rpmh

#include <trace/define_trace.h>

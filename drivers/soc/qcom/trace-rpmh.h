/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2019, 2021, The Linux Foundation. All rights reserved.
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
		       __assign_str(name, d->name);
		       __entry->m = m;
		       __entry->addr = r->cmds[0].addr;
		       __entry->data = r->cmds[0].data;
	),

	TP_printk("%s: ack: tcs-m: %d addr: %#x data: %#x",
		  __get_str(name), __entry->m, __entry->addr, __entry->data)
);

TRACE_EVENT(rpmh_send_msg,

	TP_PROTO(struct rsc_drv *d, int m, int n, u32 h,
		 const struct tcs_cmd *c),

	TP_ARGS(d, m, n, h, c),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(int, m)
			 __field(int, n)
			 __field(u32, hdr)
			 __field(u32, addr)
			 __field(u32, data)
			 __field(bool, wait)
	),

	TP_fast_assign(
		       __assign_str(name, d->name);
		       __entry->m = m;
		       __entry->n = n;
		       __entry->hdr = h;
		       __entry->addr = c->addr;
		       __entry->data = c->data;
		       __entry->wait = c->wait;
	),

	TP_printk("%s: send-msg: tcs(m): %d cmd(n): %d msgid: %#x addr: %#x data: %#x complete: %d",
		  __get_str(name), __entry->m, __entry->n, __entry->hdr,
		  __entry->addr, __entry->data, __entry->wait)
);

TRACE_EVENT(rpmh_solver_set,

	TP_PROTO(struct rsc_drv *d, bool set),

	TP_ARGS(d, set),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(bool, set)
	),

	TP_fast_assign(
		       __assign_str(name, d->name);
		       __entry->set = set;
	),

	TP_printk("%s: solver mode set: %d",
		  __get_str(name), __entry->set)
);

TRACE_EVENT(rpmh_switch_channel,

	TP_PROTO(struct rsc_drv *d, int ch, int ret),

	TP_ARGS(d, ch, ret),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(int, ch)
			 __field(int, ret)
	),

	TP_fast_assign(
		       __assign_str(name, d->name);
		       __entry->ch = ch;
		       __entry->ret = ret;
	),

	TP_printk("%s: channel switched to: %d ret: %d",
		  __get_str(name), __entry->ch, __entry->ret)
);

TRACE_EVENT(rpmh_drv_enable,

	TP_PROTO(struct rsc_drv *d, bool enable, int ret),

	TP_ARGS(d, enable, ret),

	TP_STRUCT__entry(
			 __string(name, d->name)
			 __field(bool, enable)
			 __field(int, ret)
	),

	TP_fast_assign(
		       __assign_str(name, d->name);
		       __entry->enable = enable;
		       __entry->ret = ret;
	),

	TP_printk("%s: drv enable: %d ret: %d",
		  __get_str(name), __entry->enable, __entry->ret)
);

#endif /* _TRACE_RPMH_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-rpmh

#include <trace/define_trace.h>

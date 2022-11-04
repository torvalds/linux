/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2022 1&1 IONOS SE. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtrs_srv

#if !defined(_TRACE_RTRS_SRV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RTRS_SRV_H

#include <linux/tracepoint.h>

struct rtrs_srv_op;
struct rtrs_srv_con;
struct rtrs_srv_path;

TRACE_DEFINE_ENUM(RTRS_SRV_CONNECTING);
TRACE_DEFINE_ENUM(RTRS_SRV_CONNECTED);
TRACE_DEFINE_ENUM(RTRS_SRV_CLOSING);
TRACE_DEFINE_ENUM(RTRS_SRV_CLOSED);

#define show_rtrs_srv_state(x) \
	__print_symbolic(x, \
		{ RTRS_SRV_CONNECTING,	"CONNECTING" }, \
		{ RTRS_SRV_CONNECTED,	"CONNECTED" }, \
		{ RTRS_SRV_CLOSING,	"CLOSING" }, \
		{ RTRS_SRV_CLOSED,	"CLOSED" })

TRACE_EVENT(send_io_resp_imm,
	TP_PROTO(struct rtrs_srv_op *id,
		 bool need_inval,
		 bool always_invalidate,
		 int errno),

	TP_ARGS(id, need_inval, always_invalidate, errno),

	TP_STRUCT__entry(
		__field(u8, dir)
		__field(bool, need_inval)
		__field(bool, always_invalidate)
		__field(u32, msg_id)
		__field(int, wr_cnt)
		__field(u32, signal_interval)
		__field(int, state)
		__field(int, errno)
		__array(char, sessname, NAME_MAX)
	),

	TP_fast_assign(
		struct rtrs_srv_con *con = id->con;
		struct rtrs_path *s = con->c.path;
		struct rtrs_srv_path *srv_path = to_srv_path(s);

		__entry->dir = id->dir;
		__entry->state = srv_path->state;
		__entry->errno = errno;
		__entry->need_inval = need_inval;
		__entry->always_invalidate = always_invalidate;
		__entry->msg_id = id->msg_id;
		__entry->wr_cnt = atomic_read(&con->c.wr_cnt);
		__entry->signal_interval = s->signal_interval;
		memcpy(__entry->sessname, kobject_name(&srv_path->kobj), NAME_MAX);
	),

	TP_printk("sess='%s' state='%s' dir=%s err='%d' inval='%d' glob-inval='%d' msgid='%u' wrcnt='%d' sig-interval='%u'",
		   __entry->sessname,
		   show_rtrs_srv_state(__entry->state),
		   __print_symbolic(__entry->dir,
			 { READ,  "READ" },
			 { WRITE, "WRITE" }),
		   __entry->errno,
		   __entry->need_inval,
		   __entry->always_invalidate,
		   __entry->msg_id,
		   __entry->wr_cnt,
		   __entry->signal_interval
	)
);

#endif /* _TRACE_RTRS_SRV_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE rtrs-srv-trace
#include <trace/define_trace.h>


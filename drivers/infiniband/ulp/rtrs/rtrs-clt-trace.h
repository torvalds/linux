/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2022 1&1 IONOS SE. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtrs_clt

#if !defined(_TRACE_RTRS_CLT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RTRS_CLT_H

#include <linux/tracepoint.h>

struct rtrs_clt_path;
struct rtrs_clt_sess;

TRACE_DEFINE_ENUM(RTRS_CLT_CONNECTING);
TRACE_DEFINE_ENUM(RTRS_CLT_CONNECTING_ERR);
TRACE_DEFINE_ENUM(RTRS_CLT_RECONNECTING);
TRACE_DEFINE_ENUM(RTRS_CLT_CONNECTED);
TRACE_DEFINE_ENUM(RTRS_CLT_CLOSING);
TRACE_DEFINE_ENUM(RTRS_CLT_CLOSED);
TRACE_DEFINE_ENUM(RTRS_CLT_DEAD);

#define show_rtrs_clt_state(x) \
	__print_symbolic(x, \
		{ RTRS_CLT_CONNECTING,		"CONNECTING" }, \
		{ RTRS_CLT_CONNECTING_ERR,	"CONNECTING_ERR" }, \
		{ RTRS_CLT_RECONNECTING,	"RECONNECTING" }, \
		{ RTRS_CLT_CONNECTED,		"CONNECTED" }, \
		{ RTRS_CLT_CLOSING,		"CLOSING" }, \
		{ RTRS_CLT_CLOSED,		"CLOSED" }, \
		{ RTRS_CLT_DEAD,		"DEAD" })

DECLARE_EVENT_CLASS(rtrs_clt_conn_class,
	TP_PROTO(struct rtrs_clt_path *clt_path),

	TP_ARGS(clt_path),

	TP_STRUCT__entry(
		__field(int, state)
		__field(int, reconnect_attempts)
		__field(int, max_reconnect_attempts)
		__field(int, fail_cnt)
		__field(int, success_cnt)
		__array(char, sessname, NAME_MAX)
	),

	TP_fast_assign(
		struct rtrs_clt_sess *clt = clt_path->clt;

		__entry->state = clt_path->state;
		__entry->reconnect_attempts = clt_path->reconnect_attempts;
		__entry->max_reconnect_attempts = clt->max_reconnect_attempts;
		__entry->fail_cnt = clt_path->stats->reconnects.fail_cnt;
		__entry->success_cnt = clt_path->stats->reconnects.successful_cnt;
		memcpy(__entry->sessname, kobject_name(&clt_path->kobj), NAME_MAX);
	),

	TP_printk("RTRS-CLT: sess='%s' state=%s attempts='%d' max-attempts='%d' fail='%d' success='%d'",
		   __entry->sessname,
		   show_rtrs_clt_state(__entry->state),
		   __entry->reconnect_attempts,
		   __entry->max_reconnect_attempts,
		   __entry->fail_cnt,
		   __entry->success_cnt
	)
);

#define DEFINE_CLT_CONN_EVENT(name) \
DEFINE_EVENT(rtrs_clt_conn_class, rtrs_##name, \
	TP_PROTO(struct rtrs_clt_path *clt_path), \
	TP_ARGS(clt_path))

DEFINE_CLT_CONN_EVENT(clt_reconnect_work);
DEFINE_CLT_CONN_EVENT(clt_close_conns);
DEFINE_CLT_CONN_EVENT(rdma_error_recovery);

#endif /* _TRACE_RTRS_CLT_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE rtrs-clt-trace
#include <trace/define_trace.h>


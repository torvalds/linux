/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace point definitions for the RDMA Connect Manager.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2020 Oracle and/or its affiliates.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ib_cma

#if !defined(_TRACE_IB_CMA_H) || defined(TRACE_HEADER_MULTI_READ)

#define _TRACE_IB_CMA_H

#include <linux/tracepoint.h>
#include <rdma/ib_cm.h>
#include <trace/misc/rdma.h>

/*
 * enum ib_cm_state, from include/rdma/ib_cm.h
 */
#define IB_CM_STATE_LIST					\
	ib_cm_state(IDLE)					\
	ib_cm_state(LISTEN)					\
	ib_cm_state(REQ_SENT)					\
	ib_cm_state(REQ_RCVD)					\
	ib_cm_state(MRA_REQ_SENT)				\
	ib_cm_state(MRA_REQ_RCVD)				\
	ib_cm_state(REP_SENT)					\
	ib_cm_state(REP_RCVD)					\
	ib_cm_state(MRA_REP_SENT)				\
	ib_cm_state(MRA_REP_RCVD)				\
	ib_cm_state(ESTABLISHED)				\
	ib_cm_state(DREQ_SENT)					\
	ib_cm_state(DREQ_RCVD)					\
	ib_cm_state(TIMEWAIT)					\
	ib_cm_state(SIDR_REQ_SENT)				\
	ib_cm_state_end(SIDR_REQ_RCVD)

#undef  ib_cm_state
#undef  ib_cm_state_end
#define ib_cm_state(x)		TRACE_DEFINE_ENUM(IB_CM_##x);
#define ib_cm_state_end(x)	TRACE_DEFINE_ENUM(IB_CM_##x);

IB_CM_STATE_LIST

#undef  ib_cm_state
#undef  ib_cm_state_end
#define ib_cm_state(x)		{ IB_CM_##x, #x },
#define ib_cm_state_end(x)	{ IB_CM_##x, #x }

#define show_ib_cm_state(x) \
		__print_symbolic(x, IB_CM_STATE_LIST)

/*
 * enum ib_cm_lap_state, from include/rdma/ib_cm.h
 */
#define IB_CM_LAP_STATE_LIST					\
	ib_cm_lap_state(LAP_UNINIT)				\
	ib_cm_lap_state(LAP_IDLE)				\
	ib_cm_lap_state(LAP_SENT)				\
	ib_cm_lap_state(LAP_RCVD)				\
	ib_cm_lap_state(MRA_LAP_SENT)				\
	ib_cm_lap_state_end(MRA_LAP_RCVD)

#undef  ib_cm_lap_state
#undef  ib_cm_lap_state_end
#define ib_cm_lap_state(x)	TRACE_DEFINE_ENUM(IB_CM_##x);
#define ib_cm_lap_state_end(x)	TRACE_DEFINE_ENUM(IB_CM_##x);

IB_CM_LAP_STATE_LIST

#undef  ib_cm_lap_state
#undef  ib_cm_lap_state_end
#define ib_cm_lap_state(x)	{ IB_CM_##x, #x },
#define ib_cm_lap_state_end(x)	{ IB_CM_##x, #x }

#define show_ib_cm_lap_state(x) \
		__print_symbolic(x, IB_CM_LAP_STATE_LIST)

/*
 * enum ib_cm_rej_reason, from include/rdma/ib_cm.h
 */
#define IB_CM_REJ_REASON_LIST					\
	ib_cm_rej_reason(REJ_NO_QP)				\
	ib_cm_rej_reason(REJ_NO_EEC)				\
	ib_cm_rej_reason(REJ_NO_RESOURCES)			\
	ib_cm_rej_reason(REJ_TIMEOUT)				\
	ib_cm_rej_reason(REJ_UNSUPPORTED)			\
	ib_cm_rej_reason(REJ_INVALID_COMM_ID)			\
	ib_cm_rej_reason(REJ_INVALID_COMM_INSTANCE)		\
	ib_cm_rej_reason(REJ_INVALID_SERVICE_ID)		\
	ib_cm_rej_reason(REJ_INVALID_TRANSPORT_TYPE)		\
	ib_cm_rej_reason(REJ_STALE_CONN)			\
	ib_cm_rej_reason(REJ_RDC_NOT_EXIST)			\
	ib_cm_rej_reason(REJ_INVALID_GID)			\
	ib_cm_rej_reason(REJ_INVALID_LID)			\
	ib_cm_rej_reason(REJ_INVALID_SL)			\
	ib_cm_rej_reason(REJ_INVALID_TRAFFIC_CLASS)		\
	ib_cm_rej_reason(REJ_INVALID_HOP_LIMIT)			\
	ib_cm_rej_reason(REJ_INVALID_PACKET_RATE)		\
	ib_cm_rej_reason(REJ_INVALID_ALT_GID)			\
	ib_cm_rej_reason(REJ_INVALID_ALT_LID)			\
	ib_cm_rej_reason(REJ_INVALID_ALT_SL)			\
	ib_cm_rej_reason(REJ_INVALID_ALT_TRAFFIC_CLASS)		\
	ib_cm_rej_reason(REJ_INVALID_ALT_HOP_LIMIT)		\
	ib_cm_rej_reason(REJ_INVALID_ALT_PACKET_RATE)		\
	ib_cm_rej_reason(REJ_PORT_CM_REDIRECT)			\
	ib_cm_rej_reason(REJ_PORT_REDIRECT)			\
	ib_cm_rej_reason(REJ_INVALID_MTU)			\
	ib_cm_rej_reason(REJ_INSUFFICIENT_RESP_RESOURCES)	\
	ib_cm_rej_reason(REJ_CONSUMER_DEFINED)			\
	ib_cm_rej_reason(REJ_INVALID_RNR_RETRY)			\
	ib_cm_rej_reason(REJ_DUPLICATE_LOCAL_COMM_ID)		\
	ib_cm_rej_reason(REJ_INVALID_CLASS_VERSION)		\
	ib_cm_rej_reason(REJ_INVALID_FLOW_LABEL)		\
	ib_cm_rej_reason(REJ_INVALID_ALT_FLOW_LABEL)		\
	ib_cm_rej_reason_end(REJ_VENDOR_OPTION_NOT_SUPPORTED)

#undef  ib_cm_rej_reason
#undef  ib_cm_rej_reason_end
#define ib_cm_rej_reason(x)	TRACE_DEFINE_ENUM(IB_CM_##x);
#define ib_cm_rej_reason_end(x)	TRACE_DEFINE_ENUM(IB_CM_##x);

IB_CM_REJ_REASON_LIST

#undef  ib_cm_rej_reason
#undef  ib_cm_rej_reason_end
#define ib_cm_rej_reason(x)	{ IB_CM_##x, #x },
#define ib_cm_rej_reason_end(x)	{ IB_CM_##x, #x }

#define show_ib_cm_rej_reason(x) \
		__print_symbolic(x, IB_CM_REJ_REASON_LIST)

DECLARE_EVENT_CLASS(icm_id_class,
	TP_PROTO(
		const struct ib_cm_id *cm_id
	),

	TP_ARGS(cm_id),

	TP_STRUCT__entry(
		__field(const void *, cm_id)	/* for eBPF scripts */
		__field(unsigned int, local_id)
		__field(unsigned int, remote_id)
		__field(unsigned long, state)
		__field(unsigned long, lap_state)
	),

	TP_fast_assign(
		__entry->cm_id = cm_id;
		__entry->local_id = be32_to_cpu(cm_id->local_id);
		__entry->remote_id = be32_to_cpu(cm_id->remote_id);
		__entry->state = cm_id->state;
		__entry->lap_state = cm_id->lap_state;
	),

	TP_printk("local_id=%u remote_id=%u state=%s lap_state=%s",
		__entry->local_id, __entry->remote_id,
		show_ib_cm_state(__entry->state),
		show_ib_cm_lap_state(__entry->lap_state)
	)
);

#define DEFINE_CM_SEND_EVENT(name)					\
		DEFINE_EVENT(icm_id_class,				\
				icm_send_##name,				\
				TP_PROTO(				\
					const struct ib_cm_id *cm_id	\
				),					\
				TP_ARGS(cm_id))

DEFINE_CM_SEND_EVENT(req);
DEFINE_CM_SEND_EVENT(rep);
DEFINE_CM_SEND_EVENT(dup_req);
DEFINE_CM_SEND_EVENT(dup_rep);
DEFINE_CM_SEND_EVENT(rtu);
DEFINE_CM_SEND_EVENT(mra);
DEFINE_CM_SEND_EVENT(sidr_req);
DEFINE_CM_SEND_EVENT(sidr_rep);
DEFINE_CM_SEND_EVENT(dreq);
DEFINE_CM_SEND_EVENT(drep);

TRACE_EVENT(icm_send_rej,
	TP_PROTO(
		const struct ib_cm_id *cm_id,
		enum ib_cm_rej_reason reason
	),

	TP_ARGS(cm_id, reason),

	TP_STRUCT__entry(
		__field(const void *, cm_id)
		__field(u32, local_id)
		__field(u32, remote_id)
		__field(unsigned long, state)
		__field(unsigned long, reason)
	),

	TP_fast_assign(
		__entry->cm_id = cm_id;
		__entry->local_id = be32_to_cpu(cm_id->local_id);
		__entry->remote_id = be32_to_cpu(cm_id->remote_id);
		__entry->state = cm_id->state;
		__entry->reason = reason;
	),

	TP_printk("local_id=%u remote_id=%u state=%s reason=%s",
		__entry->local_id, __entry->remote_id,
		show_ib_cm_state(__entry->state),
		show_ib_cm_rej_reason(__entry->reason)
	)
);

#define DEFINE_CM_ERR_EVENT(name)					\
		DEFINE_EVENT(icm_id_class,				\
				icm_##name##_err,			\
				TP_PROTO(				\
					const struct ib_cm_id *cm_id	\
				),					\
				TP_ARGS(cm_id))

DEFINE_CM_ERR_EVENT(send_cm_rtu);
DEFINE_CM_ERR_EVENT(establish);
DEFINE_CM_ERR_EVENT(no_listener);
DEFINE_CM_ERR_EVENT(send_drep);
DEFINE_CM_ERR_EVENT(dreq_unknown);
DEFINE_CM_ERR_EVENT(send_unknown_rej);
DEFINE_CM_ERR_EVENT(rej_unknown);
DEFINE_CM_ERR_EVENT(prepare_mra_unknown);
DEFINE_CM_ERR_EVENT(mra_unknown);
DEFINE_CM_ERR_EVENT(qp_init);
DEFINE_CM_ERR_EVENT(qp_rtr);
DEFINE_CM_ERR_EVENT(qp_rts);

DEFINE_EVENT(icm_id_class,						\
	icm_dreq_skipped,						\
	TP_PROTO(							\
		const struct ib_cm_id *cm_id				\
	),								\
	TP_ARGS(cm_id)							\
);

DECLARE_EVENT_CLASS(icm_local_class,
	TP_PROTO(
		unsigned int local_id,
		unsigned int remote_id
	),

	TP_ARGS(local_id, remote_id),

	TP_STRUCT__entry(
		__field(unsigned int, local_id)
		__field(unsigned int, remote_id)
	),

	TP_fast_assign(
		__entry->local_id = local_id;
		__entry->remote_id = remote_id;
	),

	TP_printk("local_id=%u remote_id=%u",
		__entry->local_id, __entry->remote_id
	)
);

#define DEFINE_CM_LOCAL_EVENT(name)					\
		DEFINE_EVENT(icm_local_class,				\
				icm_##name,				\
				TP_PROTO(				\
					unsigned int local_id,			\
					unsigned int remote_id			\
				),					\
				TP_ARGS(local_id, remote_id))

DEFINE_CM_LOCAL_EVENT(issue_rej);
DEFINE_CM_LOCAL_EVENT(issue_drep);
DEFINE_CM_LOCAL_EVENT(staleconn_err);
DEFINE_CM_LOCAL_EVENT(no_priv_err);

DECLARE_EVENT_CLASS(icm_remote_class,
	TP_PROTO(
		u32 remote_id
	),

	TP_ARGS(remote_id),

	TP_STRUCT__entry(
		__field(u32, remote_id)
	),

	TP_fast_assign(
		__entry->remote_id = remote_id;
	),

	TP_printk("remote_id=%u",
		__entry->remote_id
	)
);

#define DEFINE_CM_REMOTE_EVENT(name)					\
		DEFINE_EVENT(icm_remote_class,				\
				icm_##name,				\
				TP_PROTO(				\
					u32 remote_id			\
				),					\
				TP_ARGS(remote_id))

DEFINE_CM_REMOTE_EVENT(remote_no_priv_err);
DEFINE_CM_REMOTE_EVENT(insert_failed_err);

TRACE_EVENT(icm_send_rep_err,
	TP_PROTO(
		__be32 local_id,
		enum ib_cm_state state
	),

	TP_ARGS(local_id, state),

	TP_STRUCT__entry(
		__field(unsigned int, local_id)
		__field(unsigned long, state)
	),

	TP_fast_assign(
		__entry->local_id = be32_to_cpu(local_id);
		__entry->state = state;
	),

	TP_printk("local_id=%u state=%s",
		__entry->local_id, show_ib_cm_state(__entry->state)
	)
);

TRACE_EVENT(icm_rep_unknown_err,
	TP_PROTO(
		unsigned int local_id,
		unsigned int remote_id,
		enum ib_cm_state state
	),

	TP_ARGS(local_id, remote_id, state),

	TP_STRUCT__entry(
		__field(unsigned int, local_id)
		__field(unsigned int, remote_id)
		__field(unsigned long, state)
	),

	TP_fast_assign(
		__entry->local_id = local_id;
		__entry->remote_id = remote_id;
		__entry->state = state;
	),

	TP_printk("local_id=%u remote_id=%u state=%s",
		__entry->local_id, __entry->remote_id,
		show_ib_cm_state(__entry->state)
	)
);

TRACE_EVENT(icm_handler_err,
	TP_PROTO(
		enum ib_cm_event_type event
	),

	TP_ARGS(event),

	TP_STRUCT__entry(
		__field(unsigned long, event)
	),

	TP_fast_assign(
		__entry->event = event;
	),

	TP_printk("unhandled event=%s",
		rdma_show_ib_cm_event(__entry->event)
	)
);

TRACE_EVENT(icm_mad_send_err,
	TP_PROTO(
		enum ib_cm_state state,
		enum ib_wc_status wc_status
	),

	TP_ARGS(state, wc_status),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__field(unsigned long, wc_status)
	),

	TP_fast_assign(
		__entry->state = state;
		__entry->wc_status = wc_status;
	),

	TP_printk("state=%s completion status=%s",
		show_ib_cm_state(__entry->state),
		rdma_show_wc_status(__entry->wc_status)
	)
);

#endif /* _TRACE_IB_CMA_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/infiniband/core
#define TRACE_INCLUDE_FILE cm_trace

#include <trace/define_trace.h>

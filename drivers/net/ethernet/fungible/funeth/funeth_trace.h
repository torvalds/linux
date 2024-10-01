/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM funeth

#if !defined(_TRACE_FUNETH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FUNETH_H

#include <linux/tracepoint.h>

#include "funeth_txrx.h"

TRACE_EVENT(funeth_tx,

	TP_PROTO(const struct funeth_txq *txq,
		 u32 len,
		 u32 sqe_idx,
		 u32 ngle),

	TP_ARGS(txq, len, sqe_idx, ngle),

	TP_STRUCT__entry(
		__field(u32, qidx)
		__field(u32, len)
		__field(u32, sqe_idx)
		__field(u32, ngle)
		__string(devname, txq->netdev->name)
	),

	TP_fast_assign(
		__entry->qidx = txq->qidx;
		__entry->len = len;
		__entry->sqe_idx = sqe_idx;
		__entry->ngle = ngle;
		__assign_str(devname);
	),

	TP_printk("%s: Txq %u, SQE idx %u, len %u, num GLEs %u",
		  __get_str(devname), __entry->qidx, __entry->sqe_idx,
		  __entry->len, __entry->ngle)
);

TRACE_EVENT(funeth_tx_free,

	TP_PROTO(const struct funeth_txq *txq,
		 u32 sqe_idx,
		 u32 num_sqes,
		 u32 hw_head),

	TP_ARGS(txq, sqe_idx, num_sqes, hw_head),

	TP_STRUCT__entry(
		__field(u32, qidx)
		__field(u32, sqe_idx)
		__field(u32, num_sqes)
		__field(u32, hw_head)
		__string(devname, txq->netdev->name)
	),

	TP_fast_assign(
		__entry->qidx = txq->qidx;
		__entry->sqe_idx = sqe_idx;
		__entry->num_sqes = num_sqes;
		__entry->hw_head = hw_head;
		__assign_str(devname);
	),

	TP_printk("%s: Txq %u, SQE idx %u, SQEs %u, HW head %u",
		  __get_str(devname), __entry->qidx, __entry->sqe_idx,
		  __entry->num_sqes, __entry->hw_head)
);

TRACE_EVENT(funeth_rx,

	TP_PROTO(const struct funeth_rxq *rxq,
		 u32 num_rqes,
		 u32 pkt_len,
		 u32 hash,
		 u32 cls_vec),

	TP_ARGS(rxq, num_rqes, pkt_len, hash, cls_vec),

	TP_STRUCT__entry(
		__field(u32, qidx)
		__field(u32, cq_head)
		__field(u32, num_rqes)
		__field(u32, len)
		__field(u32, hash)
		__field(u32, cls_vec)
		__string(devname, rxq->netdev->name)
	),

	TP_fast_assign(
		__entry->qidx = rxq->qidx;
		__entry->cq_head = rxq->cq_head;
		__entry->num_rqes = num_rqes;
		__entry->len = pkt_len;
		__entry->hash = hash;
		__entry->cls_vec = cls_vec;
		__assign_str(devname);
	),

	TP_printk("%s: Rxq %u, CQ head %u, RQEs %u, len %u, hash %u, CV %#x",
		  __get_str(devname), __entry->qidx, __entry->cq_head,
		  __entry->num_rqes, __entry->len, __entry->hash,
		  __entry->cls_vec)
);

#endif /* _TRACE_FUNETH_H */

/* Below must be outside protection. */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE funeth_trace

#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */
#if !defined(__RVT_TRACE_TX_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_TX_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_qp.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt_tx

#define wr_opcode_name(opcode) { IB_WR_##opcode, #opcode  }
#define show_wr_opcode(opcode)                             \
__print_symbolic(opcode,                                   \
	wr_opcode_name(RDMA_WRITE),                        \
	wr_opcode_name(RDMA_WRITE_WITH_IMM),               \
	wr_opcode_name(SEND),                              \
	wr_opcode_name(SEND_WITH_IMM),                     \
	wr_opcode_name(RDMA_READ),                         \
	wr_opcode_name(ATOMIC_CMP_AND_SWP),                \
	wr_opcode_name(ATOMIC_FETCH_AND_ADD),              \
	wr_opcode_name(LSO),                               \
	wr_opcode_name(SEND_WITH_INV),                     \
	wr_opcode_name(RDMA_READ_WITH_INV),                \
	wr_opcode_name(LOCAL_INV),                         \
	wr_opcode_name(MASKED_ATOMIC_CMP_AND_SWP),         \
	wr_opcode_name(MASKED_ATOMIC_FETCH_AND_ADD),       \
	wr_opcode_name(RESERVED1),                         \
	wr_opcode_name(RESERVED2),                         \
	wr_opcode_name(RESERVED3),                         \
	wr_opcode_name(RESERVED4),                         \
	wr_opcode_name(RESERVED5),                         \
	wr_opcode_name(RESERVED6),                         \
	wr_opcode_name(RESERVED7),                         \
	wr_opcode_name(RESERVED8),                         \
	wr_opcode_name(RESERVED9),                         \
	wr_opcode_name(RESERVED10))

#define POS_PRN \
"[%s] wqe %p wr_id %llx send_flags %x qpn %x qpt %u psn %x lpsn %x ssn %x length %u opcode 0x%.2x,%s size %u avail %u head %u last %u pid %u num_sge %u wr_num_sge %u"

TRACE_EVENT(
	rvt_post_one_wr,
	TP_PROTO(struct rvt_qp *qp, struct rvt_swqe *wqe, int wr_num_sge),
	TP_ARGS(qp, wqe, wr_num_sge),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(u64, wr_id)
		__field(struct rvt_swqe *, wqe)
		__field(u32, qpn)
		__field(u32, qpt)
		__field(u32, psn)
		__field(u32, lpsn)
		__field(u32, length)
		__field(u32, opcode)
		__field(u32, size)
		__field(u32, avail)
		__field(u32, head)
		__field(u32, last)
		__field(u32, ssn)
		__field(int, send_flags)
		__field(pid_t, pid)
		__field(int, num_sge)
		__field(int, wr_num_sge)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device));
		__entry->wqe = wqe;
		__entry->wr_id = wqe->wr.wr_id;
		__entry->qpn = qp->ibqp.qp_num;
		__entry->qpt = qp->ibqp.qp_type;
		__entry->psn = wqe->psn;
		__entry->lpsn = wqe->lpsn;
		__entry->length = wqe->length;
		__entry->opcode = wqe->wr.opcode;
		__entry->size = qp->s_size;
		__entry->avail = qp->s_avail;
		__entry->head = qp->s_head;
		__entry->last = qp->s_last;
		__entry->pid = qp->pid;
		__entry->ssn = wqe->ssn;
		__entry->send_flags = wqe->wr.send_flags;
		__entry->num_sge = wqe->wr.num_sge;
		__entry->wr_num_sge = wr_num_sge;
	),
	TP_printk(
		POS_PRN,
		__get_str(dev),
		__entry->wqe,
		__entry->wr_id,
		__entry->send_flags,
		__entry->qpn,
		__entry->qpt,
		__entry->psn,
		__entry->lpsn,
		__entry->ssn,
		__entry->length,
		__entry->opcode, show_wr_opcode(__entry->opcode),
		__entry->size,
		__entry->avail,
		__entry->head,
		__entry->last,
		__entry->pid,
		__entry->num_sge,
		__entry->wr_num_sge
	)
);

TRACE_EVENT(
	rvt_qp_send_completion,
	TP_PROTO(struct rvt_qp *qp, struct rvt_swqe *wqe, u32 idx),
	TP_ARGS(qp, wqe, idx),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(struct rvt_swqe *, wqe)
		__field(u64, wr_id)
		__field(u32, qpn)
		__field(u32, qpt)
		__field(u32, length)
		__field(u32, idx)
		__field(u32, ssn)
		__field(enum ib_wr_opcode, opcode)
		__field(int, send_flags)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device));
		__entry->wqe = wqe;
		__entry->wr_id = wqe->wr.wr_id;
		__entry->qpn = qp->ibqp.qp_num;
		__entry->qpt = qp->ibqp.qp_type;
		__entry->length = wqe->length;
		__entry->idx = idx;
		__entry->ssn = wqe->ssn;
		__entry->opcode = wqe->wr.opcode;
		__entry->send_flags = wqe->wr.send_flags;
	),
	TP_printk(
		"[%s] qpn 0x%x qpt %u wqe %p idx %u wr_id %llx length %u ssn %u opcode %x send_flags %x",
		__get_str(dev),
		__entry->qpn,
		__entry->qpt,
		__entry->wqe,
		__entry->idx,
		__entry->wr_id,
		__entry->length,
		__entry->ssn,
		__entry->opcode,
		__entry->send_flags
	)
);
#endif /* __RVT_TRACE_TX_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_tx
#include <trace/define_trace.h>


/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 */
#if !defined(__RVT_TRACE_CQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_CQ_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_cq.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt_cq

#define wc_opcode_name(opcode) { IB_WC_##opcode, #opcode  }
#define show_wc_opcode(opcode)                                \
__print_symbolic(opcode,                                      \
	wc_opcode_name(SEND),                                 \
	wc_opcode_name(RDMA_WRITE),                           \
	wc_opcode_name(RDMA_READ),                            \
	wc_opcode_name(COMP_SWAP),                            \
	wc_opcode_name(FETCH_ADD),                            \
	wc_opcode_name(LSO),                                  \
	wc_opcode_name(LOCAL_INV),                            \
	wc_opcode_name(REG_MR),                               \
	wc_opcode_name(MASKED_COMP_SWAP),                     \
	wc_opcode_name(RECV),                                 \
	wc_opcode_name(RECV_RDMA_WITH_IMM))

#define CQ_ATTR_PRINT \
"[%s] user cq %s cqe %u comp_vector %d comp_vector_cpu %d flags %x"

DECLARE_EVENT_CLASS(rvt_cq_template,
		    TP_PROTO(struct rvt_cq *cq,
			     const struct ib_cq_init_attr *attr),
		    TP_ARGS(cq, attr),
		    TP_STRUCT__entry(RDI_DEV_ENTRY(cq->rdi)
				     __field(struct rvt_mmap_info *, ip)
				     __field(unsigned int, cqe)
				     __field(int, comp_vector)
				     __field(int, comp_vector_cpu)
				     __field(u32, flags)
				     ),
		    TP_fast_assign(RDI_DEV_ASSIGN(cq->rdi);
				   __entry->ip = cq->ip;
				   __entry->cqe = attr->cqe;
				   __entry->comp_vector = attr->comp_vector;
				   __entry->comp_vector_cpu =
							cq->comp_vector_cpu;
				   __entry->flags = attr->flags;
				   ),
		    TP_printk(CQ_ATTR_PRINT, __get_str(dev),
			      __entry->ip ? "true" : "false", __entry->cqe,
			      __entry->comp_vector, __entry->comp_vector_cpu,
			      __entry->flags
			      )
);

DEFINE_EVENT(rvt_cq_template, rvt_create_cq,
	     TP_PROTO(struct rvt_cq *cq, const struct ib_cq_init_attr *attr),
	     TP_ARGS(cq, attr));

#define CQ_PRN \
"[%s] idx %u wr_id %llx status %u opcode %u,%s length %u qpn %x flags %x imm %x"

DECLARE_EVENT_CLASS(
	rvt_cq_entry_template,
	TP_PROTO(struct rvt_cq *cq, struct ib_wc *wc, u32 idx),
	TP_ARGS(cq, wc, idx),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(cq->rdi)
		__field(u64, wr_id)
		__field(u32, status)
		__field(u32, opcode)
		__field(u32, qpn)
		__field(u32, length)
		__field(u32, idx)
		__field(u32, flags)
		__field(u32, imm)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(cq->rdi);
		__entry->wr_id = wc->wr_id;
		__entry->status = wc->status;
		__entry->opcode = wc->opcode;
		__entry->length = wc->byte_len;
		__entry->qpn = wc->qp->qp_num;
		__entry->idx = idx;
		__entry->flags = wc->wc_flags;
		__entry->imm = be32_to_cpu(wc->ex.imm_data);
	),
	TP_printk(
		CQ_PRN,
		__get_str(dev),
		__entry->idx,
		__entry->wr_id,
		__entry->status,
		__entry->opcode, show_wc_opcode(__entry->opcode),
		__entry->length,
		__entry->qpn,
		__entry->flags,
		__entry->imm
	)
);

DEFINE_EVENT(
	rvt_cq_entry_template, rvt_cq_enter,
	TP_PROTO(struct rvt_cq *cq, struct ib_wc *wc, u32 idx),
	TP_ARGS(cq, wc, idx));

DEFINE_EVENT(
	rvt_cq_entry_template, rvt_cq_poll,
	TP_PROTO(struct rvt_cq *cq, struct ib_wc *wc, u32 idx),
	TP_ARGS(cq, wc, idx));

#endif /* __RVT_TRACE_CQ_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_cq
#include <trace/define_trace.h>

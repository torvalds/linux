/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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

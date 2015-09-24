/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR hfi1

#if !defined(__HFI1_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"
#include "mad.h"
#include "sdma.h"

#define DD_DEV_ENTRY(dd)       __string(dev, dev_name(&(dd)->pcidev->dev))
#define DD_DEV_ASSIGN(dd)      __assign_str(dev, dev_name(&(dd)->pcidev->dev))

#define packettype_name(etype) { RHF_RCV_TYPE_##etype, #etype }
#define show_packettype(etype)                  \
__print_symbolic(etype,                         \
	packettype_name(EXPECTED),              \
	packettype_name(EAGER),                 \
	packettype_name(IB),                    \
	packettype_name(ERROR),                 \
	packettype_name(BYPASS))

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_rx

TRACE_EVENT(hfi1_rcvhdr,
	TP_PROTO(struct hfi1_devdata *dd,
		 u64 eflags,
		 u32 ctxt,
		 u32 etype,
		 u32 hlen,
		 u32 tlen,
		 u32 updegr,
		 u32 etail),
	TP_ARGS(dd, ctxt, eflags, etype, hlen, tlen, updegr, etail),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__field(u64, eflags)
		__field(u32, ctxt)
		__field(u32, etype)
		__field(u32, hlen)
		__field(u32, tlen)
		__field(u32, updegr)
		__field(u32, etail)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd);
		__entry->eflags = eflags;
		__entry->ctxt = ctxt;
		__entry->etype = etype;
		__entry->hlen = hlen;
		__entry->tlen = tlen;
		__entry->updegr = updegr;
		__entry->etail = etail;
	),
	TP_printk(
"[%s] ctxt %d eflags 0x%llx etype %d,%s hlen %d tlen %d updegr %d etail %d",
		__get_str(dev),
		__entry->ctxt,
		__entry->eflags,
		__entry->etype, show_packettype(__entry->etype),
		__entry->hlen,
		__entry->tlen,
		__entry->updegr,
		__entry->etail
	)
);

TRACE_EVENT(hfi1_receive_interrupt,
	TP_PROTO(struct hfi1_devdata *dd, u32 ctxt),
	TP_ARGS(dd, ctxt),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__field(u32, ctxt)
		__field(u8, slow_path)
		__field(u8, dma_rtail)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd);
		__entry->ctxt = ctxt;
		if (dd->rcd[ctxt]->do_interrupt ==
		    &handle_receive_interrupt) {
			__entry->slow_path = 1;
			__entry->dma_rtail = 0xFF;
		} else if (dd->rcd[ctxt]->do_interrupt ==
			&handle_receive_interrupt_dma_rtail){
			__entry->dma_rtail = 1;
			__entry->slow_path = 0;
		} else if (dd->rcd[ctxt]->do_interrupt ==
			 &handle_receive_interrupt_nodma_rtail) {
			__entry->dma_rtail = 0;
			__entry->slow_path = 0;
		}
	),
	TP_printk(
		"[%s] ctxt %d SlowPath: %d DmaRtail: %d",
		__get_str(dev),
		__entry->ctxt,
		__entry->slow_path,
		__entry->dma_rtail
	)
);

const char *print_u64_array(struct trace_seq *, u64 *, int);

TRACE_EVENT(hfi1_exp_tid_map,
	    TP_PROTO(unsigned ctxt, u16 subctxt, int dir,
		     unsigned long *maps, u16 count),
	    TP_ARGS(ctxt, subctxt, dir, maps, count),
	    TP_STRUCT__entry(
		    __field(unsigned, ctxt)
		    __field(u16, subctxt)
		    __field(int, dir)
		    __field(u16, count)
		    __dynamic_array(unsigned long, maps, sizeof(*maps) * count)
		    ),
	    TP_fast_assign(
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->dir = dir;
		    __entry->count = count;
		    memcpy(__get_dynamic_array(maps), maps,
			   sizeof(*maps) * count);
		    ),
	    TP_printk("[%3u:%02u] %s tidmaps %s",
		      __entry->ctxt,
		      __entry->subctxt,
		      (__entry->dir ? ">" : "<"),
		      print_u64_array(p, __get_dynamic_array(maps),
				      __entry->count)
		    )
	);

TRACE_EVENT(hfi1_exp_rcv_set,
	    TP_PROTO(unsigned ctxt, u16 subctxt, u32 tid,
		     unsigned long vaddr, u64 phys_addr, void *page),
	    TP_ARGS(ctxt, subctxt, tid, vaddr, phys_addr, page),
	    TP_STRUCT__entry(
		    __field(unsigned, ctxt)
		    __field(u16, subctxt)
		    __field(u32, tid)
		    __field(unsigned long, vaddr)
		    __field(u64, phys_addr)
		    __field(void *, page)
		    ),
	    TP_fast_assign(
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->tid = tid;
		    __entry->vaddr = vaddr;
		    __entry->phys_addr = phys_addr;
		    __entry->page = page;
		    ),
	    TP_printk("[%u:%u] TID %u, vaddrs 0x%lx, physaddr 0x%llx, pgp %p",
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->tid,
		      __entry->vaddr,
		      __entry->phys_addr,
		      __entry->page
		    )
	);

TRACE_EVENT(hfi1_exp_rcv_free,
	    TP_PROTO(unsigned ctxt, u16 subctxt, u32 tid,
		     unsigned long phys, void *page),
	    TP_ARGS(ctxt, subctxt, tid, phys, page),
	    TP_STRUCT__entry(
		    __field(unsigned, ctxt)
		    __field(u16, subctxt)
		    __field(u32, tid)
		    __field(unsigned long, phys)
		    __field(void *, page)
		    ),
	    TP_fast_assign(
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->tid = tid;
		    __entry->phys = phys;
		    __entry->page = page;
		    ),
	    TP_printk("[%u:%u] freeing TID %u, 0x%lx, pgp %p",
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->tid,
		      __entry->phys,
		      __entry->page
		    )
	);
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_tx

TRACE_EVENT(hfi1_piofree,
	TP_PROTO(struct send_context *sc, int extra),
	TP_ARGS(sc, extra),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sc->dd)
		__field(u32, sw_index)
		__field(u32, hw_context)
		__field(int, extra)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sc->dd);
		__entry->sw_index = sc->sw_index;
		__entry->hw_context = sc->hw_context;
		__entry->extra = extra;
	),
	TP_printk(
		"[%s] ctxt %u(%u) extra %d",
		__get_str(dev),
		__entry->sw_index,
		__entry->hw_context,
		__entry->extra
	)
);

TRACE_EVENT(hfi1_wantpiointr,
	TP_PROTO(struct send_context *sc, u32 needint, u64 credit_ctrl),
	TP_ARGS(sc, needint, credit_ctrl),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sc->dd)
		__field(u32, sw_index)
		__field(u32, hw_context)
		__field(u32, needint)
		__field(u64, credit_ctrl)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sc->dd);
		__entry->sw_index = sc->sw_index;
		__entry->hw_context = sc->hw_context;
		__entry->needint = needint;
		__entry->credit_ctrl = credit_ctrl;
	),
	TP_printk(
		"[%s] ctxt %u(%u) on %d credit_ctrl 0x%llx",
		__get_str(dev),
		__entry->sw_index,
		__entry->hw_context,
		__entry->needint,
		(unsigned long long)__entry->credit_ctrl
	)
);

DECLARE_EVENT_CLASS(hfi1_qpsleepwakeup_template,
	TP_PROTO(struct hfi1_qp *qp, u32 flags),
	TP_ARGS(qp, flags),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, flags)
		__field(u32, s_flags)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->flags = flags;
		__entry->qpn = qp->ibqp.qp_num;
		__entry->s_flags = qp->s_flags;
	),
	TP_printk(
		"[%s] qpn 0x%x flags 0x%x s_flags 0x%x",
		__get_str(dev),
		__entry->qpn,
		__entry->flags,
		__entry->s_flags
	)
);

DEFINE_EVENT(hfi1_qpsleepwakeup_template, hfi1_qpwakeup,
	     TP_PROTO(struct hfi1_qp *qp, u32 flags),
	     TP_ARGS(qp, flags));

DEFINE_EVENT(hfi1_qpsleepwakeup_template, hfi1_qpsleep,
	     TP_PROTO(struct hfi1_qp *qp, u32 flags),
	     TP_ARGS(qp, flags));

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_qphash
DECLARE_EVENT_CLASS(hfi1_qphash_template,
	TP_PROTO(struct hfi1_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, bucket)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->bucket = bucket;
	),
	TP_printk(
		"[%s] qpn 0x%x bucket %u",
		__get_str(dev),
		__entry->qpn,
		__entry->bucket
	)
);

DEFINE_EVENT(hfi1_qphash_template, hfi1_qpinsert,
	TP_PROTO(struct hfi1_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

DEFINE_EVENT(hfi1_qphash_template, hfi1_qpremove,
	TP_PROTO(struct hfi1_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_ibhdrs

u8 ibhdr_exhdr_len(struct hfi1_ib_header *hdr);
const char *parse_everbs_hdrs(
	struct trace_seq *p,
	u8 opcode,
	void *ehdrs);

#define __parse_ib_ehdrs(op, ehdrs) parse_everbs_hdrs(p, op, ehdrs)

const char *parse_sdma_flags(
	struct trace_seq *p,
	u64 desc0, u64 desc1);

#define __parse_sdma_flags(desc0, desc1) parse_sdma_flags(p, desc0, desc1)


#define lrh_name(lrh) { HFI1_##lrh, #lrh }
#define show_lnh(lrh)                    \
__print_symbolic(lrh,                    \
	lrh_name(LRH_BTH),               \
	lrh_name(LRH_GRH))

#define ib_opcode_name(opcode) { IB_OPCODE_##opcode, #opcode  }
#define show_ib_opcode(opcode)                             \
__print_symbolic(opcode,                                   \
	ib_opcode_name(RC_SEND_FIRST),                     \
	ib_opcode_name(RC_SEND_MIDDLE),                    \
	ib_opcode_name(RC_SEND_LAST),                      \
	ib_opcode_name(RC_SEND_LAST_WITH_IMMEDIATE),       \
	ib_opcode_name(RC_SEND_ONLY),                      \
	ib_opcode_name(RC_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(RC_RDMA_WRITE_FIRST),               \
	ib_opcode_name(RC_RDMA_WRITE_MIDDLE),              \
	ib_opcode_name(RC_RDMA_WRITE_LAST),                \
	ib_opcode_name(RC_RDMA_WRITE_LAST_WITH_IMMEDIATE), \
	ib_opcode_name(RC_RDMA_WRITE_ONLY),                \
	ib_opcode_name(RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE), \
	ib_opcode_name(RC_RDMA_READ_REQUEST),              \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_FIRST),       \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_MIDDLE),      \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_LAST),        \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_ONLY),        \
	ib_opcode_name(RC_ACKNOWLEDGE),                    \
	ib_opcode_name(RC_ATOMIC_ACKNOWLEDGE),             \
	ib_opcode_name(RC_COMPARE_SWAP),                   \
	ib_opcode_name(RC_FETCH_ADD),                      \
	ib_opcode_name(UC_SEND_FIRST),                     \
	ib_opcode_name(UC_SEND_MIDDLE),                    \
	ib_opcode_name(UC_SEND_LAST),                      \
	ib_opcode_name(UC_SEND_LAST_WITH_IMMEDIATE),       \
	ib_opcode_name(UC_SEND_ONLY),                      \
	ib_opcode_name(UC_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(UC_RDMA_WRITE_FIRST),               \
	ib_opcode_name(UC_RDMA_WRITE_MIDDLE),              \
	ib_opcode_name(UC_RDMA_WRITE_LAST),                \
	ib_opcode_name(UC_RDMA_WRITE_LAST_WITH_IMMEDIATE), \
	ib_opcode_name(UC_RDMA_WRITE_ONLY),                \
	ib_opcode_name(UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE), \
	ib_opcode_name(UD_SEND_ONLY),                      \
	ib_opcode_name(UD_SEND_ONLY_WITH_IMMEDIATE))


#define LRH_PRN "vl %d lver %d sl %d lnh %d,%s dlid %.4x len %d slid %.4x"
#define BTH_PRN \
	"op 0x%.2x,%s se %d m %d pad %d tver %d pkey 0x%.4x " \
	"f %d b %d qpn 0x%.6x a %d psn 0x%.8x"
#define EHDR_PRN "%s"

DECLARE_EVENT_CLASS(hfi1_ibhdr_template,
	TP_PROTO(struct hfi1_devdata *dd,
		 struct hfi1_ib_header *hdr),
	TP_ARGS(dd, hdr),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		/* LRH */
		__field(u8, vl)
		__field(u8, lver)
		__field(u8, sl)
		__field(u8, lnh)
		__field(u16, dlid)
		__field(u16, len)
		__field(u16, slid)
		/* BTH */
		__field(u8, opcode)
		__field(u8, se)
		__field(u8, m)
		__field(u8, pad)
		__field(u8, tver)
		__field(u16, pkey)
		__field(u8, f)
		__field(u8, b)
		__field(u32, qpn)
		__field(u8, a)
		__field(u32, psn)
		/* extended headers */
		__dynamic_array(u8, ehdrs, ibhdr_exhdr_len(hdr))
	),
	TP_fast_assign(
		struct hfi1_other_headers *ohdr;

		DD_DEV_ASSIGN(dd);
		/* LRH */
		__entry->vl =
			(u8)(be16_to_cpu(hdr->lrh[0]) >> 12);
		__entry->lver =
			(u8)(be16_to_cpu(hdr->lrh[0]) >> 8) & 0xf;
		__entry->sl =
			(u8)(be16_to_cpu(hdr->lrh[0]) >> 4) & 0xf;
		__entry->lnh =
			(u8)(be16_to_cpu(hdr->lrh[0]) & 3);
		__entry->dlid =
			be16_to_cpu(hdr->lrh[1]);
		/* allow for larger len */
		__entry->len =
			be16_to_cpu(hdr->lrh[2]);
		__entry->slid =
			be16_to_cpu(hdr->lrh[3]);
		/* BTH */
		if (__entry->lnh == HFI1_LRH_BTH)
			ohdr = &hdr->u.oth;
		else
			ohdr = &hdr->u.l.oth;
		__entry->opcode =
			(be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;
		__entry->se =
			(be32_to_cpu(ohdr->bth[0]) >> 23) & 1;
		__entry->m =
			 (be32_to_cpu(ohdr->bth[0]) >> 22) & 1;
		__entry->pad =
			(be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		__entry->tver =
			(be32_to_cpu(ohdr->bth[0]) >> 16) & 0xf;
		__entry->pkey =
			be32_to_cpu(ohdr->bth[0]) & 0xffff;
		__entry->f =
			(be32_to_cpu(ohdr->bth[1]) >> HFI1_FECN_SHIFT)
			& HFI1_FECN_MASK;
		__entry->b =
			(be32_to_cpu(ohdr->bth[1]) >> HFI1_BECN_SHIFT)
			& HFI1_BECN_MASK;
		__entry->qpn =
			be32_to_cpu(ohdr->bth[1]) & HFI1_QPN_MASK;
		__entry->a =
			(be32_to_cpu(ohdr->bth[2]) >> 31) & 1;
		/* allow for larger PSN */
		__entry->psn =
			be32_to_cpu(ohdr->bth[2]) & 0x7fffffff;
		/* extended headers */
		 memcpy(
			__get_dynamic_array(ehdrs),
			&ohdr->u,
			ibhdr_exhdr_len(hdr));
	),
	TP_printk("[%s] " LRH_PRN " " BTH_PRN " " EHDR_PRN,
		__get_str(dev),
		/* LRH */
		__entry->vl,
		__entry->lver,
		__entry->sl,
		__entry->lnh, show_lnh(__entry->lnh),
		__entry->dlid,
		__entry->len,
		__entry->slid,
		/* BTH */
		__entry->opcode, show_ib_opcode(__entry->opcode),
		__entry->se,
		__entry->m,
		__entry->pad,
		__entry->tver,
		__entry->pkey,
		__entry->f,
		__entry->b,
		__entry->qpn,
		__entry->a,
		__entry->psn,
		/* extended headers */
		__parse_ib_ehdrs(
			__entry->opcode,
			(void *)__get_dynamic_array(ehdrs))
	)
);

DEFINE_EVENT(hfi1_ibhdr_template, input_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd, struct hfi1_ib_header *hdr),
	     TP_ARGS(dd, hdr));

DEFINE_EVENT(hfi1_ibhdr_template, output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd, struct hfi1_ib_header *hdr),
	     TP_ARGS(dd, hdr));

#define SNOOP_PRN \
	"slid %.4x dlid %.4x qpn 0x%.6x opcode 0x%.2x,%s " \
	"svc lvl %d pkey 0x%.4x [header = %d bytes] [data = %d bytes]"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_snoop


TRACE_EVENT(snoop_capture,
	TP_PROTO(struct hfi1_devdata *dd,
		 int hdr_len,
		 struct hfi1_ib_header *hdr,
		 int data_len,
		 void *data),
	TP_ARGS(dd, hdr_len, hdr, data_len, data),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__field(u16, slid)
		__field(u16, dlid)
		__field(u32, qpn)
		__field(u8, opcode)
		__field(u8, sl)
		__field(u16, pkey)
		__field(u32, hdr_len)
		__field(u32, data_len)
		__field(u8, lnh)
		__dynamic_array(u8, raw_hdr, hdr_len)
		__dynamic_array(u8, raw_pkt, data_len)
	),
	TP_fast_assign(
		struct hfi1_other_headers *ohdr;

		__entry->lnh = (u8)(be16_to_cpu(hdr->lrh[0]) & 3);
		if (__entry->lnh == HFI1_LRH_BTH)
			ohdr = &hdr->u.oth;
		else
			ohdr = &hdr->u.l.oth;
		DD_DEV_ASSIGN(dd);
		__entry->slid = be16_to_cpu(hdr->lrh[3]);
		__entry->dlid = be16_to_cpu(hdr->lrh[1]);
		__entry->qpn = be32_to_cpu(ohdr->bth[1]) & HFI1_QPN_MASK;
		__entry->opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;
		__entry->sl = (u8)(be16_to_cpu(hdr->lrh[0]) >> 4) & 0xf;
		__entry->pkey =	be32_to_cpu(ohdr->bth[0]) & 0xffff;
		__entry->hdr_len = hdr_len;
		__entry->data_len = data_len;
		memcpy(__get_dynamic_array(raw_hdr), hdr, hdr_len);
		memcpy(__get_dynamic_array(raw_pkt), data, data_len);
	),
	TP_printk("[%s] " SNOOP_PRN,
		__get_str(dev),
		__entry->slid,
		__entry->dlid,
		__entry->qpn,
		__entry->opcode,
		show_ib_opcode(__entry->opcode),
		__entry->sl,
		__entry->pkey,
		__entry->hdr_len,
		__entry->data_len
	)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_ctxts

#define UCTXT_FMT \
	"cred:%u, credaddr:0x%llx, piobase:0x%llx, rcvhdr_cnt:%u, "	\
	"rcvbase:0x%llx, rcvegrc:%u, rcvegrb:0x%llx"
TRACE_EVENT(hfi1_uctxtdata,
	    TP_PROTO(struct hfi1_devdata *dd, struct hfi1_ctxtdata *uctxt),
	    TP_ARGS(dd, uctxt),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd)
		    __field(unsigned, ctxt)
		    __field(u32, credits)
		    __field(u64, hw_free)
		    __field(u64, piobase)
		    __field(u16, rcvhdrq_cnt)
		    __field(u64, rcvhdrq_phys)
		    __field(u32, eager_cnt)
		    __field(u64, rcvegr_phys)
		    ),
	    TP_fast_assign(
		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = uctxt->ctxt;
		    __entry->credits = uctxt->sc->credits;
		    __entry->hw_free = (u64)uctxt->sc->hw_free;
		    __entry->piobase = (u64)uctxt->sc->base_addr;
		    __entry->rcvhdrq_cnt = uctxt->rcvhdrq_cnt;
		    __entry->rcvhdrq_phys = uctxt->rcvhdrq_phys;
		    __entry->eager_cnt = uctxt->egrbufs.alloced;
		    __entry->rcvegr_phys = uctxt->egrbufs.rcvtids[0].phys;
		    ),
	    TP_printk(
		    "[%s] ctxt %u " UCTXT_FMT,
		    __get_str(dev),
		    __entry->ctxt,
		    __entry->credits,
		    __entry->hw_free,
		    __entry->piobase,
		    __entry->rcvhdrq_cnt,
		    __entry->rcvhdrq_phys,
		    __entry->eager_cnt,
		    __entry->rcvegr_phys
		    )
	);

#define CINFO_FMT \
	"egrtids:%u, egr_size:%u, hdrq_cnt:%u, hdrq_size:%u, sdma_ring_size:%u"
TRACE_EVENT(hfi1_ctxt_info,
	    TP_PROTO(struct hfi1_devdata *dd, unsigned ctxt, unsigned subctxt,
		     struct hfi1_ctxt_info cinfo),
	    TP_ARGS(dd, ctxt, subctxt, cinfo),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd)
		    __field(unsigned, ctxt)
		    __field(unsigned, subctxt)
		    __field(u16, egrtids)
		    __field(u16, rcvhdrq_cnt)
		    __field(u16, rcvhdrq_size)
		    __field(u16, sdma_ring_size)
		    __field(u32, rcvegr_size)
		    ),
	    TP_fast_assign(
		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->egrtids = cinfo.egrtids;
		    __entry->rcvhdrq_cnt = cinfo.rcvhdrq_cnt;
		    __entry->rcvhdrq_size = cinfo.rcvhdrq_entsize;
		    __entry->sdma_ring_size = cinfo.sdma_ring_size;
		    __entry->rcvegr_size = cinfo.rcvegr_size;
		    ),
	    TP_printk(
		    "[%s] ctxt %u:%u " CINFO_FMT,
		    __get_str(dev),
		    __entry->ctxt,
		    __entry->subctxt,
		    __entry->egrtids,
		    __entry->rcvegr_size,
		    __entry->rcvhdrq_cnt,
		    __entry->rcvhdrq_size,
		    __entry->sdma_ring_size
		    )
	);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_sma

#define BCT_FORMAT \
	"shared_limit %x vls 0-7 [%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x] 15 [%x,%x]"

#define BCT(field) \
	be16_to_cpu( \
		((struct buffer_control *)__get_dynamic_array(bct))->field \
	)

DECLARE_EVENT_CLASS(hfi1_bct_template,
	TP_PROTO(struct hfi1_devdata *dd, struct buffer_control *bc),
	TP_ARGS(dd, bc),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__dynamic_array(u8, bct, sizeof(*bc))
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd);
		memcpy(
			__get_dynamic_array(bct),
			bc,
			sizeof(*bc));
	),
	TP_printk(BCT_FORMAT,
		BCT(overall_shared_limit),

		BCT(vl[0].dedicated),
		BCT(vl[0].shared),

		BCT(vl[1].dedicated),
		BCT(vl[1].shared),

		BCT(vl[2].dedicated),
		BCT(vl[2].shared),

		BCT(vl[3].dedicated),
		BCT(vl[3].shared),

		BCT(vl[4].dedicated),
		BCT(vl[4].shared),

		BCT(vl[5].dedicated),
		BCT(vl[5].shared),

		BCT(vl[6].dedicated),
		BCT(vl[6].shared),

		BCT(vl[7].dedicated),
		BCT(vl[7].shared),

		BCT(vl[15].dedicated),
		BCT(vl[15].shared)
	)
);


DEFINE_EVENT(hfi1_bct_template, bct_set,
	     TP_PROTO(struct hfi1_devdata *dd, struct buffer_control *bc),
	     TP_ARGS(dd, bc));

DEFINE_EVENT(hfi1_bct_template, bct_get,
	     TP_PROTO(struct hfi1_devdata *dd, struct buffer_control *bc),
	     TP_ARGS(dd, bc));

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_sdma

TRACE_EVENT(hfi1_sdma_descriptor,
	TP_PROTO(
		struct sdma_engine *sde,
		u64 desc0,
		u64 desc1,
		u16 e,
		void *descp),
	TP_ARGS(sde, desc0, desc1, e, descp),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(void *, descp)
		__field(u64, desc0)
		__field(u64, desc1)
		__field(u16, e)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->desc0 = desc0;
		__entry->desc1 = desc1;
		__entry->idx = sde->this_idx;
		__entry->descp = descp;
		__entry->e = e;
	),
	TP_printk(
		"[%s] SDE(%u) flags:%s addr:0x%016llx gen:%u len:%u d0:%016llx d1:%016llx to %p,%u",
		__get_str(dev),
		__entry->idx,
		__parse_sdma_flags(__entry->desc0, __entry->desc1),
		(__entry->desc0 >> SDMA_DESC0_PHY_ADDR_SHIFT)
			& SDMA_DESC0_PHY_ADDR_MASK,
		(u8)((__entry->desc1 >> SDMA_DESC1_GENERATION_SHIFT)
			& SDMA_DESC1_GENERATION_MASK),
		(u16)((__entry->desc0 >> SDMA_DESC0_BYTE_COUNT_SHIFT)
			& SDMA_DESC0_BYTE_COUNT_MASK),
		__entry->desc0,
		__entry->desc1,
		__entry->descp,
		__entry->e
	)
);

TRACE_EVENT(hfi1_sdma_engine_select,
	TP_PROTO(struct hfi1_devdata *dd, u32 sel, u8 vl, u8 idx),
	TP_ARGS(dd, sel, vl, idx),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__field(u32, sel)
		__field(u8, vl)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd);
		__entry->sel = sel;
		__entry->vl = vl;
		__entry->idx = idx;
	),
	TP_printk(
		"[%s] selecting SDE %u sel 0x%x vl %u",
		__get_str(dev),
		__entry->idx,
		__entry->sel,
		__entry->vl
	)
);

DECLARE_EVENT_CLASS(hfi1_sdma_engine_class,
	TP_PROTO(
		struct sdma_engine *sde,
		u64 status
	),
	TP_ARGS(sde, status),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(u64, status)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->status = status;
		__entry->idx = sde->this_idx;
	),
	TP_printk(
		"[%s] SDE(%u) status %llx",
		__get_str(dev),
		__entry->idx,
		(unsigned long long)__entry->status
	)
);

DEFINE_EVENT(hfi1_sdma_engine_class, hfi1_sdma_engine_interrupt,
	TP_PROTO(
		struct sdma_engine *sde,
		u64 status
	),
	TP_ARGS(sde, status)
);

DEFINE_EVENT(hfi1_sdma_engine_class, hfi1_sdma_engine_progress,
	TP_PROTO(
		struct sdma_engine *sde,
		u64 status
	),
	TP_ARGS(sde, status)
);

DECLARE_EVENT_CLASS(hfi1_sdma_ahg_ad,
	TP_PROTO(
		struct sdma_engine *sde,
		int aidx
	),
	TP_ARGS(sde, aidx),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(int, aidx)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->idx = sde->this_idx;
		__entry->aidx = aidx;
	),
	TP_printk(
		"[%s] SDE(%u) aidx %d",
		__get_str(dev),
		__entry->idx,
		__entry->aidx
	)
);

DEFINE_EVENT(hfi1_sdma_ahg_ad, hfi1_ahg_allocate,
	     TP_PROTO(
		struct sdma_engine *sde,
		int aidx
	     ),
	     TP_ARGS(sde, aidx));

DEFINE_EVENT(hfi1_sdma_ahg_ad, hfi1_ahg_deallocate,
	     TP_PROTO(
		struct sdma_engine *sde,
		int aidx
	     ),
	     TP_ARGS(sde, aidx));

#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
TRACE_EVENT(hfi1_sdma_progress,
	TP_PROTO(
		struct sdma_engine *sde,
		u16 hwhead,
		u16 swhead,
		struct sdma_txreq *txp
	),
	TP_ARGS(sde, hwhead, swhead, txp),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(u64, sn)
		__field(u16, hwhead)
		__field(u16, swhead)
		__field(u16, txnext)
		__field(u16, tx_tail)
		__field(u16, tx_head)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->hwhead = hwhead;
		__entry->swhead = swhead;
		__entry->tx_tail = sde->tx_tail;
		__entry->tx_head = sde->tx_head;
		__entry->txnext = txp ? txp->next_descq_idx : ~0;
		__entry->idx = sde->this_idx;
		__entry->sn = txp ? txp->sn : ~0;
	),
	TP_printk(
		"[%s] SDE(%u) sn %llu hwhead %u swhead %u next_descq_idx %u tx_head %u tx_tail %u",
		__get_str(dev),
		__entry->idx,
		__entry->sn,
		__entry->hwhead,
		__entry->swhead,
		__entry->txnext,
		__entry->tx_head,
		__entry->tx_tail
	)
);
#else
TRACE_EVENT(hfi1_sdma_progress,
	    TP_PROTO(
		struct sdma_engine *sde,
		u16 hwhead,
		u16 swhead,
		struct sdma_txreq *txp
	    ),
	TP_ARGS(sde, hwhead, swhead, txp),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(u16, hwhead)
		__field(u16, swhead)
		__field(u16, txnext)
		__field(u16, tx_tail)
		__field(u16, tx_head)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->hwhead = hwhead;
		__entry->swhead = swhead;
		__entry->tx_tail = sde->tx_tail;
		__entry->tx_head = sde->tx_head;
		__entry->txnext = txp ? txp->next_descq_idx : ~0;
		__entry->idx = sde->this_idx;
	),
	TP_printk(
		"[%s] SDE(%u) hwhead %u swhead %u next_descq_idx %u tx_head %u tx_tail %u",
		__get_str(dev),
		__entry->idx,
		__entry->hwhead,
		__entry->swhead,
		__entry->txnext,
		__entry->tx_head,
		__entry->tx_tail
	)
);
#endif

DECLARE_EVENT_CLASS(hfi1_sdma_sn,
	TP_PROTO(
		struct sdma_engine *sde,
		u64 sn
	),
	TP_ARGS(sde, sn),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__field(u64, sn)
		__field(u8, idx)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__entry->sn = sn;
		__entry->idx = sde->this_idx;
	),
	TP_printk(
		"[%s] SDE(%u) sn %llu",
		__get_str(dev),
		__entry->idx,
		__entry->sn
	)
);

DEFINE_EVENT(hfi1_sdma_sn, hfi1_sdma_out_sn,
	     TP_PROTO(
		struct sdma_engine *sde,
		u64 sn
	     ),
	     TP_ARGS(sde, sn)
);

DEFINE_EVENT(hfi1_sdma_sn, hfi1_sdma_in_sn,
	     TP_PROTO(
		struct sdma_engine *sde,
		u64 sn
	     ),
	     TP_ARGS(sde, sn)
);

#define USDMA_HDR_FORMAT \
	"[%s:%u:%u:%u] PBC=(0x%x 0x%x) LRH=(0x%x 0x%x) BTH=(0x%x 0x%x 0x%x) KDETH=(0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x) TIDVal=0x%x"

TRACE_EVENT(hfi1_sdma_user_header,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u8 subctxt, u16 req,
		     struct hfi1_pkt_header *hdr, u32 tidval),
	    TP_ARGS(dd, ctxt, subctxt, req, hdr, tidval),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd)
		    __field(u16, ctxt)
		    __field(u8, subctxt)
		    __field(u16, req)
		    __field(__le32, pbc0)
		    __field(__le32, pbc1)
		    __field(__be32, lrh0)
		    __field(__be32, lrh1)
		    __field(__be32, bth0)
		    __field(__be32, bth1)
		    __field(__be32, bth2)
		    __field(__le32, kdeth0)
		    __field(__le32, kdeth1)
		    __field(__le32, kdeth2)
		    __field(__le32, kdeth3)
		    __field(__le32, kdeth4)
		    __field(__le32, kdeth5)
		    __field(__le32, kdeth6)
		    __field(__le32, kdeth7)
		    __field(__le32, kdeth8)
		    __field(u32, tidval)
		    ),
	    TP_fast_assign(
		    __le32 *pbc = (__le32 *)hdr->pbc;
		    __be32 *lrh = (__be32 *)hdr->lrh;
		    __be32 *bth = (__be32 *)hdr->bth;
		    __le32 *kdeth = (__le32 *)&hdr->kdeth;

		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->req = req;
		    __entry->pbc0 = pbc[0];
		    __entry->pbc1 = pbc[1];
		    __entry->lrh0 = be32_to_cpu(lrh[0]);
		    __entry->lrh1 = be32_to_cpu(lrh[1]);
		    __entry->bth0 = be32_to_cpu(bth[0]);
		    __entry->bth1 = be32_to_cpu(bth[1]);
		    __entry->bth2 = be32_to_cpu(bth[2]);
		    __entry->kdeth0 = kdeth[0];
		    __entry->kdeth1 = kdeth[1];
		    __entry->kdeth2 = kdeth[2];
		    __entry->kdeth3 = kdeth[3];
		    __entry->kdeth4 = kdeth[4];
		    __entry->kdeth5 = kdeth[5];
		    __entry->kdeth6 = kdeth[6];
		    __entry->kdeth7 = kdeth[7];
		    __entry->kdeth8 = kdeth[8];
		    __entry->tidval = tidval;
		    ),
	    TP_printk(USDMA_HDR_FORMAT,
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->req,
		      __entry->pbc1,
		      __entry->pbc0,
		      __entry->lrh0,
		      __entry->lrh1,
		      __entry->bth0,
		      __entry->bth1,
		      __entry->bth2,
		      __entry->kdeth0,
		      __entry->kdeth1,
		      __entry->kdeth2,
		      __entry->kdeth3,
		      __entry->kdeth4,
		      __entry->kdeth5,
		      __entry->kdeth6,
		      __entry->kdeth7,
		      __entry->kdeth8,
		      __entry->tidval
		    )
	);

#define SDMA_UREQ_FMT \
	"[%s:%u:%u] ver/op=0x%x, iovcnt=%u, npkts=%u, frag=%u, idx=%u"
TRACE_EVENT(hfi1_sdma_user_reqinfo,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u8 subctxt, u16 *i),
	    TP_ARGS(dd, ctxt, subctxt, i),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd);
		    __field(u16, ctxt)
		    __field(u8, subctxt)
		    __field(u8, ver_opcode)
		    __field(u8, iovcnt)
		    __field(u16, npkts)
		    __field(u16, fragsize)
		    __field(u16, comp_idx)
		    ),
	    TP_fast_assign(
		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->ver_opcode = i[0] & 0xff;
		    __entry->iovcnt = (i[0] >> 8) & 0xff;
		    __entry->npkts = i[1];
		    __entry->fragsize = i[2];
		    __entry->comp_idx = i[3];
		    ),
	    TP_printk(SDMA_UREQ_FMT,
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->ver_opcode,
		      __entry->iovcnt,
		      __entry->npkts,
		      __entry->fragsize,
		      __entry->comp_idx
		    )
	);

#define usdma_complete_name(st) { st, #st }
#define show_usdma_complete_state(st)			\
	__print_symbolic(st,				\
			 usdma_complete_name(FREE),	\
			 usdma_complete_name(QUEUED),	\
			 usdma_complete_name(COMPLETE), \
			 usdma_complete_name(ERROR))

TRACE_EVENT(hfi1_sdma_user_completion,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u8 subctxt, u16 idx,
		     u8 state, int code),
	    TP_ARGS(dd, ctxt, subctxt, idx, state, code),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd)
		    __field(u16, ctxt)
		    __field(u8, subctxt)
		    __field(u16, idx)
		    __field(u8, state)
		    __field(int, code)
		    ),
	    TP_fast_assign(
		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->idx = idx;
		    __entry->state = state;
		    __entry->code = code;
		    ),
	    TP_printk("[%s:%u:%u:%u] SDMA completion state %s (%d)",
		      __get_str(dev), __entry->ctxt, __entry->subctxt,
		      __entry->idx, show_usdma_complete_state(__entry->state),
		      __entry->code)
	);

const char *print_u32_array(struct trace_seq *, u32 *, int);
#define __print_u32_hex(arr, len) print_u32_array(p, arr, len)

TRACE_EVENT(hfi1_sdma_user_header_ahg,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u8 subctxt, u16 req,
		     u8 sde, u8 ahgidx, u32 *ahg, int len, u32 tidval),
	    TP_ARGS(dd, ctxt, subctxt, req, sde, ahgidx, ahg, len, tidval),
	    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd)
		    __field(u16, ctxt)
		    __field(u8, subctxt)
		    __field(u16, req)
		    __field(u8, sde)
		    __field(u8, idx)
		    __field(int, len)
		    __field(u32, tidval)
		    __array(u32, ahg, 10)
		    ),
	    TP_fast_assign(
		    DD_DEV_ASSIGN(dd);
		    __entry->ctxt = ctxt;
		    __entry->subctxt = subctxt;
		    __entry->req = req;
		    __entry->sde = sde;
		    __entry->idx = ahgidx;
		    __entry->len = len;
		    __entry->tidval = tidval;
		    memcpy(__entry->ahg, ahg, len * sizeof(u32));
		    ),
	    TP_printk("[%s:%u:%u:%u] (SDE%u/AHG%u) ahg[0-%d]=(%s) TIDVal=0x%x",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->req,
		      __entry->sde,
		      __entry->idx,
		      __entry->len - 1,
		      __print_u32_hex(__entry->ahg, __entry->len),
		      __entry->tidval
		    )
	);

TRACE_EVENT(hfi1_sdma_state,
	TP_PROTO(
		struct sdma_engine *sde,
		const char *cstate,
		const char *nstate
	),
	TP_ARGS(sde, cstate, nstate),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(sde->dd)
		__string(curstate, cstate)
		__string(newstate, nstate)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(sde->dd);
		__assign_str(curstate, cstate);
		__assign_str(newstate, nstate);
	),
	TP_printk("[%s] current state %s new state %s",
		__get_str(dev),
		__get_str(curstate),
		__get_str(newstate)
	)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_rc

DECLARE_EVENT_CLASS(hfi1_sdma_rc,
	TP_PROTO(struct hfi1_qp *qp, u32 psn),
	TP_ARGS(qp, psn),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, flags)
		__field(u32, psn)
		__field(u32, sending_psn)
		__field(u32, sending_hpsn)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->flags = qp->s_flags;
		__entry->psn = psn;
		__entry->sending_psn = qp->s_sending_psn;
		__entry->sending_hpsn = qp->s_sending_hpsn;
	),
	TP_printk(
		"[%s] qpn 0x%x flags 0x%x psn 0x%x sending_psn 0x%x sending_hpsn 0x%x",
		__get_str(dev),
		__entry->qpn,
		__entry->flags,
		__entry->psn,
		__entry->sending_psn,
		__entry->sending_psn
	)
);

DEFINE_EVENT(hfi1_sdma_rc, hfi1_rc_sendcomplete,
	     TP_PROTO(struct hfi1_qp *qp, u32 psn),
	     TP_ARGS(qp, psn)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_misc

TRACE_EVENT(hfi1_interrupt,
	TP_PROTO(struct hfi1_devdata *dd, const struct is_table *is_entry,
		 int src),
	TP_ARGS(dd, is_entry, src),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd)
		__array(char, buf, 64)
		__field(int, src)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd)
		is_entry->is_name(__entry->buf, 64, src - is_entry->start);
		__entry->src = src;
	),
	TP_printk("[%s] source: %s [%d]", __get_str(dev), __entry->buf,
		  __entry->src)
);

/*
 * Note:
 * This produces a REALLY ugly trace in the console output when the string is
 * too long.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_trace

#define MAX_MSG_LEN 512

DECLARE_EVENT_CLASS(hfi1_trace_template,
	TP_PROTO(const char *function, struct va_format *vaf),
	TP_ARGS(function, vaf),
	TP_STRUCT__entry(
		__string(function, function)
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		__assign_str(function, function);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
		     MAX_MSG_LEN, vaf->fmt,
		     *vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("(%s) %s",
		  __get_str(function),
		  __get_str(msg))
);

/*
 * It may be nice to macroize the __hfi1_trace but the va_* stuff requires an
 * actual function to work and can not be in a macro.
 */
#define __hfi1_trace_def(lvl) \
void __hfi1_trace_##lvl(const char *funct, char *fmt, ...);		\
									\
DEFINE_EVENT(hfi1_trace_template, hfi1_ ##lvl,				\
	TP_PROTO(const char *function, struct va_format *vaf),		\
	TP_ARGS(function, vaf))

#define __hfi1_trace_fn(lvl) \
void __hfi1_trace_##lvl(const char *func, char *fmt, ...)		\
{									\
	struct va_format vaf = {					\
		.fmt = fmt,						\
	};								\
	va_list args;							\
									\
	va_start(args, fmt);						\
	vaf.va = &args;							\
	trace_hfi1_ ##lvl(func, &vaf);					\
	va_end(args);							\
	return;								\
}

/*
 * To create a new trace level simply define it below and as a __hfi1_trace_fn
 * in trace.c. This will create all the hooks for calling
 * hfi1_cdbg(LVL, fmt, ...); as well as take care of all
 * the debugfs stuff.
 */
__hfi1_trace_def(PKT);
__hfi1_trace_def(PROC);
__hfi1_trace_def(SDMA);
__hfi1_trace_def(LINKVERB);
__hfi1_trace_def(DEBUG);
__hfi1_trace_def(SNOOP);
__hfi1_trace_def(CNTR);
__hfi1_trace_def(PIO);
__hfi1_trace_def(DC8051);
__hfi1_trace_def(FIRMWARE);
__hfi1_trace_def(RCVCTRL);
__hfi1_trace_def(TID);

#define hfi1_cdbg(which, fmt, ...) \
	__hfi1_trace_##which(__func__, fmt, ##__VA_ARGS__)

#define hfi1_dbg(fmt, ...) \
	hfi1_cdbg(DEBUG, fmt, ##__VA_ARGS__)

/*
 * Define HFI1_EARLY_DBG at compile time or here to enable early trace
 * messages. Do not check in an enablement for this.
 */

#ifdef HFI1_EARLY_DBG
#define hfi1_dbg_early(fmt, ...) \
	trace_printk(fmt, ##__VA_ARGS__)
#else
#define hfi1_dbg_early(fmt, ...)
#endif

#endif /* __HFI1_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

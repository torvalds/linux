/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
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
#if !defined(__HFI1_TRACE_TX_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_TX_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"
#include "mad.h"
#include "sdma.h"
#include "ipoib.h"
#include "user_sdma.h"

const char *parse_sdma_flags(struct trace_seq *p, u64 desc0, u64 desc1);

#define __parse_sdma_flags(desc0, desc1) parse_sdma_flags(p, desc0, desc1)

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_tx

TRACE_EVENT(hfi1_piofree,
	    TP_PROTO(struct send_context *sc, int extra),
	    TP_ARGS(sc, extra),
	    TP_STRUCT__entry(DD_DEV_ENTRY(sc->dd)
	    __field(u32, sw_index)
	    __field(u32, hw_context)
	    __field(int, extra)
	    ),
	    TP_fast_assign(DD_DEV_ASSIGN(sc->dd);
	    __entry->sw_index = sc->sw_index;
	    __entry->hw_context = sc->hw_context;
	    __entry->extra = extra;
	    ),
	    TP_printk("[%s] ctxt %u(%u) extra %d",
		      __get_str(dev),
		      __entry->sw_index,
		      __entry->hw_context,
		      __entry->extra
	    )
);

TRACE_EVENT(hfi1_wantpiointr,
	    TP_PROTO(struct send_context *sc, u32 needint, u64 credit_ctrl),
	    TP_ARGS(sc, needint, credit_ctrl),
	    TP_STRUCT__entry(DD_DEV_ENTRY(sc->dd)
			__field(u32, sw_index)
			__field(u32, hw_context)
			__field(u32, needint)
			__field(u64, credit_ctrl)
			),
	    TP_fast_assign(DD_DEV_ASSIGN(sc->dd);
			__entry->sw_index = sc->sw_index;
			__entry->hw_context = sc->hw_context;
			__entry->needint = needint;
			__entry->credit_ctrl = credit_ctrl;
			),
	    TP_printk("[%s] ctxt %u(%u) on %d credit_ctrl 0x%llx",
		      __get_str(dev),
		      __entry->sw_index,
		      __entry->hw_context,
		      __entry->needint,
		      (unsigned long long)__entry->credit_ctrl
		      )
);

DECLARE_EVENT_CLASS(hfi1_qpsleepwakeup_template,
		    TP_PROTO(struct rvt_qp *qp, u32 flags),
		    TP_ARGS(qp, flags),
		    TP_STRUCT__entry(
		    DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		    __field(u32, qpn)
		    __field(u32, flags)
		    __field(u32, s_flags)
		    __field(u32, ps_flags)
		    __field(unsigned long, iow_flags)
		    ),
		    TP_fast_assign(
		    DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		    __entry->flags = flags;
		    __entry->qpn = qp->ibqp.qp_num;
		    __entry->s_flags = qp->s_flags;
		    __entry->ps_flags =
			((struct hfi1_qp_priv *)qp->priv)->s_flags;
		    __entry->iow_flags =
			((struct hfi1_qp_priv *)qp->priv)->s_iowait.flags;
		    ),
		    TP_printk(
		    "[%s] qpn 0x%x flags 0x%x s_flags 0x%x ps_flags 0x%x iow_flags 0x%lx",
		    __get_str(dev),
		    __entry->qpn,
		    __entry->flags,
		    __entry->s_flags,
		    __entry->ps_flags,
		    __entry->iow_flags
		    )
);

DEFINE_EVENT(hfi1_qpsleepwakeup_template, hfi1_qpwakeup,
	     TP_PROTO(struct rvt_qp *qp, u32 flags),
	     TP_ARGS(qp, flags));

DEFINE_EVENT(hfi1_qpsleepwakeup_template, hfi1_qpsleep,
	     TP_PROTO(struct rvt_qp *qp, u32 flags),
	     TP_ARGS(qp, flags));

TRACE_EVENT(hfi1_sdma_descriptor,
	    TP_PROTO(struct sdma_engine *sde,
		     u64 desc0,
		     u64 desc1,
		     u16 e,
		     void *descp),
		     TP_ARGS(sde, desc0, desc1, e, descp),
		     TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		     __field(void *, descp)
		     __field(u64, desc0)
		     __field(u64, desc1)
		     __field(u16, e)
		     __field(u8, idx)
		     ),
		     TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
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
	    (__entry->desc0 >> SDMA_DESC0_PHY_ADDR_SHIFT) &
	    SDMA_DESC0_PHY_ADDR_MASK,
	    (u8)((__entry->desc1 >> SDMA_DESC1_GENERATION_SHIFT) &
	    SDMA_DESC1_GENERATION_MASK),
	    (u16)((__entry->desc0 >> SDMA_DESC0_BYTE_COUNT_SHIFT) &
	    SDMA_DESC0_BYTE_COUNT_MASK),
	    __entry->desc0,
	    __entry->desc1,
	    __entry->descp,
	    __entry->e
	    )
);

TRACE_EVENT(hfi1_sdma_engine_select,
	    TP_PROTO(struct hfi1_devdata *dd, u32 sel, u8 vl, u8 idx),
	    TP_ARGS(dd, sel, vl, idx),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
	    __field(u32, sel)
	    __field(u8, vl)
	    __field(u8, idx)
	    ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
	    __entry->sel = sel;
	    __entry->vl = vl;
	    __entry->idx = idx;
	    ),
	    TP_printk("[%s] selecting SDE %u sel 0x%x vl %u",
		      __get_str(dev),
		      __entry->idx,
		      __entry->sel,
		      __entry->vl
		      )
);

TRACE_EVENT(hfi1_sdma_user_free_queues,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt),
	    TP_ARGS(dd, ctxt, subctxt),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u16, ctxt)
			     __field(u16, subctxt)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   ),
	    TP_printk("[%s] SDMA [%u:%u] Freeing user SDMA queues",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt
		      )
);

TRACE_EVENT(hfi1_sdma_user_process_request,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		     u16 comp_idx),
	    TP_ARGS(dd, ctxt, subctxt, comp_idx),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u16, ctxt)
			     __field(u16, subctxt)
			     __field(u16, comp_idx)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->comp_idx = comp_idx;
			   ),
	    TP_printk("[%s] SDMA [%u:%u] Using req/comp entry: %u",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->comp_idx
		      )
);

DECLARE_EVENT_CLASS(
	hfi1_sdma_value_template,
	TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt, u16 comp_idx,
		 u32 value),
	TP_ARGS(dd, ctxt, subctxt, comp_idx, value),
	TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			 __field(u16, ctxt)
			 __field(u16, subctxt)
			 __field(u16, comp_idx)
			 __field(u32, value)
		),
	TP_fast_assign(DD_DEV_ASSIGN(dd);
		       __entry->ctxt = ctxt;
		       __entry->subctxt = subctxt;
		       __entry->comp_idx = comp_idx;
		       __entry->value = value;
		),
	TP_printk("[%s] SDMA [%u:%u:%u] value: %u",
		  __get_str(dev),
		  __entry->ctxt,
		  __entry->subctxt,
		  __entry->comp_idx,
		  __entry->value
		)
);

DEFINE_EVENT(hfi1_sdma_value_template, hfi1_sdma_user_initial_tidoffset,
	     TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		      u16 comp_idx, u32 tidoffset),
	     TP_ARGS(dd, ctxt, subctxt, comp_idx, tidoffset));

DEFINE_EVENT(hfi1_sdma_value_template, hfi1_sdma_user_data_length,
	     TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		      u16 comp_idx, u32 data_len),
	     TP_ARGS(dd, ctxt, subctxt, comp_idx, data_len));

DEFINE_EVENT(hfi1_sdma_value_template, hfi1_sdma_user_compute_length,
	     TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		      u16 comp_idx, u32 data_len),
	     TP_ARGS(dd, ctxt, subctxt, comp_idx, data_len));

TRACE_EVENT(hfi1_sdma_user_tid_info,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		     u16 comp_idx, u32 tidoffset, u32 units, u8 shift),
	    TP_ARGS(dd, ctxt, subctxt, comp_idx, tidoffset, units, shift),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u16, ctxt)
			     __field(u16, subctxt)
			     __field(u16, comp_idx)
			     __field(u32, tidoffset)
			     __field(u32, units)
			     __field(u8, shift)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->comp_idx = comp_idx;
			   __entry->tidoffset = tidoffset;
			   __entry->units = units;
			   __entry->shift = shift;
			   ),
	    TP_printk("[%s] SDMA [%u:%u:%u] TID offset %ubytes %uunits om %u",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->comp_idx,
		      __entry->tidoffset,
		      __entry->units,
		      __entry->shift
		      )
);

TRACE_EVENT(hfi1_sdma_request,
	    TP_PROTO(struct hfi1_devdata *dd, u16 ctxt, u16 subctxt,
		     unsigned long dim),
	    TP_ARGS(dd, ctxt, subctxt, dim),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u16, ctxt)
			     __field(u16, subctxt)
			     __field(unsigned long, dim)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->dim = dim;
			   ),
	    TP_printk("[%s] SDMA from %u:%u (%lu)",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->dim
		      )
);

DECLARE_EVENT_CLASS(hfi1_sdma_engine_class,
		    TP_PROTO(struct sdma_engine *sde, u64 status),
		    TP_ARGS(sde, status),
		    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		    __field(u64, status)
		    __field(u8, idx)
		    ),
		    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
		    __entry->status = status;
		    __entry->idx = sde->this_idx;
		    ),
		    TP_printk("[%s] SDE(%u) status %llx",
			      __get_str(dev),
			      __entry->idx,
			      (unsigned long long)__entry->status
			      )
);

DEFINE_EVENT(hfi1_sdma_engine_class, hfi1_sdma_engine_interrupt,
	     TP_PROTO(struct sdma_engine *sde, u64 status),
	     TP_ARGS(sde, status)
);

DEFINE_EVENT(hfi1_sdma_engine_class, hfi1_sdma_engine_progress,
	     TP_PROTO(struct sdma_engine *sde, u64 status),
	     TP_ARGS(sde, status)
);

DECLARE_EVENT_CLASS(hfi1_sdma_ahg_ad,
		    TP_PROTO(struct sdma_engine *sde, int aidx),
		    TP_ARGS(sde, aidx),
		    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		    __field(int, aidx)
		    __field(u8, idx)
		    ),
		    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
		    __entry->idx = sde->this_idx;
		    __entry->aidx = aidx;
		    ),
		    TP_printk("[%s] SDE(%u) aidx %d",
			      __get_str(dev),
			      __entry->idx,
			      __entry->aidx
			      )
);

DEFINE_EVENT(hfi1_sdma_ahg_ad, hfi1_ahg_allocate,
	     TP_PROTO(struct sdma_engine *sde, int aidx),
	     TP_ARGS(sde, aidx));

DEFINE_EVENT(hfi1_sdma_ahg_ad, hfi1_ahg_deallocate,
	     TP_PROTO(struct sdma_engine *sde, int aidx),
	     TP_ARGS(sde, aidx));

#ifdef CONFIG_HFI1_DEBUG_SDMA_ORDER
TRACE_EVENT(hfi1_sdma_progress,
	    TP_PROTO(struct sdma_engine *sde,
		     u16 hwhead,
		     u16 swhead,
		     struct sdma_txreq *txp
		     ),
	    TP_ARGS(sde, hwhead, swhead, txp),
	    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
	    __field(u64, sn)
	    __field(u16, hwhead)
	    __field(u16, swhead)
	    __field(u16, txnext)
	    __field(u16, tx_tail)
	    __field(u16, tx_head)
	    __field(u8, idx)
	    ),
	    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
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
	    TP_PROTO(struct sdma_engine *sde,
		     u16 hwhead, u16 swhead,
		     struct sdma_txreq *txp
		     ),
	    TP_ARGS(sde, hwhead, swhead, txp),
	    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		    __field(u16, hwhead)
		    __field(u16, swhead)
		    __field(u16, txnext)
		    __field(u16, tx_tail)
		    __field(u16, tx_head)
		    __field(u8, idx)
		    ),
	    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
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
		    TP_PROTO(struct sdma_engine *sde, u64 sn),
		    TP_ARGS(sde, sn),
		    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		    __field(u64, sn)
		    __field(u8, idx)
		    ),
		    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
		    __entry->sn = sn;
		    __entry->idx = sde->this_idx;
		    ),
		    TP_printk("[%s] SDE(%u) sn %llu",
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
	     TP_PROTO(struct sdma_engine *sde, u64 sn),
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
		    __field(u32, pbc0)
		    __field(u32, pbc1)
		    __field(u32, lrh0)
		    __field(u32, lrh1)
		    __field(u32, bth0)
		    __field(u32, bth1)
		    __field(u32, bth2)
		    __field(u32, kdeth0)
		    __field(u32, kdeth1)
		    __field(u32, kdeth2)
		    __field(u32, kdeth3)
		    __field(u32, kdeth4)
		    __field(u32, kdeth5)
		    __field(u32, kdeth6)
		    __field(u32, kdeth7)
		    __field(u32, kdeth8)
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
		    __entry->pbc0 = le32_to_cpu(pbc[0]);
		    __entry->pbc1 = le32_to_cpu(pbc[1]);
		    __entry->lrh0 = be32_to_cpu(lrh[0]);
		    __entry->lrh1 = be32_to_cpu(lrh[1]);
		    __entry->bth0 = be32_to_cpu(bth[0]);
		    __entry->bth1 = be32_to_cpu(bth[1]);
		    __entry->bth2 = be32_to_cpu(bth[2]);
		    __entry->kdeth0 = le32_to_cpu(kdeth[0]);
		    __entry->kdeth1 = le32_to_cpu(kdeth[1]);
		    __entry->kdeth2 = le32_to_cpu(kdeth[2]);
		    __entry->kdeth3 = le32_to_cpu(kdeth[3]);
		    __entry->kdeth4 = le32_to_cpu(kdeth[4]);
		    __entry->kdeth5 = le32_to_cpu(kdeth[5]);
		    __entry->kdeth6 = le32_to_cpu(kdeth[6]);
		    __entry->kdeth7 = le32_to_cpu(kdeth[7]);
		    __entry->kdeth8 = le32_to_cpu(kdeth[8]);
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
		    DD_DEV_ENTRY(dd)
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

TRACE_EVENT(hfi1_usdma_defer,
	    TP_PROTO(struct hfi1_user_sdma_pkt_q *pq,
		     struct sdma_engine *sde,
		     struct iowait *wait),
	    TP_ARGS(pq, sde, wait),
	    TP_STRUCT__entry(DD_DEV_ENTRY(pq->dd)
			     __field(struct hfi1_user_sdma_pkt_q *, pq)
			     __field(struct sdma_engine *, sde)
			     __field(struct iowait *, wait)
			     __field(int, engine)
			     __field(int, empty)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(pq->dd);
			    __entry->pq = pq;
			    __entry->sde = sde;
			    __entry->wait = wait;
			    __entry->engine = sde->this_idx;
			    __entry->empty = list_empty(&__entry->wait->list);
			    ),
	     TP_printk("[%s] pq %llx sde %llx wait %llx engine %d empty %d",
		       __get_str(dev),
		       (unsigned long long)__entry->pq,
		       (unsigned long long)__entry->sde,
		       (unsigned long long)__entry->wait,
		       __entry->engine,
		       __entry->empty
		)
);

TRACE_EVENT(hfi1_usdma_activate,
	    TP_PROTO(struct hfi1_user_sdma_pkt_q *pq,
		     struct iowait *wait,
		     int reason),
	    TP_ARGS(pq, wait, reason),
	    TP_STRUCT__entry(DD_DEV_ENTRY(pq->dd)
			     __field(struct hfi1_user_sdma_pkt_q *, pq)
			     __field(struct iowait *, wait)
			     __field(int, reason)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(pq->dd);
			    __entry->pq = pq;
			    __entry->wait = wait;
			    __entry->reason = reason;
			    ),
	     TP_printk("[%s] pq %llx wait %llx reason %d",
		       __get_str(dev),
		       (unsigned long long)__entry->pq,
		       (unsigned long long)__entry->wait,
		       __entry->reason
		)
);

TRACE_EVENT(hfi1_usdma_we,
	    TP_PROTO(struct hfi1_user_sdma_pkt_q *pq,
		     int we_ret),
	    TP_ARGS(pq, we_ret),
	    TP_STRUCT__entry(DD_DEV_ENTRY(pq->dd)
			     __field(struct hfi1_user_sdma_pkt_q *, pq)
			     __field(int, state)
			     __field(int, we_ret)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(pq->dd);
			    __entry->pq = pq;
			    __entry->state = pq->state;
			    __entry->we_ret = we_ret;
			    ),
	     TP_printk("[%s] pq %llx state %d we_ret %d",
		       __get_str(dev),
		       (unsigned long long)__entry->pq,
		       __entry->state,
		       __entry->we_ret
		)
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
	    TP_PROTO(struct sdma_engine *sde,
		     const char *cstate,
		     const char *nstate
		     ),
	    TP_ARGS(sde, cstate, nstate),
	    TP_STRUCT__entry(DD_DEV_ENTRY(sde->dd)
		__string(curstate, cstate)
		__string(newstate, nstate)
	    ),
	    TP_fast_assign(DD_DEV_ASSIGN(sde->dd);
		__assign_str(curstate, cstate);
		__assign_str(newstate, nstate);
	    ),
	    TP_printk("[%s] current state %s new state %s",
		      __get_str(dev),
		      __get_str(curstate),
		      __get_str(newstate)
	    )
);

#define BCT_FORMAT \
	"shared_limit %x vls 0-7 [%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x][%x,%x] 15 [%x,%x]"

#define BCT(field) \
	be16_to_cpu( \
	((struct buffer_control *)__get_dynamic_array(bct))->field \
	)

DECLARE_EVENT_CLASS(hfi1_bct_template,
		    TP_PROTO(struct hfi1_devdata *dd,
			     struct buffer_control *bc),
		    TP_ARGS(dd, bc),
		    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
		    __dynamic_array(u8, bct, sizeof(*bc))
		    ),
		    TP_fast_assign(DD_DEV_ASSIGN(dd);
				   memcpy(__get_dynamic_array(bct), bc,
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

TRACE_EVENT(
	hfi1_qp_send_completion,
	TP_PROTO(struct rvt_qp *qp, struct rvt_swqe *wqe, u32 idx),
	TP_ARGS(qp, wqe, idx),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
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
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
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

DECLARE_EVENT_CLASS(
	hfi1_do_send_template,
	TP_PROTO(struct rvt_qp *qp, bool flag),
	TP_ARGS(qp, flag),
	TP_STRUCT__entry(
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(bool, flag)
	),
	TP_fast_assign(
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->flag = flag;
	),
	TP_printk(
		"[%s] qpn %x flag %d",
		__get_str(dev),
		__entry->qpn,
		__entry->flag
	)
);

DEFINE_EVENT(
	hfi1_do_send_template, hfi1_rc_do_send,
	TP_PROTO(struct rvt_qp *qp, bool flag),
	TP_ARGS(qp, flag)
);

DEFINE_EVENT(/* event */
	hfi1_do_send_template, hfi1_rc_do_tid_send,
	TP_PROTO(struct rvt_qp *qp, bool flag),
	TP_ARGS(qp, flag)
);

DEFINE_EVENT(
	hfi1_do_send_template, hfi1_rc_expired_time_slice,
	TP_PROTO(struct rvt_qp *qp, bool flag),
	TP_ARGS(qp, flag)
);

DECLARE_EVENT_CLASS(/* AIP  */
	hfi1_ipoib_txq_template,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(txq->priv->dd)
		__field(struct hfi1_ipoib_txq *, txq)
		__field(struct sdma_engine *, sde)
		__field(ulong, head)
		__field(ulong, tail)
		__field(uint, used)
		__field(uint, flow)
		__field(int, stops)
		__field(int, no_desc)
		__field(u8, idx)
		__field(u8, stopped)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(txq->priv->dd);
		__entry->txq = txq;
		__entry->sde = txq->sde;
		__entry->head = txq->tx_ring.head;
		__entry->tail = txq->tx_ring.tail;
		__entry->idx = txq->q_idx;
		__entry->used =
			txq->sent_txreqs -
			atomic64_read(&txq->complete_txreqs);
		__entry->flow = txq->flow.as_int;
		__entry->stops = atomic_read(&txq->stops);
		__entry->no_desc = atomic_read(&txq->no_desc);
		__entry->stopped =
		 __netif_subqueue_stopped(txq->priv->netdev, txq->q_idx);
	),
	TP_printk(/* print  */
		"[%s] txq %llx idx %u sde %llx head %lx tail %lx flow %x used %u stops %d no_desc %d stopped %u",
		__get_str(dev),
		(unsigned long long)__entry->txq,
		__entry->idx,
		(unsigned long long)__entry->sde,
		__entry->head,
		__entry->tail,
		__entry->flow,
		__entry->used,
		__entry->stops,
		__entry->no_desc,
		__entry->stopped
	)
);

DEFINE_EVENT(/* queue stop */
	hfi1_ipoib_txq_template, hfi1_txq_stop,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* queue wake */
	hfi1_ipoib_txq_template, hfi1_txq_wake,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* flow flush */
	hfi1_ipoib_txq_template, hfi1_flow_flush,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* flow switch */
	hfi1_ipoib_txq_template, hfi1_flow_switch,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* wakeup */
	hfi1_ipoib_txq_template, hfi1_txq_wakeup,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* full */
	hfi1_ipoib_txq_template, hfi1_txq_full,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* queued */
	hfi1_ipoib_txq_template, hfi1_txq_queued,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* xmit_stopped */
	hfi1_ipoib_txq_template, hfi1_txq_xmit_stopped,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

DEFINE_EVENT(/* xmit_unstopped */
	hfi1_ipoib_txq_template, hfi1_txq_xmit_unstopped,
	TP_PROTO(struct hfi1_ipoib_txq *txq),
	TP_ARGS(txq)
);

#endif /* __HFI1_TRACE_TX_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_tx
#include <trace/define_trace.h>

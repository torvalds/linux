/*
 * Copyright(c) 2015, 2016 Intel Corporation.
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
#if !defined(__HFI1_TRACE_RX_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_RX_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_rx

TRACE_EVENT(hfi1_rcvhdr,
	    TP_PROTO(struct hfi1_devdata *dd,
		     u32 ctxt,
		     u64 eflags,
		     u32 etype,
		     u32 hlen,
		     u32 tlen,
		     u32 updegr,
		     u32 etail
		    ),
	    TP_ARGS(dd, ctxt, eflags, etype, hlen, tlen, updegr, etail),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u64, eflags)
			     __field(u32, ctxt)
			     __field(u32, etype)
			     __field(u32, hlen)
			     __field(u32, tlen)
			     __field(u32, updegr)
			     __field(u32, etail)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(dd);
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
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u32, ctxt)
			     __field(u8, slow_path)
			     __field(u8, dma_rtail)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
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
	    TP_printk("[%s] ctxt %d SlowPath: %d DmaRtail: %d",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->slow_path,
		      __entry->dma_rtail
		      )
);

TRACE_EVENT(hfi1_exp_tid_reg,
	    TP_PROTO(unsigned int ctxt, u16 subctxt, u32 rarr,
		     u32 npages, unsigned long va, unsigned long pa,
		     dma_addr_t dma),
	    TP_ARGS(ctxt, subctxt, rarr, npages, va, pa, dma),
	    TP_STRUCT__entry(
			     __field(unsigned int, ctxt)
			     __field(u16, subctxt)
			     __field(u32, rarr)
			     __field(u32, npages)
			     __field(unsigned long, va)
			     __field(unsigned long, pa)
			     __field(dma_addr_t, dma)
			     ),
	    TP_fast_assign(
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->rarr = rarr;
			   __entry->npages = npages;
			   __entry->va = va;
			   __entry->pa = pa;
			   __entry->dma = dma;
			   ),
	    TP_printk("[%u:%u] entry:%u, %u pages @ 0x%lx, va:0x%lx dma:0x%llx",
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->rarr,
		      __entry->npages,
		      __entry->pa,
		      __entry->va,
		      __entry->dma
		      )
	);

TRACE_EVENT(hfi1_exp_tid_unreg,
	    TP_PROTO(unsigned int ctxt, u16 subctxt, u32 rarr, u32 npages,
		     unsigned long va, unsigned long pa, dma_addr_t dma),
	    TP_ARGS(ctxt, subctxt, rarr, npages, va, pa, dma),
	    TP_STRUCT__entry(
			     __field(unsigned int, ctxt)
			     __field(u16, subctxt)
			     __field(u32, rarr)
			     __field(u32, npages)
			     __field(unsigned long, va)
			     __field(unsigned long, pa)
			     __field(dma_addr_t, dma)
			     ),
	    TP_fast_assign(
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->rarr = rarr;
			   __entry->npages = npages;
			   __entry->va = va;
			   __entry->pa = pa;
			   __entry->dma = dma;
			   ),
	    TP_printk("[%u:%u] entry:%u, %u pages @ 0x%lx, va:0x%lx dma:0x%llx",
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->rarr,
		      __entry->npages,
		      __entry->pa,
		      __entry->va,
		      __entry->dma
		      )
	);

TRACE_EVENT(hfi1_exp_tid_inval,
	    TP_PROTO(unsigned int ctxt, u16 subctxt, unsigned long va, u32 rarr,
		     u32 npages, dma_addr_t dma),
	    TP_ARGS(ctxt, subctxt, va, rarr, npages, dma),
	    TP_STRUCT__entry(
			     __field(unsigned int, ctxt)
			     __field(u16, subctxt)
			     __field(unsigned long, va)
			     __field(u32, rarr)
			     __field(u32, npages)
			     __field(dma_addr_t, dma)
			     ),
	    TP_fast_assign(
			   __entry->ctxt = ctxt;
			   __entry->subctxt = subctxt;
			   __entry->va = va;
			   __entry->rarr = rarr;
			   __entry->npages = npages;
			   __entry->dma = dma;
			  ),
	    TP_printk("[%u:%u] entry:%u, %u pages @ 0x%lx dma: 0x%llx",
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->rarr,
		      __entry->npages,
		      __entry->va,
		      __entry->dma
		      )
	    );

TRACE_EVENT(hfi1_mmu_invalidate,
	    TP_PROTO(unsigned int ctxt, u16 subctxt, const char *type,
		     unsigned long start, unsigned long end),
	    TP_ARGS(ctxt, subctxt, type, start, end),
	    TP_STRUCT__entry(
			     __field(unsigned int, ctxt)
			     __field(u16, subctxt)
			     __string(type, type)
			     __field(unsigned long, start)
			     __field(unsigned long, end)
			     ),
	    TP_fast_assign(
			__entry->ctxt = ctxt;
			__entry->subctxt = subctxt;
			__assign_str(type, type);
			__entry->start = start;
			__entry->end = end;
	    ),
	    TP_printk("[%3u:%02u] MMU Invalidate (%s) 0x%lx - 0x%lx",
		      __entry->ctxt,
		      __entry->subctxt,
		      __get_str(type),
		      __entry->start,
		      __entry->end
		      )
	    );

#define SNOOP_PRN \
	"slid %.4x dlid %.4x qpn 0x%.6x opcode 0x%.2x,%s " \
	"svc lvl %d pkey 0x%.4x [header = %d bytes] [data = %d bytes]"

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
		__entry->qpn = be32_to_cpu(ohdr->bth[1]) & RVT_QPN_MASK;
		__entry->opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;
		__entry->sl = (u8)(be16_to_cpu(hdr->lrh[0]) >> 4) & 0xf;
		__entry->pkey =	be32_to_cpu(ohdr->bth[0]) & 0xffff;
		__entry->hdr_len = hdr_len;
		__entry->data_len = data_len;
		memcpy(__get_dynamic_array(raw_hdr), hdr, hdr_len);
		memcpy(__get_dynamic_array(raw_pkt), data, data_len);
		),
	    TP_printk(
		"[%s] " SNOOP_PRN,
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

#endif /* __HFI1_TRACE_RX_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_rx
#include <trace/define_trace.h>

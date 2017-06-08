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
#if !defined(__HFI1_TRACE_CTXTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_CTXTS_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_ctxts

#define UCTXT_FMT \
	"cred:%u, credaddr:0x%llx, piobase:0x%p, rcvhdr_cnt:%u, "	\
	"rcvbase:0x%llx, rcvegrc:%u, rcvegrb:0x%llx, subctxt_cnt:%u"
TRACE_EVENT(hfi1_uctxtdata,
	    TP_PROTO(struct hfi1_devdata *dd, struct hfi1_ctxtdata *uctxt,
		     unsigned int subctxt),
	    TP_ARGS(dd, uctxt, subctxt),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(unsigned int, ctxt)
			     __field(unsigned int, subctxt)
			     __field(u32, credits)
			     __field(u64, hw_free)
			     __field(void __iomem *, piobase)
			     __field(u16, rcvhdrq_cnt)
			     __field(u64, rcvhdrq_dma)
			     __field(u32, eager_cnt)
			     __field(u64, rcvegr_dma)
			     __field(unsigned int, subctxt_cnt)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   __entry->ctxt = uctxt->ctxt;
			   __entry->subctxt = subctxt;
			   __entry->credits = uctxt->sc->credits;
			   __entry->hw_free = le64_to_cpu(*uctxt->sc->hw_free);
			   __entry->piobase = uctxt->sc->base_addr;
			   __entry->rcvhdrq_cnt = uctxt->rcvhdrq_cnt;
			   __entry->rcvhdrq_dma = uctxt->rcvhdrq_dma;
			   __entry->eager_cnt = uctxt->egrbufs.alloced;
			   __entry->rcvegr_dma = uctxt->egrbufs.rcvtids[0].dma;
			   __entry->subctxt_cnt = uctxt->subctxt_cnt;
			   ),
	    TP_printk("[%s] ctxt %u:%u " UCTXT_FMT,
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->subctxt,
		      __entry->credits,
		      __entry->hw_free,
		      __entry->piobase,
		      __entry->rcvhdrq_cnt,
		      __entry->rcvhdrq_dma,
		      __entry->eager_cnt,
		      __entry->rcvegr_dma,
		      __entry->subctxt_cnt
		      )
);

#define CINFO_FMT \
	"egrtids:%u, egr_size:%u, hdrq_cnt:%u, hdrq_size:%u, sdma_ring_size:%u"
TRACE_EVENT(hfi1_ctxt_info,
	    TP_PROTO(struct hfi1_devdata *dd, unsigned int ctxt,
		     unsigned int subctxt,
		     struct hfi1_ctxt_info cinfo),
	    TP_ARGS(dd, ctxt, subctxt, cinfo),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(unsigned int, ctxt)
			     __field(unsigned int, subctxt)
			     __field(u16, egrtids)
			     __field(u16, rcvhdrq_cnt)
			     __field(u16, rcvhdrq_size)
			     __field(u16, sdma_ring_size)
			     __field(u32, rcvegr_size)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			    __entry->ctxt = ctxt;
			    __entry->subctxt = subctxt;
			    __entry->egrtids = cinfo.egrtids;
			    __entry->rcvhdrq_cnt = cinfo.rcvhdrq_cnt;
			    __entry->rcvhdrq_size = cinfo.rcvhdrq_entsize;
			    __entry->sdma_ring_size = cinfo.sdma_ring_size;
			    __entry->rcvegr_size = cinfo.rcvegr_size;
			    ),
	    TP_printk("[%s] ctxt %u:%u " CINFO_FMT,
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

#endif /* __HFI1_TRACE_CTXTS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_ctxts
#include <trace/define_trace.h>

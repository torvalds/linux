/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
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

#define tidtype_name(type) { PT_##type, #type }
#define show_tidtype(type)                   \
__print_symbolic(type,                       \
	tidtype_name(EXPECTED),              \
	tidtype_name(EAGER),                 \
	tidtype_name(INVALID))               \

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_rx

TRACE_EVENT(hfi1_rcvhdr,
	    TP_PROTO(struct hfi1_packet *packet),
	    TP_ARGS(packet),
	    TP_STRUCT__entry(DD_DEV_ENTRY(packet->rcd->dd)
			     __field(u64, eflags)
			     __field(u32, ctxt)
			     __field(u32, etype)
			     __field(u32, hlen)
			     __field(u32, tlen)
			     __field(u32, updegr)
			     __field(u32, etail)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(packet->rcd->dd);
			    __entry->eflags = rhf_err_flags(packet->rhf);
			    __entry->ctxt = packet->rcd->ctxt;
			    __entry->etype = packet->etype;
			    __entry->hlen = packet->hlen;
			    __entry->tlen = packet->tlen;
			    __entry->updegr = packet->updegr;
			    __entry->etail = rhf_egr_index(packet->rhf);
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
	    TP_PROTO(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd),
	    TP_ARGS(dd, rcd),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __field(u32, ctxt)
			     __field(u8, slow_path)
			     __field(u8, dma_rtail)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			__entry->ctxt = rcd->ctxt;
			if (rcd->do_interrupt ==
			    &handle_receive_interrupt)
				__entry->slow_path = 1;
			else
				__entry->slow_path = 0;
			__entry->dma_rtail = get_dma_rtail_setting(rcd);
			),
	    TP_printk("[%s] ctxt %d SlowPath: %d DmaRtail: %d",
		      __get_str(dev),
		      __entry->ctxt,
		      __entry->slow_path,
		      __entry->dma_rtail
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

#endif /* __HFI1_TRACE_RX_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_rx
#include <trace/define_trace.h>

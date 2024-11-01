/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
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
			__entry->slow_path = hfi1_is_slowpath(rcd);
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

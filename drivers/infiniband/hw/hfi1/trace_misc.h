/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
* Copyright(c) 2015, 2016 Intel Corporation.
*/

#if !defined(__HFI1_TRACE_MISC_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_MISC_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_misc

TRACE_EVENT(hfi1_interrupt,
	    TP_PROTO(struct hfi1_devdata *dd, const struct is_table *is_entry,
		     int src),
	    TP_ARGS(dd, is_entry, src),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd)
			     __array(char, buf, 64)
			     __field(int, src)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd);
			   is_entry->is_name(__entry->buf, 64,
					     src - is_entry->start);
			   __entry->src = src;
			   ),
	    TP_printk("[%s] source: %s [%d]", __get_str(dev), __entry->buf,
		      __entry->src)
);

DECLARE_EVENT_CLASS(
	hfi1_csr_template,
	TP_PROTO(void __iomem *addr, u64 value),
	TP_ARGS(addr, value),
	TP_STRUCT__entry(
		__field(void __iomem *, addr)
		__field(u64, value)
	),
	TP_fast_assign(
		__entry->addr = addr;
		__entry->value = value;
	),
	TP_printk("addr %p value %llx", __entry->addr, __entry->value)
);

DEFINE_EVENT(
	hfi1_csr_template, hfi1_write_rcvarray,
	TP_PROTO(void __iomem *addr, u64 value),
	TP_ARGS(addr, value));

#ifdef CONFIG_FAULT_INJECTION
TRACE_EVENT(hfi1_fault_opcode,
	    TP_PROTO(struct rvt_qp *qp, u8 opcode),
	    TP_ARGS(qp, opcode),
	    TP_STRUCT__entry(DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
			     __field(u32, qpn)
			     __field(u8, opcode)
			     ),
	    TP_fast_assign(DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
			   __entry->qpn = qp->ibqp.qp_num;
			   __entry->opcode = opcode;
			   ),
	    TP_printk("[%s] qpn 0x%x opcode 0x%x",
		      __get_str(dev), __entry->qpn, __entry->opcode)
);

TRACE_EVENT(hfi1_fault_packet,
	    TP_PROTO(struct hfi1_packet *packet),
	    TP_ARGS(packet),
	    TP_STRUCT__entry(DD_DEV_ENTRY(packet->rcd->ppd->dd)
			     __field(u64, eflags)
			     __field(u32, ctxt)
			     __field(u32, hlen)
			     __field(u32, tlen)
			     __field(u32, updegr)
			     __field(u32, etail)
			     ),
	     TP_fast_assign(DD_DEV_ASSIGN(packet->rcd->ppd->dd);
			    __entry->eflags = rhf_err_flags(packet->rhf);
			    __entry->ctxt = packet->rcd->ctxt;
			    __entry->hlen = packet->hlen;
			    __entry->tlen = packet->tlen;
			    __entry->updegr = packet->updegr;
			    __entry->etail = rhf_egr_index(packet->rhf);
			    ),
	     TP_printk(
		"[%s] ctxt %d eflags 0x%llx hlen %d tlen %d updegr %d etail %d",
		__get_str(dev),
		__entry->ctxt,
		__entry->eflags,
		__entry->hlen,
		__entry->tlen,
		__entry->updegr,
		__entry->etail
		)
);
#endif

#endif /* __HFI1_TRACE_MISC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_misc
#include <trace/define_trace.h>

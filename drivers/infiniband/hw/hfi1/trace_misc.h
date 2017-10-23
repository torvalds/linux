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
	    TP_fast_assign(DD_DEV_ASSIGN(dd)
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
	    TP_fast_assign(DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
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

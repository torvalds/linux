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
#if !defined(__HFI1_TRACE_IBHDRS_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_IBHDRS_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_ibhdrs

u8 ibhdr_exhdr_len(struct ib_header *hdr);
const char *parse_everbs_hdrs(struct trace_seq *p, u8 opcode, void *ehdrs);

#define __parse_ib_ehdrs(op, ehdrs) parse_everbs_hdrs(p, op, ehdrs)

#define lrh_name(lrh) { HFI1_##lrh, #lrh }
#define show_lnh(lrh)                    \
__print_symbolic(lrh,                    \
	lrh_name(LRH_BTH),               \
	lrh_name(LRH_GRH))

#define LRH_PRN "vl %d lver %d sl %d lnh %d,%s dlid %.4x len %d slid %.4x"
#define BTH_PRN \
	"op 0x%.2x,%s se %d m %d pad %d tver %d pkey 0x%.4x " \
	"f %d b %d qpn 0x%.6x a %d psn 0x%.8x"
#define EHDR_PRN "%s"

DECLARE_EVENT_CLASS(hfi1_ibhdr_template,
		    TP_PROTO(struct hfi1_devdata *dd,
			     struct ib_header *hdr),
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
			struct ib_other_headers *ohdr;

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
			(be32_to_cpu(ohdr->bth[1]) >> HFI1_FECN_SHIFT) &
			HFI1_FECN_MASK;
			__entry->b =
			(be32_to_cpu(ohdr->bth[1]) >> HFI1_BECN_SHIFT) &
			HFI1_BECN_MASK;
			__entry->qpn =
			be32_to_cpu(ohdr->bth[1]) & RVT_QPN_MASK;
			__entry->a =
			(be32_to_cpu(ohdr->bth[2]) >> 31) & 1;
			/* allow for larger PSN */
			__entry->psn =
			be32_to_cpu(ohdr->bth[2]) & 0x7fffffff;
			/* extended headers */
			memcpy(__get_dynamic_array(ehdrs), &ohdr->u,
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
	     TP_PROTO(struct hfi1_devdata *dd, struct ib_header *hdr),
	     TP_ARGS(dd, hdr));

DEFINE_EVENT(hfi1_ibhdr_template, pio_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd, struct ib_header *hdr),
	     TP_ARGS(dd, hdr));

DEFINE_EVENT(hfi1_ibhdr_template, ack_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd, struct ib_header *hdr),
	     TP_ARGS(dd, hdr));

DEFINE_EVENT(hfi1_ibhdr_template, sdma_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd, struct ib_header *hdr),
	     TP_ARGS(dd, hdr));

#endif /* __HFI1_TRACE_IBHDRS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_ibhdrs
#include <trace/define_trace.h>

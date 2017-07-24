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
	ib_opcode_name(UD_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(CNP))

const char *parse_everbs_hdrs(struct trace_seq *p, u8 opcode, void *ehdrs);
u8 hfi1_trace_ib_hdr_len(struct ib_header *hdr);
const char *hfi1_trace_get_packet_str(struct hfi1_packet *packet);
void hfi1_trace_parse_bth(struct ib_other_headers *ohdr,
			  u8 *ack, u8 *becn, u8 *fecn, u8 *mig,
			  u8 *se, u8 *pad, u8 *opcode, u8 *tver,
			  u16 *pkey, u32 *psn, u32 *qpn);
void hfi1_trace_parse_9b_hdr(struct ib_header *hdr, bool sc5,
			     struct ib_other_headers **ohdr,
			     u8 *lnh, u8 *lver, u8 *sl, u8 *sc,
			     u16 *len, u32 *dlid, u32 *slid);

#define __parse_ib_ehdrs(op, ehdrs) parse_everbs_hdrs(p, op, ehdrs)

#define lrh_name(lrh) { HFI1_##lrh, #lrh }
#define show_lnh(lrh)                    \
__print_symbolic(lrh,                    \
	lrh_name(LRH_BTH),               \
	lrh_name(LRH_GRH))

#define LRH_PRN "len:%d sc:%d dlid:0x%.4x slid:0x%.4x"
#define LRH_9B_PRN "lnh:%d,%s lver:%d sl:%d "
#define BTH_PRN \
	"op:0x%.2x,%s se:%d m:%d pad:%d tver:%d pkey:0x%.4x " \
	"f:%d b:%d qpn:0x%.6x a:%d psn:0x%.8x"
#define EHDR_PRN "hlen:%d %s"

DECLARE_EVENT_CLASS(hfi1_input_ibhdr_template,
		    TP_PROTO(struct hfi1_devdata *dd,
			     struct hfi1_packet *packet,
			     bool sc5),
		    TP_ARGS(dd, packet, sc5),
		    TP_STRUCT__entry(
			DD_DEV_ENTRY(dd)
			__field(u8, lnh)
			__field(u8, lver)
			__field(u8, sl)
			__field(u16, len)
			__field(u32, dlid)
			__field(u8, sc)
			__field(u32, slid)
			__field(u8, opcode)
			__field(u8, se)
			__field(u8, mig)
			__field(u8, pad)
			__field(u8, tver)
			__field(u16, pkey)
			__field(u8, fecn)
			__field(u8, becn)
			__field(u32, qpn)
			__field(u8, ack)
			__field(u32, psn)
			/* extended headers */
			__dynamic_array(u8, ehdrs,
					hfi1_trace_ib_hdr_len(packet->hdr))
			),
		    TP_fast_assign(
			   struct ib_other_headers *ohdr;

			   DD_DEV_ASSIGN(dd);

			   hfi1_trace_parse_9b_hdr(packet->hdr, sc5,
						   &ohdr,
						   &__entry->lnh,
						   &__entry->lver,
						   &__entry->sl,
						   &__entry->sc,
						   &__entry->len,
						   &__entry->dlid,
						   &__entry->slid);

			  hfi1_trace_parse_bth(ohdr, &__entry->ack,
					       &__entry->becn, &__entry->fecn,
					       &__entry->mig, &__entry->se,
					       &__entry->pad, &__entry->opcode,
					       &__entry->tver, &__entry->pkey,
					       &__entry->psn, &__entry->qpn);
			  /* extended headers */
			  memcpy(__get_dynamic_array(ehdrs), &ohdr->u,
				 __get_dynamic_array_len(ehdrs));
			 ),
		    TP_printk("[%s] (IB) " LRH_PRN " " LRH_9B_PRN " "
			      BTH_PRN " " EHDR_PRN,
			      __get_str(dev),
			      __entry->len,
			      __entry->sc,
			      __entry->dlid,
			      __entry->slid,
			      __entry->lnh, show_lnh(__entry->lnh),
			      __entry->lver,
			      __entry->sl,
			      /* BTH */
			      __entry->opcode, show_ib_opcode(__entry->opcode),
			      __entry->se,
			      __entry->mig,
			      __entry->pad,
			      __entry->tver,
			      __entry->pkey,
			      __entry->fecn,
			      __entry->becn,
			      __entry->qpn,
			      __entry->ack,
			      __entry->psn,
			      /* extended headers */
			      __get_dynamic_array_len(ehdrs),
			      __parse_ib_ehdrs(
					__entry->opcode,
					(void *)__get_dynamic_array(ehdrs))
			     )
);

DEFINE_EVENT(hfi1_input_ibhdr_template, input_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd,
		      struct hfi1_packet *packet, bool sc5),
	     TP_ARGS(dd, packet, sc5));

DECLARE_EVENT_CLASS(hfi1_output_ibhdr_template,
		    TP_PROTO(struct hfi1_devdata *dd,
			     struct ib_header *hdr,
			     bool sc5),
		    TP_ARGS(dd, hdr, sc5),
		    TP_STRUCT__entry(
			DD_DEV_ENTRY(dd)
			__field(u8, lnh)
			__field(u8, lver)
			__field(u8, sl)
			__field(u16, len)
			__field(u32, dlid)
			__field(u8, sc)
			__field(u32, slid)
			__field(u8, opcode)
			__field(u8, se)
			__field(u8, mig)
			__field(u8, pad)
			__field(u8, tver)
			__field(u16, pkey)
			__field(u8, fecn)
			__field(u8, becn)
			__field(u32, qpn)
			__field(u8, ack)
			__field(u32, psn)
			/* extended headers */
			__dynamic_array(u8, ehdrs,
					hfi1_trace_ib_hdr_len(hdr))
			),
		    TP_fast_assign(
			struct ib_other_headers *ohdr;

			DD_DEV_ASSIGN(dd);

			hfi1_trace_parse_9b_hdr(hdr, sc5,
						&ohdr, &__entry->lnh,
						&__entry->lver, &__entry->sl,
						&__entry->sc, &__entry->len,
						&__entry->dlid, &__entry->slid);

			hfi1_trace_parse_bth(ohdr, &__entry->ack,
					     &__entry->becn, &__entry->fecn,
					     &__entry->mig, &__entry->se,
					     &__entry->pad, &__entry->opcode,
					     &__entry->tver, &__entry->pkey,
					     &__entry->psn, &__entry->qpn);

			/* extended headers */
			memcpy(__get_dynamic_array(ehdrs),
			       &ohdr->u, __get_dynamic_array_len(ehdrs));
		    ),
		    TP_printk("[%s] (IB) " LRH_PRN " " LRH_9B_PRN " "
			      BTH_PRN " " EHDR_PRN,
			      __get_str(dev),
			      __entry->len,
			      __entry->sc,
			      __entry->dlid,
			      __entry->slid,
			      __entry->lnh, show_lnh(__entry->lnh),
			      __entry->lver,
			      __entry->sl,
			      /* BTH */
			      __entry->opcode, show_ib_opcode(__entry->opcode),
			      __entry->se,
			      __entry->mig,
			      __entry->pad,
			      __entry->tver,
			      __entry->pkey,
			      __entry->fecn,
			      __entry->becn,
			      __entry->qpn,
			      __entry->ack,
			      __entry->psn,
			      /* extended headers */
			      __get_dynamic_array_len(ehdrs),
			      __parse_ib_ehdrs(
					__entry->opcode,
					(void *)__get_dynamic_array(ehdrs))
			     )
);

DEFINE_EVENT(hfi1_output_ibhdr_template, pio_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd,
		      struct ib_header *hdr, bool sc5),
	     TP_ARGS(dd, hdr, sc5));

DEFINE_EVENT(hfi1_output_ibhdr_template, ack_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd,
		      struct ib_header *hdr, bool sc5),
	     TP_ARGS(dd, hdr, sc5));

DEFINE_EVENT(hfi1_output_ibhdr_template, sdma_output_ibhdr,
	     TP_PROTO(struct hfi1_devdata *dd,
		      struct ib_header *hdr, bool sc5),
	     TP_ARGS(dd, hdr, sc5));


#endif /* __HFI1_TRACE_IBHDRS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_ibhdrs
#include <trace/define_trace.h>

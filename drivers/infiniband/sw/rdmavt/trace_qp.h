/*
 * Copyright(c) 2016 Intel Corporation.
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
#if !defined(__RVT_TRACE_QP_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_QP_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_qp.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt_qp

DECLARE_EVENT_CLASS(rvt_qphash_template,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, bucket)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device))
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

DEFINE_EVENT(rvt_qphash_template, rvt_qpinsert,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

DEFINE_EVENT(rvt_qphash_template, rvt_qpremove,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

DECLARE_EVENT_CLASS(
	rvt_rnrnak_template,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(u32, qpn)
		__field(void *, hrtimer)
		__field(u32, s_flags)
		__field(u32, to)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->hrtimer = &qp->s_rnr_timer;
		__entry->s_flags = qp->s_flags;
		__entry->to = to;
	),
	TP_printk(
		"[%s] qpn 0x%x hrtimer 0x%p s_flags 0x%x timeout %u us",
		__get_str(dev),
		__entry->qpn,
		__entry->hrtimer,
		__entry->s_flags,
		__entry->to
	)
);

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_add,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_timeout,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_stop,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

#endif /* __RVT_TRACE_QP_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_qp
#include <trace/define_trace.h>


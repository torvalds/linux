/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */
#if !defined(__RVT_TRACE_MR_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_MR_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_mr.h>

#include "mr.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt_mr
DECLARE_EVENT_CLASS(
	rvt_mr_template,
	TP_PROTO(struct rvt_mregion *mr, u16 m, u16 n, void *v, size_t len),
	TP_ARGS(mr, m, n, v, len),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(mr->pd->device))
		__field(void *, vaddr)
		__field(struct page *, page)
		__field(u64, iova)
		__field(u64, user_base)
		__field(size_t, len)
		__field(size_t, length)
		__field(u32, lkey)
		__field(u32, offset)
		__field(u16, m)
		__field(u16, n)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(mr->pd->device));
		__entry->vaddr = v;
		__entry->page = virt_to_page(v);
		__entry->iova = mr->iova;
		__entry->user_base = mr->user_base;
		__entry->lkey = mr->lkey;
		__entry->m = m;
		__entry->n = n;
		__entry->len = len;
		__entry->length = mr->length;
		__entry->offset = mr->offset;
	),
	TP_printk(
		"[%s] lkey %x iova %llx user_base %llx mr_len %lu vaddr %llx page %p m %u n %u len %lu off %u",
		__get_str(dev),
		__entry->lkey,
		__entry->iova,
		__entry->user_base,
		__entry->length,
		(unsigned long long)__entry->vaddr,
		__entry->page,
		__entry->m,
		__entry->n,
		__entry->len,
		__entry->offset
	)
);

DEFINE_EVENT(
	rvt_mr_template, rvt_mr_page_seg,
	TP_PROTO(struct rvt_mregion *mr, u16 m, u16 n, void *v, size_t len),
	TP_ARGS(mr, m, n, v, len));

DEFINE_EVENT(
	rvt_mr_template, rvt_mr_fmr_seg,
	TP_PROTO(struct rvt_mregion *mr, u16 m, u16 n, void *v, size_t len),
	TP_ARGS(mr, m, n, v, len));

DEFINE_EVENT(
	rvt_mr_template, rvt_mr_user_seg,
	TP_PROTO(struct rvt_mregion *mr, u16 m, u16 n, void *v, size_t len),
	TP_ARGS(mr, m, n, v, len));

DECLARE_EVENT_CLASS(
	rvt_sge_template,
	TP_PROTO(struct rvt_sge *sge, struct ib_sge *isge),
	TP_ARGS(sge, isge),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(sge->mr->pd->device))
		__field(struct rvt_mregion *, mr)
		__field(struct rvt_sge *, sge)
		__field(struct ib_sge *, isge)
		__field(void *, vaddr)
		__field(u64, ivaddr)
		__field(u32, lkey)
		__field(u32, sge_length)
		__field(u32, length)
		__field(u32, ilength)
		__field(int, user)
		__field(u16, m)
		__field(u16, n)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(sge->mr->pd->device));
		__entry->mr = sge->mr;
		__entry->sge = sge;
		__entry->isge = isge;
		__entry->vaddr = sge->vaddr;
		__entry->ivaddr = isge->addr;
		__entry->lkey = sge->mr->lkey;
		__entry->sge_length = sge->sge_length;
		__entry->length = sge->length;
		__entry->ilength = isge->length;
		__entry->m = sge->m;
		__entry->n = sge->m;
		__entry->user = ibpd_to_rvtpd(sge->mr->pd)->user;
	),
	TP_printk(
		"[%s] mr %p sge %p isge %p vaddr %p ivaddr %llx lkey %x sge_length %u length %u ilength %u m %u n %u user %u",
		__get_str(dev),
		__entry->mr,
		__entry->sge,
		__entry->isge,
		__entry->vaddr,
		__entry->ivaddr,
		__entry->lkey,
		__entry->sge_length,
		__entry->length,
		__entry->ilength,
		__entry->m,
		__entry->n,
		__entry->user
	)
);

DEFINE_EVENT(
	rvt_sge_template, rvt_sge_adjacent,
	TP_PROTO(struct rvt_sge *sge, struct ib_sge *isge),
	TP_ARGS(sge, isge));

DEFINE_EVENT(
	rvt_sge_template, rvt_sge_new,
	TP_PROTO(struct rvt_sge *sge, struct ib_sge *isge),
	TP_ARGS(sge, isge));

TRACE_EVENT(
	rvt_map_mr_sg,
	TP_PROTO(struct ib_mr *ibmr, int sg_nents, unsigned int *sg_offset),
	TP_ARGS(ibmr, sg_nents, sg_offset),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(to_imr(ibmr)->mr.pd->device))
		__field(u64, iova)
		__field(u64, ibmr_iova)
		__field(u64, user_base)
		__field(u64, ibmr_length)
		__field(int, sg_nents)
		__field(uint, sg_offset)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(to_imr(ibmr)->mr.pd->device));
		__entry->ibmr_iova = ibmr->iova;
		__entry->iova = to_imr(ibmr)->mr.iova;
		__entry->user_base = to_imr(ibmr)->mr.user_base;
		__entry->ibmr_length = to_imr(ibmr)->mr.length;
		__entry->sg_nents = sg_nents;
		__entry->sg_offset = sg_offset ? *sg_offset : 0;
	),
	TP_printk(
		"[%s] ibmr_iova %llx iova %llx user_base %llx length %llx sg_nents %d sg_offset %u",
		__get_str(dev),
		__entry->ibmr_iova,
		__entry->iova,
		__entry->user_base,
		__entry->ibmr_length,
		__entry->sg_nents,
		__entry->sg_offset
	)
);

#endif /* __RVT_TRACE_MR_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_mr
#include <trace/define_trace.h>

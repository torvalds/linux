/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 Hisilicon Limited.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hns_roce

#if !defined(__HNS_ROCE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HNS_ROCE_TRACE_H

#include <linux/tracepoint.h>
#include <linux/string_choices.h>
#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"

DECLARE_EVENT_CLASS(flush_head_template,
		    TP_PROTO(unsigned long qpn, u32 pi,
			     enum hns_roce_trace_type type),
		    TP_ARGS(qpn, pi, type),

		    TP_STRUCT__entry(__field(unsigned long, qpn)
				     __field(u32, pi)
				     __field(enum hns_roce_trace_type, type)
		    ),

		    TP_fast_assign(__entry->qpn = qpn;
				   __entry->pi = pi;
				   __entry->type = type;
		    ),

		    TP_printk("%s 0x%lx flush head 0x%x.",
			      trace_type_to_str(__entry->type),
			      __entry->qpn, __entry->pi)
);

DEFINE_EVENT(flush_head_template, hns_sq_flush_cqe,
	     TP_PROTO(unsigned long qpn, u32 pi,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, pi, type));
DEFINE_EVENT(flush_head_template, hns_rq_flush_cqe,
	     TP_PROTO(unsigned long qpn, u32 pi,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, pi, type));

#define MAX_SGE_PER_WQE 64
#define MAX_WQE_SIZE (MAX_SGE_PER_WQE * HNS_ROCE_SGE_SIZE)
DECLARE_EVENT_CLASS(wqe_template,
		    TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len,
			     u64 id, enum hns_roce_trace_type type),
		    TP_ARGS(qpn, idx, wqe, len, id, type),

		    TP_STRUCT__entry(__field(unsigned long, qpn)
				     __field(u32, idx)
				     __array(u32, wqe,
					     MAX_WQE_SIZE / sizeof(__le32))
				     __field(u32, len)
				     __field(u64, id)
				     __field(enum hns_roce_trace_type, type)
				     ),

		    TP_fast_assign(__entry->qpn = qpn;
				   __entry->idx = idx;
				   __entry->id = id;
				   __entry->len = len / sizeof(__le32);
				   __entry->type = type;
				   for (int i = 0; i < __entry->len; i++)
					__entry->wqe[i] = le32_to_cpu(((__le32 *)wqe)[i]);
				   ),

		    TP_printk("%s 0x%lx wqe(0x%x/0x%llx): %s",
			      trace_type_to_str(__entry->type),
			      __entry->qpn, __entry->idx, __entry->id,
			      __print_array(__entry->wqe, __entry->len,
					    sizeof(__le32)))
);

DEFINE_EVENT(wqe_template, hns_sq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));
DEFINE_EVENT(wqe_template, hns_rq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));
DEFINE_EVENT(wqe_template, hns_srq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));

TRACE_EVENT(hns_ae_info,
	    TP_PROTO(int event_type, void *aeqe, unsigned int len),
	    TP_ARGS(event_type, aeqe, len),

	    TP_STRUCT__entry(__field(int, event_type)
			     __array(u32, aeqe,
				     HNS_ROCE_V3_EQE_SIZE / sizeof(__le32))
			     __field(u32, len)
	    ),

	    TP_fast_assign(__entry->event_type = event_type;
			   __entry->len = len / sizeof(__le32);
			   for (int i = 0; i < __entry->len; i++)
				__entry->aeqe[i] = le32_to_cpu(((__le32 *)aeqe)[i]);
	    ),

	    TP_printk("event %2d aeqe: %s", __entry->event_type,
		      __print_array(__entry->aeqe, __entry->len, sizeof(__le32)))
);

TRACE_EVENT(hns_mr,
	    TP_PROTO(struct hns_roce_mr *mr),
	    TP_ARGS(mr),

	    TP_STRUCT__entry(__field(u64, iova)
			     __field(u64, size)
			     __field(u32, key)
			     __field(u32, pd)
			     __field(u32, pbl_hop_num)
			     __field(u32, npages)
			     __field(int, type)
			     __field(int, enabled)
	    ),

	    TP_fast_assign(__entry->iova = mr->iova;
			   __entry->size = mr->size;
			   __entry->key = mr->key;
			   __entry->pd = mr->pd;
			   __entry->pbl_hop_num = mr->pbl_hop_num;
			   __entry->npages = mr->npages;
			   __entry->type = mr->type;
			   __entry->enabled = mr->enabled;
	    ),

	    TP_printk("iova:0x%llx, size:%llu, key:%u, pd:%u, pbl_hop:%u, npages:%u, type:%d, status:%d",
		      __entry->iova, __entry->size, __entry->key,
		      __entry->pd, __entry->pbl_hop_num, __entry->npages,
		      __entry->type, __entry->enabled)
);

TRACE_EVENT(hns_buf_attr,
	    TP_PROTO(struct hns_roce_buf_attr *attr),
	    TP_ARGS(attr),

	    TP_STRUCT__entry(__field(unsigned int, region_count)
			     __field(unsigned int, region0_size)
			     __field(int, region0_hopnum)
			     __field(unsigned int, region1_size)
			     __field(int, region1_hopnum)
			     __field(unsigned int, region2_size)
			     __field(int, region2_hopnum)
			     __field(unsigned int, page_shift)
			     __field(bool, mtt_only)
	    ),

	    TP_fast_assign(__entry->region_count = attr->region_count;
			   __entry->region0_size = attr->region[0].size;
			   __entry->region0_hopnum = attr->region[0].hopnum;
			   __entry->region1_size = attr->region[1].size;
			   __entry->region1_hopnum = attr->region[1].hopnum;
			   __entry->region2_size = attr->region[2].size;
			   __entry->region2_hopnum = attr->region[2].hopnum;
			   __entry->page_shift = attr->page_shift;
			   __entry->mtt_only = attr->mtt_only;
	    ),

	    TP_printk("rg cnt:%u, pg_sft:0x%x, mtt_only:%s, rg 0 (sz:%u, hop:%u), rg 1 (sz:%u, hop:%u), rg 2 (sz:%u, hop:%u)\n",
		      __entry->region_count, __entry->page_shift,
		      str_yes_no(__entry->mtt_only),
		      __entry->region0_size, __entry->region0_hopnum,
		      __entry->region1_size, __entry->region1_hopnum,
		      __entry->region2_size, __entry->region2_hopnum)
);

DECLARE_EVENT_CLASS(cmdq,
		    TP_PROTO(struct hns_roce_dev *hr_dev,
			     struct hns_roce_cmq_desc *desc),
		    TP_ARGS(hr_dev, desc),

		    TP_STRUCT__entry(__string(dev_name, dev_name(hr_dev->dev))
				     __field(u16, opcode)
				     __field(u16, flag)
				     __field(u16, retval)
				     __array(u32, data, 6)
		    ),

		    TP_fast_assign(__assign_str(dev_name);
				   __entry->opcode = le16_to_cpu(desc->opcode);
				   __entry->flag = le16_to_cpu(desc->flag);
				   __entry->retval = le16_to_cpu(desc->retval);
				   for (int i = 0; i < 6; i++)
					__entry->data[i] = le32_to_cpu(desc->data[i]);
		    ),

		    TP_printk("%s cmdq opcode:0x%x, flag:0x%x, retval:0x%x, data:%s\n",
			      __get_str(dev_name), __entry->opcode,
			      __entry->flag, __entry->retval,
			      __print_array(__entry->data, 6, sizeof(__le32)))
);

DEFINE_EVENT(cmdq, hns_cmdq_req,
	     TP_PROTO(struct hns_roce_dev *hr_dev,
		      struct hns_roce_cmq_desc *desc),
	     TP_ARGS(hr_dev, desc));
DEFINE_EVENT(cmdq, hns_cmdq_resp,
	     TP_PROTO(struct hns_roce_dev *hr_dev,
		      struct hns_roce_cmq_desc *desc),
	     TP_ARGS(hr_dev, desc));

#endif /* __HNS_ROCE_TRACE_H */

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hns_roce_trace
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel IOMMU trace support
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_iommu

#if !defined(_TRACE_INTEL_IOMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTEL_IOMMU_H

#include <linux/tracepoint.h>

#include "iommu.h"

#define MSG_MAX		256

TRACE_EVENT(qi_submit,
	TP_PROTO(struct intel_iommu *iommu, u64 qw0, u64 qw1, u64 qw2, u64 qw3),

	TP_ARGS(iommu, qw0, qw1, qw2, qw3),

	TP_STRUCT__entry(
		__field(u64, qw0)
		__field(u64, qw1)
		__field(u64, qw2)
		__field(u64, qw3)
		__string(iommu, iommu->name)
	),

	TP_fast_assign(
		__assign_str(iommu);
		__entry->qw0 = qw0;
		__entry->qw1 = qw1;
		__entry->qw2 = qw2;
		__entry->qw3 = qw3;
	),

	TP_printk("%s %s: 0x%llx 0x%llx 0x%llx 0x%llx",
		  __print_symbolic(__entry->qw0 & 0xf,
				   { QI_CC_TYPE,	"cc_inv" },
				   { QI_IOTLB_TYPE,	"iotlb_inv" },
				   { QI_DIOTLB_TYPE,	"dev_tlb_inv" },
				   { QI_IEC_TYPE,	"iec_inv" },
				   { QI_IWD_TYPE,	"inv_wait" },
				   { QI_EIOTLB_TYPE,	"p_iotlb_inv" },
				   { QI_PC_TYPE,	"pc_inv" },
				   { QI_DEIOTLB_TYPE,	"p_dev_tlb_inv" },
				   { QI_PGRP_RESP_TYPE,	"page_grp_resp" }),
		__get_str(iommu),
		__entry->qw0, __entry->qw1, __entry->qw2, __entry->qw3
	)
);

TRACE_EVENT(prq_report,
	TP_PROTO(struct intel_iommu *iommu, struct device *dev,
		 u64 dw0, u64 dw1, u64 dw2, u64 dw3,
		 unsigned long seq),

	TP_ARGS(iommu, dev, dw0, dw1, dw2, dw3, seq),

	TP_STRUCT__entry(
		__field(u64, dw0)
		__field(u64, dw1)
		__field(u64, dw2)
		__field(u64, dw3)
		__field(unsigned long, seq)
		__string(iommu, iommu->name)
		__string(dev, dev_name(dev))
		__dynamic_array(char, buff, MSG_MAX)
	),

	TP_fast_assign(
		__entry->dw0 = dw0;
		__entry->dw1 = dw1;
		__entry->dw2 = dw2;
		__entry->dw3 = dw3;
		__entry->seq = seq;
		__assign_str(iommu);
		__assign_str(dev);
	),

	TP_printk("%s/%s seq# %ld: %s",
		__get_str(iommu), __get_str(dev), __entry->seq,
		decode_prq_descriptor(__get_str(buff), MSG_MAX, __entry->dw0,
				      __entry->dw1, __entry->dw2, __entry->dw3)
	)
);

DECLARE_EVENT_CLASS(cache_tag_log,
	TP_PROTO(struct cache_tag *tag),
	TP_ARGS(tag),
	TP_STRUCT__entry(
		__string(iommu, tag->iommu->name)
		__string(dev, dev_name(tag->dev))
		__field(u16, type)
		__field(u16, domain_id)
		__field(u32, pasid)
		__field(u32, users)
	),
	TP_fast_assign(
		__assign_str(iommu);
		__assign_str(dev);
		__entry->type = tag->type;
		__entry->domain_id = tag->domain_id;
		__entry->pasid = tag->pasid;
		__entry->users = tag->users;
	),
	TP_printk("%s/%s type %s did %d pasid %d ref %d",
		  __get_str(iommu), __get_str(dev),
		  __print_symbolic(__entry->type,
			{ CACHE_TAG_IOTLB,		"iotlb" },
			{ CACHE_TAG_DEVTLB,		"devtlb" },
			{ CACHE_TAG_NESTING_IOTLB,	"nesting_iotlb" },
			{ CACHE_TAG_NESTING_DEVTLB,	"nesting_devtlb" }),
		__entry->domain_id, __entry->pasid, __entry->users
	)
);

DEFINE_EVENT(cache_tag_log, cache_tag_assign,
	TP_PROTO(struct cache_tag *tag),
	TP_ARGS(tag)
);

DEFINE_EVENT(cache_tag_log, cache_tag_unassign,
	TP_PROTO(struct cache_tag *tag),
	TP_ARGS(tag)
);

DEFINE_EVENT(cache_tag_log, cache_tag_flush_all,
	TP_PROTO(struct cache_tag *tag),
	TP_ARGS(tag)
);

DECLARE_EVENT_CLASS(cache_tag_flush,
	TP_PROTO(struct cache_tag *tag, unsigned long start, unsigned long end,
		 unsigned long addr, unsigned long pages, unsigned long mask),
	TP_ARGS(tag, start, end, addr, pages, mask),
	TP_STRUCT__entry(
		__string(iommu, tag->iommu->name)
		__string(dev, dev_name(tag->dev))
		__field(u16, type)
		__field(u16, domain_id)
		__field(u32, pasid)
		__field(unsigned long, start)
		__field(unsigned long, end)
		__field(unsigned long, addr)
		__field(unsigned long, pages)
		__field(unsigned long, mask)
	),
	TP_fast_assign(
		__assign_str(iommu);
		__assign_str(dev);
		__entry->type = tag->type;
		__entry->domain_id = tag->domain_id;
		__entry->pasid = tag->pasid;
		__entry->start = start;
		__entry->end = end;
		__entry->addr = addr;
		__entry->pages = pages;
		__entry->mask = mask;
	),
	TP_printk("%s %s[%d] type %s did %d [0x%lx-0x%lx] addr 0x%lx pages 0x%lx mask 0x%lx",
		  __get_str(iommu), __get_str(dev), __entry->pasid,
		  __print_symbolic(__entry->type,
			{ CACHE_TAG_IOTLB,		"iotlb" },
			{ CACHE_TAG_DEVTLB,		"devtlb" },
			{ CACHE_TAG_NESTING_IOTLB,	"nesting_iotlb" },
			{ CACHE_TAG_NESTING_DEVTLB,	"nesting_devtlb" }),
		__entry->domain_id, __entry->start, __entry->end,
		__entry->addr, __entry->pages, __entry->mask
	)
);

DEFINE_EVENT(cache_tag_flush, cache_tag_flush_range,
	TP_PROTO(struct cache_tag *tag, unsigned long start, unsigned long end,
		 unsigned long addr, unsigned long pages, unsigned long mask),
	TP_ARGS(tag, start, end, addr, pages, mask)
);

DEFINE_EVENT(cache_tag_flush, cache_tag_flush_range_np,
	TP_PROTO(struct cache_tag *tag, unsigned long start, unsigned long end,
		 unsigned long addr, unsigned long pages, unsigned long mask),
	TP_ARGS(tag, start, end, addr, pages, mask)
);
#endif /* _TRACE_INTEL_IOMMU_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/iommu/intel/
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

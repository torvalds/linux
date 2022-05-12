/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM arm_smmu

#if !defined(_TRACE_ARM_SMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ARM_SMMU_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/scatterlist.h>
#include "arm-smmu.h"

struct device;

DECLARE_EVENT_CLASS(iommu_tlbi,

	TP_PROTO(struct arm_smmu_domain *domain),

	TP_ARGS(domain),

	TP_STRUCT__entry(
		__string(group_name, dev_name(domain->dev))
	),

	TP_fast_assign(
		__assign_str(group_name, dev_name(domain->dev));
	),

	TP_printk("group=%s",
		__get_str(group_name)
	)
);

DEFINE_EVENT(iommu_tlbi, tlbi_start,

	TP_PROTO(struct arm_smmu_domain *domain),

	TP_ARGS(domain)
);

DEFINE_EVENT(iommu_tlbi, tlbi_end,

	TP_PROTO(struct arm_smmu_domain *domain),

	TP_ARGS(domain)
);

DECLARE_EVENT_CLASS(iommu_pgtable,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		unsigned long long ipa, size_t granule),

	TP_ARGS(domain, iova, ipa, granule),

	TP_STRUCT__entry(
		__string(group_name, dev_name(domain->dev))
		__field(unsigned long, iova)
		__field(unsigned long long, ipa)
		__field(size_t, granule)
	),

	TP_fast_assign(
		__assign_str(group_name, dev_name(domain->dev));
		__entry->iova = iova;
		__entry->ipa = ipa;
		__entry->granule = granule;
	),

	TP_printk("group=%s table_base_iova=%lx table_ipa=%llx table_size=%zx",
		__get_str(group_name), __entry->iova,
		__entry->ipa, __entry->granule
	)
);

DEFINE_EVENT(iommu_pgtable, iommu_pgtable_add,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		unsigned long long ipa, size_t granule),

	TP_ARGS(domain, iova, ipa, granule)
);

DEFINE_EVENT(iommu_pgtable, iommu_pgtable_remove,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		unsigned long long ipa, size_t granule),

	TP_ARGS(domain, iova, ipa, granule)
);

DECLARE_EVENT_CLASS(iommu_map_pages,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		size_t pgsize, size_t pgcount),

	TP_ARGS(domain, iova, pgsize, pgcount),

	TP_STRUCT__entry(
		__string(group_name, dev_name(domain->dev))
		__field(unsigned long, iova)
		__field(size_t, pgsize)
		__field(size_t, pgcount)
	),

	TP_fast_assign(
		__assign_str(group_name, dev_name(domain->dev));
		__entry->iova = iova;
		__entry->pgsize = pgsize;
		__entry->pgcount = pgcount;
	),

	TP_printk("group=%s iova=%lx size=%zx pgsize=%zx pgcount=%zx",
		__get_str(group_name), __entry->iova,
		__entry->pgsize * __entry->pgcount,
		__entry->pgsize, __entry->pgcount
	)
);

DEFINE_EVENT(iommu_map_pages, map_pages,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		size_t pgsize, size_t pgcount),

	TP_ARGS(domain, iova, pgsize, pgcount)
);

DEFINE_EVENT(iommu_map_pages, unmap_pages,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		size_t pgsize, size_t pgcount),

	TP_ARGS(domain, iova, pgsize, pgcount)
);

/* Refer to samples/ftrace_events */
#ifndef __TRACE_EVENT_ARM_SMMU_HELPER_FUNCTIONS
#define __TRACE_EVENT_ARM_SMMU_HELPER_FUNCTIONS
static inline unsigned long sum_scatterlist_length(struct scatterlist *sgl,
						unsigned int nents)
{
	int i = 0;
	unsigned long sum = 0;

	for (i = 0; i < nents; i++, sgl = sg_next(sgl))
		sum += sgl->length;

	return sum;
}
#endif

TRACE_EVENT(map_sg,

	TP_PROTO(struct arm_smmu_domain *domain, unsigned long iova,
		struct scatterlist *sgl, unsigned int nents),

	TP_ARGS(domain, iova, sgl, nents),

	TP_STRUCT__entry(
		__string(group_name, dev_name(domain->dev))
		__field(unsigned long, iova)
		__field(unsigned long, size)
	),

	TP_fast_assign(
		__assign_str(group_name, dev_name(domain->dev));
		__entry->iova = iova;
		__entry->size = sum_scatterlist_length(sgl, nents);
	),

	TP_printk("group=%s iova=%lx size=%lx",
		__get_str(group_name), __entry->iova,
		__entry->size
	)
);

TRACE_EVENT(tlbsync_timeout,

	TP_PROTO(struct device *dev),

	TP_ARGS(dev),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
	),

	TP_fast_assign(
		__assign_str(device, dev_name(dev));
	),

	TP_printk("smmu=%s",
		__get_str(device)
	)
);

TRACE_EVENT(smmu_init,

	TP_PROTO(u64 time),

	TP_ARGS(time),

	TP_STRUCT__entry(
		__field(u64, time)
	),

	TP_fast_assign(
		__entry->time = time;
	),

	TP_printk("ARM SMMU init latency: %lld us", __entry->time)
);
#endif /* _TRACE_ARM_SMMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/iommu/arm/arm-smmu

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE arm-smmu-trace

/* This part must be outside protection */
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * perf.h - performance monitor header
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

enum latency_type {
	DMAR_LATENCY_INV_IOTLB = 0,
	DMAR_LATENCY_INV_DEVTLB,
	DMAR_LATENCY_INV_IEC,
	DMAR_LATENCY_NUM
};

enum latency_count {
	COUNTS_10e2 = 0,	/* < 0.1us	*/
	COUNTS_10e3,		/* 0.1us ~ 1us	*/
	COUNTS_10e4,		/* 1us ~ 10us	*/
	COUNTS_10e5,		/* 10us ~ 100us	*/
	COUNTS_10e6,		/* 100us ~ 1ms	*/
	COUNTS_10e7,		/* 1ms ~ 10ms	*/
	COUNTS_10e8_plus,	/* 10ms and plus*/
	COUNTS_MIN,
	COUNTS_MAX,
	COUNTS_SUM,
	COUNTS_NUM
};

struct latency_statistic {
	bool enabled;
	u64 counter[COUNTS_NUM];
	u64 samples;
};

#ifdef CONFIG_DMAR_PERF
int dmar_latency_enable(struct intel_iommu *iommu, enum latency_type type);
void dmar_latency_disable(struct intel_iommu *iommu, enum latency_type type);
bool dmar_latency_enabled(struct intel_iommu *iommu, enum latency_type type);
void dmar_latency_update(struct intel_iommu *iommu, enum latency_type type,
			 u64 latency);
void dmar_latency_snapshot(struct intel_iommu *iommu, char *str, size_t size);
#else
static inline int
dmar_latency_enable(struct intel_iommu *iommu, enum latency_type type)
{
	return -EINVAL;
}

static inline void
dmar_latency_disable(struct intel_iommu *iommu, enum latency_type type)
{
}

static inline bool
dmar_latency_enabled(struct intel_iommu *iommu, enum latency_type type)
{
	return false;
}

static inline void
dmar_latency_update(struct intel_iommu *iommu, enum latency_type type, u64 latency)
{
}

static inline void
dmar_latency_snapshot(struct intel_iommu *iommu, char *str, size_t size)
{
}
#endif /* CONFIG_DMAR_PERF */

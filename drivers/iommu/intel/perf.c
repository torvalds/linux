// SPDX-License-Identifier: GPL-2.0
/*
 * perf.c - performance monitor
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *         Fenghua Yu <fenghua.yu@intel.com>
 */

#include <linux/spinlock.h>

#include "iommu.h"
#include "perf.h"

static DEFINE_SPINLOCK(latency_lock);

bool dmar_latency_enabled(struct intel_iommu *iommu, enum latency_type type)
{
	struct latency_statistic *lstat = iommu->perf_statistic;

	return lstat && lstat[type].enabled;
}

int dmar_latency_enable(struct intel_iommu *iommu, enum latency_type type)
{
	struct latency_statistic *lstat;
	unsigned long flags;
	int ret = -EBUSY;

	if (dmar_latency_enabled(iommu, type))
		return 0;

	spin_lock_irqsave(&latency_lock, flags);
	if (!iommu->perf_statistic) {
		iommu->perf_statistic = kzalloc(sizeof(*lstat) * DMAR_LATENCY_NUM,
						GFP_ATOMIC);
		if (!iommu->perf_statistic) {
			ret = -ENOMEM;
			goto unlock_out;
		}
	}

	lstat = iommu->perf_statistic;

	if (!lstat[type].enabled) {
		lstat[type].enabled = true;
		lstat[type].counter[COUNTS_MIN] = UINT_MAX;
		ret = 0;
	}
unlock_out:
	spin_unlock_irqrestore(&latency_lock, flags);

	return ret;
}

void dmar_latency_disable(struct intel_iommu *iommu, enum latency_type type)
{
	struct latency_statistic *lstat = iommu->perf_statistic;
	unsigned long flags;

	if (!dmar_latency_enabled(iommu, type))
		return;

	spin_lock_irqsave(&latency_lock, flags);
	memset(&lstat[type], 0, sizeof(*lstat) * DMAR_LATENCY_NUM);
	spin_unlock_irqrestore(&latency_lock, flags);
}

void dmar_latency_update(struct intel_iommu *iommu, enum latency_type type, u64 latency)
{
	struct latency_statistic *lstat = iommu->perf_statistic;
	unsigned long flags;
	u64 min, max;

	if (!dmar_latency_enabled(iommu, type))
		return;

	spin_lock_irqsave(&latency_lock, flags);
	if (latency < 100)
		lstat[type].counter[COUNTS_10e2]++;
	else if (latency < 1000)
		lstat[type].counter[COUNTS_10e3]++;
	else if (latency < 10000)
		lstat[type].counter[COUNTS_10e4]++;
	else if (latency < 100000)
		lstat[type].counter[COUNTS_10e5]++;
	else if (latency < 1000000)
		lstat[type].counter[COUNTS_10e6]++;
	else if (latency < 10000000)
		lstat[type].counter[COUNTS_10e7]++;
	else
		lstat[type].counter[COUNTS_10e8_plus]++;

	min = lstat[type].counter[COUNTS_MIN];
	max = lstat[type].counter[COUNTS_MAX];
	lstat[type].counter[COUNTS_MIN] = min_t(u64, min, latency);
	lstat[type].counter[COUNTS_MAX] = max_t(u64, max, latency);
	lstat[type].counter[COUNTS_SUM] += latency;
	lstat[type].samples++;
	spin_unlock_irqrestore(&latency_lock, flags);
}

static char *latency_counter_names[] = {
	"                  <0.1us",
	"   0.1us-1us", "    1us-10us", "  10us-100us",
	"   100us-1ms", "    1ms-10ms", "      >=10ms",
	"     min(us)", "     max(us)", " average(us)"
};

static char *latency_type_names[] = {
	"   inv_iotlb", "  inv_devtlb", "     inv_iec",
	"     svm_prq"
};

int dmar_latency_snapshot(struct intel_iommu *iommu, char *str, size_t size)
{
	struct latency_statistic *lstat = iommu->perf_statistic;
	unsigned long flags;
	int bytes = 0, i, j;

	memset(str, 0, size);

	for (i = 0; i < COUNTS_NUM; i++)
		bytes += snprintf(str + bytes, size - bytes,
				  "%s", latency_counter_names[i]);

	spin_lock_irqsave(&latency_lock, flags);
	for (i = 0; i < DMAR_LATENCY_NUM; i++) {
		if (!dmar_latency_enabled(iommu, i))
			continue;

		bytes += snprintf(str + bytes, size - bytes,
				  "\n%s", latency_type_names[i]);

		for (j = 0; j < COUNTS_NUM; j++) {
			u64 val = lstat[i].counter[j];

			switch (j) {
			case COUNTS_MIN:
				if (val == UINT_MAX)
					val = 0;
				else
					val = div_u64(val, 1000);
				break;
			case COUNTS_MAX:
				val = div_u64(val, 1000);
				break;
			case COUNTS_SUM:
				if (lstat[i].samples)
					val = div_u64(val, (lstat[i].samples * 1000));
				else
					val = 0;
				break;
			default:
				break;
			}

			bytes += snprintf(str + bytes, size - bytes,
					  "%12lld", val);
		}
	}
	spin_unlock_irqrestore(&latency_lock, flags);

	return bytes;
}

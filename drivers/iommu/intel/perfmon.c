// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support Intel IOMMU PerfMon
 * Copyright(c) 2023 Intel Corporation.
 */
#define pr_fmt(fmt)	"DMAR: " fmt
#define dev_fmt(fmt)	pr_fmt(fmt)

#include <linux/dmar.h>
#include "iommu.h"
#include "perfmon.h"

static inline void __iomem *
get_perf_reg_address(struct intel_iommu *iommu, u32 offset)
{
	u32 off = dmar_readl(iommu->reg + offset);

	return iommu->reg + off;
}

int alloc_iommu_pmu(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu;
	int i, j, ret;
	u64 perfcap;
	u32 cap;

	if (!ecap_pms(iommu->ecap))
		return 0;

	/* The IOMMU PMU requires the ECMD support as well */
	if (!cap_ecmds(iommu->cap))
		return -ENODEV;

	perfcap = dmar_readq(iommu->reg + DMAR_PERFCAP_REG);
	/* The performance monitoring is not supported. */
	if (!perfcap)
		return -ENODEV;

	/* Sanity check for the number of the counters and event groups */
	if (!pcap_num_cntr(perfcap) || !pcap_num_event_group(perfcap))
		return -ENODEV;

	/* The interrupt on overflow is required */
	if (!pcap_interrupt(perfcap))
		return -ENODEV;

	iommu_pmu = kzalloc(sizeof(*iommu_pmu), GFP_KERNEL);
	if (!iommu_pmu)
		return -ENOMEM;

	iommu_pmu->num_cntr = pcap_num_cntr(perfcap);
	iommu_pmu->cntr_width = pcap_cntr_width(perfcap);
	iommu_pmu->filter = pcap_filters_mask(perfcap);
	iommu_pmu->cntr_stride = pcap_cntr_stride(perfcap);
	iommu_pmu->num_eg = pcap_num_event_group(perfcap);

	iommu_pmu->evcap = kcalloc(iommu_pmu->num_eg, sizeof(u64), GFP_KERNEL);
	if (!iommu_pmu->evcap) {
		ret = -ENOMEM;
		goto free_pmu;
	}

	/* Parse event group capabilities */
	for (i = 0; i < iommu_pmu->num_eg; i++) {
		u64 pcap;

		pcap = dmar_readq(iommu->reg + DMAR_PERFEVNTCAP_REG +
				  i * IOMMU_PMU_CAP_REGS_STEP);
		iommu_pmu->evcap[i] = pecap_es(pcap);
	}

	iommu_pmu->cntr_evcap = kcalloc(iommu_pmu->num_cntr, sizeof(u32 *), GFP_KERNEL);
	if (!iommu_pmu->cntr_evcap) {
		ret = -ENOMEM;
		goto free_pmu_evcap;
	}
	for (i = 0; i < iommu_pmu->num_cntr; i++) {
		iommu_pmu->cntr_evcap[i] = kcalloc(iommu_pmu->num_eg, sizeof(u32), GFP_KERNEL);
		if (!iommu_pmu->cntr_evcap[i]) {
			ret = -ENOMEM;
			goto free_pmu_cntr_evcap;
		}
		/*
		 * Set to the global capabilities, will adjust according
		 * to per-counter capabilities later.
		 */
		for (j = 0; j < iommu_pmu->num_eg; j++)
			iommu_pmu->cntr_evcap[i][j] = (u32)iommu_pmu->evcap[j];
	}

	iommu_pmu->cfg_reg = get_perf_reg_address(iommu, DMAR_PERFCFGOFF_REG);
	iommu_pmu->cntr_reg = get_perf_reg_address(iommu, DMAR_PERFCNTROFF_REG);
	iommu_pmu->overflow = get_perf_reg_address(iommu, DMAR_PERFOVFOFF_REG);

	/*
	 * Check per-counter capabilities. All counters should have the
	 * same capabilities on Interrupt on Overflow Support and Counter
	 * Width.
	 */
	for (i = 0; i < iommu_pmu->num_cntr; i++) {
		cap = dmar_readl(iommu_pmu->cfg_reg +
				 i * IOMMU_PMU_CFG_OFFSET +
				 IOMMU_PMU_CFG_CNTRCAP_OFFSET);
		if (!iommu_cntrcap_pcc(cap))
			continue;

		/*
		 * It's possible that some counters have a different
		 * capability because of e.g., HW bug. Check the corner
		 * case here and simply drop those counters.
		 */
		if ((iommu_cntrcap_cw(cap) != iommu_pmu->cntr_width) ||
		    !iommu_cntrcap_ios(cap)) {
			iommu_pmu->num_cntr = i;
			pr_warn("PMU counter capability inconsistent, counter number reduced to %d\n",
				iommu_pmu->num_cntr);
		}

		/* Clear the pre-defined events group */
		for (j = 0; j < iommu_pmu->num_eg; j++)
			iommu_pmu->cntr_evcap[i][j] = 0;

		/* Override with per-counter event capabilities */
		for (j = 0; j < iommu_cntrcap_egcnt(cap); j++) {
			cap = dmar_readl(iommu_pmu->cfg_reg + i * IOMMU_PMU_CFG_OFFSET +
					 IOMMU_PMU_CFG_CNTREVCAP_OFFSET +
					 (j * IOMMU_PMU_OFF_REGS_STEP));
			iommu_pmu->cntr_evcap[i][iommu_event_group(cap)] = iommu_event_select(cap);
			/*
			 * Some events may only be supported by a specific counter.
			 * Track them in the evcap as well.
			 */
			iommu_pmu->evcap[iommu_event_group(cap)] |= iommu_event_select(cap);
		}
	}

	iommu_pmu->iommu = iommu;
	iommu->pmu = iommu_pmu;

	return 0;

free_pmu_cntr_evcap:
	for (i = 0; i < iommu_pmu->num_cntr; i++)
		kfree(iommu_pmu->cntr_evcap[i]);
	kfree(iommu_pmu->cntr_evcap);
free_pmu_evcap:
	kfree(iommu_pmu->evcap);
free_pmu:
	kfree(iommu_pmu);

	return ret;
}

void free_iommu_pmu(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu = iommu->pmu;

	if (!iommu_pmu)
		return;

	if (iommu_pmu->evcap) {
		int i;

		for (i = 0; i < iommu_pmu->num_cntr; i++)
			kfree(iommu_pmu->cntr_evcap[i]);
		kfree(iommu_pmu->cntr_evcap);
	}
	kfree(iommu_pmu->evcap);
	kfree(iommu_pmu);
	iommu->pmu = NULL;
}

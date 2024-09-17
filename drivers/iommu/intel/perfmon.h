/* SPDX-License-Identifier: GPL-2.0 */

/*
 * PERFCFGOFF_REG, PERFFRZOFF_REG
 * PERFOVFOFF_REG, PERFCNTROFF_REG
 */
#define IOMMU_PMU_NUM_OFF_REGS			4
#define IOMMU_PMU_OFF_REGS_STEP			4

#define IOMMU_PMU_CFG_OFFSET			0x100
#define IOMMU_PMU_CFG_CNTRCAP_OFFSET		0x80
#define IOMMU_PMU_CFG_CNTREVCAP_OFFSET		0x84
#define IOMMU_PMU_CFG_SIZE			0x8
#define IOMMU_PMU_CFG_FILTERS_OFFSET		0x4

#define IOMMU_PMU_CAP_REGS_STEP			8

#define iommu_cntrcap_pcc(p)			((p) & 0x1)
#define iommu_cntrcap_cw(p)			(((p) >> 8) & 0xff)
#define iommu_cntrcap_ios(p)			(((p) >> 16) & 0x1)
#define iommu_cntrcap_egcnt(p)			(((p) >> 28) & 0xf)

#define iommu_event_select(p)			((p) & 0xfffffff)
#define iommu_event_group(p)			(((p) >> 28) & 0xf)

#ifdef CONFIG_INTEL_IOMMU_PERF_EVENTS
int alloc_iommu_pmu(struct intel_iommu *iommu);
void free_iommu_pmu(struct intel_iommu *iommu);
#else
static inline int
alloc_iommu_pmu(struct intel_iommu *iommu)
{
	return 0;
}

static inline void
free_iommu_pmu(struct intel_iommu *iommu)
{
}
#endif /* CONFIG_INTEL_IOMMU_PERF_EVENTS */

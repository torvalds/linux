/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cap_audit.h - audit iommu capabilities header
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Kyung Min Park <kyung.min.park@intel.com>
 */

/*
 * Capability Register Mask
 */
#define CAP_FL5LP_MASK		BIT_ULL(60)
#define CAP_PI_MASK		BIT_ULL(59)
#define CAP_FL1GP_MASK		BIT_ULL(56)
#define CAP_RD_MASK		BIT_ULL(55)
#define CAP_WD_MASK		BIT_ULL(54)
#define CAP_MAMV_MASK		GENMASK_ULL(53, 48)
#define CAP_NFR_MASK		GENMASK_ULL(47, 40)
#define CAP_PSI_MASK		BIT_ULL(39)
#define CAP_SLLPS_MASK		GENMASK_ULL(37, 34)
#define CAP_FRO_MASK		GENMASK_ULL(33, 24)
#define CAP_ZLR_MASK		BIT_ULL(22)
#define CAP_MGAW_MASK		GENMASK_ULL(21, 16)
#define CAP_SAGAW_MASK		GENMASK_ULL(12, 8)
#define CAP_CM_MASK		BIT_ULL(7)
#define CAP_PHMR_MASK		BIT_ULL(6)
#define CAP_PLMR_MASK		BIT_ULL(5)
#define CAP_RWBF_MASK		BIT_ULL(4)
#define CAP_AFL_MASK		BIT_ULL(3)
#define CAP_NDOMS_MASK		GENMASK_ULL(2, 0)

/*
 * Extended Capability Register Mask
 */
#define ECAP_RPS_MASK		BIT_ULL(49)
#define ECAP_SMPWC_MASK		BIT_ULL(48)
#define ECAP_FLTS_MASK		BIT_ULL(47)
#define ECAP_SLTS_MASK		BIT_ULL(46)
#define ECAP_SLADS_MASK		BIT_ULL(45)
#define ECAP_VCS_MASK		BIT_ULL(44)
#define ECAP_SMTS_MASK		BIT_ULL(43)
#define ECAP_PDS_MASK		BIT_ULL(42)
#define ECAP_DIT_MASK		BIT_ULL(41)
#define ECAP_PASID_MASK		BIT_ULL(40)
#define ECAP_PSS_MASK		GENMASK_ULL(39, 35)
#define ECAP_EAFS_MASK		BIT_ULL(34)
#define ECAP_NWFS_MASK		BIT_ULL(33)
#define ECAP_SRS_MASK		BIT_ULL(31)
#define ECAP_ERS_MASK		BIT_ULL(30)
#define ECAP_PRS_MASK		BIT_ULL(29)
#define ECAP_NEST_MASK		BIT_ULL(26)
#define ECAP_MTS_MASK		BIT_ULL(25)
#define ECAP_MHMV_MASK		GENMASK_ULL(23, 20)
#define ECAP_IRO_MASK		GENMASK_ULL(17, 8)
#define ECAP_SC_MASK		BIT_ULL(7)
#define ECAP_PT_MASK		BIT_ULL(6)
#define ECAP_EIM_MASK		BIT_ULL(4)
#define ECAP_DT_MASK		BIT_ULL(2)
#define ECAP_QI_MASK		BIT_ULL(1)
#define ECAP_C_MASK		BIT_ULL(0)

/*
 * u64 intel_iommu_cap_sanity, intel_iommu_ecap_sanity will be adjusted as each
 * IOMMU gets audited.
 */
#define DO_CHECK_FEATURE_MISMATCH(a, b, cap, feature, MASK) \
do { \
	if (cap##_##feature(a) != cap##_##feature(b)) { \
		intel_iommu_##cap##_sanity &= ~(MASK); \
		pr_info("IOMMU feature %s inconsistent", #feature); \
	} \
} while (0)

#define CHECK_FEATURE_MISMATCH(a, b, cap, feature, MASK) \
	DO_CHECK_FEATURE_MISMATCH((a)->cap, (b)->cap, cap, feature, MASK)

#define CHECK_FEATURE_MISMATCH_HOTPLUG(b, cap, feature, MASK) \
do { \
	if (cap##_##feature(intel_iommu_##cap##_sanity)) \
		DO_CHECK_FEATURE_MISMATCH(intel_iommu_##cap##_sanity, \
					  (b)->cap, cap, feature, MASK); \
} while (0)

#define MINIMAL_FEATURE_IOMMU(iommu, cap, MASK) \
do { \
	u64 min_feature = intel_iommu_##cap##_sanity & (MASK); \
	min_feature = min_t(u64, min_feature, (iommu)->cap & (MASK)); \
	intel_iommu_##cap##_sanity = (intel_iommu_##cap##_sanity & ~(MASK)) | \
				     min_feature; \
} while (0)

#define MINIMAL_FEATURE_HOTPLUG(iommu, cap, feature, MASK, mismatch) \
do { \
	if ((intel_iommu_##cap##_sanity & (MASK)) > \
	    (cap##_##feature((iommu)->cap))) \
		mismatch = true; \
	else \
		(iommu)->cap = ((iommu)->cap & ~(MASK)) | \
		(intel_iommu_##cap##_sanity & (MASK)); \
} while (0)

enum cap_audit_type {
	CAP_AUDIT_STATIC_DMAR,
	CAP_AUDIT_STATIC_IRQR,
	CAP_AUDIT_HOTPLUG_DMAR,
	CAP_AUDIT_HOTPLUG_IRQR,
};

bool intel_cap_smts_sanity(void);
bool intel_cap_pasid_sanity(void);
bool intel_cap_nest_sanity(void);
bool intel_cap_flts_sanity(void);
bool intel_cap_slts_sanity(void);

static inline bool scalable_mode_support(void)
{
	return (intel_iommu_sm && intel_cap_smts_sanity());
}

static inline bool pasid_mode_support(void)
{
	return scalable_mode_support() && intel_cap_pasid_sanity();
}

static inline bool nested_mode_support(void)
{
	return scalable_mode_support() && intel_cap_nest_sanity();
}

int intel_cap_audit(enum cap_audit_type type, struct intel_iommu *iommu);

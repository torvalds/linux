// SPDX-License-Identifier: GPL-2.0
/*
 * cap_audit.c - audit iommu capabilities for boot time and hot plug
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Kyung Min Park <kyung.min.park@intel.com>
 *         Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include "iommu.h"
#include "cap_audit.h"

static u64 intel_iommu_cap_sanity;
static u64 intel_iommu_ecap_sanity;

static inline void check_irq_capabilities(struct intel_iommu *a,
					  struct intel_iommu *b)
{
	CHECK_FEATURE_MISMATCH(a, b, cap, pi_support, CAP_PI_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, eim_support, ECAP_EIM_MASK);
}

static inline void check_dmar_capabilities(struct intel_iommu *a,
					   struct intel_iommu *b)
{
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_MAMV_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_NFR_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_SLLPS_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_FRO_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_MGAW_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_SAGAW_MASK);
	MINIMAL_FEATURE_IOMMU(b, cap, CAP_NDOMS_MASK);
	MINIMAL_FEATURE_IOMMU(b, ecap, ECAP_PSS_MASK);
	MINIMAL_FEATURE_IOMMU(b, ecap, ECAP_MHMV_MASK);
	MINIMAL_FEATURE_IOMMU(b, ecap, ECAP_IRO_MASK);

	CHECK_FEATURE_MISMATCH(a, b, cap, fl5lp_support, CAP_FL5LP_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, fl1gp_support, CAP_FL1GP_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, read_drain, CAP_RD_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, write_drain, CAP_WD_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, pgsel_inv, CAP_PSI_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, zlr, CAP_ZLR_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, caching_mode, CAP_CM_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, phmr, CAP_PHMR_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, plmr, CAP_PLMR_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, rwbf, CAP_RWBF_MASK);
	CHECK_FEATURE_MISMATCH(a, b, cap, afl, CAP_AFL_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, rps, ECAP_RPS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, smpwc, ECAP_SMPWC_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, flts, ECAP_FLTS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, slts, ECAP_SLTS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, nwfs, ECAP_NWFS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, slads, ECAP_SLADS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, vcs, ECAP_VCS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, smts, ECAP_SMTS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, pds, ECAP_PDS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, dit, ECAP_DIT_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, pasid, ECAP_PASID_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, eafs, ECAP_EAFS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, srs, ECAP_SRS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, ers, ECAP_ERS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, prs, ECAP_PRS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, nest, ECAP_NEST_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, mts, ECAP_MTS_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, sc_support, ECAP_SC_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, pass_through, ECAP_PT_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, dev_iotlb_support, ECAP_DT_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, qis, ECAP_QI_MASK);
	CHECK_FEATURE_MISMATCH(a, b, ecap, coherent, ECAP_C_MASK);
}

static int cap_audit_hotplug(struct intel_iommu *iommu, enum cap_audit_type type)
{
	bool mismatch = false;
	u64 old_cap = intel_iommu_cap_sanity;
	u64 old_ecap = intel_iommu_ecap_sanity;

	if (type == CAP_AUDIT_HOTPLUG_IRQR) {
		CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, pi_support, CAP_PI_MASK);
		CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, eim_support, ECAP_EIM_MASK);
		goto out;
	}

	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, fl5lp_support, CAP_FL5LP_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, fl1gp_support, CAP_FL1GP_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, read_drain, CAP_RD_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, write_drain, CAP_WD_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, pgsel_inv, CAP_PSI_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, zlr, CAP_ZLR_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, caching_mode, CAP_CM_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, phmr, CAP_PHMR_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, plmr, CAP_PLMR_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, rwbf, CAP_RWBF_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, cap, afl, CAP_AFL_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, rps, ECAP_RPS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, smpwc, ECAP_SMPWC_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, flts, ECAP_FLTS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, slts, ECAP_SLTS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, nwfs, ECAP_NWFS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, slads, ECAP_SLADS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, vcs, ECAP_VCS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, smts, ECAP_SMTS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, pds, ECAP_PDS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, dit, ECAP_DIT_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, pasid, ECAP_PASID_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, eafs, ECAP_EAFS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, srs, ECAP_SRS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, ers, ECAP_ERS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, prs, ECAP_PRS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, nest, ECAP_NEST_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, mts, ECAP_MTS_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, sc_support, ECAP_SC_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, pass_through, ECAP_PT_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, dev_iotlb_support, ECAP_DT_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, qis, ECAP_QI_MASK);
	CHECK_FEATURE_MISMATCH_HOTPLUG(iommu, ecap, coherent, ECAP_C_MASK);

	/* Abort hot plug if the hot plug iommu feature is smaller than global */
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, max_amask_val, CAP_MAMV_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, num_fault_regs, CAP_NFR_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, super_page_val, CAP_SLLPS_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, fault_reg_offset, CAP_FRO_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, mgaw, CAP_MGAW_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, sagaw, CAP_SAGAW_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, cap, ndoms, CAP_NDOMS_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, ecap, pss, ECAP_PSS_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, ecap, max_handle_mask, ECAP_MHMV_MASK, mismatch);
	MINIMAL_FEATURE_HOTPLUG(iommu, ecap, iotlb_offset, ECAP_IRO_MASK, mismatch);

out:
	if (mismatch) {
		intel_iommu_cap_sanity = old_cap;
		intel_iommu_ecap_sanity = old_ecap;
		return -EFAULT;
	}

	return 0;
}

static int cap_audit_static(struct intel_iommu *iommu, enum cap_audit_type type)
{
	struct dmar_drhd_unit *d;
	struct intel_iommu *i;
	int rc = 0;

	rcu_read_lock();
	if (list_empty(&dmar_drhd_units))
		goto out;

	for_each_active_iommu(i, d) {
		if (!iommu) {
			intel_iommu_ecap_sanity = i->ecap;
			intel_iommu_cap_sanity = i->cap;
			iommu = i;
			continue;
		}

		if (type == CAP_AUDIT_STATIC_DMAR)
			check_dmar_capabilities(iommu, i);
		else
			check_irq_capabilities(iommu, i);
	}

	/*
	 * If the system is sane to support scalable mode, either SL or FL
	 * should be sane.
	 */
	if (intel_cap_smts_sanity() &&
	    !intel_cap_flts_sanity() && !intel_cap_slts_sanity())
		rc = -EOPNOTSUPP;

out:
	rcu_read_unlock();
	return rc;
}

int intel_cap_audit(enum cap_audit_type type, struct intel_iommu *iommu)
{
	switch (type) {
	case CAP_AUDIT_STATIC_DMAR:
	case CAP_AUDIT_STATIC_IRQR:
		return cap_audit_static(iommu, type);
	case CAP_AUDIT_HOTPLUG_DMAR:
	case CAP_AUDIT_HOTPLUG_IRQR:
		return cap_audit_hotplug(iommu, type);
	default:
		break;
	}

	return -EFAULT;
}

bool intel_cap_smts_sanity(void)
{
	return ecap_smts(intel_iommu_ecap_sanity);
}

bool intel_cap_pasid_sanity(void)
{
	return ecap_pasid(intel_iommu_ecap_sanity);
}

bool intel_cap_nest_sanity(void)
{
	return ecap_nest(intel_iommu_ecap_sanity);
}

bool intel_cap_flts_sanity(void)
{
	return ecap_flts(intel_iommu_ecap_sanity);
}

bool intel_cap_slts_sanity(void)
{
	return ecap_slts(intel_iommu_ecap_sanity);
}

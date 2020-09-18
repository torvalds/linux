// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the IOMMU SVA API for the ARM SMMUv3
 */

#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/slab.h>

#include "arm-smmu-v3.h"
#include "../../io-pgtable-arm.h"

static struct arm_smmu_ctx_desc *
arm_smmu_share_asid(struct mm_struct *mm, u16 asid)
{
	struct arm_smmu_ctx_desc *cd;

	cd = xa_load(&arm_smmu_asid_xa, asid);
	if (!cd)
		return NULL;

	if (cd->mm) {
		if (WARN_ON(cd->mm != mm))
			return ERR_PTR(-EINVAL);
		/* All devices bound to this mm use the same cd struct. */
		refcount_inc(&cd->refs);
		return cd;
	}

	/* Ouch, ASID is already in use for a private cd. */
	return ERR_PTR(-EBUSY);
}

__maybe_unused
static struct arm_smmu_ctx_desc *arm_smmu_alloc_shared_cd(struct mm_struct *mm)
{
	u16 asid;
	int err = 0;
	u64 tcr, par, reg;
	struct arm_smmu_ctx_desc *cd;
	struct arm_smmu_ctx_desc *ret = NULL;

	asid = arm64_mm_context_get(mm);
	if (!asid)
		return ERR_PTR(-ESRCH);

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd) {
		err = -ENOMEM;
		goto out_put_context;
	}

	refcount_set(&cd->refs, 1);

	mutex_lock(&arm_smmu_asid_lock);
	ret = arm_smmu_share_asid(mm, asid);
	if (ret) {
		mutex_unlock(&arm_smmu_asid_lock);
		goto out_free_cd;
	}

	err = xa_insert(&arm_smmu_asid_xa, asid, cd, GFP_KERNEL);
	mutex_unlock(&arm_smmu_asid_lock);

	if (err)
		goto out_free_asid;

	tcr = FIELD_PREP(CTXDESC_CD_0_TCR_T0SZ, 64ULL - vabits_actual) |
	      FIELD_PREP(CTXDESC_CD_0_TCR_IRGN0, ARM_LPAE_TCR_RGN_WBWA) |
	      FIELD_PREP(CTXDESC_CD_0_TCR_ORGN0, ARM_LPAE_TCR_RGN_WBWA) |
	      FIELD_PREP(CTXDESC_CD_0_TCR_SH0, ARM_LPAE_TCR_SH_IS) |
	      CTXDESC_CD_0_TCR_EPD1 | CTXDESC_CD_0_AA64;

	switch (PAGE_SIZE) {
	case SZ_4K:
		tcr |= FIELD_PREP(CTXDESC_CD_0_TCR_TG0, ARM_LPAE_TCR_TG0_4K);
		break;
	case SZ_16K:
		tcr |= FIELD_PREP(CTXDESC_CD_0_TCR_TG0, ARM_LPAE_TCR_TG0_16K);
		break;
	case SZ_64K:
		tcr |= FIELD_PREP(CTXDESC_CD_0_TCR_TG0, ARM_LPAE_TCR_TG0_64K);
		break;
	default:
		WARN_ON(1);
		err = -EINVAL;
		goto out_free_asid;
	}

	reg = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1);
	par = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_PARANGE_SHIFT);
	tcr |= FIELD_PREP(CTXDESC_CD_0_TCR_IPS, par);

	cd->ttbr = virt_to_phys(mm->pgd);
	cd->tcr = tcr;
	/*
	 * MAIR value is pretty much constant and global, so we can just get it
	 * from the current CPU register
	 */
	cd->mair = read_sysreg(mair_el1);
	cd->asid = asid;
	cd->mm = mm;

	return cd;

out_free_asid:
	arm_smmu_free_asid(cd);
out_free_cd:
	kfree(cd);
out_put_context:
	arm64_mm_context_put(mm);
	return err < 0 ? ERR_PTR(err) : ret;
}

__maybe_unused
static void arm_smmu_free_shared_cd(struct arm_smmu_ctx_desc *cd)
{
	if (arm_smmu_free_asid(cd)) {
		/* Unpin ASID */
		arm64_mm_context_put(cd->mm);
		kfree(cd);
	}
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the IOMMU SVA API for the ARM SMMUv3
 */

#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/slab.h>

#include "arm-smmu-v3.h"
#include "../../io-pgtable-arm.h"

/*
 * Check if the CPU ASID is available on the SMMU side. If a private context
 * descriptor is using it, try to replace it.
 */
static struct arm_smmu_ctx_desc *
arm_smmu_share_asid(struct mm_struct *mm, u16 asid)
{
	int ret;
	u32 new_asid;
	struct arm_smmu_ctx_desc *cd;
	struct arm_smmu_device *smmu;
	struct arm_smmu_domain *smmu_domain;

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

	smmu_domain = container_of(cd, struct arm_smmu_domain, s1_cfg.cd);
	smmu = smmu_domain->smmu;

	ret = xa_alloc(&arm_smmu_asid_xa, &new_asid, cd,
		       XA_LIMIT(1, (1 << smmu->asid_bits) - 1), GFP_KERNEL);
	if (ret)
		return ERR_PTR(-ENOSPC);
	/*
	 * Race with unmap: TLB invalidations will start targeting the new ASID,
	 * which isn't assigned yet. We'll do an invalidate-all on the old ASID
	 * later, so it doesn't matter.
	 */
	cd->asid = new_asid;
	/*
	 * Update ASID and invalidate CD in all associated masters. There will
	 * be some overlap between use of both ASIDs, until we invalidate the
	 * TLB.
	 */
	arm_smmu_write_ctx_desc(smmu_domain, 0, cd);

	/* Invalidate TLB entries previously associated with that context */
	arm_smmu_tlb_inv_asid(smmu, asid);

	xa_erase(&arm_smmu_asid_xa, asid);
	return NULL;
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

bool arm_smmu_sva_supported(struct arm_smmu_device *smmu)
{
	unsigned long reg, fld;
	unsigned long oas;
	unsigned long asid_bits;
	u32 feat_mask = ARM_SMMU_FEAT_BTM | ARM_SMMU_FEAT_COHERENCY;

	if (vabits_actual == 52)
		feat_mask |= ARM_SMMU_FEAT_VAX;

	if ((smmu->features & feat_mask) != feat_mask)
		return false;

	if (!(smmu->pgsize_bitmap & PAGE_SIZE))
		return false;

	/*
	 * Get the smallest PA size of all CPUs (sanitized by cpufeature). We're
	 * not even pretending to support AArch32 here. Abort if the MMU outputs
	 * addresses larger than what we support.
	 */
	reg = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1);
	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_PARANGE_SHIFT);
	oas = id_aa64mmfr0_parange_to_phys_shift(fld);
	if (smmu->oas < oas)
		return false;

	/* We can support bigger ASIDs than the CPU, but not smaller */
	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_ASID_SHIFT);
	asid_bits = fld ? 16 : 8;
	if (smmu->asid_bits < asid_bits)
		return false;

	/*
	 * See max_pinned_asids in arch/arm64/mm/context.c. The following is
	 * generally the maximum number of bindable processes.
	 */
	if (arm64_kernel_unmapped_at_el0())
		asid_bits--;
	dev_dbg(smmu->dev, "%d shared contexts\n", (1 << asid_bits) -
		num_possible_cpus() - 2);

	return true;
}

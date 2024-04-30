// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the IOMMU SVA API for the ARM SMMUv3
 */

#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/mmu_notifier.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

#include "arm-smmu-v3.h"
#include "../../io-pgtable-arm.h"

struct arm_smmu_mmu_notifier {
	struct mmu_notifier		mn;
	struct arm_smmu_ctx_desc	*cd;
	bool				cleared;
	refcount_t			refs;
	struct list_head		list;
	struct arm_smmu_domain		*domain;
};

#define mn_to_smmu(mn) container_of(mn, struct arm_smmu_mmu_notifier, mn)

struct arm_smmu_bond {
	struct mm_struct		*mm;
	struct arm_smmu_mmu_notifier	*smmu_mn;
	struct list_head		list;
};

#define sva_to_bond(handle) \
	container_of(handle, struct arm_smmu_bond, sva)

static DEFINE_MUTEX(sva_lock);

/*
 * Write the CD to the CD tables for all masters that this domain is attached
 * to. Note that this is only used to update existing CD entries in the target
 * CD table, for which it's assumed that arm_smmu_write_ctx_desc can't fail.
 */
static void arm_smmu_update_ctx_desc_devices(struct arm_smmu_domain *smmu_domain,
					   int ssid,
					   struct arm_smmu_ctx_desc *cd)
{
	struct arm_smmu_master *master;
	unsigned long flags;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_for_each_entry(master, &smmu_domain->devices, domain_head) {
		arm_smmu_write_ctx_desc(master, ssid, cd);
	}
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);
}

static void
arm_smmu_update_s1_domain_cd_entry(struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_master *master;
	struct arm_smmu_cd target_cd;
	unsigned long flags;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_for_each_entry(master, &smmu_domain->devices, domain_head) {
		struct arm_smmu_cd *cdptr;

		/* S1 domains only support RID attachment right now */
		cdptr = arm_smmu_get_cd_ptr(master, IOMMU_NO_PASID);
		if (WARN_ON(!cdptr))
			continue;

		arm_smmu_make_s1_cd(&target_cd, master, smmu_domain);
		arm_smmu_write_cd_entry(master, IOMMU_NO_PASID, cdptr,
					&target_cd);
	}
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);
}

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

	smmu_domain = container_of(cd, struct arm_smmu_domain, cd);
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
	arm_smmu_update_s1_domain_cd_entry(smmu_domain);

	/* Invalidate TLB entries previously associated with that context */
	arm_smmu_tlb_inv_asid(smmu, asid);

	xa_erase(&arm_smmu_asid_xa, asid);
	return NULL;
}

static struct arm_smmu_ctx_desc *arm_smmu_alloc_shared_cd(struct mm_struct *mm)
{
	u16 asid;
	int err = 0;
	u64 tcr, par, reg;
	struct arm_smmu_ctx_desc *cd;
	struct arm_smmu_ctx_desc *ret = NULL;

	/* Don't free the mm until we release the ASID */
	mmgrab(mm);

	asid = arm64_mm_context_get(mm);
	if (!asid) {
		err = -ESRCH;
		goto out_drop_mm;
	}

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
	par = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_EL1_PARANGE_SHIFT);
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
out_drop_mm:
	mmdrop(mm);
	return err < 0 ? ERR_PTR(err) : ret;
}

static void arm_smmu_free_shared_cd(struct arm_smmu_ctx_desc *cd)
{
	if (arm_smmu_free_asid(cd)) {
		/* Unpin ASID */
		arm64_mm_context_put(cd->mm);
		mmdrop(cd->mm);
		kfree(cd);
	}
}

/*
 * Cloned from the MAX_TLBI_OPS in arch/arm64/include/asm/tlbflush.h, this
 * is used as a threshold to replace per-page TLBI commands to issue in the
 * command queue with an address-space TLBI command, when SMMU w/o a range
 * invalidation feature handles too many per-page TLBI commands, which will
 * otherwise result in a soft lockup.
 */
#define CMDQ_MAX_TLBI_OPS		(1 << (PAGE_SHIFT - 3))

static void arm_smmu_mm_arch_invalidate_secondary_tlbs(struct mmu_notifier *mn,
						struct mm_struct *mm,
						unsigned long start,
						unsigned long end)
{
	struct arm_smmu_mmu_notifier *smmu_mn = mn_to_smmu(mn);
	struct arm_smmu_domain *smmu_domain = smmu_mn->domain;
	size_t size;

	/*
	 * The mm_types defines vm_end as the first byte after the end address,
	 * different from IOMMU subsystem using the last address of an address
	 * range. So do a simple translation here by calculating size correctly.
	 */
	size = end - start;
	if (!(smmu_domain->smmu->features & ARM_SMMU_FEAT_RANGE_INV)) {
		if (size >= CMDQ_MAX_TLBI_OPS * PAGE_SIZE)
			size = 0;
	} else {
		if (size == ULONG_MAX)
			size = 0;
	}

	if (!(smmu_domain->smmu->features & ARM_SMMU_FEAT_BTM)) {
		if (!size)
			arm_smmu_tlb_inv_asid(smmu_domain->smmu,
					      smmu_mn->cd->asid);
		else
			arm_smmu_tlb_inv_range_asid(start, size,
						    smmu_mn->cd->asid,
						    PAGE_SIZE, false,
						    smmu_domain);
	}

	arm_smmu_atc_inv_domain(smmu_domain, mm_get_enqcmd_pasid(mm), start,
				size);
}

static void arm_smmu_mm_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct arm_smmu_mmu_notifier *smmu_mn = mn_to_smmu(mn);
	struct arm_smmu_domain *smmu_domain = smmu_mn->domain;

	mutex_lock(&sva_lock);
	if (smmu_mn->cleared) {
		mutex_unlock(&sva_lock);
		return;
	}

	/*
	 * DMA may still be running. Keep the cd valid to avoid C_BAD_CD events,
	 * but disable translation.
	 */
	arm_smmu_update_ctx_desc_devices(smmu_domain, mm_get_enqcmd_pasid(mm),
					 &quiet_cd);

	arm_smmu_tlb_inv_asid(smmu_domain->smmu, smmu_mn->cd->asid);
	arm_smmu_atc_inv_domain(smmu_domain, mm_get_enqcmd_pasid(mm), 0, 0);

	smmu_mn->cleared = true;
	mutex_unlock(&sva_lock);
}

static void arm_smmu_mmu_notifier_free(struct mmu_notifier *mn)
{
	kfree(mn_to_smmu(mn));
}

static const struct mmu_notifier_ops arm_smmu_mmu_notifier_ops = {
	.arch_invalidate_secondary_tlbs	= arm_smmu_mm_arch_invalidate_secondary_tlbs,
	.release			= arm_smmu_mm_release,
	.free_notifier			= arm_smmu_mmu_notifier_free,
};

/* Allocate or get existing MMU notifier for this {domain, mm} pair */
static struct arm_smmu_mmu_notifier *
arm_smmu_mmu_notifier_get(struct arm_smmu_domain *smmu_domain,
			  struct mm_struct *mm)
{
	int ret;
	struct arm_smmu_ctx_desc *cd;
	struct arm_smmu_mmu_notifier *smmu_mn;

	list_for_each_entry(smmu_mn, &smmu_domain->mmu_notifiers, list) {
		if (smmu_mn->mn.mm == mm) {
			refcount_inc(&smmu_mn->refs);
			return smmu_mn;
		}
	}

	cd = arm_smmu_alloc_shared_cd(mm);
	if (IS_ERR(cd))
		return ERR_CAST(cd);

	smmu_mn = kzalloc(sizeof(*smmu_mn), GFP_KERNEL);
	if (!smmu_mn) {
		ret = -ENOMEM;
		goto err_free_cd;
	}

	refcount_set(&smmu_mn->refs, 1);
	smmu_mn->cd = cd;
	smmu_mn->domain = smmu_domain;
	smmu_mn->mn.ops = &arm_smmu_mmu_notifier_ops;

	ret = mmu_notifier_register(&smmu_mn->mn, mm);
	if (ret) {
		kfree(smmu_mn);
		goto err_free_cd;
	}

	list_add(&smmu_mn->list, &smmu_domain->mmu_notifiers);
	return smmu_mn;

err_free_cd:
	arm_smmu_free_shared_cd(cd);
	return ERR_PTR(ret);
}

static void arm_smmu_mmu_notifier_put(struct arm_smmu_mmu_notifier *smmu_mn)
{
	struct mm_struct *mm = smmu_mn->mn.mm;
	struct arm_smmu_ctx_desc *cd = smmu_mn->cd;
	struct arm_smmu_domain *smmu_domain = smmu_mn->domain;

	if (!refcount_dec_and_test(&smmu_mn->refs))
		return;

	list_del(&smmu_mn->list);

	/*
	 * If we went through clear(), we've already invalidated, and no
	 * new TLB entry can have been formed.
	 */
	if (!smmu_mn->cleared) {
		arm_smmu_tlb_inv_asid(smmu_domain->smmu, cd->asid);
		arm_smmu_atc_inv_domain(smmu_domain, mm_get_enqcmd_pasid(mm), 0,
					0);
	}

	/* Frees smmu_mn */
	mmu_notifier_put(&smmu_mn->mn);
	arm_smmu_free_shared_cd(cd);
}

static int __arm_smmu_sva_bind(struct device *dev, ioasid_t pasid,
			       struct mm_struct *mm)
{
	int ret;
	struct arm_smmu_bond *bond;
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct arm_smmu_domain *smmu_domain;

	if (!(domain->type & __IOMMU_DOMAIN_PAGING))
		return -ENODEV;
	smmu_domain = to_smmu_domain(domain);
	if (smmu_domain->stage != ARM_SMMU_DOMAIN_S1)
		return -ENODEV;

	if (!master || !master->sva_enabled)
		return -ENODEV;

	bond = kzalloc(sizeof(*bond), GFP_KERNEL);
	if (!bond)
		return -ENOMEM;

	bond->mm = mm;

	bond->smmu_mn = arm_smmu_mmu_notifier_get(smmu_domain, mm);
	if (IS_ERR(bond->smmu_mn)) {
		ret = PTR_ERR(bond->smmu_mn);
		goto err_free_bond;
	}

	ret = arm_smmu_write_ctx_desc(master, pasid, bond->smmu_mn->cd);
	if (ret)
		goto err_put_notifier;

	list_add(&bond->list, &master->bonds);
	return 0;

err_put_notifier:
	arm_smmu_mmu_notifier_put(bond->smmu_mn);
err_free_bond:
	kfree(bond);
	return ret;
}

bool arm_smmu_sva_supported(struct arm_smmu_device *smmu)
{
	unsigned long reg, fld;
	unsigned long oas;
	unsigned long asid_bits;
	u32 feat_mask = ARM_SMMU_FEAT_COHERENCY;

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
	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_EL1_PARANGE_SHIFT);
	oas = id_aa64mmfr0_parange_to_phys_shift(fld);
	if (smmu->oas < oas)
		return false;

	/* We can support bigger ASIDs than the CPU, but not smaller */
	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR0_EL1_ASIDBITS_SHIFT);
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

bool arm_smmu_master_iopf_supported(struct arm_smmu_master *master)
{
	/* We're not keeping track of SIDs in fault events */
	if (master->num_streams != 1)
		return false;

	return master->stall_enabled;
}

bool arm_smmu_master_sva_supported(struct arm_smmu_master *master)
{
	if (!(master->smmu->features & ARM_SMMU_FEAT_SVA))
		return false;

	/* SSID support is mandatory for the moment */
	return master->ssid_bits;
}

bool arm_smmu_master_sva_enabled(struct arm_smmu_master *master)
{
	bool enabled;

	mutex_lock(&sva_lock);
	enabled = master->sva_enabled;
	mutex_unlock(&sva_lock);
	return enabled;
}

static int arm_smmu_master_sva_enable_iopf(struct arm_smmu_master *master)
{
	struct device *dev = master->dev;

	/*
	 * Drivers for devices supporting PRI or stall should enable IOPF first.
	 * Others have device-specific fault handlers and don't need IOPF.
	 */
	if (!arm_smmu_master_iopf_supported(master))
		return 0;

	if (!master->iopf_enabled)
		return -EINVAL;

	return iopf_queue_add_device(master->smmu->evtq.iopf, dev);
}

static void arm_smmu_master_sva_disable_iopf(struct arm_smmu_master *master)
{
	struct device *dev = master->dev;

	if (!master->iopf_enabled)
		return;

	iopf_queue_remove_device(master->smmu->evtq.iopf, dev);
}

int arm_smmu_master_enable_sva(struct arm_smmu_master *master)
{
	int ret;

	mutex_lock(&sva_lock);
	ret = arm_smmu_master_sva_enable_iopf(master);
	if (!ret)
		master->sva_enabled = true;
	mutex_unlock(&sva_lock);

	return ret;
}

int arm_smmu_master_disable_sva(struct arm_smmu_master *master)
{
	mutex_lock(&sva_lock);
	if (!list_empty(&master->bonds)) {
		dev_err(master->dev, "cannot disable SVA, device is bound\n");
		mutex_unlock(&sva_lock);
		return -EBUSY;
	}
	arm_smmu_master_sva_disable_iopf(master);
	master->sva_enabled = false;
	mutex_unlock(&sva_lock);

	return 0;
}

void arm_smmu_sva_notifier_synchronize(void)
{
	/*
	 * Some MMU notifiers may still be waiting to be freed, using
	 * arm_smmu_mmu_notifier_free(). Wait for them.
	 */
	mmu_notifier_synchronize();
}

void arm_smmu_sva_remove_dev_pasid(struct iommu_domain *domain,
				   struct device *dev, ioasid_t id)
{
	struct mm_struct *mm = domain->mm;
	struct arm_smmu_bond *bond = NULL, *t;
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	mutex_lock(&sva_lock);

	arm_smmu_write_ctx_desc(master, id, NULL);

	list_for_each_entry(t, &master->bonds, list) {
		if (t->mm == mm) {
			bond = t;
			break;
		}
	}

	if (!WARN_ON(!bond)) {
		list_del(&bond->list);
		arm_smmu_mmu_notifier_put(bond->smmu_mn);
		kfree(bond);
	}
	mutex_unlock(&sva_lock);
}

static int arm_smmu_sva_set_dev_pasid(struct iommu_domain *domain,
				      struct device *dev, ioasid_t id)
{
	int ret = 0;
	struct mm_struct *mm = domain->mm;

	if (mm_get_enqcmd_pasid(mm) != id)
		return -EINVAL;

	mutex_lock(&sva_lock);
	ret = __arm_smmu_sva_bind(dev, id, mm);
	mutex_unlock(&sva_lock);

	return ret;
}

static void arm_smmu_sva_domain_free(struct iommu_domain *domain)
{
	kfree(domain);
}

static const struct iommu_domain_ops arm_smmu_sva_domain_ops = {
	.set_dev_pasid		= arm_smmu_sva_set_dev_pasid,
	.free			= arm_smmu_sva_domain_free
};

struct iommu_domain *arm_smmu_sva_domain_alloc(void)
{
	struct iommu_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;
	domain->ops = &arm_smmu_sva_domain_ops;

	return domain;
}

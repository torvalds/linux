// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * Macros and functions to access KVM PTEs (also known as SPTEs)
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2020 Red Hat, Inc. and/or its affiliates.
 */


#include <linux/kvm_host.h>
#include "mmu.h"
#include "mmu_internal.h"
#include "x86.h"
#include "spte.h"

#include <asm/e820/api.h>

u64 __read_mostly shadow_nx_mask;
u64 __read_mostly shadow_x_mask; /* mutual exclusive with nx_mask */
u64 __read_mostly shadow_user_mask;
u64 __read_mostly shadow_accessed_mask;
u64 __read_mostly shadow_dirty_mask;
u64 __read_mostly shadow_mmio_value;
u64 __read_mostly shadow_mmio_access_mask;
u64 __read_mostly shadow_present_mask;
u64 __read_mostly shadow_me_mask;
u64 __read_mostly shadow_acc_track_mask;

u64 __read_mostly shadow_nonpresent_or_rsvd_mask;
u64 __read_mostly shadow_nonpresent_or_rsvd_lower_gfn_mask;

u8 __read_mostly shadow_phys_bits;

static u64 generation_mmio_spte_mask(u64 gen)
{
	u64 mask;

	WARN_ON(gen & ~MMIO_SPTE_GEN_MASK);
	BUILD_BUG_ON((MMIO_SPTE_GEN_HIGH_MASK | MMIO_SPTE_GEN_LOW_MASK) & SPTE_SPECIAL_MASK);

	mask = (gen << MMIO_SPTE_GEN_LOW_START) & MMIO_SPTE_GEN_LOW_MASK;
	mask |= (gen << MMIO_SPTE_GEN_HIGH_START) & MMIO_SPTE_GEN_HIGH_MASK;
	return mask;
}

u64 make_mmio_spte(struct kvm_vcpu *vcpu, u64 gfn, unsigned int access)
{
	u64 gen = kvm_vcpu_memslots(vcpu)->generation & MMIO_SPTE_GEN_MASK;
	u64 mask = generation_mmio_spte_mask(gen);
	u64 gpa = gfn << PAGE_SHIFT;

	access &= shadow_mmio_access_mask;
	mask |= shadow_mmio_value | access;
	mask |= gpa | shadow_nonpresent_or_rsvd_mask;
	mask |= (gpa & shadow_nonpresent_or_rsvd_mask)
		<< SHADOW_NONPRESENT_OR_RSVD_MASK_LEN;

	return mask;
}

static bool kvm_is_mmio_pfn(kvm_pfn_t pfn)
{
	if (pfn_valid(pfn))
		return !is_zero_pfn(pfn) && PageReserved(pfn_to_page(pfn)) &&
			/*
			 * Some reserved pages, such as those from NVDIMM
			 * DAX devices, are not for MMIO, and can be mapped
			 * with cached memory type for better performance.
			 * However, the above check misconceives those pages
			 * as MMIO, and results in KVM mapping them with UC
			 * memory type, which would hurt the performance.
			 * Therefore, we check the host memory type in addition
			 * and only treat UC/UC-/WC pages as MMIO.
			 */
			(!pat_enabled() || pat_pfn_immune_to_uc_mtrr(pfn));

	return !e820__mapped_raw_any(pfn_to_hpa(pfn),
				     pfn_to_hpa(pfn + 1) - 1,
				     E820_TYPE_RAM);
}

int make_spte(struct kvm_vcpu *vcpu, unsigned int pte_access, int level,
		     gfn_t gfn, kvm_pfn_t pfn, u64 old_spte, bool speculative,
		     bool can_unsync, bool host_writable, bool ad_disabled,
		     u64 *new_spte)
{
	u64 spte = 0;
	int ret = 0;

	if (ad_disabled)
		spte |= SPTE_AD_DISABLED_MASK;
	else if (kvm_vcpu_ad_need_write_protect(vcpu))
		spte |= SPTE_AD_WRPROT_ONLY_MASK;

	/*
	 * For the EPT case, shadow_present_mask is 0 if hardware
	 * supports exec-only page table entries.  In that case,
	 * ACC_USER_MASK and shadow_user_mask are used to represent
	 * read access.  See FNAME(gpte_access) in paging_tmpl.h.
	 */
	spte |= shadow_present_mask;
	if (!speculative)
		spte |= spte_shadow_accessed_mask(spte);

	if (level > PG_LEVEL_4K && (pte_access & ACC_EXEC_MASK) &&
	    is_nx_huge_page_enabled()) {
		pte_access &= ~ACC_EXEC_MASK;
	}

	if (pte_access & ACC_EXEC_MASK)
		spte |= shadow_x_mask;
	else
		spte |= shadow_nx_mask;

	if (pte_access & ACC_USER_MASK)
		spte |= shadow_user_mask;

	if (level > PG_LEVEL_4K)
		spte |= PT_PAGE_SIZE_MASK;
	if (tdp_enabled)
		spte |= kvm_x86_ops.get_mt_mask(vcpu, gfn,
			kvm_is_mmio_pfn(pfn));

	if (host_writable)
		spte |= SPTE_HOST_WRITEABLE;
	else
		pte_access &= ~ACC_WRITE_MASK;

	if (!kvm_is_mmio_pfn(pfn))
		spte |= shadow_me_mask;

	spte |= (u64)pfn << PAGE_SHIFT;

	if (pte_access & ACC_WRITE_MASK) {
		spte |= PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE;

		/*
		 * Optimization: for pte sync, if spte was writable the hash
		 * lookup is unnecessary (and expensive). Write protection
		 * is responsibility of mmu_get_page / kvm_sync_page.
		 * Same reasoning can be applied to dirty page accounting.
		 */
		if (!can_unsync && is_writable_pte(old_spte))
			goto out;

		if (mmu_need_write_protect(vcpu, gfn, can_unsync)) {
			pgprintk("%s: found shadow page for %llx, marking ro\n",
				 __func__, gfn);
			ret |= SET_SPTE_WRITE_PROTECTED_PT;
			pte_access &= ~ACC_WRITE_MASK;
			spte &= ~(PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE);
		}
	}

	if (pte_access & ACC_WRITE_MASK)
		spte |= spte_shadow_dirty_mask(spte);

	if (speculative)
		spte = mark_spte_for_access_track(spte);

out:
	*new_spte = spte;
	return ret;
}

u64 make_nonleaf_spte(u64 *child_pt, bool ad_disabled)
{
	u64 spte;

	spte = __pa(child_pt) | shadow_present_mask | PT_WRITABLE_MASK |
	       shadow_user_mask | shadow_x_mask | shadow_me_mask;

	if (ad_disabled)
		spte |= SPTE_AD_DISABLED_MASK;
	else
		spte |= shadow_accessed_mask;

	return spte;
}

u64 kvm_mmu_changed_pte_notifier_make_spte(u64 old_spte, kvm_pfn_t new_pfn)
{
	u64 new_spte;

	new_spte = old_spte & ~PT64_BASE_ADDR_MASK;
	new_spte |= (u64)new_pfn << PAGE_SHIFT;

	new_spte &= ~PT_WRITABLE_MASK;
	new_spte &= ~SPTE_HOST_WRITEABLE;

	new_spte = mark_spte_for_access_track(new_spte);

	return new_spte;
}

static u8 kvm_get_shadow_phys_bits(void)
{
	/*
	 * boot_cpu_data.x86_phys_bits is reduced when MKTME or SME are detected
	 * in CPU detection code, but the processor treats those reduced bits as
	 * 'keyID' thus they are not reserved bits. Therefore KVM needs to look at
	 * the physical address bits reported by CPUID.
	 */
	if (likely(boot_cpu_data.extended_cpuid_level >= 0x80000008))
		return cpuid_eax(0x80000008) & 0xff;

	/*
	 * Quite weird to have VMX or SVM but not MAXPHYADDR; probably a VM with
	 * custom CPUID.  Proceed with whatever the kernel found since these features
	 * aren't virtualizable (SME/SEV also require CPUIDs higher than 0x80000008).
	 */
	return boot_cpu_data.x86_phys_bits;
}

u64 mark_spte_for_access_track(u64 spte)
{
	if (spte_ad_enabled(spte))
		return spte & ~shadow_accessed_mask;

	if (is_access_track_spte(spte))
		return spte;

	/*
	 * Making an Access Tracking PTE will result in removal of write access
	 * from the PTE. So, verify that we will be able to restore the write
	 * access in the fast page fault path later on.
	 */
	WARN_ONCE((spte & PT_WRITABLE_MASK) &&
		  !spte_can_locklessly_be_made_writable(spte),
		  "kvm: Writable SPTE is not locklessly dirty-trackable\n");

	WARN_ONCE(spte & (SHADOW_ACC_TRACK_SAVED_BITS_MASK <<
			  SHADOW_ACC_TRACK_SAVED_BITS_SHIFT),
		  "kvm: Access Tracking saved bit locations are not zero\n");

	spte |= (spte & SHADOW_ACC_TRACK_SAVED_BITS_MASK) <<
		SHADOW_ACC_TRACK_SAVED_BITS_SHIFT;
	spte &= ~shadow_acc_track_mask;

	return spte;
}

void kvm_mmu_set_mmio_spte_mask(u64 mmio_value, u64 access_mask)
{
	BUG_ON((u64)(unsigned)access_mask != access_mask);
	WARN_ON(mmio_value & (shadow_nonpresent_or_rsvd_mask << SHADOW_NONPRESENT_OR_RSVD_MASK_LEN));
	WARN_ON(mmio_value & shadow_nonpresent_or_rsvd_lower_gfn_mask);
	shadow_mmio_value = mmio_value | SPTE_MMIO_MASK;
	shadow_mmio_access_mask = access_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mmio_spte_mask);

/*
 * Sets the shadow PTE masks used by the MMU.
 *
 * Assumptions:
 *  - Setting either @accessed_mask or @dirty_mask requires setting both
 *  - At least one of @accessed_mask or @acc_track_mask must be set
 */
void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask, u64 p_mask,
		u64 acc_track_mask, u64 me_mask)
{
	BUG_ON(!dirty_mask != !accessed_mask);
	BUG_ON(!accessed_mask && !acc_track_mask);
	BUG_ON(acc_track_mask & SPTE_SPECIAL_MASK);

	shadow_user_mask = user_mask;
	shadow_accessed_mask = accessed_mask;
	shadow_dirty_mask = dirty_mask;
	shadow_nx_mask = nx_mask;
	shadow_x_mask = x_mask;
	shadow_present_mask = p_mask;
	shadow_acc_track_mask = acc_track_mask;
	shadow_me_mask = me_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mask_ptes);

void kvm_mmu_reset_all_pte_masks(void)
{
	u8 low_phys_bits;

	shadow_user_mask = 0;
	shadow_accessed_mask = 0;
	shadow_dirty_mask = 0;
	shadow_nx_mask = 0;
	shadow_x_mask = 0;
	shadow_present_mask = 0;
	shadow_acc_track_mask = 0;

	shadow_phys_bits = kvm_get_shadow_phys_bits();

	/*
	 * If the CPU has 46 or less physical address bits, then set an
	 * appropriate mask to guard against L1TF attacks. Otherwise, it is
	 * assumed that the CPU is not vulnerable to L1TF.
	 *
	 * Some Intel CPUs address the L1 cache using more PA bits than are
	 * reported by CPUID. Use the PA width of the L1 cache when possible
	 * to achieve more effective mitigation, e.g. if system RAM overlaps
	 * the most significant bits of legal physical address space.
	 */
	shadow_nonpresent_or_rsvd_mask = 0;
	low_phys_bits = boot_cpu_data.x86_phys_bits;
	if (boot_cpu_has_bug(X86_BUG_L1TF) &&
	    !WARN_ON_ONCE(boot_cpu_data.x86_cache_bits >=
			  52 - SHADOW_NONPRESENT_OR_RSVD_MASK_LEN)) {
		low_phys_bits = boot_cpu_data.x86_cache_bits
			- SHADOW_NONPRESENT_OR_RSVD_MASK_LEN;
		shadow_nonpresent_or_rsvd_mask =
			rsvd_bits(low_phys_bits, boot_cpu_data.x86_cache_bits - 1);
	}

	shadow_nonpresent_or_rsvd_lower_gfn_mask =
		GENMASK_ULL(low_phys_bits - 1, PAGE_SHIFT);
}

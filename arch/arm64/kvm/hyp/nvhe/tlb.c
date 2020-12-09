// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/tlbflush.h>

struct tlb_inv_context {
	u64		tcr;
};

static void __tlb_switch_to_guest(struct kvm_s2_mmu *mmu,
				  struct tlb_inv_context *cxt)
{
	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		u64 val;

		/*
		 * For CPUs that are affected by ARM 1319367, we need to
		 * avoid a host Stage-1 walk while we have the guest's
		 * VMID set in the VTTBR in order to invalidate TLBs.
		 * We're guaranteed that the S1 MMU is enabled, so we can
		 * simply set the EPD bits to avoid any further TLB fill.
		 */
		val = cxt->tcr = read_sysreg_el1(SYS_TCR);
		val |= TCR_EPD1_MASK | TCR_EPD0_MASK;
		write_sysreg_el1(val, SYS_TCR);
		isb();
	}

	/*
	 * __load_guest_stage2() includes an ISB only when the AT
	 * workaround is applied. Take care of the opposite condition,
	 * ensuring that we always have an ISB, but not two ISBs back
	 * to back.
	 */
	__load_guest_stage2(mmu);
	asm(ALTERNATIVE("isb", "nop", ARM64_WORKAROUND_SPECULATIVE_AT));
}

static void __tlb_switch_to_host(struct tlb_inv_context *cxt)
{
	write_sysreg(0, vttbr_el2);

	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		/* Ensure write of the host VMID */
		isb();
		/* Restore the host's TCR_EL1 */
		write_sysreg_el1(cxt->tcr, SYS_TCR);
	}
}

void __kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu,
			      phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	dsb(ishst);

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt);

	/*
	 * We could do so much better if we had the VA as well.
	 * Instead, we invalidate Stage-2 for this IPA, and the
	 * whole of Stage-1. Weep...
	 */
	ipa >>= 12;
	__tlbi_level(ipas2e1is, ipa, level);

	/*
	 * We have to ensure completion of the invalidation at Stage-2,
	 * since a table walk on another CPU could refill a TLB with a
	 * complete (S1 + S2) walk based on the old Stage-2 mapping if
	 * the Stage-1 invalidation happened first.
	 */
	dsb(ish);
	__tlbi(vmalle1is);
	dsb(ish);
	isb();

	/*
	 * If the host is running at EL1 and we have a VPIPT I-cache,
	 * then we must perform I-cache maintenance at EL2 in order for
	 * it to have an effect on the guest. Since the guest cannot hit
	 * I-cache lines allocated with a different VMID, we don't need
	 * to worry about junk out of guest reset (we nuke the I-cache on
	 * VMID rollover), but we do need to be careful when remapping
	 * executable pages for the same guest. This can happen when KSM
	 * takes a CoW fault on an executable page, copies the page into
	 * a page that was previously mapped in the guest and then needs
	 * to invalidate the guest view of the I-cache for that page
	 * from EL1. To solve this, we invalidate the entire I-cache when
	 * unmapping a page from a guest if we have a VPIPT I-cache but
	 * the host is running at EL1. As above, we could do better if
	 * we had the VA.
	 *
	 * The moral of this story is: if you have a VPIPT I-cache, then
	 * you should be running with VHE enabled.
	 */
	if (icache_is_vpipt())
		__flush_icache_all();

	__tlb_switch_to_host(&cxt);
}

void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	dsb(ishst);

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt);

	__tlbi(vmalls12e1is);
	dsb(ish);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_tlb_flush_local_vmid(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt);

	__tlbi(vmalle1);
	dsb(nsh);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_flush_vm_context(void)
{
	dsb(ishst);
	__tlbi(alle1is);

	/*
	 * VIPT and PIPT caches are not affected by VMID, so no maintenance
	 * is necessary across a VMID rollover.
	 *
	 * VPIPT caches constrain lookup and maintenance to the active VMID,
	 * so we need to invalidate lines with a stale VMID to avoid an ABA
	 * race after multiple rollovers.
	 *
	 */
	if (icache_is_vpipt())
		asm volatile("ic ialluis");

	dsb(ish);
}

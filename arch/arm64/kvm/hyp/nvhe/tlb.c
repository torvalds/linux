// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/tlbflush.h>

#include <nvhe/mem_protect.h>

struct tlb_inv_context {
	u64		tcr;
};

static void __tlb_switch_to_guest(struct kvm_s2_mmu *mmu,
				  struct tlb_inv_context *cxt,
				  bool nsh)
{
	/*
	 * We have two requirements:
	 *
	 * - ensure that the page table updates are visible to all
	 *   CPUs, for which a dsb(DOMAIN-st) is what we need, DOMAIN
	 *   being either ish or nsh, depending on the invalidation
	 *   type.
	 *
	 * - complete any speculative page table walk started before
	 *   we trapped to EL2 so that we can mess with the MM
	 *   registers out of context, for which dsb(nsh) is enough
	 *
	 * The composition of these two barriers is a dsb(DOMAIN), and
	 * the 'nsh' parameter tracks the distinction between
	 * Inner-Shareable and Non-Shareable, as specified by the
	 * callers.
	 */
	if (nsh)
		dsb(nsh);
	else
		dsb(ish);

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
	 * __load_stage2() includes an ISB only when the AT
	 * workaround is applied. Take care of the opposite condition,
	 * ensuring that we always have an ISB, but not two ISBs back
	 * to back.
	 */
	__load_stage2(mmu, kern_hyp_va(mmu->arch));
	asm(ALTERNATIVE("isb", "nop", ARM64_WORKAROUND_SPECULATIVE_AT));
}

static void __tlb_switch_to_host(struct tlb_inv_context *cxt)
{
	__load_host_stage2();

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

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt, false);

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

	__tlb_switch_to_host(&cxt);
}

void __kvm_tlb_flush_vmid_ipa_nsh(struct kvm_s2_mmu *mmu,
				  phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt, true);

	/*
	 * We could do so much better if we had the VA as well.
	 * Instead, we invalidate Stage-2 for this IPA, and the
	 * whole of Stage-1. Weep...
	 */
	ipa >>= 12;
	__tlbi_level(ipas2e1, ipa, level);

	/*
	 * We have to ensure completion of the invalidation at Stage-2,
	 * since a table walk on another CPU could refill a TLB with a
	 * complete (S1 + S2) walk based on the old Stage-2 mapping if
	 * the Stage-1 invalidation happened first.
	 */
	dsb(nsh);
	__tlbi(vmalle1);
	dsb(nsh);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_tlb_flush_vmid_range(struct kvm_s2_mmu *mmu,
				phys_addr_t start, unsigned long pages)
{
	struct tlb_inv_context cxt;
	unsigned long stride;

	/*
	 * Since the range of addresses may not be mapped at
	 * the same level, assume the worst case as PAGE_SIZE
	 */
	stride = PAGE_SIZE;
	start = round_down(start, stride);

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt, false);

	__flush_s2_tlb_range_op(ipas2e1is, start, pages, stride,
				TLBI_TTL_UNKNOWN);

	dsb(ish);
	__tlbi(vmalle1is);
	dsb(ish);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt, false);

	__tlbi(vmalls12e1is);
	dsb(ish);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_flush_cpu_context(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	__tlb_switch_to_guest(mmu, &cxt, false);

	__tlbi(vmalle1);
	asm volatile("ic iallu");
	dsb(nsh);
	isb();

	__tlb_switch_to_host(&cxt);
}

void __kvm_flush_vm_context(void)
{
	/* Same remark as in __tlb_switch_to_guest() */
	dsb(ish);
	__tlbi(alle1is);
	dsb(ish);
}

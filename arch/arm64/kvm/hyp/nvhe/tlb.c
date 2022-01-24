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
	struct kvm_s2_mmu	*mmu;
	u64			tcr;
	u64			sctlr;
};

static void enter_vmid_context(struct kvm_s2_mmu *mmu,
			       struct tlb_inv_context *cxt)
{
	struct kvm_s2_mmu *host_s2_mmu = &host_mmu.arch.mmu;
	struct kvm_cpu_context *host_ctxt;
	struct kvm_vcpu *vcpu;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	vcpu = host_ctxt->__hyp_running_vcpu;
	cxt->mmu = NULL;

	/*
	 * If we're already in the desired context, then there's nothing
	 * to do.
	 */
	if (vcpu) {
		if (mmu == vcpu->arch.hw_mmu || WARN_ON(mmu != host_s2_mmu))
			return;
	} else if (mmu == host_s2_mmu) {
		return;
	}

	cxt->mmu = mmu;
	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		u64 val;

		/*
		 * For CPUs that are affected by ARM 1319367, we need to
		 * avoid a Stage-1 walk with the old VMID while we have
		 * the new VMID set in the VTTBR in order to invalidate TLBs.
		 * We're guaranteed that the host S1 MMU is enabled, so
		 * we can simply set the EPD bits to avoid any further
		 * TLB fill. For guests, we ensure that the S1 MMU is
		 * temporarily enabled in the next context.
		 */
		val = cxt->tcr = read_sysreg_el1(SYS_TCR);
		val |= TCR_EPD1_MASK | TCR_EPD0_MASK;
		write_sysreg_el1(val, SYS_TCR);
		isb();

		if (vcpu) {
			val = cxt->sctlr = read_sysreg_el1(SYS_SCTLR);
			if (!(val & SCTLR_ELx_M)) {
				val |= SCTLR_ELx_M;
				write_sysreg_el1(val, SYS_SCTLR);
				isb();
			}
		} else {
			/* The host S1 MMU is always enabled. */
			cxt->sctlr = SCTLR_ELx_M;
		}
	}

	/*
	 * __load_stage2() includes an ISB only when the AT
	 * workaround is applied. Take care of the opposite condition,
	 * ensuring that we always have an ISB, but not two ISBs back
	 * to back.
	 */
	if (vcpu)
		__load_host_stage2();
	else
		__load_stage2(mmu, kern_hyp_va(mmu->arch));

	asm(ALTERNATIVE("isb", "nop", ARM64_WORKAROUND_SPECULATIVE_AT));
}

static void exit_vmid_context(struct tlb_inv_context *cxt)
{
	struct kvm_s2_mmu *mmu = cxt->mmu;
	struct kvm_cpu_context *host_ctxt;
	struct kvm_vcpu *vcpu;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	vcpu = host_ctxt->__hyp_running_vcpu;

	if (!mmu)
		return;

	if (vcpu)
		__load_stage2(mmu, kern_hyp_va(mmu->arch));
	else
		__load_host_stage2();

	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		/* Ensure write of the old VMID */
		isb();

		if (!(cxt->sctlr & SCTLR_ELx_M)) {
			write_sysreg_el1(cxt->sctlr, SYS_SCTLR);
			isb();
		}

		write_sysreg_el1(cxt->tcr, SYS_TCR);
	}

	cxt->mmu = NULL;
}

void __kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu,
			      phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	dsb(ishst);

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt);

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
		icache_inval_all_pou();

	exit_vmid_context(&cxt);
}

void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	dsb(ishst);

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt);

	__tlbi(vmalls12e1is);
	dsb(ish);
	isb();

	exit_vmid_context(&cxt);
}

void __kvm_flush_cpu_context(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt);

	__tlbi(vmalle1);
	asm volatile("ic iallu");
	dsb(nsh);
	isb();

	exit_vmid_context(&cxt);
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

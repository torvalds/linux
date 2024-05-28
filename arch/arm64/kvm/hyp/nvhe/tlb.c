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
			       struct tlb_inv_context *cxt,
			       bool nsh)
{
	struct kvm_s2_mmu *host_s2_mmu = &host_mmu.arch.mmu;
	struct kvm_cpu_context *host_ctxt;
	struct kvm_vcpu *vcpu;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	vcpu = host_ctxt->__hyp_running_vcpu;
	cxt->mmu = NULL;

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

	/*
	 * If we're already in the desired context, then there's nothing to do.
	 */
	if (vcpu) {
		/*
		 * We're in guest context. However, for this to work, this needs
		 * to be called from within __kvm_vcpu_run(), which ensures that
		 * __hyp_running_vcpu is set to the current guest vcpu.
		 */
		if (mmu == vcpu->arch.hw_mmu || WARN_ON(mmu != host_s2_mmu))
			return;

		cxt->mmu = vcpu->arch.hw_mmu;
	} else {
		/* We're in host context. */
		if (mmu == host_s2_mmu)
			return;

		cxt->mmu = host_s2_mmu;
	}

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
}

void __kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu,
			      phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt, false);

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

	exit_vmid_context(&cxt);
}

void __kvm_tlb_flush_vmid_ipa_nsh(struct kvm_s2_mmu *mmu,
				  phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt, true);

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

	exit_vmid_context(&cxt);
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
	enter_vmid_context(mmu, &cxt, false);

	__flush_s2_tlb_range_op(ipas2e1is, start, pages, stride,
				TLBI_TTL_UNKNOWN);

	dsb(ish);
	__tlbi(vmalle1is);
	dsb(ish);
	isb();

	exit_vmid_context(&cxt);
}

void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt, false);

	__tlbi(vmalls12e1is);
	dsb(ish);
	isb();

	exit_vmid_context(&cxt);
}

void __kvm_flush_cpu_context(struct kvm_s2_mmu *mmu)
{
	struct tlb_inv_context cxt;

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt, false);

	__tlbi(vmalle1);
	asm volatile("ic iallu");
	dsb(nsh);
	isb();

	exit_vmid_context(&cxt);
}

void __kvm_flush_vm_context(void)
{
	/* Same remark as in enter_vmid_context() */
	dsb(ish);
	__tlbi(alle1is);
	dsb(ish);
}

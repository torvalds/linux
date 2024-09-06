// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/irqflags.h>

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/tlbflush.h>

struct tlb_inv_context {
	struct kvm_s2_mmu	*mmu;
	unsigned long		flags;
	u64			tcr;
	u64			sctlr;
};

static void enter_vmid_context(struct kvm_s2_mmu *mmu,
			       struct tlb_inv_context *cxt)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();
	u64 val;

	local_irq_save(cxt->flags);

	if (vcpu && mmu != vcpu->arch.hw_mmu)
		cxt->mmu = vcpu->arch.hw_mmu;
	else
		cxt->mmu = NULL;

	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		/*
		 * For CPUs that are affected by ARM errata 1165522 or 1530923,
		 * we cannot trust stage-1 to be in a correct state at that
		 * point. Since we do not want to force a full load of the
		 * vcpu state, we prevent the EL1 page-table walker to
		 * allocate new TLBs. This is done by setting the EPD bits
		 * in the TCR_EL1 register. We also need to prevent it to
		 * allocate IPA->PA walks, so we enable the S1 MMU...
		 */
		val = cxt->tcr = read_sysreg_el1(SYS_TCR);
		val |= TCR_EPD1_MASK | TCR_EPD0_MASK;
		write_sysreg_el1(val, SYS_TCR);
		val = cxt->sctlr = read_sysreg_el1(SYS_SCTLR);
		val |= SCTLR_ELx_M;
		write_sysreg_el1(val, SYS_SCTLR);
	}

	/*
	 * With VHE enabled, we have HCR_EL2.{E2H,TGE} = {1,1}, and
	 * most TLB operations target EL2/EL0. In order to affect the
	 * guest TLBs (EL1/EL0), we need to change one of these two
	 * bits. Changing E2H is impossible (goodbye TTBR1_EL2), so
	 * let's flip TGE before executing the TLB operation.
	 *
	 * ARM erratum 1165522 requires some special handling (again),
	 * as we need to make sure both stages of translation are in
	 * place before clearing TGE. __load_stage2() already
	 * has an ISB in order to deal with this.
	 */
	__load_stage2(mmu, mmu->arch);
	val = read_sysreg(hcr_el2);
	val &= ~HCR_TGE;
	write_sysreg(val, hcr_el2);
	isb();
}

static void exit_vmid_context(struct tlb_inv_context *cxt)
{
	/*
	 * We're done with the TLB operation, let's restore the host's
	 * view of HCR_EL2.
	 */
	write_sysreg(HCR_HOST_VHE_FLAGS, hcr_el2);
	isb();

	/* ... and the stage-2 MMU context that we switched away from */
	if (cxt->mmu)
		__load_stage2(cxt->mmu, cxt->mmu->arch);

	if (cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		/* Restore the registers to what they were */
		write_sysreg_el1(cxt->tcr, SYS_TCR);
		write_sysreg_el1(cxt->sctlr, SYS_SCTLR);
	}

	local_irq_restore(cxt->flags);
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

	exit_vmid_context(&cxt);
}

void __kvm_tlb_flush_vmid_ipa_nsh(struct kvm_s2_mmu *mmu,
				  phys_addr_t ipa, int level)
{
	struct tlb_inv_context cxt;

	dsb(nshst);

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt);

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

	dsb(ishst);

	/* Switch to requested VMID */
	enter_vmid_context(mmu, &cxt);

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
	dsb(ish);
}

/*
 * TLB invalidation emulation for NV. For any given instruction, we
 * perform the following transformtions:
 *
 * - a TLBI targeting EL2 S1 is remapped to EL1 S1
 * - a non-shareable TLBI is upgraded to being inner-shareable
 * - an outer-shareable TLBI is also mapped to inner-shareable
 * - an nXS TLBI is upgraded to XS
 */
int __kvm_tlbi_s1e2(struct kvm_s2_mmu *mmu, u64 va, u64 sys_encoding)
{
	struct tlb_inv_context cxt;
	int ret = 0;

	/*
	 * The guest will have provided its own DSB ISHST before trapping.
	 * If it hasn't, that's its own problem, and we won't paper over it
	 * (plus, there is plenty of extra synchronisation before we even
	 * get here...).
	 */

	if (mmu)
		enter_vmid_context(mmu, &cxt);

	switch (sys_encoding) {
	case OP_TLBI_ALLE2:
	case OP_TLBI_ALLE2IS:
	case OP_TLBI_ALLE2OS:
	case OP_TLBI_VMALLE1:
	case OP_TLBI_VMALLE1IS:
	case OP_TLBI_VMALLE1OS:
	case OP_TLBI_ALLE2NXS:
	case OP_TLBI_ALLE2ISNXS:
	case OP_TLBI_ALLE2OSNXS:
	case OP_TLBI_VMALLE1NXS:
	case OP_TLBI_VMALLE1ISNXS:
	case OP_TLBI_VMALLE1OSNXS:
		__tlbi(vmalle1is);
		break;
	case OP_TLBI_VAE2:
	case OP_TLBI_VAE2IS:
	case OP_TLBI_VAE2OS:
	case OP_TLBI_VAE1:
	case OP_TLBI_VAE1IS:
	case OP_TLBI_VAE1OS:
	case OP_TLBI_VAE2NXS:
	case OP_TLBI_VAE2ISNXS:
	case OP_TLBI_VAE2OSNXS:
	case OP_TLBI_VAE1NXS:
	case OP_TLBI_VAE1ISNXS:
	case OP_TLBI_VAE1OSNXS:
		__tlbi(vae1is, va);
		break;
	case OP_TLBI_VALE2:
	case OP_TLBI_VALE2IS:
	case OP_TLBI_VALE2OS:
	case OP_TLBI_VALE1:
	case OP_TLBI_VALE1IS:
	case OP_TLBI_VALE1OS:
	case OP_TLBI_VALE2NXS:
	case OP_TLBI_VALE2ISNXS:
	case OP_TLBI_VALE2OSNXS:
	case OP_TLBI_VALE1NXS:
	case OP_TLBI_VALE1ISNXS:
	case OP_TLBI_VALE1OSNXS:
		__tlbi(vale1is, va);
		break;
	case OP_TLBI_ASIDE1:
	case OP_TLBI_ASIDE1IS:
	case OP_TLBI_ASIDE1OS:
	case OP_TLBI_ASIDE1NXS:
	case OP_TLBI_ASIDE1ISNXS:
	case OP_TLBI_ASIDE1OSNXS:
		__tlbi(aside1is, va);
		break;
	case OP_TLBI_VAAE1:
	case OP_TLBI_VAAE1IS:
	case OP_TLBI_VAAE1OS:
	case OP_TLBI_VAAE1NXS:
	case OP_TLBI_VAAE1ISNXS:
	case OP_TLBI_VAAE1OSNXS:
		__tlbi(vaae1is, va);
		break;
	case OP_TLBI_VAALE1:
	case OP_TLBI_VAALE1IS:
	case OP_TLBI_VAALE1OS:
	case OP_TLBI_VAALE1NXS:
	case OP_TLBI_VAALE1ISNXS:
	case OP_TLBI_VAALE1OSNXS:
		__tlbi(vaale1is, va);
		break;
	case OP_TLBI_RVAE2:
	case OP_TLBI_RVAE2IS:
	case OP_TLBI_RVAE2OS:
	case OP_TLBI_RVAE1:
	case OP_TLBI_RVAE1IS:
	case OP_TLBI_RVAE1OS:
	case OP_TLBI_RVAE2NXS:
	case OP_TLBI_RVAE2ISNXS:
	case OP_TLBI_RVAE2OSNXS:
	case OP_TLBI_RVAE1NXS:
	case OP_TLBI_RVAE1ISNXS:
	case OP_TLBI_RVAE1OSNXS:
		__tlbi(rvae1is, va);
		break;
	case OP_TLBI_RVALE2:
	case OP_TLBI_RVALE2IS:
	case OP_TLBI_RVALE2OS:
	case OP_TLBI_RVALE1:
	case OP_TLBI_RVALE1IS:
	case OP_TLBI_RVALE1OS:
	case OP_TLBI_RVALE2NXS:
	case OP_TLBI_RVALE2ISNXS:
	case OP_TLBI_RVALE2OSNXS:
	case OP_TLBI_RVALE1NXS:
	case OP_TLBI_RVALE1ISNXS:
	case OP_TLBI_RVALE1OSNXS:
		__tlbi(rvale1is, va);
		break;
	case OP_TLBI_RVAAE1:
	case OP_TLBI_RVAAE1IS:
	case OP_TLBI_RVAAE1OS:
	case OP_TLBI_RVAAE1NXS:
	case OP_TLBI_RVAAE1ISNXS:
	case OP_TLBI_RVAAE1OSNXS:
		__tlbi(rvaae1is, va);
		break;
	case OP_TLBI_RVAALE1:
	case OP_TLBI_RVAALE1IS:
	case OP_TLBI_RVAALE1OS:
	case OP_TLBI_RVAALE1NXS:
	case OP_TLBI_RVAALE1ISNXS:
	case OP_TLBI_RVAALE1OSNXS:
		__tlbi(rvaale1is, va);
		break;
	default:
		ret = -EINVAL;
	}
	dsb(ish);
	isb();

	if (mmu)
		exit_vmid_context(&cxt);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM64_KVM_HYP_SYSREG_SR_H__
#define __ARM64_KVM_HYP_SYSREG_SR_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

static inline void __sysreg_save_common_state(struct kvm_cpu_context *ctxt)
{
	ctxt_sys_reg(ctxt, MDSCR_EL1)	= read_sysreg(mdscr_el1);
}

static inline void __sysreg_save_user_state(struct kvm_cpu_context *ctxt)
{
	ctxt_sys_reg(ctxt, TPIDR_EL0)	= read_sysreg(tpidr_el0);
	ctxt_sys_reg(ctxt, TPIDRRO_EL0)	= read_sysreg(tpidrro_el0);
}

static inline struct kvm_vcpu *ctxt_to_vcpu(struct kvm_cpu_context *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->__hyp_running_vcpu;

	if (!vcpu)
		vcpu = container_of(ctxt, struct kvm_vcpu, arch.ctxt);

	return vcpu;
}

static inline bool ctxt_has_mte(struct kvm_cpu_context *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt_to_vcpu(ctxt);

	return kvm_has_mte(kern_hyp_va(vcpu->kvm));
}

static inline bool ctxt_has_s1pie(struct kvm_cpu_context *ctxt)
{
	struct kvm_vcpu *vcpu;

	if (!cpus_have_final_cap(ARM64_HAS_S1PIE))
		return false;

	vcpu = ctxt_to_vcpu(ctxt);
	return kvm_has_feat(kern_hyp_va(vcpu->kvm), ID_AA64MMFR3_EL1, S1PIE, IMP);
}

static inline bool ctxt_has_tcrx(struct kvm_cpu_context *ctxt)
{
	struct kvm_vcpu *vcpu;

	if (!cpus_have_final_cap(ARM64_HAS_TCR2))
		return false;

	vcpu = ctxt_to_vcpu(ctxt);
	return kvm_has_feat(kern_hyp_va(vcpu->kvm), ID_AA64MMFR3_EL1, TCRX, IMP);
}

static inline void __sysreg_save_el1_state(struct kvm_cpu_context *ctxt)
{
	ctxt_sys_reg(ctxt, SCTLR_EL1)	= read_sysreg_el1(SYS_SCTLR);
	ctxt_sys_reg(ctxt, CPACR_EL1)	= read_sysreg_el1(SYS_CPACR);
	ctxt_sys_reg(ctxt, TTBR0_EL1)	= read_sysreg_el1(SYS_TTBR0);
	ctxt_sys_reg(ctxt, TTBR1_EL1)	= read_sysreg_el1(SYS_TTBR1);
	ctxt_sys_reg(ctxt, TCR_EL1)	= read_sysreg_el1(SYS_TCR);
	if (ctxt_has_tcrx(ctxt)) {
		ctxt_sys_reg(ctxt, TCR2_EL1)	= read_sysreg_el1(SYS_TCR2);

		if (ctxt_has_s1pie(ctxt)) {
			ctxt_sys_reg(ctxt, PIR_EL1)	= read_sysreg_el1(SYS_PIR);
			ctxt_sys_reg(ctxt, PIRE0_EL1)	= read_sysreg_el1(SYS_PIRE0);
		}
	}
	ctxt_sys_reg(ctxt, ESR_EL1)	= read_sysreg_el1(SYS_ESR);
	ctxt_sys_reg(ctxt, AFSR0_EL1)	= read_sysreg_el1(SYS_AFSR0);
	ctxt_sys_reg(ctxt, AFSR1_EL1)	= read_sysreg_el1(SYS_AFSR1);
	ctxt_sys_reg(ctxt, FAR_EL1)	= read_sysreg_el1(SYS_FAR);
	ctxt_sys_reg(ctxt, MAIR_EL1)	= read_sysreg_el1(SYS_MAIR);
	ctxt_sys_reg(ctxt, VBAR_EL1)	= read_sysreg_el1(SYS_VBAR);
	ctxt_sys_reg(ctxt, CONTEXTIDR_EL1) = read_sysreg_el1(SYS_CONTEXTIDR);
	ctxt_sys_reg(ctxt, AMAIR_EL1)	= read_sysreg_el1(SYS_AMAIR);
	ctxt_sys_reg(ctxt, CNTKCTL_EL1)	= read_sysreg_el1(SYS_CNTKCTL);
	ctxt_sys_reg(ctxt, PAR_EL1)	= read_sysreg_par();
	ctxt_sys_reg(ctxt, TPIDR_EL1)	= read_sysreg(tpidr_el1);

	if (ctxt_has_mte(ctxt)) {
		ctxt_sys_reg(ctxt, TFSR_EL1) = read_sysreg_el1(SYS_TFSR);
		ctxt_sys_reg(ctxt, TFSRE0_EL1) = read_sysreg_s(SYS_TFSRE0_EL1);
	}

	ctxt_sys_reg(ctxt, SP_EL1)	= read_sysreg(sp_el1);
	ctxt_sys_reg(ctxt, ELR_EL1)	= read_sysreg_el1(SYS_ELR);
	ctxt_sys_reg(ctxt, SPSR_EL1)	= read_sysreg_el1(SYS_SPSR);
}

static inline void __sysreg_save_el2_return_state(struct kvm_cpu_context *ctxt)
{
	ctxt->regs.pc			= read_sysreg_el2(SYS_ELR);
	/*
	 * Guest PSTATE gets saved at guest fixup time in all
	 * cases. We still need to handle the nVHE host side here.
	 */
	if (!has_vhe() && ctxt->__hyp_running_vcpu)
		ctxt->regs.pstate	= read_sysreg_el2(SYS_SPSR);

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN))
		ctxt_sys_reg(ctxt, DISR_EL1) = read_sysreg_s(SYS_VDISR_EL2);
}

static inline void __sysreg_restore_common_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt_sys_reg(ctxt, MDSCR_EL1),  mdscr_el1);
}

static inline void __sysreg_restore_user_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt_sys_reg(ctxt, TPIDR_EL0),	tpidr_el0);
	write_sysreg(ctxt_sys_reg(ctxt, TPIDRRO_EL0),	tpidrro_el0);
}

static inline void __sysreg_restore_el1_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt_sys_reg(ctxt, MPIDR_EL1),	vmpidr_el2);

	if (has_vhe() ||
	    !cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		write_sysreg_el1(ctxt_sys_reg(ctxt, SCTLR_EL1),	SYS_SCTLR);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TCR_EL1),	SYS_TCR);
	} else	if (!ctxt->__hyp_running_vcpu) {
		/*
		 * Must only be done for guest registers, hence the context
		 * test. We're coming from the host, so SCTLR.M is already
		 * set. Pairs with nVHE's __activate_traps().
		 */
		write_sysreg_el1((ctxt_sys_reg(ctxt, TCR_EL1) |
				  TCR_EPD1_MASK | TCR_EPD0_MASK),
				 SYS_TCR);
		isb();
	}

	write_sysreg_el1(ctxt_sys_reg(ctxt, CPACR_EL1),	SYS_CPACR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR0_EL1),	SYS_TTBR0);
	write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR1_EL1),	SYS_TTBR1);
	if (ctxt_has_tcrx(ctxt)) {
		write_sysreg_el1(ctxt_sys_reg(ctxt, TCR2_EL1),	SYS_TCR2);

		if (ctxt_has_s1pie(ctxt)) {
			write_sysreg_el1(ctxt_sys_reg(ctxt, PIR_EL1),	SYS_PIR);
			write_sysreg_el1(ctxt_sys_reg(ctxt, PIRE0_EL1),	SYS_PIRE0);
		}
	}
	write_sysreg_el1(ctxt_sys_reg(ctxt, ESR_EL1),	SYS_ESR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AFSR0_EL1),	SYS_AFSR0);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AFSR1_EL1),	SYS_AFSR1);
	write_sysreg_el1(ctxt_sys_reg(ctxt, FAR_EL1),	SYS_FAR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, MAIR_EL1),	SYS_MAIR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, VBAR_EL1),	SYS_VBAR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, CONTEXTIDR_EL1), SYS_CONTEXTIDR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AMAIR_EL1),	SYS_AMAIR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, CNTKCTL_EL1), SYS_CNTKCTL);
	write_sysreg(ctxt_sys_reg(ctxt, PAR_EL1),	par_el1);
	write_sysreg(ctxt_sys_reg(ctxt, TPIDR_EL1),	tpidr_el1);

	if (ctxt_has_mte(ctxt)) {
		write_sysreg_el1(ctxt_sys_reg(ctxt, TFSR_EL1), SYS_TFSR);
		write_sysreg_s(ctxt_sys_reg(ctxt, TFSRE0_EL1), SYS_TFSRE0_EL1);
	}

	if (!has_vhe() &&
	    cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT) &&
	    ctxt->__hyp_running_vcpu) {
		/*
		 * Must only be done for host registers, hence the context
		 * test. Pairs with nVHE's __deactivate_traps().
		 */
		isb();
		/*
		 * At this stage, and thanks to the above isb(), S2 is
		 * deconfigured and disabled. We can now restore the host's
		 * S1 configuration: SCTLR, and only then TCR.
		 */
		write_sysreg_el1(ctxt_sys_reg(ctxt, SCTLR_EL1),	SYS_SCTLR);
		isb();
		write_sysreg_el1(ctxt_sys_reg(ctxt, TCR_EL1),	SYS_TCR);
	}

	write_sysreg(ctxt_sys_reg(ctxt, SP_EL1),	sp_el1);
	write_sysreg_el1(ctxt_sys_reg(ctxt, ELR_EL1),	SYS_ELR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, SPSR_EL1),	SYS_SPSR);
}

/* Read the VCPU state's PSTATE, but translate (v)EL2 to EL1. */
static inline u64 to_hw_pstate(const struct kvm_cpu_context *ctxt)
{
	u64 mode = ctxt->regs.pstate & (PSR_MODE_MASK | PSR_MODE32_BIT);

	switch (mode) {
	case PSR_MODE_EL2t:
		mode = PSR_MODE_EL1t;
		break;
	case PSR_MODE_EL2h:
		mode = PSR_MODE_EL1h;
		break;
	}

	return (ctxt->regs.pstate & ~(PSR_MODE_MASK | PSR_MODE32_BIT)) | mode;
}

static inline void __sysreg_restore_el2_return_state(struct kvm_cpu_context *ctxt)
{
	u64 pstate = to_hw_pstate(ctxt);
	u64 mode = pstate & PSR_AA32_MODE_MASK;

	/*
	 * Safety check to ensure we're setting the CPU up to enter the guest
	 * in a less privileged mode.
	 *
	 * If we are attempting a return to EL2 or higher in AArch64 state,
	 * program SPSR_EL2 with M=EL2h and the IL bit set which ensures that
	 * we'll take an illegal exception state exception immediately after
	 * the ERET to the guest.  Attempts to return to AArch32 Hyp will
	 * result in an illegal exception return because EL2's execution state
	 * is determined by SCR_EL3.RW.
	 */
	if (!(mode & PSR_MODE32_BIT) && mode >= PSR_MODE_EL2t)
		pstate = PSR_MODE_EL2h | PSR_IL_BIT;

	write_sysreg_el2(ctxt->regs.pc,			SYS_ELR);
	write_sysreg_el2(pstate,			SYS_SPSR);

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN))
		write_sysreg_s(ctxt_sys_reg(ctxt, DISR_EL1), SYS_VDISR_EL2);
}

static inline void __sysreg32_save_state(struct kvm_vcpu *vcpu)
{
	if (!vcpu_el1_is_32bit(vcpu))
		return;

	vcpu->arch.ctxt.spsr_abt = read_sysreg(spsr_abt);
	vcpu->arch.ctxt.spsr_und = read_sysreg(spsr_und);
	vcpu->arch.ctxt.spsr_irq = read_sysreg(spsr_irq);
	vcpu->arch.ctxt.spsr_fiq = read_sysreg(spsr_fiq);

	__vcpu_sys_reg(vcpu, DACR32_EL2) = read_sysreg(dacr32_el2);
	__vcpu_sys_reg(vcpu, IFSR32_EL2) = read_sysreg(ifsr32_el2);

	if (has_vhe() || vcpu_get_flag(vcpu, DEBUG_DIRTY))
		__vcpu_sys_reg(vcpu, DBGVCR32_EL2) = read_sysreg(dbgvcr32_el2);
}

static inline void __sysreg32_restore_state(struct kvm_vcpu *vcpu)
{
	if (!vcpu_el1_is_32bit(vcpu))
		return;

	write_sysreg(vcpu->arch.ctxt.spsr_abt, spsr_abt);
	write_sysreg(vcpu->arch.ctxt.spsr_und, spsr_und);
	write_sysreg(vcpu->arch.ctxt.spsr_irq, spsr_irq);
	write_sysreg(vcpu->arch.ctxt.spsr_fiq, spsr_fiq);

	write_sysreg(__vcpu_sys_reg(vcpu, DACR32_EL2), dacr32_el2);
	write_sysreg(__vcpu_sys_reg(vcpu, IFSR32_EL2), ifsr32_el2);

	if (has_vhe() || vcpu_get_flag(vcpu, DEBUG_DIRTY))
		write_sysreg(__vcpu_sys_reg(vcpu, DBGVCR32_EL2), dbgvcr32_el2);
}

#endif /* __ARM64_KVM_HYP_SYSREG_SR_H__ */

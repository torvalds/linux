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

static inline void __sysreg_save_common_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[MDSCR_EL1]	= read_sysreg(mdscr_el1);
}

static inline void __sysreg_save_user_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[TPIDR_EL0]	= read_sysreg(tpidr_el0);
	ctxt->sys_regs[TPIDRRO_EL0]	= read_sysreg(tpidrro_el0);
}

static inline void __sysreg_save_el1_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[CSSELR_EL1]	= read_sysreg(csselr_el1);
	ctxt->sys_regs[SCTLR_EL1]	= read_sysreg_el1(SYS_SCTLR);
	ctxt->sys_regs[CPACR_EL1]	= read_sysreg_el1(SYS_CPACR);
	ctxt->sys_regs[TTBR0_EL1]	= read_sysreg_el1(SYS_TTBR0);
	ctxt->sys_regs[TTBR1_EL1]	= read_sysreg_el1(SYS_TTBR1);
	ctxt->sys_regs[TCR_EL1]		= read_sysreg_el1(SYS_TCR);
	ctxt->sys_regs[ESR_EL1]		= read_sysreg_el1(SYS_ESR);
	ctxt->sys_regs[AFSR0_EL1]	= read_sysreg_el1(SYS_AFSR0);
	ctxt->sys_regs[AFSR1_EL1]	= read_sysreg_el1(SYS_AFSR1);
	ctxt->sys_regs[FAR_EL1]		= read_sysreg_el1(SYS_FAR);
	ctxt->sys_regs[MAIR_EL1]	= read_sysreg_el1(SYS_MAIR);
	ctxt->sys_regs[VBAR_EL1]	= read_sysreg_el1(SYS_VBAR);
	ctxt->sys_regs[CONTEXTIDR_EL1]	= read_sysreg_el1(SYS_CONTEXTIDR);
	ctxt->sys_regs[AMAIR_EL1]	= read_sysreg_el1(SYS_AMAIR);
	ctxt->sys_regs[CNTKCTL_EL1]	= read_sysreg_el1(SYS_CNTKCTL);
	ctxt->sys_regs[PAR_EL1]		= read_sysreg(par_el1);
	ctxt->sys_regs[TPIDR_EL1]	= read_sysreg(tpidr_el1);

	ctxt->gp_regs.sp_el1		= read_sysreg(sp_el1);
	ctxt->gp_regs.elr_el1		= read_sysreg_el1(SYS_ELR);
	ctxt->gp_regs.spsr[KVM_SPSR_EL1]= read_sysreg_el1(SYS_SPSR);
}

static inline void __sysreg_save_el2_return_state(struct kvm_cpu_context *ctxt)
{
	ctxt->gp_regs.regs.pc		= read_sysreg_el2(SYS_ELR);
	ctxt->gp_regs.regs.pstate	= read_sysreg_el2(SYS_SPSR);

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN))
		ctxt->sys_regs[DISR_EL1] = read_sysreg_s(SYS_VDISR_EL2);
}

static inline void __sysreg_restore_common_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[MDSCR_EL1],	  mdscr_el1);
}

static inline void __sysreg_restore_user_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[TPIDR_EL0],		tpidr_el0);
	write_sysreg(ctxt->sys_regs[TPIDRRO_EL0],	tpidrro_el0);
}

static inline void __sysreg_restore_el1_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[MPIDR_EL1],		vmpidr_el2);
	write_sysreg(ctxt->sys_regs[CSSELR_EL1],	csselr_el1);

	if (has_vhe() ||
	    !cpus_have_final_cap(ARM64_WORKAROUND_SPECULATIVE_AT)) {
		write_sysreg_el1(ctxt->sys_regs[SCTLR_EL1],	SYS_SCTLR);
		write_sysreg_el1(ctxt->sys_regs[TCR_EL1],	SYS_TCR);
	} else	if (!ctxt->__hyp_running_vcpu) {
		/*
		 * Must only be done for guest registers, hence the context
		 * test. We're coming from the host, so SCTLR.M is already
		 * set. Pairs with nVHE's __activate_traps().
		 */
		write_sysreg_el1((ctxt->sys_regs[TCR_EL1] |
				  TCR_EPD1_MASK | TCR_EPD0_MASK),
				 SYS_TCR);
		isb();
	}

	write_sysreg_el1(ctxt->sys_regs[CPACR_EL1],	SYS_CPACR);
	write_sysreg_el1(ctxt->sys_regs[TTBR0_EL1],	SYS_TTBR0);
	write_sysreg_el1(ctxt->sys_regs[TTBR1_EL1],	SYS_TTBR1);
	write_sysreg_el1(ctxt->sys_regs[ESR_EL1],	SYS_ESR);
	write_sysreg_el1(ctxt->sys_regs[AFSR0_EL1],	SYS_AFSR0);
	write_sysreg_el1(ctxt->sys_regs[AFSR1_EL1],	SYS_AFSR1);
	write_sysreg_el1(ctxt->sys_regs[FAR_EL1],	SYS_FAR);
	write_sysreg_el1(ctxt->sys_regs[MAIR_EL1],	SYS_MAIR);
	write_sysreg_el1(ctxt->sys_regs[VBAR_EL1],	SYS_VBAR);
	write_sysreg_el1(ctxt->sys_regs[CONTEXTIDR_EL1],SYS_CONTEXTIDR);
	write_sysreg_el1(ctxt->sys_regs[AMAIR_EL1],	SYS_AMAIR);
	write_sysreg_el1(ctxt->sys_regs[CNTKCTL_EL1],	SYS_CNTKCTL);
	write_sysreg(ctxt->sys_regs[PAR_EL1],		par_el1);
	write_sysreg(ctxt->sys_regs[TPIDR_EL1],		tpidr_el1);

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
		write_sysreg_el1(ctxt->sys_regs[SCTLR_EL1],	SYS_SCTLR);
		isb();
		write_sysreg_el1(ctxt->sys_regs[TCR_EL1],	SYS_TCR);
	}

	write_sysreg(ctxt->gp_regs.sp_el1,		sp_el1);
	write_sysreg_el1(ctxt->gp_regs.elr_el1,		SYS_ELR);
	write_sysreg_el1(ctxt->gp_regs.spsr[KVM_SPSR_EL1],SYS_SPSR);
}

static inline void __sysreg_restore_el2_return_state(struct kvm_cpu_context *ctxt)
{
	u64 pstate = ctxt->gp_regs.regs.pstate;
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

	write_sysreg_el2(ctxt->gp_regs.regs.pc,		SYS_ELR);
	write_sysreg_el2(pstate,			SYS_SPSR);

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN))
		write_sysreg_s(ctxt->sys_regs[DISR_EL1], SYS_VDISR_EL2);
}

static inline void __sysreg32_save_state(struct kvm_vcpu *vcpu)
{
	u64 *spsr, *sysreg;

	if (!vcpu_el1_is_32bit(vcpu))
		return;

	spsr = vcpu->arch.ctxt.gp_regs.spsr;
	sysreg = vcpu->arch.ctxt.sys_regs;

	spsr[KVM_SPSR_ABT] = read_sysreg(spsr_abt);
	spsr[KVM_SPSR_UND] = read_sysreg(spsr_und);
	spsr[KVM_SPSR_IRQ] = read_sysreg(spsr_irq);
	spsr[KVM_SPSR_FIQ] = read_sysreg(spsr_fiq);

	sysreg[DACR32_EL2] = read_sysreg(dacr32_el2);
	sysreg[IFSR32_EL2] = read_sysreg(ifsr32_el2);

	if (has_vhe() || vcpu->arch.flags & KVM_ARM64_DEBUG_DIRTY)
		sysreg[DBGVCR32_EL2] = read_sysreg(dbgvcr32_el2);
}

static inline void __sysreg32_restore_state(struct kvm_vcpu *vcpu)
{
	u64 *spsr, *sysreg;

	if (!vcpu_el1_is_32bit(vcpu))
		return;

	spsr = vcpu->arch.ctxt.gp_regs.spsr;
	sysreg = vcpu->arch.ctxt.sys_regs;

	write_sysreg(spsr[KVM_SPSR_ABT], spsr_abt);
	write_sysreg(spsr[KVM_SPSR_UND], spsr_und);
	write_sysreg(spsr[KVM_SPSR_IRQ], spsr_irq);
	write_sysreg(spsr[KVM_SPSR_FIQ], spsr_fiq);

	write_sysreg(sysreg[DACR32_EL2], dacr32_el2);
	write_sysreg(sysreg[IFSR32_EL2], ifsr32_el2);

	if (has_vhe() || vcpu->arch.flags & KVM_ARM64_DEBUG_DIRTY)
		write_sysreg(sysreg[DBGVCR32_EL2], dbgvcr32_el2);
}

#endif /* __ARM64_KVM_HYP_SYSREG_SR_H__ */

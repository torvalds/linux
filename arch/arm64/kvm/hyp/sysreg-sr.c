// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

/*
 * Non-VHE: Both host and guest must save everything.
 *
 * VHE: Host and guest must save mdscr_el1 and sp_el0 (and the PC and pstate,
 * which are handled as part of the el2 return state) on every switch.
 * tpidr_el0 and tpidrro_el0 only need to be switched when going
 * to host userspace or a different VCPU.  EL1 registers only need to be
 * switched when potentially going to run a different VCPU.  The latter two
 * classes are handled as part of kvm_arch_vcpu_load and kvm_arch_vcpu_put.
 */

static void __hyp_text __sysreg_save_common_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[MDSCR_EL1]	= read_sysreg(mdscr_el1);

	/*
	 * The host arm64 Linux uses sp_el0 to point to 'current' and it must
	 * therefore be saved/restored on every entry/exit to/from the guest.
	 */
	ctxt->gp_regs.regs.sp		= read_sysreg(sp_el0);
}

static void __hyp_text __sysreg_save_user_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[TPIDR_EL0]	= read_sysreg(tpidr_el0);
	ctxt->sys_regs[TPIDRRO_EL0]	= read_sysreg(tpidrro_el0);
}

static void __hyp_text __sysreg_save_el1_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[CSSELR_EL1]	= read_sysreg(csselr_el1);
	ctxt->sys_regs[SCTLR_EL1]	= read_sysreg_el1(sctlr);
	ctxt->sys_regs[ACTLR_EL1]	= read_sysreg(actlr_el1);
	ctxt->sys_regs[CPACR_EL1]	= read_sysreg_el1(cpacr);
	ctxt->sys_regs[TTBR0_EL1]	= read_sysreg_el1(ttbr0);
	ctxt->sys_regs[TTBR1_EL1]	= read_sysreg_el1(ttbr1);
	ctxt->sys_regs[TCR_EL1]		= read_sysreg_el1(tcr);
	ctxt->sys_regs[ESR_EL1]		= read_sysreg_el1(esr);
	ctxt->sys_regs[AFSR0_EL1]	= read_sysreg_el1(afsr0);
	ctxt->sys_regs[AFSR1_EL1]	= read_sysreg_el1(afsr1);
	ctxt->sys_regs[FAR_EL1]		= read_sysreg_el1(far);
	ctxt->sys_regs[MAIR_EL1]	= read_sysreg_el1(mair);
	ctxt->sys_regs[VBAR_EL1]	= read_sysreg_el1(vbar);
	ctxt->sys_regs[CONTEXTIDR_EL1]	= read_sysreg_el1(contextidr);
	ctxt->sys_regs[AMAIR_EL1]	= read_sysreg_el1(amair);
	ctxt->sys_regs[CNTKCTL_EL1]	= read_sysreg_el1(cntkctl);
	ctxt->sys_regs[PAR_EL1]		= read_sysreg(par_el1);
	ctxt->sys_regs[TPIDR_EL1]	= read_sysreg(tpidr_el1);

	ctxt->gp_regs.sp_el1		= read_sysreg(sp_el1);
	ctxt->gp_regs.elr_el1		= read_sysreg_el1(elr);
	ctxt->gp_regs.spsr[KVM_SPSR_EL1]= read_sysreg_el1(spsr);
}

static void __hyp_text __sysreg_save_el2_return_state(struct kvm_cpu_context *ctxt)
{
	ctxt->gp_regs.regs.pc		= read_sysreg_el2(elr);
	ctxt->gp_regs.regs.pstate	= read_sysreg_el2(spsr);

	if (cpus_have_const_cap(ARM64_HAS_RAS_EXTN))
		ctxt->sys_regs[DISR_EL1] = read_sysreg_s(SYS_VDISR_EL2);
}

void __hyp_text __sysreg_save_state_nvhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_el1_state(ctxt);
	__sysreg_save_common_state(ctxt);
	__sysreg_save_user_state(ctxt);
	__sysreg_save_el2_return_state(ctxt);
}

void sysreg_save_host_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_common_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_save_host_state_vhe);

void sysreg_save_guest_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_common_state(ctxt);
	__sysreg_save_el2_return_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_save_guest_state_vhe);

static void __hyp_text __sysreg_restore_common_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[MDSCR_EL1],	  mdscr_el1);

	/*
	 * The host arm64 Linux uses sp_el0 to point to 'current' and it must
	 * therefore be saved/restored on every entry/exit to/from the guest.
	 */
	write_sysreg(ctxt->gp_regs.regs.sp,	  sp_el0);
}

static void __hyp_text __sysreg_restore_user_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[TPIDR_EL0],	  	tpidr_el0);
	write_sysreg(ctxt->sys_regs[TPIDRRO_EL0], 	tpidrro_el0);
}

static void __hyp_text __sysreg_restore_el1_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[MPIDR_EL1],		vmpidr_el2);
	write_sysreg(ctxt->sys_regs[CSSELR_EL1],	csselr_el1);
	write_sysreg_el1(ctxt->sys_regs[SCTLR_EL1],	sctlr);
	write_sysreg(ctxt->sys_regs[ACTLR_EL1],	  	actlr_el1);
	write_sysreg_el1(ctxt->sys_regs[CPACR_EL1],	cpacr);
	write_sysreg_el1(ctxt->sys_regs[TTBR0_EL1],	ttbr0);
	write_sysreg_el1(ctxt->sys_regs[TTBR1_EL1],	ttbr1);
	write_sysreg_el1(ctxt->sys_regs[TCR_EL1],	tcr);
	write_sysreg_el1(ctxt->sys_regs[ESR_EL1],	esr);
	write_sysreg_el1(ctxt->sys_regs[AFSR0_EL1],	afsr0);
	write_sysreg_el1(ctxt->sys_regs[AFSR1_EL1],	afsr1);
	write_sysreg_el1(ctxt->sys_regs[FAR_EL1],	far);
	write_sysreg_el1(ctxt->sys_regs[MAIR_EL1],	mair);
	write_sysreg_el1(ctxt->sys_regs[VBAR_EL1],	vbar);
	write_sysreg_el1(ctxt->sys_regs[CONTEXTIDR_EL1],contextidr);
	write_sysreg_el1(ctxt->sys_regs[AMAIR_EL1],	amair);
	write_sysreg_el1(ctxt->sys_regs[CNTKCTL_EL1], 	cntkctl);
	write_sysreg(ctxt->sys_regs[PAR_EL1],		par_el1);
	write_sysreg(ctxt->sys_regs[TPIDR_EL1],		tpidr_el1);

	write_sysreg(ctxt->gp_regs.sp_el1,		sp_el1);
	write_sysreg_el1(ctxt->gp_regs.elr_el1,		elr);
	write_sysreg_el1(ctxt->gp_regs.spsr[KVM_SPSR_EL1],spsr);
}

static void __hyp_text
__sysreg_restore_el2_return_state(struct kvm_cpu_context *ctxt)
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

	write_sysreg_el2(ctxt->gp_regs.regs.pc,		elr);
	write_sysreg_el2(pstate,			spsr);

	if (cpus_have_const_cap(ARM64_HAS_RAS_EXTN))
		write_sysreg_s(ctxt->sys_regs[DISR_EL1], SYS_VDISR_EL2);
}

void __hyp_text __sysreg_restore_state_nvhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_el1_state(ctxt);
	__sysreg_restore_common_state(ctxt);
	__sysreg_restore_user_state(ctxt);
	__sysreg_restore_el2_return_state(ctxt);
}

void sysreg_restore_host_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_common_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_restore_host_state_vhe);

void sysreg_restore_guest_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_common_state(ctxt);
	__sysreg_restore_el2_return_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_restore_guest_state_vhe);

void __hyp_text __sysreg32_save_state(struct kvm_vcpu *vcpu)
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

void __hyp_text __sysreg32_restore_state(struct kvm_vcpu *vcpu)
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

/**
 * kvm_vcpu_load_sysregs - Load guest system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Load system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_load() function
 * and loading system register state early avoids having to load them on
 * every entry to the VM.
 */
void kvm_vcpu_load_sysregs(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt = vcpu->arch.host_cpu_context;
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;

	if (!has_vhe())
		return;

	__sysreg_save_user_state(host_ctxt);

	/*
	 * Load guest EL1 and user state
	 *
	 * We must restore the 32-bit state before the sysregs, thanks
	 * to erratum #852523 (Cortex-A57) or #853709 (Cortex-A72).
	 */
	__sysreg32_restore_state(vcpu);
	__sysreg_restore_user_state(guest_ctxt);
	__sysreg_restore_el1_state(guest_ctxt);

	vcpu->arch.sysregs_loaded_on_cpu = true;

	activate_traps_vhe_load(vcpu);
}

/**
 * kvm_vcpu_put_sysregs - Restore host system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Save guest system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_put() function
 * and deferring saving system register state until we're no longer running the
 * VCPU avoids having to save them on every exit from the VM.
 */
void kvm_vcpu_put_sysregs(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt = vcpu->arch.host_cpu_context;
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;

	if (!has_vhe())
		return;

	deactivate_traps_vhe_put();

	__sysreg_save_el1_state(guest_ctxt);
	__sysreg_save_user_state(guest_ctxt);
	__sysreg32_save_state(vcpu);

	/* Restore host user state */
	__sysreg_restore_user_state(host_ctxt);

	vcpu->arch.sysregs_loaded_on_cpu = false;
}

void __hyp_text __kvm_enable_ssbs(void)
{
	u64 tmp;

	asm volatile(
	"mrs	%0, sctlr_el2\n"
	"orr	%0, %0, %1\n"
	"msr	sctlr_el2, %0"
	: "=&r" (tmp) : "L" (SCTLR_ELx_DSSBS));
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/sysreg-sr.h>

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_nested.h>

static void __sysreg_save_vel2_state(struct kvm_vcpu *vcpu)
{
	/* These registers are common with EL1 */
	__vcpu_assign_sys_reg(vcpu, PAR_EL1,	 read_sysreg(par_el1));
	__vcpu_assign_sys_reg(vcpu, TPIDR_EL1,	 read_sysreg(tpidr_el1));

	__vcpu_assign_sys_reg(vcpu, ESR_EL2,	 read_sysreg_el1(SYS_ESR));
	__vcpu_assign_sys_reg(vcpu, AFSR0_EL2,	 read_sysreg_el1(SYS_AFSR0));
	__vcpu_assign_sys_reg(vcpu, AFSR1_EL2,	 read_sysreg_el1(SYS_AFSR1));
	__vcpu_assign_sys_reg(vcpu, FAR_EL2,	 read_sysreg_el1(SYS_FAR));
	__vcpu_assign_sys_reg(vcpu, MAIR_EL2,	 read_sysreg_el1(SYS_MAIR));
	__vcpu_assign_sys_reg(vcpu, VBAR_EL2,	 read_sysreg_el1(SYS_VBAR));
	__vcpu_assign_sys_reg(vcpu, CONTEXTIDR_EL2, read_sysreg_el1(SYS_CONTEXTIDR));
	__vcpu_assign_sys_reg(vcpu, AMAIR_EL2,	 read_sysreg_el1(SYS_AMAIR));

	/*
	 * In VHE mode those registers are compatible between EL1 and EL2,
	 * and the guest uses the _EL1 versions on the CPU naturally.
	 * So we save them into their _EL2 versions here.
	 * For nVHE mode we trap accesses to those registers, so our
	 * _EL2 copy in sys_regs[] is always up-to-date and we don't need
	 * to save anything here.
	 */
	if (vcpu_el2_e2h_is_set(vcpu)) {
		u64 val;

		/*
		 * We don't save CPTR_EL2, as accesses to CPACR_EL1
		 * are always trapped, ensuring that the in-memory
		 * copy is always up-to-date. A small blessing...
		 */
		__vcpu_assign_sys_reg(vcpu, SCTLR_EL2,	 read_sysreg_el1(SYS_SCTLR));
		__vcpu_assign_sys_reg(vcpu, TTBR0_EL2,	 read_sysreg_el1(SYS_TTBR0));
		__vcpu_assign_sys_reg(vcpu, TTBR1_EL2,	 read_sysreg_el1(SYS_TTBR1));
		__vcpu_assign_sys_reg(vcpu, TCR_EL2,	 read_sysreg_el1(SYS_TCR));

		if (ctxt_has_tcrx(&vcpu->arch.ctxt)) {
			__vcpu_assign_sys_reg(vcpu, TCR2_EL2, read_sysreg_el1(SYS_TCR2));

			if (ctxt_has_s1pie(&vcpu->arch.ctxt)) {
				__vcpu_assign_sys_reg(vcpu, PIRE0_EL2, read_sysreg_el1(SYS_PIRE0));
				__vcpu_assign_sys_reg(vcpu, PIR_EL2, read_sysreg_el1(SYS_PIR));
			}

			if (ctxt_has_s1poe(&vcpu->arch.ctxt))
				__vcpu_assign_sys_reg(vcpu, POR_EL2, read_sysreg_el1(SYS_POR));
		}

		/*
		 * The EL1 view of CNTKCTL_EL1 has a bunch of RES0 bits where
		 * the interesting CNTHCTL_EL2 bits live. So preserve these
		 * bits when reading back the guest-visible value.
		 */
		val = read_sysreg_el1(SYS_CNTKCTL);
		val &= CNTKCTL_VALID_BITS;
		__vcpu_rmw_sys_reg(vcpu, CNTHCTL_EL2, &=, ~CNTKCTL_VALID_BITS);
		__vcpu_rmw_sys_reg(vcpu, CNTHCTL_EL2, |=, val);
	}

	__vcpu_assign_sys_reg(vcpu, SP_EL2,	 read_sysreg(sp_el1));
	__vcpu_assign_sys_reg(vcpu, ELR_EL2,	 read_sysreg_el1(SYS_ELR));
	__vcpu_assign_sys_reg(vcpu, SPSR_EL2,	 read_sysreg_el1(SYS_SPSR));
}

static void __sysreg_restore_vel2_state(struct kvm_vcpu *vcpu)
{
	u64 val;

	/* These registers are common with EL1 */
	write_sysreg(__vcpu_sys_reg(vcpu, PAR_EL1),	par_el1);
	write_sysreg(__vcpu_sys_reg(vcpu, TPIDR_EL1),	tpidr_el1);

	write_sysreg(ctxt_midr_el1(&vcpu->arch.ctxt),			vpidr_el2);
	write_sysreg(__vcpu_sys_reg(vcpu, MPIDR_EL1),			vmpidr_el2);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, MAIR_EL2),		SYS_MAIR);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, VBAR_EL2),		SYS_VBAR);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, CONTEXTIDR_EL2),		SYS_CONTEXTIDR);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, AMAIR_EL2),		SYS_AMAIR);

	if (vcpu_el2_e2h_is_set(vcpu)) {
		/*
		 * In VHE mode those registers are compatible between
		 * EL1 and EL2.
		 */
		write_sysreg_el1(__vcpu_sys_reg(vcpu, SCTLR_EL2),   SYS_SCTLR);
		write_sysreg_el1(__vcpu_sys_reg(vcpu, CPTR_EL2),    SYS_CPACR);
		write_sysreg_el1(__vcpu_sys_reg(vcpu, TTBR0_EL2),   SYS_TTBR0);
		write_sysreg_el1(__vcpu_sys_reg(vcpu, TTBR1_EL2),   SYS_TTBR1);
		write_sysreg_el1(__vcpu_sys_reg(vcpu, TCR_EL2),	    SYS_TCR);
		write_sysreg_el1(__vcpu_sys_reg(vcpu, CNTHCTL_EL2), SYS_CNTKCTL);
	} else {
		/*
		 * CNTHCTL_EL2 only affects EL1 when running nVHE, so
		 * no need to restore it.
		 */
		val = translate_sctlr_el2_to_sctlr_el1(__vcpu_sys_reg(vcpu, SCTLR_EL2));
		write_sysreg_el1(val, SYS_SCTLR);
		val = translate_cptr_el2_to_cpacr_el1(__vcpu_sys_reg(vcpu, CPTR_EL2));
		write_sysreg_el1(val, SYS_CPACR);
		val = translate_ttbr0_el2_to_ttbr0_el1(__vcpu_sys_reg(vcpu, TTBR0_EL2));
		write_sysreg_el1(val, SYS_TTBR0);
		val = translate_tcr_el2_to_tcr_el1(__vcpu_sys_reg(vcpu, TCR_EL2));
		write_sysreg_el1(val, SYS_TCR);
	}

	if (ctxt_has_tcrx(&vcpu->arch.ctxt)) {
		write_sysreg_el1(__vcpu_sys_reg(vcpu, TCR2_EL2), SYS_TCR2);

		if (ctxt_has_s1pie(&vcpu->arch.ctxt)) {
			write_sysreg_el1(__vcpu_sys_reg(vcpu, PIR_EL2), SYS_PIR);
			write_sysreg_el1(__vcpu_sys_reg(vcpu, PIRE0_EL2), SYS_PIRE0);
		}

		if (ctxt_has_s1poe(&vcpu->arch.ctxt))
			write_sysreg_el1(__vcpu_sys_reg(vcpu, POR_EL2), SYS_POR);
	}

	write_sysreg_el1(__vcpu_sys_reg(vcpu, ESR_EL2),		SYS_ESR);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, AFSR0_EL2),	SYS_AFSR0);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, AFSR1_EL2),	SYS_AFSR1);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, FAR_EL2),		SYS_FAR);
	write_sysreg(__vcpu_sys_reg(vcpu, SP_EL2),		sp_el1);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, ELR_EL2),		SYS_ELR);
	write_sysreg_el1(__vcpu_sys_reg(vcpu, SPSR_EL2),	SYS_SPSR);
}

/*
 * VHE: Host and guest must save mdscr_el1 and sp_el0 (and the PC and
 * pstate, which are handled as part of the el2 return state) on every
 * switch (sp_el0 is being dealt with in the assembly code).
 * tpidr_el0 and tpidrro_el0 only need to be switched when going
 * to host userspace or a different VCPU.  EL1 registers only need to be
 * switched when potentially going to run a different VCPU.  The latter two
 * classes are handled as part of kvm_arch_vcpu_load and kvm_arch_vcpu_put.
 */

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

/**
 * __vcpu_load_switch_sysregs - Load guest system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Load system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_load() function
 * and loading system register state early avoids having to load them on
 * every entry to the VM.
 */
void __vcpu_load_switch_sysregs(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;
	struct kvm_cpu_context *host_ctxt;
	u64 midr, mpidr;

	host_ctxt = host_data_ptr(host_ctxt);
	__sysreg_save_user_state(host_ctxt);

	/*
	 * When running a normal EL1 guest, we only load a new vcpu
	 * after a context switch, which imvolves a DSB, so all
	 * speculative EL1&0 walks will have already completed.
	 * If running NV, the vcpu may transition between vEL1 and
	 * vEL2 without a context switch, so make sure we complete
	 * those walks before loading a new context.
	 */
	if (vcpu_has_nv(vcpu))
		dsb(nsh);

	/*
	 * Load guest EL1 and user state
	 *
	 * We must restore the 32-bit state before the sysregs, thanks
	 * to erratum #852523 (Cortex-A57) or #853709 (Cortex-A72).
	 */
	__sysreg32_restore_state(vcpu);
	__sysreg_restore_user_state(guest_ctxt);

	if (unlikely(is_hyp_ctxt(vcpu))) {
		__sysreg_restore_vel2_state(vcpu);
	} else {
		if (vcpu_has_nv(vcpu)) {
			/*
			 * As we're restoring a nested guest, set the value
			 * provided by the guest hypervisor.
			 */
			midr = ctxt_sys_reg(guest_ctxt, VPIDR_EL2);
			mpidr = ctxt_sys_reg(guest_ctxt, VMPIDR_EL2);
		} else {
			midr = ctxt_midr_el1(guest_ctxt);
			mpidr = ctxt_sys_reg(guest_ctxt, MPIDR_EL1);
		}

		__sysreg_restore_el1_state(guest_ctxt, midr, mpidr);
	}

	vcpu_set_flag(vcpu, SYSREGS_ON_CPU);
}

/**
 * __vcpu_put_switch_sysregs - Restore host system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Save guest system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_put() function
 * and deferring saving system register state until we're no longer running the
 * VCPU avoids having to save them on every exit from the VM.
 */
void __vcpu_put_switch_sysregs(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;
	struct kvm_cpu_context *host_ctxt;

	host_ctxt = host_data_ptr(host_ctxt);

	if (unlikely(is_hyp_ctxt(vcpu)))
		__sysreg_save_vel2_state(vcpu);
	else
		__sysreg_save_el1_state(guest_ctxt);

	__sysreg_save_user_state(guest_ctxt);
	__sysreg32_save_state(vcpu);

	/* Restore host user state */
	__sysreg_restore_user_state(host_ctxt);

	vcpu_clear_flag(vcpu, SYSREGS_ON_CPU);
}

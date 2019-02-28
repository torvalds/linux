/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <uapi/linux/psci.h>

#include <kvm/arm_psci.h>

#include <asm/cpufeature.h>
#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/fpsimd.h>
#include <asm/debug-monitors.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

/* Check whether the FP regs were dirtied while in the host-side run loop: */
static bool __hyp_text update_fp_enabled(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.host_thread_info->flags & _TIF_FOREIGN_FPSTATE)
		vcpu->arch.flags &= ~(KVM_ARM64_FP_ENABLED |
				      KVM_ARM64_FP_HOST);

	return !!(vcpu->arch.flags & KVM_ARM64_FP_ENABLED);
}

/* Save the 32-bit only FPSIMD system register state */
static void __hyp_text __fpsimd_save_fpexc32(struct kvm_vcpu *vcpu)
{
	if (!vcpu_el1_is_32bit(vcpu))
		return;

	vcpu->arch.ctxt.sys_regs[FPEXC32_EL2] = read_sysreg(fpexc32_el2);
}

static void __hyp_text __activate_traps_fpsimd32(struct kvm_vcpu *vcpu)
{
	/*
	 * We are about to set CPTR_EL2.TFP to trap all floating point
	 * register accesses to EL2, however, the ARM ARM clearly states that
	 * traps are only taken to EL2 if the operation would not otherwise
	 * trap to EL1.  Therefore, always make sure that for 32-bit guests,
	 * we set FPEXC.EN to prevent traps to EL1, when setting the TFP bit.
	 * If FP/ASIMD is not implemented, FPEXC is UNDEFINED and any access to
	 * it will cause an exception.
	 */
	if (vcpu_el1_is_32bit(vcpu) && system_supports_fpsimd()) {
		write_sysreg(1 << 30, fpexc32_el2);
		isb();
	}
}

static void __hyp_text __activate_traps_common(struct kvm_vcpu *vcpu)
{
	/* Trap on AArch32 cp15 c15 (impdef sysregs) accesses (EL1 or EL0) */
	write_sysreg(1 << 15, hstr_el2);

	/*
	 * Make sure we trap PMU access from EL0 to EL2. Also sanitize
	 * PMSELR_EL0 to make sure it never contains the cycle
	 * counter, which could make a PMXEVCNTR_EL0 access UNDEF at
	 * EL1 instead of being trapped to EL2.
	 */
	write_sysreg(0, pmselr_el0);
	write_sysreg(ARMV8_PMU_USERENR_MASK, pmuserenr_el0);
	write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);
}

static void __hyp_text __deactivate_traps_common(void)
{
	write_sysreg(0, hstr_el2);
	write_sysreg(0, pmuserenr_el0);
}

static void activate_traps_vhe(struct kvm_vcpu *vcpu)
{
	u64 val;

	val = read_sysreg(cpacr_el1);
	val |= CPACR_EL1_TTA;
	val &= ~CPACR_EL1_ZEN;
	if (!update_fp_enabled(vcpu)) {
		val &= ~CPACR_EL1_FPEN;
		__activate_traps_fpsimd32(vcpu);
	}

	write_sysreg(val, cpacr_el1);

	write_sysreg(kvm_get_hyp_vector(), vbar_el1);
}
NOKPROBE_SYMBOL(activate_traps_vhe);

static void __hyp_text __activate_traps_nvhe(struct kvm_vcpu *vcpu)
{
	u64 val;

	__activate_traps_common(vcpu);

	val = CPTR_EL2_DEFAULT;
	val |= CPTR_EL2_TTA | CPTR_EL2_TZ;
	if (!update_fp_enabled(vcpu)) {
		val |= CPTR_EL2_TFP;
		__activate_traps_fpsimd32(vcpu);
	}

	write_sysreg(val, cptr_el2);
}

static void __hyp_text __activate_traps(struct kvm_vcpu *vcpu)
{
	u64 hcr = vcpu->arch.hcr_el2;

	write_sysreg(hcr, hcr_el2);

	if (cpus_have_const_cap(ARM64_HAS_RAS_EXTN) && (hcr & HCR_VSE))
		write_sysreg_s(vcpu->arch.vsesr_el2, SYS_VSESR_EL2);

	if (has_vhe())
		activate_traps_vhe(vcpu);
	else
		__activate_traps_nvhe(vcpu);
}

static void deactivate_traps_vhe(void)
{
	extern char vectors[];	/* kernel exception vectors */
	write_sysreg(HCR_HOST_VHE_FLAGS, hcr_el2);

	/*
	 * ARM erratum 1165522 requires the actual execution of the above
	 * before we can switch to the EL2/EL0 translation regime used by
	 * the host.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_1165522));

	write_sysreg(CPACR_EL1_DEFAULT, cpacr_el1);
	write_sysreg(vectors, vbar_el1);
}
NOKPROBE_SYMBOL(deactivate_traps_vhe);

static void __hyp_text __deactivate_traps_nvhe(void)
{
	u64 mdcr_el2 = read_sysreg(mdcr_el2);

	__deactivate_traps_common();

	mdcr_el2 &= MDCR_EL2_HPMN_MASK;
	mdcr_el2 |= MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT;

	write_sysreg(mdcr_el2, mdcr_el2);
	write_sysreg(HCR_HOST_NVHE_FLAGS, hcr_el2);
	write_sysreg(CPTR_EL2_DEFAULT, cptr_el2);
}

static void __hyp_text __deactivate_traps(struct kvm_vcpu *vcpu)
{
	/*
	 * If we pended a virtual abort, preserve it until it gets
	 * cleared. See D1.14.3 (Virtual Interrupts) for details, but
	 * the crucial bit is "On taking a vSError interrupt,
	 * HCR_EL2.VSE is cleared to 0."
	 */
	if (vcpu->arch.hcr_el2 & HCR_VSE)
		vcpu->arch.hcr_el2 = read_sysreg(hcr_el2);

	if (has_vhe())
		deactivate_traps_vhe();
	else
		__deactivate_traps_nvhe();
}

void activate_traps_vhe_load(struct kvm_vcpu *vcpu)
{
	__activate_traps_common(vcpu);
}

void deactivate_traps_vhe_put(void)
{
	u64 mdcr_el2 = read_sysreg(mdcr_el2);

	mdcr_el2 &= MDCR_EL2_HPMN_MASK |
		    MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT |
		    MDCR_EL2_TPMS;

	write_sysreg(mdcr_el2, mdcr_el2);

	__deactivate_traps_common();
}

static void __hyp_text __activate_vm(struct kvm *kvm)
{
	__load_guest_stage2(kvm);
}

static void __hyp_text __deactivate_vm(struct kvm_vcpu *vcpu)
{
	write_sysreg(0, vttbr_el2);
}

/* Save VGICv3 state on non-VHE systems */
static void __hyp_text __hyp_vgic_save_state(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif)) {
		__vgic_v3_save_state(vcpu);
		__vgic_v3_deactivate_traps(vcpu);
	}
}

/* Restore VGICv3 state on non_VEH systems */
static void __hyp_text __hyp_vgic_restore_state(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif)) {
		__vgic_v3_activate_traps(vcpu);
		__vgic_v3_restore_state(vcpu);
	}
}

static bool __hyp_text __true_value(void)
{
	return true;
}

static bool __hyp_text __false_value(void)
{
	return false;
}

static hyp_alternate_select(__check_arm_834220,
			    __false_value, __true_value,
			    ARM64_WORKAROUND_834220);

static bool __hyp_text __translate_far_to_hpfar(u64 far, u64 *hpfar)
{
	u64 par, tmp;

	/*
	 * Resolve the IPA the hard way using the guest VA.
	 *
	 * Stage-1 translation already validated the memory access
	 * rights. As such, we can use the EL1 translation regime, and
	 * don't have to distinguish between EL0 and EL1 access.
	 *
	 * We do need to save/restore PAR_EL1 though, as we haven't
	 * saved the guest context yet, and we may return early...
	 */
	par = read_sysreg(par_el1);
	asm volatile("at s1e1r, %0" : : "r" (far));
	isb();

	tmp = read_sysreg(par_el1);
	write_sysreg(par, par_el1);

	if (unlikely(tmp & 1))
		return false; /* Translation failed, back to guest */

	/* Convert PAR to HPFAR format */
	*hpfar = PAR_TO_HPFAR(tmp);
	return true;
}

static bool __hyp_text __populate_fault_info(struct kvm_vcpu *vcpu)
{
	u8 ec;
	u64 esr;
	u64 hpfar, far;

	esr = vcpu->arch.fault.esr_el2;
	ec = ESR_ELx_EC(esr);

	if (ec != ESR_ELx_EC_DABT_LOW && ec != ESR_ELx_EC_IABT_LOW)
		return true;

	far = read_sysreg_el2(far);

	/*
	 * The HPFAR can be invalid if the stage 2 fault did not
	 * happen during a stage 1 page table walk (the ESR_EL2.S1PTW
	 * bit is clear) and one of the two following cases are true:
	 *   1. The fault was due to a permission fault
	 *   2. The processor carries errata 834220
	 *
	 * Therefore, for all non S1PTW faults where we either have a
	 * permission fault or the errata workaround is enabled, we
	 * resolve the IPA using the AT instruction.
	 */
	if (!(esr & ESR_ELx_S1PTW) &&
	    (__check_arm_834220()() || (esr & ESR_ELx_FSC_TYPE) == FSC_PERM)) {
		if (!__translate_far_to_hpfar(far, &hpfar))
			return false;
	} else {
		hpfar = read_sysreg(hpfar_el2);
	}

	vcpu->arch.fault.far_el2 = far;
	vcpu->arch.fault.hpfar_el2 = hpfar;
	return true;
}

static bool __hyp_text __hyp_switch_fpsimd(struct kvm_vcpu *vcpu)
{
	struct user_fpsimd_state *host_fpsimd = vcpu->arch.host_fpsimd_state;

	if (has_vhe())
		write_sysreg(read_sysreg(cpacr_el1) | CPACR_EL1_FPEN,
			     cpacr_el1);
	else
		write_sysreg(read_sysreg(cptr_el2) & ~(u64)CPTR_EL2_TFP,
			     cptr_el2);

	isb();

	if (vcpu->arch.flags & KVM_ARM64_FP_HOST) {
		/*
		 * In the SVE case, VHE is assumed: it is enforced by
		 * Kconfig and kvm_arch_init().
		 */
		if (system_supports_sve() &&
		    (vcpu->arch.flags & KVM_ARM64_HOST_SVE_IN_USE)) {
			struct thread_struct *thread = container_of(
				host_fpsimd,
				struct thread_struct, uw.fpsimd_state);

			sve_save_state(sve_pffr(thread), &host_fpsimd->fpsr);
		} else {
			__fpsimd_save_state(host_fpsimd);
		}

		vcpu->arch.flags &= ~KVM_ARM64_FP_HOST;
	}

	__fpsimd_restore_state(&vcpu->arch.ctxt.gp_regs.fp_regs);

	/* Skip restoring fpexc32 for AArch64 guests */
	if (!(read_sysreg(hcr_el2) & HCR_RW))
		write_sysreg(vcpu->arch.ctxt.sys_regs[FPEXC32_EL2],
			     fpexc32_el2);

	vcpu->arch.flags |= KVM_ARM64_FP_ENABLED;

	return true;
}

/*
 * Return true when we were able to fixup the guest exit and should return to
 * the guest, false when we should restore the host state and return to the
 * main run loop.
 */
static bool __hyp_text fixup_guest_exit(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	if (ARM_EXCEPTION_CODE(*exit_code) != ARM_EXCEPTION_IRQ)
		vcpu->arch.fault.esr_el2 = read_sysreg_el2(esr);

	/*
	 * We're using the raw exception code in order to only process
	 * the trap if no SError is pending. We will come back to the
	 * same PC once the SError has been injected, and replay the
	 * trapping instruction.
	 */
	if (*exit_code != ARM_EXCEPTION_TRAP)
		goto exit;

	/*
	 * We trap the first access to the FP/SIMD to save the host context
	 * and restore the guest context lazily.
	 * If FP/SIMD is not implemented, handle the trap and inject an
	 * undefined instruction exception to the guest.
	 */
	if (system_supports_fpsimd() &&
	    kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_FP_ASIMD)
		return __hyp_switch_fpsimd(vcpu);

	if (!__populate_fault_info(vcpu))
		return true;

	if (static_branch_unlikely(&vgic_v2_cpuif_trap)) {
		bool valid;

		valid = kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_DABT_LOW &&
			kvm_vcpu_trap_get_fault_type(vcpu) == FSC_FAULT &&
			kvm_vcpu_dabt_isvalid(vcpu) &&
			!kvm_vcpu_dabt_isextabt(vcpu) &&
			!kvm_vcpu_dabt_iss1tw(vcpu);

		if (valid) {
			int ret = __vgic_v2_perform_cpuif_access(vcpu);

			if (ret == 1)
				return true;

			/* Promote an illegal access to an SError.*/
			if (ret == -1)
				*exit_code = ARM_EXCEPTION_EL1_SERROR;

			goto exit;
		}
	}

	if (static_branch_unlikely(&vgic_v3_cpuif_trap) &&
	    (kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_SYS64 ||
	     kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_CP15_32)) {
		int ret = __vgic_v3_perform_cpuif_access(vcpu);

		if (ret == 1)
			return true;
	}

exit:
	/* Return to the host kernel and handle the exit */
	return false;
}

static inline bool __hyp_text __needs_ssbd_off(struct kvm_vcpu *vcpu)
{
	if (!cpus_have_const_cap(ARM64_SSBD))
		return false;

	return !(vcpu->arch.workaround_flags & VCPU_WORKAROUND_2_FLAG);
}

static void __hyp_text __set_guest_arch_workaround_state(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_ARM64_SSBD
	/*
	 * The host runs with the workaround always present. If the
	 * guest wants it disabled, so be it...
	 */
	if (__needs_ssbd_off(vcpu) &&
	    __hyp_this_cpu_read(arm64_ssbd_callback_required))
		arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2, 0, NULL);
#endif
}

static void __hyp_text __set_host_arch_workaround_state(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_ARM64_SSBD
	/*
	 * If the guest has disabled the workaround, bring it back on.
	 */
	if (__needs_ssbd_off(vcpu) &&
	    __hyp_this_cpu_read(arm64_ssbd_callback_required))
		arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2, 1, NULL);
#endif
}

/* Switch to the guest for VHE systems running in EL2 */
int kvm_vcpu_run_vhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	u64 exit_code;

	host_ctxt = vcpu->arch.host_cpu_context;
	host_ctxt->__hyp_running_vcpu = vcpu;
	guest_ctxt = &vcpu->arch.ctxt;

	sysreg_save_host_state_vhe(host_ctxt);

	/*
	 * ARM erratum 1165522 requires us to configure both stage 1 and
	 * stage 2 translation for the guest context before we clear
	 * HCR_EL2.TGE.
	 *
	 * We have already configured the guest's stage 1 translation in
	 * kvm_vcpu_load_sysregs above.  We must now call __activate_vm
	 * before __activate_traps, because __activate_vm configures
	 * stage 2 translation, and __activate_traps clear HCR_EL2.TGE
	 * (among other things).
	 */
	__activate_vm(vcpu->kvm);
	__activate_traps(vcpu);

	sysreg_restore_guest_state_vhe(guest_ctxt);
	__debug_switch_to_guest(vcpu);

	__set_guest_arch_workaround_state(vcpu);

	do {
		/* Jump in the fire! */
		exit_code = __guest_enter(vcpu, host_ctxt);

		/* And we're baaack! */
	} while (fixup_guest_exit(vcpu, &exit_code));

	__set_host_arch_workaround_state(vcpu);

	sysreg_save_guest_state_vhe(guest_ctxt);

	__deactivate_traps(vcpu);

	sysreg_restore_host_state_vhe(host_ctxt);

	if (vcpu->arch.flags & KVM_ARM64_FP_ENABLED)
		__fpsimd_save_fpexc32(vcpu);

	__debug_switch_to_host(vcpu);

	return exit_code;
}
NOKPROBE_SYMBOL(kvm_vcpu_run_vhe);

/* Switch to the guest for legacy non-VHE systems */
int __hyp_text __kvm_vcpu_run_nvhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	u64 exit_code;

	vcpu = kern_hyp_va(vcpu);

	host_ctxt = kern_hyp_va(vcpu->arch.host_cpu_context);
	host_ctxt->__hyp_running_vcpu = vcpu;
	guest_ctxt = &vcpu->arch.ctxt;

	__sysreg_save_state_nvhe(host_ctxt);

	__activate_vm(kern_hyp_va(vcpu->kvm));
	__activate_traps(vcpu);

	__hyp_vgic_restore_state(vcpu);
	__timer_enable_traps(vcpu);

	/*
	 * We must restore the 32-bit state before the sysregs, thanks
	 * to erratum #852523 (Cortex-A57) or #853709 (Cortex-A72).
	 */
	__sysreg32_restore_state(vcpu);
	__sysreg_restore_state_nvhe(guest_ctxt);
	__debug_switch_to_guest(vcpu);

	__set_guest_arch_workaround_state(vcpu);

	do {
		/* Jump in the fire! */
		exit_code = __guest_enter(vcpu, host_ctxt);

		/* And we're baaack! */
	} while (fixup_guest_exit(vcpu, &exit_code));

	__set_host_arch_workaround_state(vcpu);

	__sysreg_save_state_nvhe(guest_ctxt);
	__sysreg32_save_state(vcpu);
	__timer_disable_traps(vcpu);
	__hyp_vgic_save_state(vcpu);

	__deactivate_traps(vcpu);
	__deactivate_vm(vcpu);

	__sysreg_restore_state_nvhe(host_ctxt);

	if (vcpu->arch.flags & KVM_ARM64_FP_ENABLED)
		__fpsimd_save_fpexc32(vcpu);

	/*
	 * This must come after restoring the host sysregs, since a non-VHE
	 * system may enable SPE here and make use of the TTBRs.
	 */
	__debug_switch_to_host(vcpu);

	return exit_code;
}

static const char __hyp_panic_string[] = "HYP panic:\nPS:%08llx PC:%016llx ESR:%08llx\nFAR:%016llx HPFAR:%016llx PAR:%016llx\nVCPU:%p\n";

static void __hyp_text __hyp_call_panic_nvhe(u64 spsr, u64 elr, u64 par,
					     struct kvm_cpu_context *__host_ctxt)
{
	struct kvm_vcpu *vcpu;
	unsigned long str_va;

	vcpu = __host_ctxt->__hyp_running_vcpu;

	if (read_sysreg(vttbr_el2)) {
		__timer_disable_traps(vcpu);
		__deactivate_traps(vcpu);
		__deactivate_vm(vcpu);
		__sysreg_restore_state_nvhe(__host_ctxt);
	}

	/*
	 * Force the panic string to be loaded from the literal pool,
	 * making sure it is a kernel address and not a PC-relative
	 * reference.
	 */
	asm volatile("ldr %0, =__hyp_panic_string" : "=r" (str_va));

	__hyp_do_panic(str_va,
		       spsr,  elr,
		       read_sysreg(esr_el2),   read_sysreg_el2(far),
		       read_sysreg(hpfar_el2), par, vcpu);
}

static void __hyp_call_panic_vhe(u64 spsr, u64 elr, u64 par,
				 struct kvm_cpu_context *host_ctxt)
{
	struct kvm_vcpu *vcpu;
	vcpu = host_ctxt->__hyp_running_vcpu;

	__deactivate_traps(vcpu);
	sysreg_restore_host_state_vhe(host_ctxt);

	panic(__hyp_panic_string,
	      spsr,  elr,
	      read_sysreg_el2(esr),   read_sysreg_el2(far),
	      read_sysreg(hpfar_el2), par, vcpu);
}
NOKPROBE_SYMBOL(__hyp_call_panic_vhe);

void __hyp_text __noreturn hyp_panic(struct kvm_cpu_context *host_ctxt)
{
	u64 spsr = read_sysreg_el2(spsr);
	u64 elr = read_sysreg_el2(elr);
	u64 par = read_sysreg(par_el1);

	if (!has_vhe())
		__hyp_call_panic_nvhe(spsr, elr, par, host_ctxt);
	else
		__hyp_call_panic_vhe(spsr, elr, par, host_ctxt);

	unreachable();
}

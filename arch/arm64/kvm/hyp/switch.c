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

#include "hyp.h"

static void __hyp_text __activate_traps(struct kvm_vcpu *vcpu)
{
	u64 val;

	/*
	 * We are about to set CPTR_EL2.TFP to trap all floating point
	 * register accesses to EL2, however, the ARM ARM clearly states that
	 * traps are only taken to EL2 if the operation would not otherwise
	 * trap to EL1.  Therefore, always make sure that for 32-bit guests,
	 * we set FPEXC.EN to prevent traps to EL1, when setting the TFP bit.
	 */
	val = vcpu->arch.hcr_el2;
	if (!(val & HCR_RW)) {
		write_sysreg(1 << 30, fpexc32_el2);
		isb();
	}
	write_sysreg(val, hcr_el2);
	/* Trap on AArch32 cp15 c15 accesses (EL1 or EL0) */
	write_sysreg(1 << 15, hstr_el2);
	write_sysreg(CPTR_EL2_TTA | CPTR_EL2_TFP, cptr_el2);
	write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);
}

static void __hyp_text __deactivate_traps(struct kvm_vcpu *vcpu)
{
	write_sysreg(HCR_RW, hcr_el2);
	write_sysreg(0, hstr_el2);
	write_sysreg(read_sysreg(mdcr_el2) & MDCR_EL2_HPMN_MASK, mdcr_el2);
	write_sysreg(0, cptr_el2);
}

static void __hyp_text __activate_vm(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	write_sysreg(kvm->arch.vttbr, vttbr_el2);
}

static void __hyp_text __deactivate_vm(struct kvm_vcpu *vcpu)
{
	write_sysreg(0, vttbr_el2);
}

static hyp_alternate_select(__vgic_call_save_state,
			    __vgic_v2_save_state, __vgic_v3_save_state,
			    ARM64_HAS_SYSREG_GIC_CPUIF);

static hyp_alternate_select(__vgic_call_restore_state,
			    __vgic_v2_restore_state, __vgic_v3_restore_state,
			    ARM64_HAS_SYSREG_GIC_CPUIF);

static void __hyp_text __vgic_save_state(struct kvm_vcpu *vcpu)
{
	__vgic_call_save_state()(vcpu);
	write_sysreg(read_sysreg(hcr_el2) & ~HCR_INT_OVERRIDE, hcr_el2);
}

static void __hyp_text __vgic_restore_state(struct kvm_vcpu *vcpu)
{
	u64 val;

	val = read_sysreg(hcr_el2);
	val |= 	HCR_INT_OVERRIDE;
	val |= vcpu->arch.irq_lines;
	write_sysreg(val, hcr_el2);

	__vgic_call_restore_state()(vcpu);
}

int __hyp_text __guest_run(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	bool fp_enabled;
	u64 exit_code;

	vcpu = kern_hyp_va(vcpu);
	write_sysreg(vcpu, tpidr_el2);

	host_ctxt = kern_hyp_va(vcpu->arch.host_cpu_context);
	guest_ctxt = &vcpu->arch.ctxt;

	__sysreg_save_state(host_ctxt);
	__debug_cond_save_host_state(vcpu);

	__activate_traps(vcpu);
	__activate_vm(vcpu);

	__vgic_restore_state(vcpu);
	__timer_restore_state(vcpu);

	/*
	 * We must restore the 32-bit state before the sysregs, thanks
	 * to Cortex-A57 erratum #852523.
	 */
	__sysreg32_restore_state(vcpu);
	__sysreg_restore_state(guest_ctxt);
	__debug_restore_state(vcpu, kern_hyp_va(vcpu->arch.debug_ptr), guest_ctxt);

	/* Jump in the fire! */
	exit_code = __guest_enter(vcpu, host_ctxt);
	/* And we're baaack! */

	fp_enabled = __fpsimd_enabled();

	__sysreg_save_state(guest_ctxt);
	__sysreg32_save_state(vcpu);
	__timer_save_state(vcpu);
	__vgic_save_state(vcpu);

	__deactivate_traps(vcpu);
	__deactivate_vm(vcpu);

	__sysreg_restore_state(host_ctxt);

	if (fp_enabled) {
		__fpsimd_save_state(&guest_ctxt->gp_regs.fp_regs);
		__fpsimd_restore_state(&host_ctxt->gp_regs.fp_regs);
	}

	__debug_save_state(vcpu, kern_hyp_va(vcpu->arch.debug_ptr), guest_ctxt);
	__debug_cond_restore_host_state(vcpu);

	return exit_code;
}

__alias(__guest_run)
int __weak __kvm_vcpu_run(struct kvm_vcpu *vcpu);

static const char __hyp_panic_string[] = "HYP panic:\nPS:%08llx PC:%016llx ESR:%08llx\nFAR:%016llx HPFAR:%016llx PAR:%016llx\nVCPU:%p\n";

void __hyp_text __noreturn __hyp_panic(void)
{
	unsigned long str_va = (unsigned long)__hyp_panic_string;
	u64 spsr = read_sysreg(spsr_el2);
	u64 elr = read_sysreg(elr_el2);
	u64 par = read_sysreg(par_el1);

	if (read_sysreg(vttbr_el2)) {
		struct kvm_vcpu *vcpu;
		struct kvm_cpu_context *host_ctxt;

		vcpu = (struct kvm_vcpu *)read_sysreg(tpidr_el2);
		host_ctxt = kern_hyp_va(vcpu->arch.host_cpu_context);
		__deactivate_traps(vcpu);
		__deactivate_vm(vcpu);
		__sysreg_restore_state(host_ctxt);
	}

	/* Call panic for real */
	__hyp_do_panic(hyp_kern_va(str_va),
		       spsr,  elr,
		       read_sysreg(esr_el2),   read_sysreg(far_el2),
		       read_sysreg(hpfar_el2), par,
		       (void *)read_sysreg(tpidr_el2));

	unreachable();
}

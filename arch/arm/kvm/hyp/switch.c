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

#include <asm/kvm_asm.h>
#include "hyp.h"

__asm__(".arch_extension     virt");

/*
 * Activate the traps, saving the host's fpexc register before
 * overwriting it. We'll restore it on VM exit.
 */
static void __hyp_text __activate_traps(struct kvm_vcpu *vcpu, u32 *fpexc_host)
{
	u32 val;

	/*
	 * We are about to set HCPTR.TCP10/11 to trap all floating point
	 * register accesses to HYP, however, the ARM ARM clearly states that
	 * traps are only taken to HYP if the operation would not otherwise
	 * trap to SVC.  Therefore, always make sure that for 32-bit guests,
	 * we set FPEXC.EN to prevent traps to SVC, when setting the TCP bits.
	 */
	val = read_sysreg(VFP_FPEXC);
	*fpexc_host = val;
	if (!(val & FPEXC_EN)) {
		write_sysreg(val | FPEXC_EN, VFP_FPEXC);
		isb();
	}

	write_sysreg(vcpu->arch.hcr | vcpu->arch.irq_lines, HCR);
	/* Trap on AArch32 cp15 c15 accesses (EL1 or EL0) */
	write_sysreg(HSTR_T(15), HSTR);
	write_sysreg(HCPTR_TTA | HCPTR_TCP(10) | HCPTR_TCP(11), HCPTR);
	val = read_sysreg(HDCR);
	write_sysreg(val | HDCR_TPM | HDCR_TPMCR, HDCR);
}

static void __hyp_text __deactivate_traps(struct kvm_vcpu *vcpu)
{
	u32 val;

	write_sysreg(0, HCR);
	write_sysreg(0, HSTR);
	val = read_sysreg(HDCR);
	write_sysreg(val & ~(HDCR_TPM | HDCR_TPMCR), HDCR);
	write_sysreg(0, HCPTR);
}

static void __hyp_text __activate_vm(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	write_sysreg(kvm->arch.vttbr, VTTBR);
	write_sysreg(vcpu->arch.midr, VPIDR);
}

static void __hyp_text __deactivate_vm(struct kvm_vcpu *vcpu)
{
	write_sysreg(0, VTTBR);
	write_sysreg(read_sysreg(MIDR), VPIDR);
}

static void __hyp_text __vgic_save_state(struct kvm_vcpu *vcpu)
{
	__vgic_v2_save_state(vcpu);
}

static void __hyp_text __vgic_restore_state(struct kvm_vcpu *vcpu)
{
	__vgic_v2_restore_state(vcpu);
}

static int __hyp_text __guest_run(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	bool fp_enabled;
	u64 exit_code;
	u32 fpexc;

	vcpu = kern_hyp_va(vcpu);
	write_sysreg(vcpu, HTPIDR);

	host_ctxt = kern_hyp_va(vcpu->arch.host_cpu_context);
	guest_ctxt = &vcpu->arch.ctxt;

	__sysreg_save_state(host_ctxt);
	__banked_save_state(host_ctxt);

	__activate_traps(vcpu, &fpexc);
	__activate_vm(vcpu);

	__vgic_restore_state(vcpu);
	__timer_restore_state(vcpu);

	__sysreg_restore_state(guest_ctxt);
	__banked_restore_state(guest_ctxt);

	/* Jump in the fire! */
	exit_code = __guest_enter(vcpu, host_ctxt);
	/* And we're baaack! */

	fp_enabled = __vfp_enabled();

	__banked_save_state(guest_ctxt);
	__sysreg_save_state(guest_ctxt);
	__timer_save_state(vcpu);
	__vgic_save_state(vcpu);

	__deactivate_traps(vcpu);
	__deactivate_vm(vcpu);

	__banked_restore_state(host_ctxt);
	__sysreg_restore_state(host_ctxt);

	if (fp_enabled) {
		__vfp_save_state(&guest_ctxt->vfp);
		__vfp_restore_state(&host_ctxt->vfp);
	}

	write_sysreg(fpexc, VFP_FPEXC);

	return exit_code;
}

__alias(__guest_run) int __weak __kvm_vcpu_run(struct kvm_vcpu *vcpu);

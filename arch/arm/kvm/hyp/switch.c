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
#include <linux/jump_label.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

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

	write_sysreg(vcpu->arch.hcr, HCR);
	/* Trap on AArch32 cp15 c15 accesses (EL1 or EL0) */
	write_sysreg(HSTR_T(15), HSTR);
	write_sysreg(HCPTR_TTA | HCPTR_TCP(10) | HCPTR_TCP(11), HCPTR);
	val = read_sysreg(HDCR);
	val |= HDCR_TPM | HDCR_TPMCR; /* trap performance monitors */
	val |= HDCR_TDRA | HDCR_TDOSA | HDCR_TDA; /* trap debug regs */
	write_sysreg(val, HDCR);
}

static void __hyp_text __deactivate_traps(struct kvm_vcpu *vcpu)
{
	u32 val;

	/*
	 * If we pended a virtual abort, preserve it until it gets
	 * cleared. See B1.9.9 (Virtual Abort exception) for details,
	 * but the crucial bit is the zeroing of HCR.VA in the
	 * pseudocode.
	 */
	if (vcpu->arch.hcr & HCR_VA)
		vcpu->arch.hcr = read_sysreg(HCR);

	write_sysreg(0, HCR);
	write_sysreg(0, HSTR);
	val = read_sysreg(HDCR);
	write_sysreg(val & ~(HDCR_TPM | HDCR_TPMCR), HDCR);
	write_sysreg(0, HCPTR);
}

static void __hyp_text __activate_vm(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	write_sysreg(kvm_get_vttbr(kvm), VTTBR);
	write_sysreg(vcpu->arch.midr, VPIDR);
}

static void __hyp_text __deactivate_vm(struct kvm_vcpu *vcpu)
{
	write_sysreg(0, VTTBR);
	write_sysreg(read_sysreg(MIDR), VPIDR);
}


static void __hyp_text __vgic_save_state(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif)) {
		__vgic_v3_save_state(vcpu);
		__vgic_v3_deactivate_traps(vcpu);
	}
}

static void __hyp_text __vgic_restore_state(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif)) {
		__vgic_v3_activate_traps(vcpu);
		__vgic_v3_restore_state(vcpu);
	}
}

static bool __hyp_text __populate_fault_info(struct kvm_vcpu *vcpu)
{
	u32 hsr = read_sysreg(HSR);
	u8 ec = hsr >> HSR_EC_SHIFT;
	u32 hpfar, far;

	vcpu->arch.fault.hsr = hsr;

	if (ec == HSR_EC_IABT)
		far = read_sysreg(HIFAR);
	else if (ec == HSR_EC_DABT)
		far = read_sysreg(HDFAR);
	else
		return true;

	/*
	 * B3.13.5 Reporting exceptions taken to the Non-secure PL2 mode:
	 *
	 * Abort on the stage 2 translation for a memory access from a
	 * Non-secure PL1 or PL0 mode:
	 *
	 * For any Access flag fault or Translation fault, and also for any
	 * Permission fault on the stage 2 translation of a memory access
	 * made as part of a translation table walk for a stage 1 translation,
	 * the HPFAR holds the IPA that caused the fault. Otherwise, the HPFAR
	 * is UNKNOWN.
	 */
	if (!(hsr & HSR_DABT_S1PTW) && (hsr & HSR_FSC_TYPE) == FSC_PERM) {
		u64 par, tmp;

		par = read_sysreg(PAR);
		write_sysreg(far, ATS1CPR);
		isb();

		tmp = read_sysreg(PAR);
		write_sysreg(par, PAR);

		if (unlikely(tmp & 1))
			return false; /* Translation failed, back to guest */

		hpfar = ((tmp >> 12) & ((1UL << 28) - 1)) << 4;
	} else {
		hpfar = read_sysreg(HPFAR);
	}

	vcpu->arch.fault.hxfar = far;
	vcpu->arch.fault.hpfar = hpfar;
	return true;
}

int __hyp_text __kvm_vcpu_run_nvhe(struct kvm_vcpu *vcpu)
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
	__timer_enable_traps(vcpu);

	__sysreg_restore_state(guest_ctxt);
	__banked_restore_state(guest_ctxt);

	/* Jump in the fire! */
again:
	exit_code = __guest_enter(vcpu, host_ctxt);
	/* And we're baaack! */

	if (exit_code == ARM_EXCEPTION_HVC && !__populate_fault_info(vcpu))
		goto again;

	fp_enabled = __vfp_enabled();

	__banked_save_state(guest_ctxt);
	__sysreg_save_state(guest_ctxt);
	__timer_disable_traps(vcpu);

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

static const char * const __hyp_panic_string[] = {
	[ARM_EXCEPTION_RESET]      = "\nHYP panic: RST   PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_UNDEFINED]  = "\nHYP panic: UNDEF PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_SOFTWARE]   = "\nHYP panic: SVC   PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_PREF_ABORT] = "\nHYP panic: PABRT PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_DATA_ABORT] = "\nHYP panic: DABRT PC:%08x ADDR:%08x",
	[ARM_EXCEPTION_IRQ]        = "\nHYP panic: IRQ   PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_FIQ]        = "\nHYP panic: FIQ   PC:%08x CPSR:%08x",
	[ARM_EXCEPTION_HVC]        = "\nHYP panic: HVC   PC:%08x CPSR:%08x",
};

void __hyp_text __noreturn __hyp_panic(int cause)
{
	u32 elr = read_special(ELR_hyp);
	u32 val;

	if (cause == ARM_EXCEPTION_DATA_ABORT)
		val = read_sysreg(HDFAR);
	else
		val = read_special(SPSR);

	if (read_sysreg(VTTBR)) {
		struct kvm_vcpu *vcpu;
		struct kvm_cpu_context *host_ctxt;

		vcpu = (struct kvm_vcpu *)read_sysreg(HTPIDR);
		host_ctxt = kern_hyp_va(vcpu->arch.host_cpu_context);
		__timer_disable_traps(vcpu);
		__deactivate_traps(vcpu);
		__deactivate_vm(vcpu);
		__banked_restore_state(host_ctxt);
		__sysreg_restore_state(host_ctxt);
	}

	/* Call panic for real */
	__hyp_do_panic(__hyp_panic_string[cause], elr, val);

	unreachable();
}

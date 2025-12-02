/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_X86_VMX_COMMON_H
#define __KVM_X86_VMX_COMMON_H

#include <linux/kvm_host.h>
#include <asm/posted_intr.h>

#include "mmu.h"

union vmx_exit_reason {
	struct {
		u32	basic			: 16;
		u32	reserved16		: 1;
		u32	reserved17		: 1;
		u32	reserved18		: 1;
		u32	reserved19		: 1;
		u32	reserved20		: 1;
		u32	reserved21		: 1;
		u32	reserved22		: 1;
		u32	reserved23		: 1;
		u32	reserved24		: 1;
		u32	reserved25		: 1;
		u32	bus_lock_detected	: 1;
		u32	enclave_mode		: 1;
		u32	smi_pending_mtf		: 1;
		u32	smi_from_vmx_root	: 1;
		u32	reserved30		: 1;
		u32	failed_vmentry		: 1;
	};
	u32 full;
};

struct vcpu_vt {
	/* Posted interrupt descriptor */
	struct pi_desc pi_desc;

	/* Used if this vCPU is waiting for PI notification wakeup. */
	struct list_head pi_wakeup_list;

	union vmx_exit_reason exit_reason;

	unsigned long	exit_qualification;
	u32		exit_intr_info;

	/*
	 * If true, guest state has been loaded into hardware, and host state
	 * saved into vcpu_{vt,vmx,tdx}.  If false, host state is loaded into
	 * hardware.
	 */
	bool		guest_state_loaded;
	bool		emulation_required;

#ifdef CONFIG_X86_64
	u64		msr_host_kernel_gs_base;
#endif
};

#ifdef CONFIG_KVM_INTEL_TDX

static __always_inline bool is_td(struct kvm *kvm)
{
	return kvm->arch.vm_type == KVM_X86_TDX_VM;
}

static __always_inline bool is_td_vcpu(struct kvm_vcpu *vcpu)
{
	return is_td(vcpu->kvm);
}

#else

static __always_inline bool is_td(struct kvm *kvm) { return false; }
static __always_inline bool is_td_vcpu(struct kvm_vcpu *vcpu) { return false; }

#endif

static inline bool vt_is_tdx_private_gpa(struct kvm *kvm, gpa_t gpa)
{
	/* For TDX the direct mask is the shared mask. */
	return !kvm_is_addr_direct(kvm, gpa);
}

static inline int __vmx_handle_ept_violation(struct kvm_vcpu *vcpu, gpa_t gpa,
					     unsigned long exit_qualification)
{
	u64 error_code;

	/* Is it a read fault? */
	error_code = (exit_qualification & EPT_VIOLATION_ACC_READ)
		     ? PFERR_USER_MASK : 0;
	/* Is it a write fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_WRITE)
		      ? PFERR_WRITE_MASK : 0;
	/* Is it a fetch fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_INSTR)
		      ? PFERR_FETCH_MASK : 0;
	/* ept page table entry is present? */
	error_code |= (exit_qualification & EPT_VIOLATION_PROT_MASK)
		      ? PFERR_PRESENT_MASK : 0;

	if (exit_qualification & EPT_VIOLATION_GVA_IS_VALID)
		error_code |= (exit_qualification & EPT_VIOLATION_GVA_TRANSLATED) ?
			      PFERR_GUEST_FINAL_MASK : PFERR_GUEST_PAGE_MASK;

	if (vt_is_tdx_private_gpa(vcpu->kvm, gpa))
		error_code |= PFERR_PRIVATE_ACCESS;

	return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}

static inline void kvm_vcpu_trigger_posted_interrupt(struct kvm_vcpu *vcpu,
						     int pi_vec)
{
#ifdef CONFIG_SMP
	if (vcpu->mode == IN_GUEST_MODE) {
		/*
		 * The vector of the virtual has already been set in the PIR.
		 * Send a notification event to deliver the virtual interrupt
		 * unless the vCPU is the currently running vCPU, i.e. the
		 * event is being sent from a fastpath VM-Exit handler, in
		 * which case the PIR will be synced to the vIRR before
		 * re-entering the guest.
		 *
		 * When the target is not the running vCPU, the following
		 * possibilities emerge:
		 *
		 * Case 1: vCPU stays in non-root mode. Sending a notification
		 * event posts the interrupt to the vCPU.
		 *
		 * Case 2: vCPU exits to root mode and is still runnable. The
		 * PIR will be synced to the vIRR before re-entering the guest.
		 * Sending a notification event is ok as the host IRQ handler
		 * will ignore the spurious event.
		 *
		 * Case 3: vCPU exits to root mode and is blocked. vcpu_block()
		 * has already synced PIR to vIRR and never blocks the vCPU if
		 * the vIRR is not empty. Therefore, a blocked vCPU here does
		 * not wait for any requested interrupts in PIR, and sending a
		 * notification event also results in a benign, spurious event.
		 */

		if (vcpu != kvm_get_running_vcpu())
			__apic_send_IPI_mask(get_cpu_mask(vcpu->cpu), pi_vec);
		return;
	}
#endif
	/*
	 * The vCPU isn't in the guest; wake the vCPU in case it is blocking,
	 * otherwise do nothing as KVM will grab the highest priority pending
	 * IRQ via ->sync_pir_to_irr() in vcpu_enter_guest().
	 */
	kvm_vcpu_wake_up(vcpu);
}

/*
 * Post an interrupt to a vCPU's PIR and trigger the vCPU to process the
 * interrupt if necessary.
 */
static inline void __vmx_deliver_posted_interrupt(struct kvm_vcpu *vcpu,
						  struct pi_desc *pi_desc, int vector)
{
	if (pi_test_and_set_pir(vector, pi_desc))
		return;

	/* If a previous notification has sent the IPI, nothing to do.  */
	if (pi_test_and_set_on(pi_desc))
		return;

	/*
	 * The implied barrier in pi_test_and_set_on() pairs with the smp_mb_*()
	 * after setting vcpu->mode in vcpu_enter_guest(), thus the vCPU is
	 * guaranteed to see PID.ON=1 and sync the PIR to IRR if triggering a
	 * posted interrupt "fails" because vcpu->mode != IN_GUEST_MODE.
	 */
	kvm_vcpu_trigger_posted_interrupt(vcpu, POSTED_INTR_VECTOR);
}

noinstr void vmx_handle_nmi(struct kvm_vcpu *vcpu);

#endif /* __KVM_X86_VMX_COMMON_H */

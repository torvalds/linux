/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_NESTED_H
#define __KVM_X86_VMX_NESTED_H

#include "kvm_cache_regs.h"
#include "vmcs12.h"
#include "vmx.h"

void vmx_leave_nested(struct kvm_vcpu *vcpu);
void nested_vmx_setup_ctls_msrs(struct nested_vmx_msrs *msrs, u32 ept_caps,
				bool apicv);
void nested_vmx_hardware_unsetup(void);
__init int nested_vmx_hardware_setup(int (*exit_handlers[])(struct kvm_vcpu *));
void nested_vmx_vcpu_setup(void);
void nested_vmx_free_vcpu(struct kvm_vcpu *vcpu);
int nested_vmx_enter_non_root_mode(struct kvm_vcpu *vcpu, bool from_vmentry);
bool nested_vmx_exit_reflected(struct kvm_vcpu *vcpu, u32 exit_reason);
void nested_vmx_vmexit(struct kvm_vcpu *vcpu, u32 exit_reason,
		       u32 exit_intr_info, unsigned long exit_qualification);
void nested_sync_from_vmcs12(struct kvm_vcpu *vcpu);
int vmx_set_vmx_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data);
int vmx_get_vmx_msr(struct nested_vmx_msrs *msrs, u32 msr_index, u64 *pdata);
int get_vmx_mem_address(struct kvm_vcpu *vcpu, unsigned long exit_qualification,
			u32 vmx_instruction_info, bool wr, gva_t *ret);

static inline struct vmcs12 *get_vmcs12(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.cached_vmcs12;
}

static inline struct vmcs12 *get_shadow_vmcs12(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.cached_shadow_vmcs12;
}

static inline int vmx_has_valid_vmcs12(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * In case we do two consecutive get/set_nested_state()s while L2 was
	 * running hv_evmcs may end up not being mapped (we map it from
	 * nested_vmx_run()/vmx_vcpu_run()). Check is_guest_mode() as we always
	 * have vmcs12 if it is true.
	 */
	return is_guest_mode(vcpu) || vmx->nested.current_vmptr != -1ull ||
		vmx->nested.hv_evmcs;
}

static inline unsigned long nested_ept_get_cr3(struct kvm_vcpu *vcpu)
{
	/* return the page table to be shadowed - in our case, EPT12 */
	return get_vmcs12(vcpu)->ept_pointer;
}

static inline bool nested_ept_ad_enabled(struct kvm_vcpu *vcpu)
{
	return nested_ept_get_cr3(vcpu) & VMX_EPTP_AD_ENABLE_BIT;
}

/*
 * Reflect a VM Exit into L1.
 */
static inline int nested_vmx_reflect_vmexit(struct kvm_vcpu *vcpu,
					    u32 exit_reason)
{
	u32 exit_intr_info = vmcs_read32(VM_EXIT_INTR_INFO);

	/*
	 * At this point, the exit interruption info in exit_intr_info
	 * is only valid for EXCEPTION_NMI exits.  For EXTERNAL_INTERRUPT
	 * we need to query the in-kernel LAPIC.
	 */
	WARN_ON(exit_reason == EXIT_REASON_EXTERNAL_INTERRUPT);
	if ((exit_intr_info &
	     (INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK)) ==
	    (INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK)) {
		struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

		vmcs12->vm_exit_intr_error_code =
			vmcs_read32(VM_EXIT_INTR_ERROR_CODE);
	}

	nested_vmx_vmexit(vcpu, exit_reason, exit_intr_info,
			  vmcs_readl(EXIT_QUALIFICATION));
	return 1;
}

/*
 * Return the cr0 value that a nested guest would read. This is a combination
 * of the real cr0 used to run the guest (guest_cr0), and the bits shadowed by
 * its hypervisor (cr0_read_shadow).
 */
static inline unsigned long nested_read_cr0(struct vmcs12 *fields)
{
	return (fields->guest_cr0 & ~fields->cr0_guest_host_mask) |
		(fields->cr0_read_shadow & fields->cr0_guest_host_mask);
}
static inline unsigned long nested_read_cr4(struct vmcs12 *fields)
{
	return (fields->guest_cr4 & ~fields->cr4_guest_host_mask) |
		(fields->cr4_read_shadow & fields->cr4_guest_host_mask);
}

static inline unsigned nested_cpu_vmx_misc_cr3_count(struct kvm_vcpu *vcpu)
{
	return vmx_misc_cr3_count(to_vmx(vcpu)->nested.msrs.misc_low);
}

/*
 * Do the virtual VMX capability MSRs specify that L1 can use VMWRITE
 * to modify any valid field of the VMCS, or are the VM-exit
 * information fields read-only?
 */
static inline bool nested_cpu_has_vmwrite_any_field(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.msrs.misc_low &
		MSR_IA32_VMX_MISC_VMWRITE_SHADOW_RO_FIELDS;
}

static inline bool nested_cpu_has_zero_length_injection(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.msrs.misc_low & VMX_MISC_ZERO_LEN_INS;
}

static inline bool nested_cpu_supports_monitor_trap_flag(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.msrs.procbased_ctls_high &
			CPU_BASED_MONITOR_TRAP_FLAG;
}

static inline bool nested_cpu_has_vmx_shadow_vmcs(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.msrs.secondary_ctls_high &
		SECONDARY_EXEC_SHADOW_VMCS;
}

static inline bool nested_cpu_has(struct vmcs12 *vmcs12, u32 bit)
{
	return vmcs12->cpu_based_vm_exec_control & bit;
}

static inline bool nested_cpu_has2(struct vmcs12 *vmcs12, u32 bit)
{
	return (vmcs12->cpu_based_vm_exec_control &
			CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) &&
		(vmcs12->secondary_vm_exec_control & bit);
}

static inline bool nested_cpu_has_preemption_timer(struct vmcs12 *vmcs12)
{
	return vmcs12->pin_based_vm_exec_control &
		PIN_BASED_VMX_PREEMPTION_TIMER;
}

static inline bool nested_cpu_has_nmi_exiting(struct vmcs12 *vmcs12)
{
	return vmcs12->pin_based_vm_exec_control & PIN_BASED_NMI_EXITING;
}

static inline bool nested_cpu_has_virtual_nmis(struct vmcs12 *vmcs12)
{
	return vmcs12->pin_based_vm_exec_control & PIN_BASED_VIRTUAL_NMIS;
}

static inline int nested_cpu_has_ept(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_ENABLE_EPT);
}

static inline bool nested_cpu_has_xsaves(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_XSAVES);
}

static inline bool nested_cpu_has_pml(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_ENABLE_PML);
}

static inline bool nested_cpu_has_virt_x2apic_mode(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE);
}

static inline bool nested_cpu_has_vpid(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_ENABLE_VPID);
}

static inline bool nested_cpu_has_apic_reg_virt(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_APIC_REGISTER_VIRT);
}

static inline bool nested_cpu_has_vid(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);
}

static inline bool nested_cpu_has_posted_intr(struct vmcs12 *vmcs12)
{
	return vmcs12->pin_based_vm_exec_control & PIN_BASED_POSTED_INTR;
}

static inline bool nested_cpu_has_vmfunc(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_ENABLE_VMFUNC);
}

static inline bool nested_cpu_has_eptp_switching(struct vmcs12 *vmcs12)
{
	return nested_cpu_has_vmfunc(vmcs12) &&
		(vmcs12->vm_function_control &
		 VMX_VMFUNC_EPTP_SWITCHING);
}

static inline bool nested_cpu_has_shadow_vmcs(struct vmcs12 *vmcs12)
{
	return nested_cpu_has2(vmcs12, SECONDARY_EXEC_SHADOW_VMCS);
}

static inline bool nested_cpu_has_save_preemption_timer(struct vmcs12 *vmcs12)
{
	return vmcs12->vm_exit_controls &
	    VM_EXIT_SAVE_VMX_PREEMPTION_TIMER;
}

/*
 * In nested virtualization, check if L1 asked to exit on external interrupts.
 * For most existing hypervisors, this will always return true.
 */
static inline bool nested_exit_on_intr(struct kvm_vcpu *vcpu)
{
	return get_vmcs12(vcpu)->pin_based_vm_exec_control &
		PIN_BASED_EXT_INTR_MASK;
}

/*
 * if fixed0[i] == 1: val[i] must be 1
 * if fixed1[i] == 0: val[i] must be 0
 */
static inline bool fixed_bits_valid(u64 val, u64 fixed0, u64 fixed1)
{
	return ((val & fixed1) | fixed0) == val;
}

static bool nested_guest_cr0_valid(struct kvm_vcpu *vcpu, unsigned long val)
{
	u64 fixed0 = to_vmx(vcpu)->nested.msrs.cr0_fixed0;
	u64 fixed1 = to_vmx(vcpu)->nested.msrs.cr0_fixed1;
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	if (to_vmx(vcpu)->nested.msrs.secondary_ctls_high &
		SECONDARY_EXEC_UNRESTRICTED_GUEST &&
	    nested_cpu_has2(vmcs12, SECONDARY_EXEC_UNRESTRICTED_GUEST))
		fixed0 &= ~(X86_CR0_PE | X86_CR0_PG);

	return fixed_bits_valid(val, fixed0, fixed1);
}

static bool nested_host_cr0_valid(struct kvm_vcpu *vcpu, unsigned long val)
{
	u64 fixed0 = to_vmx(vcpu)->nested.msrs.cr0_fixed0;
	u64 fixed1 = to_vmx(vcpu)->nested.msrs.cr0_fixed1;

	return fixed_bits_valid(val, fixed0, fixed1);
}

static bool nested_cr4_valid(struct kvm_vcpu *vcpu, unsigned long val)
{
	u64 fixed0 = to_vmx(vcpu)->nested.msrs.cr4_fixed0;
	u64 fixed1 = to_vmx(vcpu)->nested.msrs.cr4_fixed1;

	return fixed_bits_valid(val, fixed0, fixed1);
}

/* No difference in the restrictions on guest and host CR4 in VMX operation. */
#define nested_guest_cr4_valid	nested_cr4_valid
#define nested_host_cr4_valid	nested_cr4_valid

#endif /* __KVM_X86_VMX_NESTED_H */

// SPDX-License-Identifier: GPL-2.0
#include <linux/moduleparam.h>

#include "x86_ops.h"
#include "vmx.h"
#include "mmu.h"
#include "nested.h"
#include "pmu.h"
#include "posted_intr.h"
#include "tdx.h"
#include "tdx_arch.h"

#ifdef CONFIG_KVM_INTEL_TDX
static_assert(offsetof(struct vcpu_vmx, vt) == offsetof(struct vcpu_tdx, vt));

static void vt_disable_virtualization_cpu(void)
{
	/* Note, TDX *and* VMX need to be disabled if TDX is enabled. */
	if (enable_tdx)
		tdx_disable_virtualization_cpu();
	vmx_disable_virtualization_cpu();
}

static __init int vt_hardware_setup(void)
{
	int ret;

	ret = vmx_hardware_setup();
	if (ret)
		return ret;

	if (enable_tdx)
		tdx_hardware_setup();

	return 0;
}

static int vt_vm_init(struct kvm *kvm)
{
	if (is_td(kvm))
		return tdx_vm_init(kvm);

	return vmx_vm_init(kvm);
}

static void vt_vm_pre_destroy(struct kvm *kvm)
{
	if (is_td(kvm))
		return tdx_mmu_release_hkid(kvm);
}

static void vt_vm_destroy(struct kvm *kvm)
{
	if (is_td(kvm))
               return tdx_vm_destroy(kvm);

       vmx_vm_destroy(kvm);
}

static int vt_vcpu_precreate(struct kvm *kvm)
{
	if (is_td(kvm))
		return 0;

	return vmx_vcpu_precreate(kvm);
}

static int vt_vcpu_create(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return tdx_vcpu_create(vcpu);

	return vmx_vcpu_create(vcpu);
}

static void vt_vcpu_free(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_vcpu_free(vcpu);
		return;
	}

	vmx_vcpu_free(vcpu);
}

static void vt_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	if (is_td_vcpu(vcpu)) {
		tdx_vcpu_reset(vcpu, init_event);
		return;
	}

	vmx_vcpu_reset(vcpu, init_event);
}

static void vt_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_vcpu_load(vcpu, cpu);
		return;
	}

	vmx_vcpu_load(vcpu, cpu);
}

static void vt_update_cpu_dirty_logging(struct kvm_vcpu *vcpu)
{
	/*
	 * Basic TDX does not support feature PML. KVM does not enable PML in
	 * TD's VMCS, nor does it allocate or flush PML buffer for TDX.
	 */
	if (WARN_ON_ONCE(is_td_vcpu(vcpu)))
		return;

	vmx_update_cpu_dirty_logging(vcpu);
}

static void vt_prepare_switch_to_guest(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_prepare_switch_to_guest(vcpu);
		return;
	}

	vmx_prepare_switch_to_guest(vcpu);
}

static void vt_vcpu_put(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_vcpu_put(vcpu);
		return;
	}

	vmx_vcpu_put(vcpu);
}

static int vt_vcpu_pre_run(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return tdx_vcpu_pre_run(vcpu);

	return vmx_vcpu_pre_run(vcpu);
}

static fastpath_t vt_vcpu_run(struct kvm_vcpu *vcpu, u64 run_flags)
{
	if (is_td_vcpu(vcpu))
		return tdx_vcpu_run(vcpu, run_flags);

	return vmx_vcpu_run(vcpu, run_flags);
}

static int vt_handle_exit(struct kvm_vcpu *vcpu,
			  enum exit_fastpath_completion fastpath)
{
	if (is_td_vcpu(vcpu))
		return tdx_handle_exit(vcpu, fastpath);

	return vmx_handle_exit(vcpu, fastpath);
}

static int vt_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	if (unlikely(is_td_vcpu(vcpu)))
		return tdx_set_msr(vcpu, msr_info);

	return vmx_set_msr(vcpu, msr_info);
}

/*
 * The kvm parameter can be NULL (module initialization, or invocation before
 * VM creation). Be sure to check the kvm parameter before using it.
 */
static bool vt_has_emulated_msr(struct kvm *kvm, u32 index)
{
	if (kvm && is_td(kvm))
		return tdx_has_emulated_msr(index);

	return vmx_has_emulated_msr(kvm, index);
}

static int vt_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	if (unlikely(is_td_vcpu(vcpu)))
		return tdx_get_msr(vcpu, msr_info);

	return vmx_get_msr(vcpu, msr_info);
}

static void vt_recalc_intercepts(struct kvm_vcpu *vcpu)
{
	/*
	 * TDX doesn't allow VMM to configure interception of instructions or
	 * MSR accesses.  TDX guest requests MSR accesses by calling TDVMCALL.
	 * The MSR filters will be applied when handling the TDVMCALL for
	 * RDMSR/WRMSR if the userspace has set any.
	 */
	if (is_td_vcpu(vcpu))
		return;

	vmx_recalc_intercepts(vcpu);
}

static int vt_complete_emulated_msr(struct kvm_vcpu *vcpu, int err)
{
	if (is_td_vcpu(vcpu))
		return tdx_complete_emulated_msr(vcpu, err);

	return vmx_complete_emulated_msr(vcpu, err);
}

#ifdef CONFIG_KVM_SMM
static int vt_smi_allowed(struct kvm_vcpu *vcpu, bool for_injection)
{
	if (KVM_BUG_ON(is_td_vcpu(vcpu), vcpu->kvm))
		return 0;

	return vmx_smi_allowed(vcpu, for_injection);
}

static int vt_enter_smm(struct kvm_vcpu *vcpu, union kvm_smram *smram)
{
	if (KVM_BUG_ON(is_td_vcpu(vcpu), vcpu->kvm))
		return 0;

	return vmx_enter_smm(vcpu, smram);
}

static int vt_leave_smm(struct kvm_vcpu *vcpu, const union kvm_smram *smram)
{
	if (KVM_BUG_ON(is_td_vcpu(vcpu), vcpu->kvm))
		return 0;

	return vmx_leave_smm(vcpu, smram);
}

static void vt_enable_smi_window(struct kvm_vcpu *vcpu)
{
	if (KVM_BUG_ON(is_td_vcpu(vcpu), vcpu->kvm))
		return;

	/* RSM will cause a vmexit anyway.  */
	vmx_enable_smi_window(vcpu);
}
#endif

static int vt_check_emulate_instruction(struct kvm_vcpu *vcpu, int emul_type,
					void *insn, int insn_len)
{
	/*
	 * For TDX, this can only be triggered for MMIO emulation.  Let the
	 * guest retry after installing the SPTE with suppress #VE bit cleared,
	 * so that the guest will receive #VE when retry.  The guest is expected
	 * to call TDG.VP.VMCALL<MMIO> to request VMM to do MMIO emulation on
	 * #VE.
	 */
	if (is_td_vcpu(vcpu))
		return X86EMUL_RETRY_INSTR;

	return vmx_check_emulate_instruction(vcpu, emul_type, insn, insn_len);
}

static bool vt_apic_init_signal_blocked(struct kvm_vcpu *vcpu)
{
	/*
	 * INIT and SIPI are always blocked for TDX, i.e., INIT handling and
	 * the OP vcpu_deliver_sipi_vector() won't be called.
	 */
	if (is_td_vcpu(vcpu))
		return true;

	return vmx_apic_init_signal_blocked(vcpu);
}

static void vt_set_virtual_apic_mode(struct kvm_vcpu *vcpu)
{
	/* Only x2APIC mode is supported for TD. */
	if (is_td_vcpu(vcpu))
		return;

	return vmx_set_virtual_apic_mode(vcpu);
}

static void vt_hwapic_isr_update(struct kvm_vcpu *vcpu, int max_isr)
{
	if (is_td_vcpu(vcpu))
		return;

	return vmx_hwapic_isr_update(vcpu, max_isr);
}

static int vt_sync_pir_to_irr(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return -1;

	return vmx_sync_pir_to_irr(vcpu);
}

static void vt_deliver_interrupt(struct kvm_lapic *apic, int delivery_mode,
			   int trig_mode, int vector)
{
	if (is_td_vcpu(apic->vcpu)) {
		tdx_deliver_interrupt(apic, delivery_mode, trig_mode,
					     vector);
		return;
	}

	vmx_deliver_interrupt(apic, delivery_mode, trig_mode, vector);
}

static void vt_vcpu_after_set_cpuid(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_vcpu_after_set_cpuid(vcpu);
}

static void vt_update_exception_bitmap(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_update_exception_bitmap(vcpu);
}

static u64 vt_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_segment_base(vcpu, seg);
}

static void vt_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var,
			      int seg)
{
	if (is_td_vcpu(vcpu)) {
		memset(var, 0, sizeof(*var));
		return;
	}

	vmx_get_segment(vcpu, var, seg);
}

static void vt_set_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var,
			      int seg)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_segment(vcpu, var, seg);
}

static int vt_get_cpl(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_cpl(vcpu);
}

static int vt_get_cpl_no_cache(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_cpl_no_cache(vcpu);
}

static void vt_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l)
{
	if (is_td_vcpu(vcpu)) {
		*db = 0;
		*l = 0;
		return;
	}

	vmx_get_cs_db_l_bits(vcpu, db, l);
}

static bool vt_is_valid_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	if (is_td_vcpu(vcpu))
		return true;

	return vmx_is_valid_cr0(vcpu, cr0);
}

static void vt_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_cr0(vcpu, cr0);
}

static bool vt_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (is_td_vcpu(vcpu))
		return true;

	return vmx_is_valid_cr4(vcpu, cr4);
}

static void vt_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_cr4(vcpu, cr4);
}

static int vt_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_set_efer(vcpu, efer);
}

static void vt_get_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	if (is_td_vcpu(vcpu)) {
		memset(dt, 0, sizeof(*dt));
		return;
	}

	vmx_get_idt(vcpu, dt);
}

static void vt_set_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_idt(vcpu, dt);
}

static void vt_get_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	if (is_td_vcpu(vcpu)) {
		memset(dt, 0, sizeof(*dt));
		return;
	}

	vmx_get_gdt(vcpu, dt);
}

static void vt_set_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_gdt(vcpu, dt);
}

static void vt_set_dr7(struct kvm_vcpu *vcpu, unsigned long val)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_dr7(vcpu, val);
}

static void vt_sync_dirty_debug_regs(struct kvm_vcpu *vcpu)
{
	/*
	 * MOV-DR exiting is always cleared for TD guest, even in debug mode.
	 * Thus KVM_DEBUGREG_WONT_EXIT can never be set and it should never
	 * reach here for TD vcpu.
	 */
	if (is_td_vcpu(vcpu))
		return;

	vmx_sync_dirty_debug_regs(vcpu);
}

static void vt_cache_reg(struct kvm_vcpu *vcpu, enum kvm_reg reg)
{
	if (WARN_ON_ONCE(is_td_vcpu(vcpu)))
		return;

	vmx_cache_reg(vcpu, reg);
}

static unsigned long vt_get_rflags(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_rflags(vcpu);
}

static void vt_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_rflags(vcpu, rflags);
}

static bool vt_get_if_flag(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return false;

	return vmx_get_if_flag(vcpu);
}

static void vt_flush_tlb_all(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_flush_tlb_all(vcpu);
		return;
	}

	vmx_flush_tlb_all(vcpu);
}

static void vt_flush_tlb_current(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_flush_tlb_current(vcpu);
		return;
	}

	vmx_flush_tlb_current(vcpu);
}

static void vt_flush_tlb_gva(struct kvm_vcpu *vcpu, gva_t addr)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_flush_tlb_gva(vcpu, addr);
}

static void vt_flush_tlb_guest(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_flush_tlb_guest(vcpu);
}

static void vt_inject_nmi(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		tdx_inject_nmi(vcpu);
		return;
	}

	vmx_inject_nmi(vcpu);
}

static int vt_nmi_allowed(struct kvm_vcpu *vcpu, bool for_injection)
{
	/*
	 * The TDX module manages NMI windows and NMI reinjection, and hides NMI
	 * blocking, all KVM can do is throw an NMI over the wall.
	 */
	if (is_td_vcpu(vcpu))
		return true;

	return vmx_nmi_allowed(vcpu, for_injection);
}

static bool vt_get_nmi_mask(struct kvm_vcpu *vcpu)
{
	/*
	 * KVM can't get NMI blocking status for TDX guest, assume NMIs are
	 * always unmasked.
	 */
	if (is_td_vcpu(vcpu))
		return false;

	return vmx_get_nmi_mask(vcpu);
}

static void vt_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_nmi_mask(vcpu, masked);
}

static void vt_enable_nmi_window(struct kvm_vcpu *vcpu)
{
	/* Refer to the comments in tdx_inject_nmi(). */
	if (is_td_vcpu(vcpu))
		return;

	vmx_enable_nmi_window(vcpu);
}

static void vt_load_mmu_pgd(struct kvm_vcpu *vcpu, hpa_t root_hpa,
			    int pgd_level)
{
	if (is_td_vcpu(vcpu)) {
		tdx_load_mmu_pgd(vcpu, root_hpa, pgd_level);
		return;
	}

	vmx_load_mmu_pgd(vcpu, root_hpa, pgd_level);
}

static void vt_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_interrupt_shadow(vcpu, mask);
}

static u32 vt_get_interrupt_shadow(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_interrupt_shadow(vcpu);
}

static void vt_patch_hypercall(struct kvm_vcpu *vcpu,
				  unsigned char *hypercall)
{
	/*
	 * Because guest memory is protected, guest can't be patched. TD kernel
	 * is modified to use TDG.VP.VMCALL for hypercall.
	 */
	if (is_td_vcpu(vcpu))
		return;

	vmx_patch_hypercall(vcpu, hypercall);
}

static void vt_inject_irq(struct kvm_vcpu *vcpu, bool reinjected)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_inject_irq(vcpu, reinjected);
}

static void vt_inject_exception(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_inject_exception(vcpu);
}

static void vt_cancel_injection(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_cancel_injection(vcpu);
}

static int vt_interrupt_allowed(struct kvm_vcpu *vcpu, bool for_injection)
{
	if (is_td_vcpu(vcpu))
		return tdx_interrupt_allowed(vcpu);

	return vmx_interrupt_allowed(vcpu, for_injection);
}

static void vt_enable_irq_window(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_enable_irq_window(vcpu);
}

static void vt_get_entry_info(struct kvm_vcpu *vcpu, u32 *intr_info, u32 *error_code)
{
	*intr_info = 0;
	*error_code = 0;

	if (is_td_vcpu(vcpu))
		return;

	vmx_get_entry_info(vcpu, intr_info, error_code);
}

static void vt_get_exit_info(struct kvm_vcpu *vcpu, u32 *reason,
			u64 *info1, u64 *info2, u32 *intr_info, u32 *error_code)
{
	if (is_td_vcpu(vcpu)) {
		tdx_get_exit_info(vcpu, reason, info1, info2, intr_info,
				  error_code);
		return;
	}

	vmx_get_exit_info(vcpu, reason, info1, info2, intr_info, error_code);
}

static void vt_update_cr8_intercept(struct kvm_vcpu *vcpu, int tpr, int irr)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_update_cr8_intercept(vcpu, tpr, irr);
}

static void vt_set_apic_access_page_addr(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_set_apic_access_page_addr(vcpu);
}

static void vt_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu)) {
		KVM_BUG_ON(!kvm_vcpu_apicv_active(vcpu), vcpu->kvm);
		return;
	}

	vmx_refresh_apicv_exec_ctrl(vcpu);
}

static void vt_load_eoi_exitmap(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_load_eoi_exitmap(vcpu, eoi_exit_bitmap);
}

static int vt_set_tss_addr(struct kvm *kvm, unsigned int addr)
{
	if (is_td(kvm))
		return 0;

	return vmx_set_tss_addr(kvm, addr);
}

static int vt_set_identity_map_addr(struct kvm *kvm, u64 ident_addr)
{
	if (is_td(kvm))
		return 0;

	return vmx_set_identity_map_addr(kvm, ident_addr);
}

static u64 vt_get_l2_tsc_offset(struct kvm_vcpu *vcpu)
{
	/* TDX doesn't support L2 guest at the moment. */
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_l2_tsc_offset(vcpu);
}

static u64 vt_get_l2_tsc_multiplier(struct kvm_vcpu *vcpu)
{
	/* TDX doesn't support L2 guest at the moment. */
	if (is_td_vcpu(vcpu))
		return 0;

	return vmx_get_l2_tsc_multiplier(vcpu);
}

static void vt_write_tsc_offset(struct kvm_vcpu *vcpu)
{
	/* In TDX, tsc offset can't be changed. */
	if (is_td_vcpu(vcpu))
		return;

	vmx_write_tsc_offset(vcpu);
}

static void vt_write_tsc_multiplier(struct kvm_vcpu *vcpu)
{
	/* In TDX, tsc multiplier can't be changed. */
	if (is_td_vcpu(vcpu))
		return;

	vmx_write_tsc_multiplier(vcpu);
}

#ifdef CONFIG_X86_64
static int vt_set_hv_timer(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
			      bool *expired)
{
	/* VMX-preemption timer isn't available for TDX. */
	if (is_td_vcpu(vcpu))
		return -EINVAL;

	return vmx_set_hv_timer(vcpu, guest_deadline_tsc, expired);
}

static void vt_cancel_hv_timer(struct kvm_vcpu *vcpu)
{
	/* VMX-preemption timer can't be set.  See vt_set_hv_timer(). */
	if (is_td_vcpu(vcpu))
		return;

	vmx_cancel_hv_timer(vcpu);
}
#endif

static void vt_setup_mce(struct kvm_vcpu *vcpu)
{
	if (is_td_vcpu(vcpu))
		return;

	vmx_setup_mce(vcpu);
}

static int vt_mem_enc_ioctl(struct kvm *kvm, void __user *argp)
{
	if (!is_td(kvm))
		return -ENOTTY;

	return tdx_vm_ioctl(kvm, argp);
}

static int vt_vcpu_mem_enc_ioctl(struct kvm_vcpu *vcpu, void __user *argp)
{
	if (!is_td_vcpu(vcpu))
		return -EINVAL;

	return tdx_vcpu_ioctl(vcpu, argp);
}

static int vt_gmem_max_mapping_level(struct kvm *kvm, kvm_pfn_t pfn,
				     bool is_private)
{
	if (is_td(kvm))
		return tdx_gmem_max_mapping_level(kvm, pfn, is_private);

	return 0;
}

#define vt_op(name) vt_##name
#define vt_op_tdx_only(name) vt_##name
#else /* CONFIG_KVM_INTEL_TDX */
#define vt_op(name) vmx_##name
#define vt_op_tdx_only(name) NULL
#endif /* CONFIG_KVM_INTEL_TDX */

#define VMX_REQUIRED_APICV_INHIBITS				\
	(BIT(APICV_INHIBIT_REASON_DISABLED) |			\
	 BIT(APICV_INHIBIT_REASON_ABSENT) |			\
	 BIT(APICV_INHIBIT_REASON_HYPERV) |			\
	 BIT(APICV_INHIBIT_REASON_BLOCKIRQ) |			\
	 BIT(APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED) |	\
	 BIT(APICV_INHIBIT_REASON_APIC_ID_MODIFIED) |		\
	 BIT(APICV_INHIBIT_REASON_APIC_BASE_MODIFIED))

struct kvm_x86_ops vt_x86_ops __initdata = {
	.name = KBUILD_MODNAME,

	.check_processor_compatibility = vmx_check_processor_compat,

	.hardware_unsetup = vmx_hardware_unsetup,

	.enable_virtualization_cpu = vmx_enable_virtualization_cpu,
	.disable_virtualization_cpu = vt_op(disable_virtualization_cpu),
	.emergency_disable_virtualization_cpu = vmx_emergency_disable_virtualization_cpu,

	.has_emulated_msr = vt_op(has_emulated_msr),

	.vm_size = sizeof(struct kvm_vmx),

	.vm_init = vt_op(vm_init),
	.vm_destroy = vt_op(vm_destroy),
	.vm_pre_destroy = vt_op_tdx_only(vm_pre_destroy),

	.vcpu_precreate = vt_op(vcpu_precreate),
	.vcpu_create = vt_op(vcpu_create),
	.vcpu_free = vt_op(vcpu_free),
	.vcpu_reset = vt_op(vcpu_reset),

	.prepare_switch_to_guest = vt_op(prepare_switch_to_guest),
	.vcpu_load = vt_op(vcpu_load),
	.vcpu_put = vt_op(vcpu_put),

	.HOST_OWNED_DEBUGCTL = VMX_HOST_OWNED_DEBUGCTL_BITS,

	.update_exception_bitmap = vt_op(update_exception_bitmap),
	.get_feature_msr = vmx_get_feature_msr,
	.get_msr = vt_op(get_msr),
	.set_msr = vt_op(set_msr),

	.get_segment_base = vt_op(get_segment_base),
	.get_segment = vt_op(get_segment),
	.set_segment = vt_op(set_segment),
	.get_cpl = vt_op(get_cpl),
	.get_cpl_no_cache = vt_op(get_cpl_no_cache),
	.get_cs_db_l_bits = vt_op(get_cs_db_l_bits),
	.is_valid_cr0 = vt_op(is_valid_cr0),
	.set_cr0 = vt_op(set_cr0),
	.is_valid_cr4 = vt_op(is_valid_cr4),
	.set_cr4 = vt_op(set_cr4),
	.set_efer = vt_op(set_efer),
	.get_idt = vt_op(get_idt),
	.set_idt = vt_op(set_idt),
	.get_gdt = vt_op(get_gdt),
	.set_gdt = vt_op(set_gdt),
	.set_dr7 = vt_op(set_dr7),
	.sync_dirty_debug_regs = vt_op(sync_dirty_debug_regs),
	.cache_reg = vt_op(cache_reg),
	.get_rflags = vt_op(get_rflags),
	.set_rflags = vt_op(set_rflags),
	.get_if_flag = vt_op(get_if_flag),

	.flush_tlb_all = vt_op(flush_tlb_all),
	.flush_tlb_current = vt_op(flush_tlb_current),
	.flush_tlb_gva = vt_op(flush_tlb_gva),
	.flush_tlb_guest = vt_op(flush_tlb_guest),

	.vcpu_pre_run = vt_op(vcpu_pre_run),
	.vcpu_run = vt_op(vcpu_run),
	.handle_exit = vt_op(handle_exit),
	.skip_emulated_instruction = vmx_skip_emulated_instruction,
	.update_emulated_instruction = vmx_update_emulated_instruction,
	.set_interrupt_shadow = vt_op(set_interrupt_shadow),
	.get_interrupt_shadow = vt_op(get_interrupt_shadow),
	.patch_hypercall = vt_op(patch_hypercall),
	.inject_irq = vt_op(inject_irq),
	.inject_nmi = vt_op(inject_nmi),
	.inject_exception = vt_op(inject_exception),
	.cancel_injection = vt_op(cancel_injection),
	.interrupt_allowed = vt_op(interrupt_allowed),
	.nmi_allowed = vt_op(nmi_allowed),
	.get_nmi_mask = vt_op(get_nmi_mask),
	.set_nmi_mask = vt_op(set_nmi_mask),
	.enable_nmi_window = vt_op(enable_nmi_window),
	.enable_irq_window = vt_op(enable_irq_window),
	.update_cr8_intercept = vt_op(update_cr8_intercept),

	.x2apic_icr_is_split = false,
	.set_virtual_apic_mode = vt_op(set_virtual_apic_mode),
	.set_apic_access_page_addr = vt_op(set_apic_access_page_addr),
	.refresh_apicv_exec_ctrl = vt_op(refresh_apicv_exec_ctrl),
	.load_eoi_exitmap = vt_op(load_eoi_exitmap),
	.apicv_pre_state_restore = pi_apicv_pre_state_restore,
	.required_apicv_inhibits = VMX_REQUIRED_APICV_INHIBITS,
	.hwapic_isr_update = vt_op(hwapic_isr_update),
	.sync_pir_to_irr = vt_op(sync_pir_to_irr),
	.deliver_interrupt = vt_op(deliver_interrupt),
	.dy_apicv_has_pending_interrupt = pi_has_pending_interrupt,

	.set_tss_addr = vt_op(set_tss_addr),
	.set_identity_map_addr = vt_op(set_identity_map_addr),
	.get_mt_mask = vmx_get_mt_mask,

	.get_exit_info = vt_op(get_exit_info),
	.get_entry_info = vt_op(get_entry_info),

	.vcpu_after_set_cpuid = vt_op(vcpu_after_set_cpuid),

	.has_wbinvd_exit = cpu_has_vmx_wbinvd_exit,

	.get_l2_tsc_offset = vt_op(get_l2_tsc_offset),
	.get_l2_tsc_multiplier = vt_op(get_l2_tsc_multiplier),
	.write_tsc_offset = vt_op(write_tsc_offset),
	.write_tsc_multiplier = vt_op(write_tsc_multiplier),

	.load_mmu_pgd = vt_op(load_mmu_pgd),

	.check_intercept = vmx_check_intercept,
	.handle_exit_irqoff = vmx_handle_exit_irqoff,

	.update_cpu_dirty_logging = vt_op(update_cpu_dirty_logging),

	.nested_ops = &vmx_nested_ops,

	.pi_update_irte = vmx_pi_update_irte,
	.pi_start_bypass = vmx_pi_start_bypass,

#ifdef CONFIG_X86_64
	.set_hv_timer = vt_op(set_hv_timer),
	.cancel_hv_timer = vt_op(cancel_hv_timer),
#endif

	.setup_mce = vt_op(setup_mce),

#ifdef CONFIG_KVM_SMM
	.smi_allowed = vt_op(smi_allowed),
	.enter_smm = vt_op(enter_smm),
	.leave_smm = vt_op(leave_smm),
	.enable_smi_window = vt_op(enable_smi_window),
#endif

	.check_emulate_instruction = vt_op(check_emulate_instruction),
	.apic_init_signal_blocked = vt_op(apic_init_signal_blocked),
	.migrate_timers = vmx_migrate_timers,

	.recalc_intercepts = vt_op(recalc_intercepts),
	.complete_emulated_msr = vt_op(complete_emulated_msr),

	.vcpu_deliver_sipi_vector = kvm_vcpu_deliver_sipi_vector,

	.get_untagged_addr = vmx_get_untagged_addr,

	.mem_enc_ioctl = vt_op_tdx_only(mem_enc_ioctl),
	.vcpu_mem_enc_ioctl = vt_op_tdx_only(vcpu_mem_enc_ioctl),

	.gmem_max_mapping_level = vt_op_tdx_only(gmem_max_mapping_level)
};

struct kvm_x86_init_ops vt_init_ops __initdata = {
	.hardware_setup = vt_op(hardware_setup),
	.handle_intel_pt_intr = NULL,

	.runtime_ops = &vt_x86_ops,
	.pmu_ops = &intel_pmu_ops,
};

static void __exit vt_exit(void)
{
	kvm_exit();
	tdx_cleanup();
	vmx_exit();
}
module_exit(vt_exit);

static int __init vt_init(void)
{
	unsigned vcpu_size, vcpu_align;
	int r;

	r = vmx_init();
	if (r)
		return r;

	/* tdx_init() has been taken */
	r = tdx_bringup();
	if (r)
		goto err_tdx_bringup;

	/*
	 * TDX and VMX have different vCPU structures.  Calculate the
	 * maximum size/align so that kvm_init() can use the larger
	 * values to create the kmem_vcpu_cache.
	 */
	vcpu_size = sizeof(struct vcpu_vmx);
	vcpu_align = __alignof__(struct vcpu_vmx);
	if (enable_tdx) {
		vcpu_size = max_t(unsigned, vcpu_size,
				sizeof(struct vcpu_tdx));
		vcpu_align = max_t(unsigned, vcpu_align,
				__alignof__(struct vcpu_tdx));
		kvm_caps.supported_vm_types |= BIT(KVM_X86_TDX_VM);
	}

	/*
	 * Common KVM initialization _must_ come last, after this, /dev/kvm is
	 * exposed to userspace!
	 */
	r = kvm_init(vcpu_size, vcpu_align, THIS_MODULE);
	if (r)
		goto err_kvm_init;

	return 0;

err_kvm_init:
	tdx_cleanup();
err_tdx_bringup:
	vmx_exit();
	return r;
}
module_init(vt_init);

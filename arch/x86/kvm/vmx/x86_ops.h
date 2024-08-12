/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_X86_OPS_H
#define __KVM_X86_VMX_X86_OPS_H

#include <linux/kvm_host.h>

#include "x86.h"

__init int vmx_hardware_setup(void);

extern struct kvm_x86_ops vt_x86_ops __initdata;
extern struct kvm_x86_init_ops vt_init_ops __initdata;

void vmx_hardware_unsetup(void);
int vmx_check_processor_compat(void);
int vmx_hardware_enable(void);
void vmx_hardware_disable(void);
int vmx_vm_init(struct kvm *kvm);
void vmx_vm_destroy(struct kvm *kvm);
int vmx_vcpu_precreate(struct kvm *kvm);
int vmx_vcpu_create(struct kvm_vcpu *vcpu);
int vmx_vcpu_pre_run(struct kvm_vcpu *vcpu);
fastpath_t vmx_vcpu_run(struct kvm_vcpu *vcpu, bool force_immediate_exit);
void vmx_vcpu_free(struct kvm_vcpu *vcpu);
void vmx_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event);
void vmx_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void vmx_vcpu_put(struct kvm_vcpu *vcpu);
int vmx_handle_exit(struct kvm_vcpu *vcpu, fastpath_t exit_fastpath);
void vmx_handle_exit_irqoff(struct kvm_vcpu *vcpu);
int vmx_skip_emulated_instruction(struct kvm_vcpu *vcpu);
void vmx_update_emulated_instruction(struct kvm_vcpu *vcpu);
int vmx_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
#ifdef CONFIG_KVM_SMM
int vmx_smi_allowed(struct kvm_vcpu *vcpu, bool for_injection);
int vmx_enter_smm(struct kvm_vcpu *vcpu, union kvm_smram *smram);
int vmx_leave_smm(struct kvm_vcpu *vcpu, const union kvm_smram *smram);
void vmx_enable_smi_window(struct kvm_vcpu *vcpu);
#endif
int vmx_check_emulate_instruction(struct kvm_vcpu *vcpu, int emul_type,
				  void *insn, int insn_len);
int vmx_check_intercept(struct kvm_vcpu *vcpu,
			struct x86_instruction_info *info,
			enum x86_intercept_stage stage,
			struct x86_exception *exception);
bool vmx_apic_init_signal_blocked(struct kvm_vcpu *vcpu);
void vmx_migrate_timers(struct kvm_vcpu *vcpu);
void vmx_set_virtual_apic_mode(struct kvm_vcpu *vcpu);
void vmx_apicv_pre_state_restore(struct kvm_vcpu *vcpu);
void vmx_hwapic_irr_update(struct kvm_vcpu *vcpu, int max_irr);
void vmx_hwapic_isr_update(int max_isr);
int vmx_sync_pir_to_irr(struct kvm_vcpu *vcpu);
void vmx_deliver_interrupt(struct kvm_lapic *apic, int delivery_mode,
			   int trig_mode, int vector);
void vmx_vcpu_after_set_cpuid(struct kvm_vcpu *vcpu);
bool vmx_has_emulated_msr(struct kvm *kvm, u32 index);
void vmx_msr_filter_changed(struct kvm_vcpu *vcpu);
void vmx_prepare_switch_to_guest(struct kvm_vcpu *vcpu);
void vmx_update_exception_bitmap(struct kvm_vcpu *vcpu);
int vmx_get_msr_feature(struct kvm_msr_entry *msr);
int vmx_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
u64 vmx_get_segment_base(struct kvm_vcpu *vcpu, int seg);
void vmx_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
void vmx_set_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
int vmx_get_cpl(struct kvm_vcpu *vcpu);
void vmx_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l);
bool vmx_is_valid_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void vmx_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void vmx_load_mmu_pgd(struct kvm_vcpu *vcpu, hpa_t root_hpa, int root_level);
void vmx_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
bool vmx_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
int vmx_set_efer(struct kvm_vcpu *vcpu, u64 efer);
void vmx_get_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
void vmx_set_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
void vmx_get_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
void vmx_set_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
void vmx_set_dr7(struct kvm_vcpu *vcpu, unsigned long val);
void vmx_sync_dirty_debug_regs(struct kvm_vcpu *vcpu);
void vmx_cache_reg(struct kvm_vcpu *vcpu, enum kvm_reg reg);
unsigned long vmx_get_rflags(struct kvm_vcpu *vcpu);
void vmx_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
bool vmx_get_if_flag(struct kvm_vcpu *vcpu);
void vmx_flush_tlb_all(struct kvm_vcpu *vcpu);
void vmx_flush_tlb_current(struct kvm_vcpu *vcpu);
void vmx_flush_tlb_gva(struct kvm_vcpu *vcpu, gva_t addr);
void vmx_flush_tlb_guest(struct kvm_vcpu *vcpu);
void vmx_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask);
u32 vmx_get_interrupt_shadow(struct kvm_vcpu *vcpu);
void vmx_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall);
void vmx_inject_irq(struct kvm_vcpu *vcpu, bool reinjected);
void vmx_inject_nmi(struct kvm_vcpu *vcpu);
void vmx_inject_exception(struct kvm_vcpu *vcpu);
void vmx_cancel_injection(struct kvm_vcpu *vcpu);
int vmx_interrupt_allowed(struct kvm_vcpu *vcpu, bool for_injection);
int vmx_nmi_allowed(struct kvm_vcpu *vcpu, bool for_injection);
bool vmx_get_nmi_mask(struct kvm_vcpu *vcpu);
void vmx_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked);
void vmx_enable_nmi_window(struct kvm_vcpu *vcpu);
void vmx_enable_irq_window(struct kvm_vcpu *vcpu);
void vmx_update_cr8_intercept(struct kvm_vcpu *vcpu, int tpr, int irr);
void vmx_set_apic_access_page_addr(struct kvm_vcpu *vcpu);
void vmx_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu);
void vmx_load_eoi_exitmap(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap);
int vmx_set_tss_addr(struct kvm *kvm, unsigned int addr);
int vmx_set_identity_map_addr(struct kvm *kvm, u64 ident_addr);
u8 vmx_get_mt_mask(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio);
void vmx_get_exit_info(struct kvm_vcpu *vcpu, u32 *reason,
		       u64 *info1, u64 *info2, u32 *intr_info, u32 *error_code);
u64 vmx_get_l2_tsc_offset(struct kvm_vcpu *vcpu);
u64 vmx_get_l2_tsc_multiplier(struct kvm_vcpu *vcpu);
void vmx_write_tsc_offset(struct kvm_vcpu *vcpu);
void vmx_write_tsc_multiplier(struct kvm_vcpu *vcpu);
void vmx_update_cpu_dirty_logging(struct kvm_vcpu *vcpu);
#ifdef CONFIG_X86_64
int vmx_set_hv_timer(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
		     bool *expired);
void vmx_cancel_hv_timer(struct kvm_vcpu *vcpu);
#endif
void vmx_setup_mce(struct kvm_vcpu *vcpu);

#endif /* __KVM_X86_VMX_X86_OPS_H */

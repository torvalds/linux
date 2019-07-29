/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_H
#define __KVM_X86_VMX_H

#include <linux/kvm_host.h>

#include <asm/kvm.h>
#include <asm/intel_pt.h>

#include "capabilities.h"
#include "ops.h"
#include "vmcs.h"

extern const u32 vmx_msr_index[];
extern u64 host_efer;

#define MSR_TYPE_R	1
#define MSR_TYPE_W	2
#define MSR_TYPE_RW	3

#define X2APIC_MSR(r) (APIC_BASE_MSR + ((r) >> 4))

#define NR_AUTOLOAD_MSRS 8

struct vmx_msrs {
	unsigned int		nr;
	struct vmx_msr_entry	val[NR_AUTOLOAD_MSRS];
};

struct shared_msr_entry {
	unsigned index;
	u64 data;
	u64 mask;
};

enum segment_cache_field {
	SEG_FIELD_SEL = 0,
	SEG_FIELD_BASE = 1,
	SEG_FIELD_LIMIT = 2,
	SEG_FIELD_AR = 3,

	SEG_FIELD_NR = 4
};

/* Posted-Interrupt Descriptor */
struct pi_desc {
	u32 pir[8];     /* Posted interrupt requested */
	union {
		struct {
				/* bit 256 - Outstanding Notification */
			u16	on	: 1,
				/* bit 257 - Suppress Notification */
				sn	: 1,
				/* bit 271:258 - Reserved */
				rsvd_1	: 14;
				/* bit 279:272 - Notification Vector */
			u8	nv;
				/* bit 287:280 - Reserved */
			u8	rsvd_2;
				/* bit 319:288 - Notification Destination */
			u32	ndst;
		};
		u64 control;
	};
	u32 rsvd[6];
} __aligned(64);

#define RTIT_ADDR_RANGE		4

struct pt_ctx {
	u64 ctl;
	u64 status;
	u64 output_base;
	u64 output_mask;
	u64 cr3_match;
	u64 addr_a[RTIT_ADDR_RANGE];
	u64 addr_b[RTIT_ADDR_RANGE];
};

struct pt_desc {
	u64 ctl_bitmask;
	u32 addr_range;
	u32 caps[PT_CPUID_REGS_NUM * PT_CPUID_LEAVES];
	struct pt_ctx host;
	struct pt_ctx guest;
};

/*
 * The nested_vmx structure is part of vcpu_vmx, and holds information we need
 * for correct emulation of VMX (i.e., nested VMX) on this vcpu.
 */
struct nested_vmx {
	/* Has the level1 guest done vmxon? */
	bool vmxon;
	gpa_t vmxon_ptr;
	bool pml_full;

	/* The guest-physical address of the current VMCS L1 keeps for L2 */
	gpa_t current_vmptr;
	/*
	 * Cache of the guest's VMCS, existing outside of guest memory.
	 * Loaded from guest memory during VMPTRLD. Flushed to guest
	 * memory during VMCLEAR and VMPTRLD.
	 */
	struct vmcs12 *cached_vmcs12;
	/*
	 * Cache of the guest's shadow VMCS, existing outside of guest
	 * memory. Loaded from guest memory during VM entry. Flushed
	 * to guest memory during VM exit.
	 */
	struct vmcs12 *cached_shadow_vmcs12;
	/*
	 * Indicates if the shadow vmcs or enlightened vmcs must be updated
	 * with the data held by struct vmcs12.
	 */
	bool need_vmcs12_sync;
	bool dirty_vmcs12;

	/*
	 * vmcs02 has been initialized, i.e. state that is constant for
	 * vmcs02 has been written to the backing VMCS.  Initialization
	 * is delayed until L1 actually attempts to run a nested VM.
	 */
	bool vmcs02_initialized;

	bool change_vmcs01_virtual_apic_mode;

	/*
	 * Enlightened VMCS has been enabled. It does not mean that L1 has to
	 * use it. However, VMX features available to L1 will be limited based
	 * on what the enlightened VMCS supports.
	 */
	bool enlightened_vmcs_enabled;

	/* L2 must run next, and mustn't decide to exit to L1. */
	bool nested_run_pending;

	struct loaded_vmcs vmcs02;

	/*
	 * Guest pages referred to in the vmcs02 with host-physical
	 * pointers, so we must keep them pinned while L2 runs.
	 */
	struct page *apic_access_page;
	struct page *virtual_apic_page;
	struct page *pi_desc_page;
	struct pi_desc *pi_desc;
	bool pi_pending;
	u16 posted_intr_nv;

	struct hrtimer preemption_timer;
	bool preemption_timer_expired;

	/* to migrate it to L2 if VM_ENTRY_LOAD_DEBUG_CONTROLS is off */
	u64 vmcs01_debugctl;
	u64 vmcs01_guest_bndcfgs;

	u16 vpid02;
	u16 last_vpid;

	struct nested_vmx_msrs msrs;

	/* SMM related state */
	struct {
		/* in VMX operation on SMM entry? */
		bool vmxon;
		/* in guest mode on SMM entry? */
		bool guest_mode;
	} smm;

	gpa_t hv_evmcs_vmptr;
	struct page *hv_evmcs_page;
	struct hv_enlightened_vmcs *hv_evmcs;
};

struct vcpu_vmx {
	struct kvm_vcpu       vcpu;
	unsigned long         host_rsp;
	u8                    fail;
	u8		      msr_bitmap_mode;
	u32                   exit_intr_info;
	u32                   idt_vectoring_info;
	ulong                 rflags;
	struct shared_msr_entry *guest_msrs;
	int                   nmsrs;
	int                   save_nmsrs;
	bool                  guest_msrs_dirty;
	unsigned long	      host_idt_base;
#ifdef CONFIG_X86_64
	u64		      msr_host_kernel_gs_base;
	u64		      msr_guest_kernel_gs_base;
#endif

	u64		      arch_capabilities;
	u64		      spec_ctrl;

	u32 vm_entry_controls_shadow;
	u32 vm_exit_controls_shadow;
	u32 secondary_exec_control;

	/*
	 * loaded_vmcs points to the VMCS currently used in this vcpu. For a
	 * non-nested (L1) guest, it always points to vmcs01. For a nested
	 * guest (L2), it points to a different VMCS.  loaded_cpu_state points
	 * to the VMCS whose state is loaded into the CPU registers that only
	 * need to be switched when transitioning to/from the kernel; a NULL
	 * value indicates that host state is loaded.
	 */
	struct loaded_vmcs    vmcs01;
	struct loaded_vmcs   *loaded_vmcs;
	struct loaded_vmcs   *loaded_cpu_state;
	bool                  __launched; /* temporary, used in vmx_vcpu_run */
	struct msr_autoload {
		struct vmx_msrs guest;
		struct vmx_msrs host;
	} msr_autoload;

	struct {
		int vm86_active;
		ulong save_rflags;
		struct kvm_segment segs[8];
	} rmode;
	struct {
		u32 bitmask; /* 4 bits per segment (1 bit per field) */
		struct kvm_save_segment {
			u16 selector;
			unsigned long base;
			u32 limit;
			u32 ar;
		} seg[8];
	} segment_cache;
	int vpid;
	bool emulation_required;

	u32 exit_reason;

	/* Posted interrupt descriptor */
	struct pi_desc pi_desc;

	/* Support for a guest hypervisor (nested VMX) */
	struct nested_vmx nested;

	/* Dynamic PLE window. */
	int ple_window;
	bool ple_window_dirty;

	bool req_immediate_exit;

	/* Support for PML */
#define PML_ENTITY_NUM		512
	struct page *pml_pg;

	/* apic deadline value in host tsc */
	u64 hv_deadline_tsc;

	u64 current_tsc_ratio;

	u32 host_pkru;

	unsigned long host_debugctlmsr;

	/*
	 * Only bits masked by msr_ia32_feature_control_valid_bits can be set in
	 * msr_ia32_feature_control. FEATURE_CONTROL_LOCKED is always included
	 * in msr_ia32_feature_control_valid_bits.
	 */
	u64 msr_ia32_feature_control;
	u64 msr_ia32_feature_control_valid_bits;
	u64 ept_pointer;

	struct pt_desc pt_desc;
};

enum ept_pointers_status {
	EPT_POINTERS_CHECK = 0,
	EPT_POINTERS_MATCH = 1,
	EPT_POINTERS_MISMATCH = 2
};

struct kvm_vmx {
	struct kvm kvm;

	unsigned int tss_addr;
	bool ept_identity_pagetable_done;
	gpa_t ept_identity_map_addr;

	enum ept_pointers_status ept_pointers_match;
	spinlock_t ept_pointer_lock;
};

bool nested_vmx_allowed(struct kvm_vcpu *vcpu);
void vmx_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void vmx_vcpu_put(struct kvm_vcpu *vcpu);
int allocate_vpid(void);
void free_vpid(int vpid);
void vmx_set_constant_host_state(struct vcpu_vmx *vmx);
void vmx_prepare_switch_to_guest(struct kvm_vcpu *vcpu);
int vmx_get_cpl(struct kvm_vcpu *vcpu);
unsigned long vmx_get_rflags(struct kvm_vcpu *vcpu);
void vmx_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
u32 vmx_get_interrupt_shadow(struct kvm_vcpu *vcpu);
void vmx_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask);
void vmx_set_efer(struct kvm_vcpu *vcpu, u64 efer);
void vmx_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void vmx_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3);
int vmx_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
void set_cr4_guest_host_mask(struct vcpu_vmx *vmx);
void ept_save_pdptrs(struct kvm_vcpu *vcpu);
void vmx_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
void vmx_set_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
u64 construct_eptp(struct kvm_vcpu *vcpu, unsigned long root_hpa);
void update_exception_bitmap(struct kvm_vcpu *vcpu);
void vmx_update_msr_bitmap(struct kvm_vcpu *vcpu);
bool vmx_get_nmi_mask(struct kvm_vcpu *vcpu);
void vmx_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked);
void vmx_set_virtual_apic_mode(struct kvm_vcpu *vcpu);
struct shared_msr_entry *find_msr_entry(struct vcpu_vmx *vmx, u32 msr);
void pt_update_intercept_for_msr(struct vcpu_vmx *vmx);

#define POSTED_INTR_ON  0
#define POSTED_INTR_SN  1

static inline bool pi_test_and_set_on(struct pi_desc *pi_desc)
{
	return test_and_set_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_clear_on(struct pi_desc *pi_desc)
{
	return test_and_clear_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_and_set_pir(int vector, struct pi_desc *pi_desc)
{
	return test_and_set_bit(vector, (unsigned long *)pi_desc->pir);
}

static inline void pi_set_sn(struct pi_desc *pi_desc)
{
	return set_bit(POSTED_INTR_SN,
			(unsigned long *)&pi_desc->control);
}

static inline void pi_set_on(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_clear_on(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline int pi_test_on(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_sn(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_SN,
			(unsigned long *)&pi_desc->control);
}

static inline u8 vmx_get_rvi(void)
{
	return vmcs_read16(GUEST_INTR_STATUS) & 0xff;
}

static inline void vm_entry_controls_reset_shadow(struct vcpu_vmx *vmx)
{
	vmx->vm_entry_controls_shadow = vmcs_read32(VM_ENTRY_CONTROLS);
}

static inline void vm_entry_controls_init(struct vcpu_vmx *vmx, u32 val)
{
	vmcs_write32(VM_ENTRY_CONTROLS, val);
	vmx->vm_entry_controls_shadow = val;
}

static inline void vm_entry_controls_set(struct vcpu_vmx *vmx, u32 val)
{
	if (vmx->vm_entry_controls_shadow != val)
		vm_entry_controls_init(vmx, val);
}

static inline u32 vm_entry_controls_get(struct vcpu_vmx *vmx)
{
	return vmx->vm_entry_controls_shadow;
}

static inline void vm_entry_controls_setbit(struct vcpu_vmx *vmx, u32 val)
{
	vm_entry_controls_set(vmx, vm_entry_controls_get(vmx) | val);
}

static inline void vm_entry_controls_clearbit(struct vcpu_vmx *vmx, u32 val)
{
	vm_entry_controls_set(vmx, vm_entry_controls_get(vmx) & ~val);
}

static inline void vm_exit_controls_reset_shadow(struct vcpu_vmx *vmx)
{
	vmx->vm_exit_controls_shadow = vmcs_read32(VM_EXIT_CONTROLS);
}

static inline void vm_exit_controls_init(struct vcpu_vmx *vmx, u32 val)
{
	vmcs_write32(VM_EXIT_CONTROLS, val);
	vmx->vm_exit_controls_shadow = val;
}

static inline void vm_exit_controls_set(struct vcpu_vmx *vmx, u32 val)
{
	if (vmx->vm_exit_controls_shadow != val)
		vm_exit_controls_init(vmx, val);
}

static inline u32 vm_exit_controls_get(struct vcpu_vmx *vmx)
{
	return vmx->vm_exit_controls_shadow;
}

static inline void vm_exit_controls_setbit(struct vcpu_vmx *vmx, u32 val)
{
	vm_exit_controls_set(vmx, vm_exit_controls_get(vmx) | val);
}

static inline void vm_exit_controls_clearbit(struct vcpu_vmx *vmx, u32 val)
{
	vm_exit_controls_set(vmx, vm_exit_controls_get(vmx) & ~val);
}

static inline void vmx_segment_cache_clear(struct vcpu_vmx *vmx)
{
	vmx->segment_cache.bitmask = 0;
}

static inline u32 vmx_vmentry_ctrl(void)
{
	u32 vmentry_ctrl = vmcs_config.vmentry_ctrl;
	if (pt_mode == PT_MODE_SYSTEM)
		vmentry_ctrl &= ~(VM_EXIT_PT_CONCEAL_PIP | VM_EXIT_CLEAR_IA32_RTIT_CTL);
	/* Loading of EFER and PERF_GLOBAL_CTRL are toggled dynamically */
	return vmentry_ctrl &
		~(VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VM_ENTRY_LOAD_IA32_EFER);
}

static inline u32 vmx_vmexit_ctrl(void)
{
	u32 vmexit_ctrl = vmcs_config.vmexit_ctrl;
	if (pt_mode == PT_MODE_SYSTEM)
		vmexit_ctrl &= ~(VM_ENTRY_PT_CONCEAL_PIP | VM_ENTRY_LOAD_IA32_RTIT_CTL);
	/* Loading of EFER and PERF_GLOBAL_CTRL are toggled dynamically */
	return vmcs_config.vmexit_ctrl &
		~(VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL | VM_EXIT_LOAD_IA32_EFER);
}

u32 vmx_exec_control(struct vcpu_vmx *vmx);

static inline struct kvm_vmx *to_kvm_vmx(struct kvm *kvm)
{
	return container_of(kvm, struct kvm_vmx, kvm);
}

static inline struct vcpu_vmx *to_vmx(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_vmx, vcpu);
}

static inline struct pi_desc *vcpu_to_pi_desc(struct kvm_vcpu *vcpu)
{
	return &(to_vmx(vcpu)->pi_desc);
}

struct vmcs *alloc_vmcs_cpu(bool shadow, int cpu);
void free_vmcs(struct vmcs *vmcs);
int alloc_loaded_vmcs(struct loaded_vmcs *loaded_vmcs);
void free_loaded_vmcs(struct loaded_vmcs *loaded_vmcs);
void loaded_vmcs_init(struct loaded_vmcs *loaded_vmcs);
void loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs);

static inline struct vmcs *alloc_vmcs(bool shadow)
{
	return alloc_vmcs_cpu(shadow, raw_smp_processor_id());
}

u64 construct_eptp(struct kvm_vcpu *vcpu, unsigned long root_hpa);

static inline void __vmx_flush_tlb(struct kvm_vcpu *vcpu, int vpid,
				bool invalidate_gpa)
{
	if (enable_ept && (invalidate_gpa || !enable_vpid)) {
		if (!VALID_PAGE(vcpu->arch.mmu->root_hpa))
			return;
		ept_sync_context(construct_eptp(vcpu,
						vcpu->arch.mmu->root_hpa));
	} else {
		vpid_sync_context(vpid);
	}
}

static inline void vmx_flush_tlb(struct kvm_vcpu *vcpu, bool invalidate_gpa)
{
	__vmx_flush_tlb(vcpu, to_vmx(vcpu)->vpid, invalidate_gpa);
}

static inline void decache_tsc_multiplier(struct vcpu_vmx *vmx)
{
	vmx->current_tsc_ratio = vmx->vcpu.arch.tsc_scaling_ratio;
	vmcs_write64(TSC_MULTIPLIER, vmx->current_tsc_ratio);
}

#endif /* __KVM_X86_VMX_H */

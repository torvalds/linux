// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * AMD SVM support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 */

#ifndef __SVM_SVM_H
#define __SVM_SVM_H

#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/bits.h>

#include <asm/svm.h>
#include <asm/sev-common.h>

#include "kvm_cache_regs.h"

#define __sme_page_pa(x) __sme_set(page_to_pfn(x) << PAGE_SHIFT)

#define	IOPM_SIZE PAGE_SIZE * 3
#define	MSRPM_SIZE PAGE_SIZE * 2

#define MAX_DIRECT_ACCESS_MSRS	46
#define MSRPM_OFFSETS	32
extern u32 msrpm_offsets[MSRPM_OFFSETS] __read_mostly;
extern bool npt_enabled;
extern int vgif;
extern bool intercept_smi;
extern bool x2avic_enabled;
extern bool vnmi;

/*
 * Clean bits in VMCB.
 * VMCB_ALL_CLEAN_MASK might also need to
 * be updated if this enum is modified.
 */
enum {
	VMCB_INTERCEPTS, /* Intercept vectors, TSC offset,
			    pause filter count */
	VMCB_PERM_MAP,   /* IOPM Base and MSRPM Base */
	VMCB_ASID,	 /* ASID */
	VMCB_INTR,	 /* int_ctl, int_vector */
	VMCB_NPT,        /* npt_en, nCR3, gPAT */
	VMCB_CR,	 /* CR0, CR3, CR4, EFER */
	VMCB_DR,         /* DR6, DR7 */
	VMCB_DT,         /* GDT, IDT */
	VMCB_SEG,        /* CS, DS, SS, ES, CPL */
	VMCB_CR2,        /* CR2 only */
	VMCB_LBR,        /* DBGCTL, BR_FROM, BR_TO, LAST_EX_FROM, LAST_EX_TO */
	VMCB_AVIC,       /* AVIC APIC_BAR, AVIC APIC_BACKING_PAGE,
			  * AVIC PHYSICAL_TABLE pointer,
			  * AVIC LOGICAL_TABLE pointer
			  */
	VMCB_SW = 31,    /* Reserved for hypervisor/software use */
};

#define VMCB_ALL_CLEAN_MASK (					\
	(1U << VMCB_INTERCEPTS) | (1U << VMCB_PERM_MAP) |	\
	(1U << VMCB_ASID) | (1U << VMCB_INTR) |			\
	(1U << VMCB_NPT) | (1U << VMCB_CR) | (1U << VMCB_DR) |	\
	(1U << VMCB_DT) | (1U << VMCB_SEG) | (1U << VMCB_CR2) |	\
	(1U << VMCB_LBR) | (1U << VMCB_AVIC) |			\
	(1U << VMCB_SW))

/* TPR and CR2 are always written before VMRUN */
#define VMCB_ALWAYS_DIRTY_MASK	((1U << VMCB_INTR) | (1U << VMCB_CR2))

struct kvm_sev_info {
	bool active;		/* SEV enabled guest */
	bool es_active;		/* SEV-ES enabled guest */
	unsigned int asid;	/* ASID used for this guest */
	unsigned int handle;	/* SEV firmware handle */
	int fd;			/* SEV device fd */
	unsigned long pages_locked; /* Number of pages locked */
	struct list_head regions_list;  /* List of registered regions */
	u64 ap_jump_table;	/* SEV-ES AP Jump Table address */
	struct kvm *enc_context_owner; /* Owner of copied encryption context */
	struct list_head mirror_vms; /* List of VMs mirroring */
	struct list_head mirror_entry; /* Use as a list entry of mirrors */
	struct misc_cg *misc_cg; /* For misc cgroup accounting */
	atomic_t migration_in_progress;
};

struct kvm_svm {
	struct kvm kvm;

	/* Struct members for AVIC */
	u32 avic_vm_id;
	struct page *avic_logical_id_table_page;
	struct page *avic_physical_id_table_page;
	struct hlist_node hnode;

	struct kvm_sev_info sev_info;
};

struct kvm_vcpu;

struct kvm_vmcb_info {
	struct vmcb *ptr;
	unsigned long pa;
	int cpu;
	uint64_t asid_generation;
};

struct vmcb_save_area_cached {
	u64 efer;
	u64 cr4;
	u64 cr3;
	u64 cr0;
	u64 dr7;
	u64 dr6;
};

struct vmcb_ctrl_area_cached {
	u32 intercepts[MAX_INTERCEPT];
	u16 pause_filter_thresh;
	u16 pause_filter_count;
	u64 iopm_base_pa;
	u64 msrpm_base_pa;
	u64 tsc_offset;
	u32 asid;
	u8 tlb_ctl;
	u32 int_ctl;
	u32 int_vector;
	u32 int_state;
	u32 exit_code;
	u32 exit_code_hi;
	u64 exit_info_1;
	u64 exit_info_2;
	u32 exit_int_info;
	u32 exit_int_info_err;
	u64 nested_ctl;
	u32 event_inj;
	u32 event_inj_err;
	u64 next_rip;
	u64 nested_cr3;
	u64 virt_ext;
	u32 clean;
	union {
		struct hv_vmcb_enlightenments hv_enlightenments;
		u8 reserved_sw[32];
	};
};

struct svm_nested_state {
	struct kvm_vmcb_info vmcb02;
	u64 hsave_msr;
	u64 vm_cr_msr;
	u64 vmcb12_gpa;
	u64 last_vmcb12_gpa;

	/* These are the merged vectors */
	u32 *msrpm;

	/* A VMRUN has started but has not yet been performed, so
	 * we cannot inject a nested vmexit yet.  */
	bool nested_run_pending;

	/* cache for control fields of the guest */
	struct vmcb_ctrl_area_cached ctl;

	/*
	 * Note: this struct is not kept up-to-date while L2 runs; it is only
	 * valid within nested_svm_vmrun.
	 */
	struct vmcb_save_area_cached save;

	bool initialized;

	/*
	 * Indicates whether MSR bitmap for L2 needs to be rebuilt due to
	 * changes in MSR bitmap for L1 or switching to a different L2. Note,
	 * this flag can only be used reliably in conjunction with a paravirt L1
	 * which informs L0 whether any changes to MSR bitmap for L2 were done
	 * on its side.
	 */
	bool force_msr_bitmap_recalc;
};

struct vcpu_sev_es_state {
	/* SEV-ES support */
	struct sev_es_save_area *vmsa;
	struct ghcb *ghcb;
	struct kvm_host_map ghcb_map;
	bool received_first_sipi;

	/* SEV-ES scratch area support */
	void *ghcb_sa;
	u32 ghcb_sa_len;
	bool ghcb_sa_sync;
	bool ghcb_sa_free;
};

struct vcpu_svm {
	struct kvm_vcpu vcpu;
	/* vmcb always points at current_vmcb->ptr, it's purely a shorthand. */
	struct vmcb *vmcb;
	struct kvm_vmcb_info vmcb01;
	struct kvm_vmcb_info *current_vmcb;
	u32 asid;
	u32 sysenter_esp_hi;
	u32 sysenter_eip_hi;
	uint64_t tsc_aux;

	u64 msr_decfg;

	u64 next_rip;

	u64 spec_ctrl;

	u64 tsc_ratio_msr;
	/*
	 * Contains guest-controlled bits of VIRT_SPEC_CTRL, which will be
	 * translated into the appropriate L2_CFG bits on the host to
	 * perform speculative control.
	 */
	u64 virt_spec_ctrl;

	u32 *msrpm;

	ulong nmi_iret_rip;

	struct svm_nested_state nested;

	/* NMI mask value, used when vNMI is not enabled */
	bool nmi_masked;

	/*
	 * True when NMIs are still masked but guest IRET was just intercepted
	 * and KVM is waiting for RIP to change, which will signal that the
	 * intercepted IRET was retired and thus NMI can be unmasked.
	 */
	bool awaiting_iret_completion;

	/*
	 * Set when KVM is awaiting IRET completion and needs to inject NMIs as
	 * soon as the IRET completes (e.g. NMI is pending injection).  KVM
	 * temporarily steals RFLAGS.TF to single-step the guest in this case
	 * in order to regain control as soon as the NMI-blocking condition
	 * goes away.
	 */
	bool nmi_singlestep;
	u64 nmi_singlestep_guest_rflags;

	bool nmi_l1_to_l2;

	unsigned long soft_int_csbase;
	unsigned long soft_int_old_rip;
	unsigned long soft_int_next_rip;
	bool soft_int_injected;

	/* optional nested SVM features that are enabled for this guest  */
	bool nrips_enabled                : 1;
	bool tsc_scaling_enabled          : 1;
	bool v_vmload_vmsave_enabled      : 1;
	bool lbrv_enabled                 : 1;
	bool pause_filter_enabled         : 1;
	bool pause_threshold_enabled      : 1;
	bool vgif_enabled                 : 1;
	bool vnmi_enabled                 : 1;

	u32 ldr_reg;
	u32 dfr_reg;
	struct page *avic_backing_page;
	u64 *avic_physical_id_cache;

	/*
	 * Per-vcpu list of struct amd_svm_iommu_ir:
	 * This is used mainly to store interrupt remapping information used
	 * when update the vcpu affinity. This avoids the need to scan for
	 * IRTE and try to match ga_tag in the IOMMU driver.
	 */
	struct list_head ir_list;
	spinlock_t ir_list_lock;

	/* Save desired MSR intercept (read: pass-through) state */
	struct {
		DECLARE_BITMAP(read, MAX_DIRECT_ACCESS_MSRS);
		DECLARE_BITMAP(write, MAX_DIRECT_ACCESS_MSRS);
	} shadow_msr_intercept;

	struct vcpu_sev_es_state sev_es;

	bool guest_state_loaded;

	bool x2avic_msrs_intercepted;

	/* Guest GIF value, used when vGIF is not enabled */
	bool guest_gif;
};

struct svm_cpu_data {
	u64 asid_generation;
	u32 max_asid;
	u32 next_asid;
	u32 min_asid;

	struct page *save_area;
	unsigned long save_area_pa;

	struct vmcb *current_vmcb;

	/* index = sev_asid, value = vmcb pointer */
	struct vmcb **sev_vmcbs;
};

DECLARE_PER_CPU(struct svm_cpu_data, svm_data);

void recalc_intercepts(struct vcpu_svm *svm);

static __always_inline struct kvm_svm *to_kvm_svm(struct kvm *kvm)
{
	return container_of(kvm, struct kvm_svm, kvm);
}

static __always_inline bool sev_guest(struct kvm *kvm)
{
#ifdef CONFIG_KVM_AMD_SEV
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	return sev->active;
#else
	return false;
#endif
}

static __always_inline bool sev_es_guest(struct kvm *kvm)
{
#ifdef CONFIG_KVM_AMD_SEV
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	return sev->es_active && !WARN_ON_ONCE(!sev->active);
#else
	return false;
#endif
}

static inline void vmcb_mark_all_dirty(struct vmcb *vmcb)
{
	vmcb->control.clean = 0;
}

static inline void vmcb_mark_all_clean(struct vmcb *vmcb)
{
	vmcb->control.clean = VMCB_ALL_CLEAN_MASK
			       & ~VMCB_ALWAYS_DIRTY_MASK;
}

static inline void vmcb_mark_dirty(struct vmcb *vmcb, int bit)
{
	vmcb->control.clean &= ~(1 << bit);
}

static inline bool vmcb_is_dirty(struct vmcb *vmcb, int bit)
{
        return !test_bit(bit, (unsigned long *)&vmcb->control.clean);
}

static __always_inline struct vcpu_svm *to_svm(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_svm, vcpu);
}

/*
 * Only the PDPTRs are loaded on demand into the shadow MMU.  All other
 * fields are synchronized on VM-Exit, because accessing the VMCB is cheap.
 *
 * CR3 might be out of date in the VMCB but it is not marked dirty; instead,
 * KVM_REQ_LOAD_MMU_PGD is always requested when the cached vcpu->arch.cr3
 * is changed.  svm_load_mmu_pgd() then syncs the new CR3 value into the VMCB.
 */
#define SVM_REGS_LAZY_LOAD_SET	(1 << VCPU_EXREG_PDPTR)

static inline void vmcb_set_intercept(struct vmcb_control_area *control, u32 bit)
{
	WARN_ON_ONCE(bit >= 32 * MAX_INTERCEPT);
	__set_bit(bit, (unsigned long *)&control->intercepts);
}

static inline void vmcb_clr_intercept(struct vmcb_control_area *control, u32 bit)
{
	WARN_ON_ONCE(bit >= 32 * MAX_INTERCEPT);
	__clear_bit(bit, (unsigned long *)&control->intercepts);
}

static inline bool vmcb_is_intercept(struct vmcb_control_area *control, u32 bit)
{
	WARN_ON_ONCE(bit >= 32 * MAX_INTERCEPT);
	return test_bit(bit, (unsigned long *)&control->intercepts);
}

static inline bool vmcb12_is_intercept(struct vmcb_ctrl_area_cached *control, u32 bit)
{
	WARN_ON_ONCE(bit >= 32 * MAX_INTERCEPT);
	return test_bit(bit, (unsigned long *)&control->intercepts);
}

static inline void set_dr_intercepts(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	if (!sev_es_guest(svm->vcpu.kvm)) {
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR0_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR1_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR2_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR3_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR4_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR5_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR6_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR0_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR1_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR2_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR3_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR4_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR5_WRITE);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR6_WRITE);
	}

	vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_READ);
	vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_WRITE);

	recalc_intercepts(svm);
}

static inline void clr_dr_intercepts(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	vmcb->control.intercepts[INTERCEPT_DR] = 0;

	/* DR7 access must remain intercepted for an SEV-ES guest */
	if (sev_es_guest(svm->vcpu.kvm)) {
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_WRITE);
	}

	recalc_intercepts(svm);
}

static inline void set_exception_intercept(struct vcpu_svm *svm, u32 bit)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	WARN_ON_ONCE(bit >= 32);
	vmcb_set_intercept(&vmcb->control, INTERCEPT_EXCEPTION_OFFSET + bit);

	recalc_intercepts(svm);
}

static inline void clr_exception_intercept(struct vcpu_svm *svm, u32 bit)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	WARN_ON_ONCE(bit >= 32);
	vmcb_clr_intercept(&vmcb->control, INTERCEPT_EXCEPTION_OFFSET + bit);

	recalc_intercepts(svm);
}

static inline void svm_set_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	vmcb_set_intercept(&vmcb->control, bit);

	recalc_intercepts(svm);
}

static inline void svm_clr_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	vmcb_clr_intercept(&vmcb->control, bit);

	recalc_intercepts(svm);
}

static inline bool svm_is_intercept(struct vcpu_svm *svm, int bit)
{
	return vmcb_is_intercept(&svm->vmcb->control, bit);
}

static inline bool nested_vgif_enabled(struct vcpu_svm *svm)
{
	return svm->vgif_enabled && (svm->nested.ctl.int_ctl & V_GIF_ENABLE_MASK);
}

static inline struct vmcb *get_vgif_vmcb(struct vcpu_svm *svm)
{
	if (!vgif)
		return NULL;

	if (is_guest_mode(&svm->vcpu) && !nested_vgif_enabled(svm))
		return svm->nested.vmcb02.ptr;
	else
		return svm->vmcb01.ptr;
}

static inline void enable_gif(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_vgif_vmcb(svm);

	if (vmcb)
		vmcb->control.int_ctl |= V_GIF_MASK;
	else
		svm->guest_gif = true;
}

static inline void disable_gif(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_vgif_vmcb(svm);

	if (vmcb)
		vmcb->control.int_ctl &= ~V_GIF_MASK;
	else
		svm->guest_gif = false;
}

static inline bool gif_set(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_vgif_vmcb(svm);

	if (vmcb)
		return !!(vmcb->control.int_ctl & V_GIF_MASK);
	else
		return svm->guest_gif;
}

static inline bool nested_npt_enabled(struct vcpu_svm *svm)
{
	return svm->nested.ctl.nested_ctl & SVM_NESTED_CTL_NP_ENABLE;
}

static inline bool nested_vnmi_enabled(struct vcpu_svm *svm)
{
	return svm->vnmi_enabled &&
	       (svm->nested.ctl.int_ctl & V_NMI_ENABLE_MASK);
}

static inline bool is_x2apic_msrpm_offset(u32 offset)
{
	/* 4 msrs per u8, and 4 u8 in u32 */
	u32 msr = offset * 16;

	return (msr >= APIC_BASE_MSR) &&
	       (msr < (APIC_BASE_MSR + 0x100));
}

static inline struct vmcb *get_vnmi_vmcb_l1(struct vcpu_svm *svm)
{
	if (!vnmi)
		return NULL;

	if (is_guest_mode(&svm->vcpu))
		return NULL;
	else
		return svm->vmcb01.ptr;
}

static inline bool is_vnmi_enabled(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_vnmi_vmcb_l1(svm);

	if (vmcb)
		return !!(vmcb->control.int_ctl & V_NMI_ENABLE_MASK);
	else
		return false;
}

/* svm.c */
#define MSR_INVALID				0xffffffffU

#define DEBUGCTL_RESERVED_BITS (~(0x3fULL))

extern bool dump_invalid_vmcb;

u32 svm_msrpm_offset(u32 msr);
u32 *svm_vcpu_alloc_msrpm(void);
void svm_vcpu_init_msrpm(struct kvm_vcpu *vcpu, u32 *msrpm);
void svm_vcpu_free_msrpm(u32 *msrpm);
void svm_copy_lbrs(struct vmcb *to_vmcb, struct vmcb *from_vmcb);
void svm_update_lbrv(struct kvm_vcpu *vcpu);

int svm_set_efer(struct kvm_vcpu *vcpu, u64 efer);
void svm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void svm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
void disable_nmi_singlestep(struct vcpu_svm *svm);
bool svm_smi_blocked(struct kvm_vcpu *vcpu);
bool svm_nmi_blocked(struct kvm_vcpu *vcpu);
bool svm_interrupt_blocked(struct kvm_vcpu *vcpu);
void svm_set_gif(struct vcpu_svm *svm, bool value);
int svm_invoke_exit_handler(struct kvm_vcpu *vcpu, u64 exit_code);
void set_msr_interception(struct kvm_vcpu *vcpu, u32 *msrpm, u32 msr,
			  int read, int write);
void svm_set_x2apic_msr_interception(struct vcpu_svm *svm, bool disable);
void svm_complete_interrupt_delivery(struct kvm_vcpu *vcpu, int delivery_mode,
				     int trig_mode, int vec);

/* nested.c */

#define NESTED_EXIT_HOST	0	/* Exit handled on host level */
#define NESTED_EXIT_DONE	1	/* Exit caused nested vmexit  */
#define NESTED_EXIT_CONTINUE	2	/* Further checks needed      */

static inline bool nested_svm_virtualize_tpr(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return is_guest_mode(vcpu) && (svm->nested.ctl.int_ctl & V_INTR_MASKING_MASK);
}

static inline bool nested_exit_on_smi(struct vcpu_svm *svm)
{
	return vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_SMI);
}

static inline bool nested_exit_on_intr(struct vcpu_svm *svm)
{
	return vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_INTR);
}

static inline bool nested_exit_on_nmi(struct vcpu_svm *svm)
{
	return vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_NMI);
}

int enter_svm_guest_mode(struct kvm_vcpu *vcpu,
			 u64 vmcb_gpa, struct vmcb *vmcb12, bool from_vmrun);
void svm_leave_nested(struct kvm_vcpu *vcpu);
void svm_free_nested(struct vcpu_svm *svm);
int svm_allocate_nested(struct vcpu_svm *svm);
int nested_svm_vmrun(struct kvm_vcpu *vcpu);
void svm_copy_vmrun_state(struct vmcb_save_area *to_save,
			  struct vmcb_save_area *from_save);
void svm_copy_vmloadsave_state(struct vmcb *to_vmcb, struct vmcb *from_vmcb);
int nested_svm_vmexit(struct vcpu_svm *svm);

static inline int nested_svm_simple_vmexit(struct vcpu_svm *svm, u32 exit_code)
{
	svm->vmcb->control.exit_code   = exit_code;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;
	return nested_svm_vmexit(svm);
}

int nested_svm_exit_handled(struct vcpu_svm *svm);
int nested_svm_check_permissions(struct kvm_vcpu *vcpu);
int nested_svm_check_exception(struct vcpu_svm *svm, unsigned nr,
			       bool has_error_code, u32 error_code);
int nested_svm_exit_special(struct vcpu_svm *svm);
void nested_svm_update_tsc_ratio_msr(struct kvm_vcpu *vcpu);
void __svm_write_tsc_multiplier(u64 multiplier);
void nested_copy_vmcb_control_to_cache(struct vcpu_svm *svm,
				       struct vmcb_control_area *control);
void nested_copy_vmcb_save_to_cache(struct vcpu_svm *svm,
				    struct vmcb_save_area *save);
void nested_sync_control_from_vmcb02(struct vcpu_svm *svm);
void nested_vmcb02_compute_g_pat(struct vcpu_svm *svm);
void svm_switch_vmcb(struct vcpu_svm *svm, struct kvm_vmcb_info *target_vmcb);

extern struct kvm_x86_nested_ops svm_nested_ops;

/* avic.c */
#define AVIC_REQUIRED_APICV_INHIBITS			\
(							\
	BIT(APICV_INHIBIT_REASON_DISABLE) |		\
	BIT(APICV_INHIBIT_REASON_ABSENT) |		\
	BIT(APICV_INHIBIT_REASON_HYPERV) |		\
	BIT(APICV_INHIBIT_REASON_NESTED) |		\
	BIT(APICV_INHIBIT_REASON_IRQWIN) |		\
	BIT(APICV_INHIBIT_REASON_PIT_REINJ) |		\
	BIT(APICV_INHIBIT_REASON_BLOCKIRQ) |		\
	BIT(APICV_INHIBIT_REASON_SEV)      |		\
	BIT(APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED) |	\
	BIT(APICV_INHIBIT_REASON_APIC_ID_MODIFIED) |	\
	BIT(APICV_INHIBIT_REASON_APIC_BASE_MODIFIED) |	\
	BIT(APICV_INHIBIT_REASON_LOGICAL_ID_ALIASED)	\
)

bool avic_hardware_setup(void);
int avic_ga_log_notifier(u32 ga_tag);
void avic_vm_destroy(struct kvm *kvm);
int avic_vm_init(struct kvm *kvm);
void avic_init_vmcb(struct vcpu_svm *svm, struct vmcb *vmcb);
int avic_incomplete_ipi_interception(struct kvm_vcpu *vcpu);
int avic_unaccelerated_access_interception(struct kvm_vcpu *vcpu);
int avic_init_vcpu(struct vcpu_svm *svm);
void avic_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void avic_vcpu_put(struct kvm_vcpu *vcpu);
void avic_apicv_post_state_restore(struct kvm_vcpu *vcpu);
void avic_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu);
int avic_pi_update_irte(struct kvm *kvm, unsigned int host_irq,
			uint32_t guest_irq, bool set);
void avic_vcpu_blocking(struct kvm_vcpu *vcpu);
void avic_vcpu_unblocking(struct kvm_vcpu *vcpu);
void avic_ring_doorbell(struct kvm_vcpu *vcpu);
unsigned long avic_vcpu_get_apicv_inhibit_reasons(struct kvm_vcpu *vcpu);
void avic_refresh_virtual_apic_mode(struct kvm_vcpu *vcpu);


/* sev.c */

#define GHCB_VERSION_MAX	1ULL
#define GHCB_VERSION_MIN	1ULL


extern unsigned int max_sev_asid;

void sev_vm_destroy(struct kvm *kvm);
int sev_mem_enc_ioctl(struct kvm *kvm, void __user *argp);
int sev_mem_enc_register_region(struct kvm *kvm,
				struct kvm_enc_region *range);
int sev_mem_enc_unregister_region(struct kvm *kvm,
				  struct kvm_enc_region *range);
int sev_vm_copy_enc_context_from(struct kvm *kvm, unsigned int source_fd);
int sev_vm_move_enc_context_from(struct kvm *kvm, unsigned int source_fd);
void sev_guest_memory_reclaimed(struct kvm *kvm);

void pre_sev_run(struct vcpu_svm *svm, int cpu);
void __init sev_set_cpu_caps(void);
void __init sev_hardware_setup(void);
void sev_hardware_unsetup(void);
int sev_cpu_init(struct svm_cpu_data *sd);
void sev_init_vmcb(struct vcpu_svm *svm);
void sev_free_vcpu(struct kvm_vcpu *vcpu);
int sev_handle_vmgexit(struct kvm_vcpu *vcpu);
int sev_es_string_io(struct vcpu_svm *svm, int size, unsigned int port, int in);
void sev_es_vcpu_reset(struct vcpu_svm *svm);
void sev_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector);
void sev_es_prepare_switch_to_guest(struct sev_es_save_area *hostsa);
void sev_es_unmap_ghcb(struct vcpu_svm *svm);

/* vmenter.S */

void __svm_sev_es_vcpu_run(struct vcpu_svm *svm, bool spec_ctrl_intercepted);
void __svm_vcpu_run(struct vcpu_svm *svm, bool spec_ctrl_intercepted);

#endif

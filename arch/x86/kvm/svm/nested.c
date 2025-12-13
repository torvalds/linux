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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/kernel.h>

#include <asm/msr-index.h>
#include <asm/debugreg.h>

#include "kvm_emulate.h"
#include "trace.h"
#include "mmu.h"
#include "x86.h"
#include "smm.h"
#include "cpuid.h"
#include "lapic.h"
#include "svm.h"
#include "hyperv.h"

#define CC KVM_NESTED_VMENTER_CONSISTENCY_CHECK

static void nested_svm_inject_npf_exit(struct kvm_vcpu *vcpu,
				       struct x86_exception *fault)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;

	if (vmcb->control.exit_code != SVM_EXIT_NPF) {
		/*
		 * TODO: track the cause of the nested page fault, and
		 * correctly fill in the high bits of exit_info_1.
		 */
		vmcb->control.exit_code = SVM_EXIT_NPF;
		vmcb->control.exit_code_hi = 0;
		vmcb->control.exit_info_1 = (1ULL << 32);
		vmcb->control.exit_info_2 = fault->address;
	}

	vmcb->control.exit_info_1 &= ~0xffffffffULL;
	vmcb->control.exit_info_1 |= fault->error_code;

	nested_svm_vmexit(svm);
}

static u64 nested_svm_get_tdp_pdptr(struct kvm_vcpu *vcpu, int index)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr3 = svm->nested.ctl.nested_cr3;
	u64 pdpte;
	int ret;

	/*
	 * Note, nCR3 is "assumed" to be 32-byte aligned, i.e. the CPU ignores
	 * nCR3[4:0] when loading PDPTEs from memory.
	 */
	ret = kvm_vcpu_read_guest_page(vcpu, gpa_to_gfn(cr3), &pdpte,
				       (cr3 & GENMASK(11, 5)) + index * 8, 8);
	if (ret)
		return 0;
	return pdpte;
}

static unsigned long nested_svm_get_tdp_cr3(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return svm->nested.ctl.nested_cr3;
}

static void nested_svm_init_mmu_context(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	WARN_ON(mmu_is_nested(vcpu));

	vcpu->arch.mmu = &vcpu->arch.guest_mmu;

	/*
	 * The NPT format depends on L1's CR4 and EFER, which is in vmcb01.  Note,
	 * when called via KVM_SET_NESTED_STATE, that state may _not_ match current
	 * vCPU state.  CR0.WP is explicitly ignored, while CR0.PG is required.
	 */
	kvm_init_shadow_npt_mmu(vcpu, X86_CR0_PG, svm->vmcb01.ptr->save.cr4,
				svm->vmcb01.ptr->save.efer,
				svm->nested.ctl.nested_cr3);
	vcpu->arch.mmu->get_guest_pgd     = nested_svm_get_tdp_cr3;
	vcpu->arch.mmu->get_pdptr         = nested_svm_get_tdp_pdptr;
	vcpu->arch.mmu->inject_page_fault = nested_svm_inject_npf_exit;
	vcpu->arch.walk_mmu              = &vcpu->arch.nested_mmu;
}

static void nested_svm_uninit_mmu_context(struct kvm_vcpu *vcpu)
{
	vcpu->arch.mmu = &vcpu->arch.root_mmu;
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;
}

static bool nested_vmcb_needs_vls_intercept(struct vcpu_svm *svm)
{
	if (!guest_cpu_cap_has(&svm->vcpu, X86_FEATURE_V_VMSAVE_VMLOAD))
		return true;

	if (!nested_npt_enabled(svm))
		return true;

	if (!(svm->nested.ctl.virt_ext & VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK))
		return true;

	return false;
}

void recalc_intercepts(struct vcpu_svm *svm)
{
	struct vmcb_control_area *c, *h;
	struct vmcb_ctrl_area_cached *g;
	unsigned int i;

	vmcb_mark_dirty(svm->vmcb, VMCB_INTERCEPTS);

	if (!is_guest_mode(&svm->vcpu))
		return;

	c = &svm->vmcb->control;
	h = &svm->vmcb01.ptr->control;
	g = &svm->nested.ctl;

	for (i = 0; i < MAX_INTERCEPT; i++)
		c->intercepts[i] = h->intercepts[i];

	if (g->int_ctl & V_INTR_MASKING_MASK) {
		/*
		 * If L2 is active and V_INTR_MASKING is enabled in vmcb12,
		 * disable intercept of CR8 writes as L2's CR8 does not affect
		 * any interrupt KVM may want to inject.
		 *
		 * Similarly, disable intercept of virtual interrupts (used to
		 * detect interrupt windows) if the saved RFLAGS.IF is '0', as
		 * the effective RFLAGS.IF for L1 interrupts will never be set
		 * while L2 is running (L2's RFLAGS.IF doesn't affect L1 IRQs).
		 */
		vmcb_clr_intercept(c, INTERCEPT_CR8_WRITE);
		if (!(svm->vmcb01.ptr->save.rflags & X86_EFLAGS_IF))
			vmcb_clr_intercept(c, INTERCEPT_VINTR);
	}

	/*
	 * We want to see VMMCALLs from a nested guest only when Hyper-V L2 TLB
	 * flush feature is enabled.
	 */
	if (!nested_svm_l2_tlb_flush_enabled(&svm->vcpu))
		vmcb_clr_intercept(c, INTERCEPT_VMMCALL);

	for (i = 0; i < MAX_INTERCEPT; i++)
		c->intercepts[i] |= g->intercepts[i];

	/* If SMI is not intercepted, ignore guest SMI intercept as well  */
	if (!intercept_smi)
		vmcb_clr_intercept(c, INTERCEPT_SMI);

	if (nested_vmcb_needs_vls_intercept(svm)) {
		/*
		 * If the virtual VMLOAD/VMSAVE is not enabled for the L2,
		 * we must intercept these instructions to correctly
		 * emulate them in case L1 doesn't intercept them.
		 */
		vmcb_set_intercept(c, INTERCEPT_VMLOAD);
		vmcb_set_intercept(c, INTERCEPT_VMSAVE);
	} else {
		WARN_ON(!(c->virt_ext & VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK));
	}
}

/*
 * This array (and its actual size) holds the set of offsets (indexing by chunk
 * size) to process when merging vmcb12's MSRPM with vmcb01's MSRPM.  Note, the
 * set of MSRs for which interception is disabled in vmcb01 is per-vCPU, e.g.
 * based on CPUID features.  This array only tracks MSRs that *might* be passed
 * through to the guest.
 *
 * Hardcode the capacity of the array based on the maximum number of _offsets_.
 * MSRs are batched together, so there are fewer offsets than MSRs.
 */
static int nested_svm_msrpm_merge_offsets[7] __ro_after_init;
static int nested_svm_nr_msrpm_merge_offsets __ro_after_init;
typedef unsigned long nsvm_msrpm_merge_t;

int __init nested_svm_init_msrpm_merge_offsets(void)
{
	static const u32 merge_msrs[] __initconst = {
		MSR_STAR,
		MSR_IA32_SYSENTER_CS,
		MSR_IA32_SYSENTER_EIP,
		MSR_IA32_SYSENTER_ESP,
	#ifdef CONFIG_X86_64
		MSR_GS_BASE,
		MSR_FS_BASE,
		MSR_KERNEL_GS_BASE,
		MSR_LSTAR,
		MSR_CSTAR,
		MSR_SYSCALL_MASK,
	#endif
		MSR_IA32_SPEC_CTRL,
		MSR_IA32_PRED_CMD,
		MSR_IA32_FLUSH_CMD,
		MSR_IA32_APERF,
		MSR_IA32_MPERF,
		MSR_IA32_LASTBRANCHFROMIP,
		MSR_IA32_LASTBRANCHTOIP,
		MSR_IA32_LASTINTFROMIP,
		MSR_IA32_LASTINTTOIP,
	};
	int i, j;

	for (i = 0; i < ARRAY_SIZE(merge_msrs); i++) {
		int bit_nr = svm_msrpm_bit_nr(merge_msrs[i]);
		u32 offset;

		if (WARN_ON(bit_nr < 0))
			return -EIO;

		/*
		 * Merging is done in chunks to reduce the number of accesses
		 * to L1's bitmap.
		 */
		offset = bit_nr / BITS_PER_BYTE / sizeof(nsvm_msrpm_merge_t);

		for (j = 0; j < nested_svm_nr_msrpm_merge_offsets; j++) {
			if (nested_svm_msrpm_merge_offsets[j] == offset)
				break;
		}

		if (j < nested_svm_nr_msrpm_merge_offsets)
			continue;

		if (WARN_ON(j >= ARRAY_SIZE(nested_svm_msrpm_merge_offsets)))
			return -EIO;

		nested_svm_msrpm_merge_offsets[j] = offset;
		nested_svm_nr_msrpm_merge_offsets++;
	}

	return 0;
}

/*
 * Merge L0's (KVM) and L1's (Nested VMCB) MSR permission bitmaps. The function
 * is optimized in that it only merges the parts where KVM MSR permission bitmap
 * may contain zero bits.
 */
static bool nested_svm_merge_msrpm(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	nsvm_msrpm_merge_t *msrpm02 = svm->nested.msrpm;
	nsvm_msrpm_merge_t *msrpm01 = svm->msrpm;
	int i;

	/*
	 * MSR bitmap update can be skipped when:
	 * - MSR bitmap for L1 hasn't changed.
	 * - Nested hypervisor (L1) is attempting to launch the same L2 as
	 *   before.
	 * - Nested hypervisor (L1) is using Hyper-V emulation interface and
	 * tells KVM (L0) there were no changes in MSR bitmap for L2.
	 */
#ifdef CONFIG_KVM_HYPERV
	if (!svm->nested.force_msr_bitmap_recalc) {
		struct hv_vmcb_enlightenments *hve = &svm->nested.ctl.hv_enlightenments;

		if (kvm_hv_hypercall_enabled(vcpu) &&
		    hve->hv_enlightenments_control.msr_bitmap &&
		    (svm->nested.ctl.clean & BIT(HV_VMCB_NESTED_ENLIGHTENMENTS)))
			goto set_msrpm_base_pa;
	}
#endif

	if (!(vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_MSR_PROT)))
		return true;

	for (i = 0; i < nested_svm_nr_msrpm_merge_offsets; i++) {
		const int p = nested_svm_msrpm_merge_offsets[i];
		nsvm_msrpm_merge_t l1_val;
		gpa_t gpa;

		gpa = svm->nested.ctl.msrpm_base_pa + (p * sizeof(l1_val));

		if (kvm_vcpu_read_guest(vcpu, gpa, &l1_val, sizeof(l1_val)))
			return false;

		msrpm02[p] = msrpm01[p] | l1_val;
	}

	svm->nested.force_msr_bitmap_recalc = false;

#ifdef CONFIG_KVM_HYPERV
set_msrpm_base_pa:
#endif
	svm->vmcb->control.msrpm_base_pa = __sme_set(__pa(svm->nested.msrpm));

	return true;
}

/*
 * Bits 11:0 of bitmap address are ignored by hardware
 */
static bool nested_svm_check_bitmap_pa(struct kvm_vcpu *vcpu, u64 pa, u32 size)
{
	u64 addr = PAGE_ALIGN(pa);

	return kvm_vcpu_is_legal_gpa(vcpu, addr) &&
	    kvm_vcpu_is_legal_gpa(vcpu, addr + size - 1);
}

static bool __nested_vmcb_check_controls(struct kvm_vcpu *vcpu,
					 struct vmcb_ctrl_area_cached *control)
{
	if (CC(!vmcb12_is_intercept(control, INTERCEPT_VMRUN)))
		return false;

	if (CC(control->asid == 0))
		return false;

	if (CC((control->nested_ctl & SVM_NESTED_CTL_NP_ENABLE) && !npt_enabled))
		return false;

	if (CC(!nested_svm_check_bitmap_pa(vcpu, control->msrpm_base_pa,
					   MSRPM_SIZE)))
		return false;
	if (CC(!nested_svm_check_bitmap_pa(vcpu, control->iopm_base_pa,
					   IOPM_SIZE)))
		return false;

	if (CC((control->int_ctl & V_NMI_ENABLE_MASK) &&
	       !vmcb12_is_intercept(control, INTERCEPT_NMI))) {
		return false;
	}

	return true;
}

/* Common checks that apply to both L1 and L2 state.  */
static bool __nested_vmcb_check_save(struct kvm_vcpu *vcpu,
				     struct vmcb_save_area_cached *save)
{
	if (CC(!(save->efer & EFER_SVME)))
		return false;

	if (CC((save->cr0 & X86_CR0_CD) == 0 && (save->cr0 & X86_CR0_NW)) ||
	    CC(save->cr0 & ~0xffffffffULL))
		return false;

	if (CC(!kvm_dr6_valid(save->dr6)) || CC(!kvm_dr7_valid(save->dr7)))
		return false;

	/*
	 * These checks are also performed by KVM_SET_SREGS,
	 * except that EFER.LMA is not checked by SVM against
	 * CR0.PG && EFER.LME.
	 */
	if ((save->efer & EFER_LME) && (save->cr0 & X86_CR0_PG)) {
		if (CC(!(save->cr4 & X86_CR4_PAE)) ||
		    CC(!(save->cr0 & X86_CR0_PE)) ||
		    CC(!kvm_vcpu_is_legal_cr3(vcpu, save->cr3)))
			return false;
	}

	/* Note, SVM doesn't have any additional restrictions on CR4. */
	if (CC(!__kvm_is_valid_cr4(vcpu, save->cr4)))
		return false;

	if (CC(!kvm_valid_efer(vcpu, save->efer)))
		return false;

	return true;
}

static bool nested_vmcb_check_save(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_save_area_cached *save = &svm->nested.save;

	return __nested_vmcb_check_save(vcpu, save);
}

static bool nested_vmcb_check_controls(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_ctrl_area_cached *ctl = &svm->nested.ctl;

	return __nested_vmcb_check_controls(vcpu, ctl);
}

static
void __nested_copy_vmcb_control_to_cache(struct kvm_vcpu *vcpu,
					 struct vmcb_ctrl_area_cached *to,
					 struct vmcb_control_area *from)
{
	unsigned int i;

	for (i = 0; i < MAX_INTERCEPT; i++)
		to->intercepts[i] = from->intercepts[i];

	to->iopm_base_pa        = from->iopm_base_pa;
	to->msrpm_base_pa       = from->msrpm_base_pa;
	to->tsc_offset          = from->tsc_offset;
	to->tlb_ctl             = from->tlb_ctl;
	to->int_ctl             = from->int_ctl;
	to->int_vector          = from->int_vector;
	to->int_state           = from->int_state;
	to->exit_code           = from->exit_code;
	to->exit_code_hi        = from->exit_code_hi;
	to->exit_info_1         = from->exit_info_1;
	to->exit_info_2         = from->exit_info_2;
	to->exit_int_info       = from->exit_int_info;
	to->exit_int_info_err   = from->exit_int_info_err;
	to->nested_ctl          = from->nested_ctl;
	to->event_inj           = from->event_inj;
	to->event_inj_err       = from->event_inj_err;
	to->next_rip            = from->next_rip;
	to->nested_cr3          = from->nested_cr3;
	to->virt_ext            = from->virt_ext;
	to->pause_filter_count  = from->pause_filter_count;
	to->pause_filter_thresh = from->pause_filter_thresh;

	/* Copy asid here because nested_vmcb_check_controls will check it.  */
	to->asid           = from->asid;
	to->msrpm_base_pa &= ~0x0fffULL;
	to->iopm_base_pa  &= ~0x0fffULL;

#ifdef CONFIG_KVM_HYPERV
	/* Hyper-V extensions (Enlightened VMCB) */
	if (kvm_hv_hypercall_enabled(vcpu)) {
		to->clean = from->clean;
		memcpy(&to->hv_enlightenments, &from->hv_enlightenments,
		       sizeof(to->hv_enlightenments));
	}
#endif
}

void nested_copy_vmcb_control_to_cache(struct vcpu_svm *svm,
				       struct vmcb_control_area *control)
{
	__nested_copy_vmcb_control_to_cache(&svm->vcpu, &svm->nested.ctl, control);
}

static void __nested_copy_vmcb_save_to_cache(struct vmcb_save_area_cached *to,
					     struct vmcb_save_area *from)
{
	/*
	 * Copy only fields that are validated, as we need them
	 * to avoid TOC/TOU races.
	 */
	to->efer = from->efer;
	to->cr0 = from->cr0;
	to->cr3 = from->cr3;
	to->cr4 = from->cr4;

	to->dr6 = from->dr6;
	to->dr7 = from->dr7;
}

void nested_copy_vmcb_save_to_cache(struct vcpu_svm *svm,
				    struct vmcb_save_area *save)
{
	__nested_copy_vmcb_save_to_cache(&svm->nested.save, save);
}

/*
 * Synchronize fields that are written by the processor, so that
 * they can be copied back into the vmcb12.
 */
void nested_sync_control_from_vmcb02(struct vcpu_svm *svm)
{
	u32 mask;
	svm->nested.ctl.event_inj      = svm->vmcb->control.event_inj;
	svm->nested.ctl.event_inj_err  = svm->vmcb->control.event_inj_err;

	/* Only a few fields of int_ctl are written by the processor.  */
	mask = V_IRQ_MASK | V_TPR_MASK;
	/*
	 * Don't sync vmcb02 V_IRQ back to vmcb12 if KVM (L0) is intercepting
	 * virtual interrupts in order to request an interrupt window, as KVM
	 * has usurped vmcb02's int_ctl.  If an interrupt window opens before
	 * the next VM-Exit, svm_clear_vintr() will restore vmcb12's int_ctl.
	 * If no window opens, V_IRQ will be correctly preserved in vmcb12's
	 * int_ctl (because it was never recognized while L2 was running).
	 */
	if (svm_is_intercept(svm, INTERCEPT_VINTR) &&
	    !test_bit(INTERCEPT_VINTR, (unsigned long *)svm->nested.ctl.intercepts))
		mask &= ~V_IRQ_MASK;

	if (nested_vgif_enabled(svm))
		mask |= V_GIF_MASK;

	if (nested_vnmi_enabled(svm))
		mask |= V_NMI_BLOCKING_MASK | V_NMI_PENDING_MASK;

	svm->nested.ctl.int_ctl        &= ~mask;
	svm->nested.ctl.int_ctl        |= svm->vmcb->control.int_ctl & mask;
}

/*
 * Transfer any event that L0 or L1 wanted to inject into L2 to
 * EXIT_INT_INFO.
 */
static void nested_save_pending_event_to_vmcb12(struct vcpu_svm *svm,
						struct vmcb *vmcb12)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	u32 exit_int_info = 0;
	unsigned int nr;

	if (vcpu->arch.exception.injected) {
		nr = vcpu->arch.exception.vector;
		exit_int_info = nr | SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_EXEPT;

		if (vcpu->arch.exception.has_error_code) {
			exit_int_info |= SVM_EVTINJ_VALID_ERR;
			vmcb12->control.exit_int_info_err =
				vcpu->arch.exception.error_code;
		}

	} else if (vcpu->arch.nmi_injected) {
		exit_int_info = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_NMI;

	} else if (vcpu->arch.interrupt.injected) {
		nr = vcpu->arch.interrupt.nr;
		exit_int_info = nr | SVM_EVTINJ_VALID;

		if (vcpu->arch.interrupt.soft)
			exit_int_info |= SVM_EVTINJ_TYPE_SOFT;
		else
			exit_int_info |= SVM_EVTINJ_TYPE_INTR;
	}

	vmcb12->control.exit_int_info = exit_int_info;
}

static void nested_svm_transition_tlb_flush(struct kvm_vcpu *vcpu)
{
	/* Handle pending Hyper-V TLB flush requests */
	kvm_hv_nested_transtion_tlb_flush(vcpu, npt_enabled);

	/*
	 * TODO: optimize unconditional TLB flush/MMU sync.  A partial list of
	 * things to fix before this can be conditional:
	 *
	 *  - Flush TLBs for both L1 and L2 remote TLB flush
	 *  - Honor L1's request to flush an ASID on nested VMRUN
	 *  - Sync nested NPT MMU on VMRUN that flushes L2's ASID[*]
	 *  - Don't crush a pending TLB flush in vmcb02 on nested VMRUN
	 *  - Flush L1's ASID on KVM_REQ_TLB_FLUSH_GUEST
	 *
	 * [*] Unlike nested EPT, SVM's ASID management can invalidate nested
	 *     NPT guest-physical mappings on VMRUN.
	 */
	kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
	kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
}

/*
 * Load guest's/host's cr3 on nested vmentry or vmexit. @nested_npt is true
 * if we are emulating VM-Entry into a guest with NPT enabled.
 */
static int nested_svm_load_cr3(struct kvm_vcpu *vcpu, unsigned long cr3,
			       bool nested_npt, bool reload_pdptrs)
{
	if (CC(!kvm_vcpu_is_legal_cr3(vcpu, cr3)))
		return -EINVAL;

	if (reload_pdptrs && !nested_npt && is_pae_paging(vcpu) &&
	    CC(!load_pdptrs(vcpu, cr3)))
		return -EINVAL;

	vcpu->arch.cr3 = cr3;

	/* Re-initialize the MMU, e.g. to pick up CR4 MMU role changes. */
	kvm_init_mmu(vcpu);

	if (!nested_npt)
		kvm_mmu_new_pgd(vcpu, cr3);

	return 0;
}

void nested_vmcb02_compute_g_pat(struct vcpu_svm *svm)
{
	if (!svm->nested.vmcb02.ptr)
		return;

	/* FIXME: merge g_pat from vmcb01 and vmcb12.  */
	svm->nested.vmcb02.ptr->save.g_pat = svm->vmcb01.ptr->save.g_pat;
}

static void nested_vmcb02_prepare_save(struct vcpu_svm *svm, struct vmcb *vmcb12)
{
	bool new_vmcb12 = false;
	struct vmcb *vmcb01 = svm->vmcb01.ptr;
	struct vmcb *vmcb02 = svm->nested.vmcb02.ptr;
	struct kvm_vcpu *vcpu = &svm->vcpu;

	nested_vmcb02_compute_g_pat(svm);

	/* Load the nested guest state */
	if (svm->nested.vmcb12_gpa != svm->nested.last_vmcb12_gpa) {
		new_vmcb12 = true;
		svm->nested.last_vmcb12_gpa = svm->nested.vmcb12_gpa;
		svm->nested.force_msr_bitmap_recalc = true;
	}

	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_SEG))) {
		vmcb02->save.es = vmcb12->save.es;
		vmcb02->save.cs = vmcb12->save.cs;
		vmcb02->save.ss = vmcb12->save.ss;
		vmcb02->save.ds = vmcb12->save.ds;
		vmcb02->save.cpl = vmcb12->save.cpl;
		vmcb_mark_dirty(vmcb02, VMCB_SEG);
	}

	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_DT))) {
		vmcb02->save.gdtr = vmcb12->save.gdtr;
		vmcb02->save.idtr = vmcb12->save.idtr;
		vmcb_mark_dirty(vmcb02, VMCB_DT);
	}

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) &&
	    (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_CET)))) {
		vmcb02->save.s_cet  = vmcb12->save.s_cet;
		vmcb02->save.isst_addr = vmcb12->save.isst_addr;
		vmcb02->save.ssp = vmcb12->save.ssp;
		vmcb_mark_dirty(vmcb02, VMCB_CET);
	}

	kvm_set_rflags(vcpu, vmcb12->save.rflags | X86_EFLAGS_FIXED);

	svm_set_efer(vcpu, svm->nested.save.efer);

	svm_set_cr0(vcpu, svm->nested.save.cr0);
	svm_set_cr4(vcpu, svm->nested.save.cr4);

	svm->vcpu.arch.cr2 = vmcb12->save.cr2;

	kvm_rax_write(vcpu, vmcb12->save.rax);
	kvm_rsp_write(vcpu, vmcb12->save.rsp);
	kvm_rip_write(vcpu, vmcb12->save.rip);

	/* In case we don't even reach vcpu_run, the fields are not updated */
	vmcb02->save.rax = vmcb12->save.rax;
	vmcb02->save.rsp = vmcb12->save.rsp;
	vmcb02->save.rip = vmcb12->save.rip;

	/* These bits will be set properly on the first execution when new_vmc12 is true */
	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_DR))) {
		vmcb02->save.dr7 = svm->nested.save.dr7 | DR7_FIXED_1;
		svm->vcpu.arch.dr6  = svm->nested.save.dr6 | DR6_ACTIVE_LOW;
		vmcb_mark_dirty(vmcb02, VMCB_DR);
	}

	if (unlikely(guest_cpu_cap_has(vcpu, X86_FEATURE_LBRV) &&
		     (svm->nested.ctl.virt_ext & LBR_CTL_ENABLE_MASK))) {
		/*
		 * Reserved bits of DEBUGCTL are ignored.  Be consistent with
		 * svm_set_msr's definition of reserved bits.
		 */
		svm_copy_lbrs(vmcb02, vmcb12);
		vmcb02->save.dbgctl &= ~DEBUGCTL_RESERVED_BITS;
	} else {
		svm_copy_lbrs(vmcb02, vmcb01);
	}
	svm_update_lbrv(&svm->vcpu);
}

static inline bool is_evtinj_soft(u32 evtinj)
{
	u32 type = evtinj & SVM_EVTINJ_TYPE_MASK;
	u8 vector = evtinj & SVM_EVTINJ_VEC_MASK;

	if (!(evtinj & SVM_EVTINJ_VALID))
		return false;

	if (type == SVM_EVTINJ_TYPE_SOFT)
		return true;

	return type == SVM_EVTINJ_TYPE_EXEPT && kvm_exception_is_soft(vector);
}

static bool is_evtinj_nmi(u32 evtinj)
{
	u32 type = evtinj & SVM_EVTINJ_TYPE_MASK;

	if (!(evtinj & SVM_EVTINJ_VALID))
		return false;

	return type == SVM_EVTINJ_TYPE_NMI;
}

static void nested_vmcb02_prepare_control(struct vcpu_svm *svm,
					  unsigned long vmcb12_rip,
					  unsigned long vmcb12_csbase)
{
	u32 int_ctl_vmcb01_bits = V_INTR_MASKING_MASK;
	u32 int_ctl_vmcb12_bits = V_TPR_MASK | V_IRQ_INJECTION_BITS_MASK;

	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct vmcb *vmcb01 = svm->vmcb01.ptr;
	struct vmcb *vmcb02 = svm->nested.vmcb02.ptr;
	u32 pause_count12;
	u32 pause_thresh12;

	nested_svm_transition_tlb_flush(vcpu);

	/* Enter Guest-Mode */
	enter_guest_mode(vcpu);

	/*
	 * Filled at exit: exit_code, exit_code_hi, exit_info_1, exit_info_2,
	 * exit_int_info, exit_int_info_err, next_rip, insn_len, insn_bytes.
	 */

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_VGIF) &&
	    (svm->nested.ctl.int_ctl & V_GIF_ENABLE_MASK))
		int_ctl_vmcb12_bits |= (V_GIF_MASK | V_GIF_ENABLE_MASK);
	else
		int_ctl_vmcb01_bits |= (V_GIF_MASK | V_GIF_ENABLE_MASK);

	if (vnmi) {
		if (vmcb01->control.int_ctl & V_NMI_PENDING_MASK) {
			svm->vcpu.arch.nmi_pending++;
			kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
		}
		if (nested_vnmi_enabled(svm))
			int_ctl_vmcb12_bits |= (V_NMI_PENDING_MASK |
						V_NMI_ENABLE_MASK |
						V_NMI_BLOCKING_MASK);
	}

	/* Copied from vmcb01.  msrpm_base can be overwritten later.  */
	vmcb02->control.nested_ctl = vmcb01->control.nested_ctl;
	vmcb02->control.iopm_base_pa = vmcb01->control.iopm_base_pa;
	vmcb02->control.msrpm_base_pa = vmcb01->control.msrpm_base_pa;

	/*
	 * Stash vmcb02's counter if the guest hasn't moved past the guilty
	 * instruction; otherwise, reset the counter to '0'.
	 *
	 * In order to detect if L2 has made forward progress or not, track the
	 * RIP at which a bus lock has occurred on a per-vmcb12 basis.  If RIP
	 * is changed, guest has clearly made forward progress, bus_lock_counter
	 * still remained '1', so reset bus_lock_counter to '0'. Eg. In the
	 * scenario, where a buslock happened in L1 before VMRUN, the bus lock
	 * firmly happened on an instruction in the past. Even if vmcb01's
	 * counter is still '1', (because the guilty instruction got patched),
	 * the vCPU has clearly made forward progress and so KVM should reset
	 * vmcb02's counter to '0'.
	 *
	 * If the RIP hasn't changed, stash the bus lock counter at nested VMRUN
	 * to prevent the same guilty instruction from triggering a VM-Exit. Eg.
	 * if userspace rate-limits the vCPU, then it's entirely possible that
	 * L1's tick interrupt is pending by the time userspace re-runs the
	 * vCPU.  If KVM unconditionally clears the counter on VMRUN, then when
	 * L1 re-enters L2, the same instruction will trigger a VM-Exit and the
	 * entire cycle start over.
	 */
	if (vmcb02->save.rip && (svm->nested.ctl.bus_lock_rip == vmcb02->save.rip))
		vmcb02->control.bus_lock_counter = 1;
	else
		vmcb02->control.bus_lock_counter = 0;

	/* Done at vmrun: asid.  */

	/* Also overwritten later if necessary.  */
	vmcb02->control.tlb_ctl = TLB_CONTROL_DO_NOTHING;

	/* nested_cr3.  */
	if (nested_npt_enabled(svm))
		nested_svm_init_mmu_context(vcpu);

	vcpu->arch.tsc_offset = kvm_calc_nested_tsc_offset(
			vcpu->arch.l1_tsc_offset,
			svm->nested.ctl.tsc_offset,
			svm->tsc_ratio_msr);

	vmcb02->control.tsc_offset = vcpu->arch.tsc_offset;

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_TSCRATEMSR) &&
	    svm->tsc_ratio_msr != kvm_caps.default_tsc_scaling_ratio)
		nested_svm_update_tsc_ratio_msr(vcpu);

	vmcb02->control.int_ctl             =
		(svm->nested.ctl.int_ctl & int_ctl_vmcb12_bits) |
		(vmcb01->control.int_ctl & int_ctl_vmcb01_bits);

	vmcb02->control.int_vector          = svm->nested.ctl.int_vector;
	vmcb02->control.int_state           = svm->nested.ctl.int_state;
	vmcb02->control.event_inj           = svm->nested.ctl.event_inj;
	vmcb02->control.event_inj_err       = svm->nested.ctl.event_inj_err;

	/*
	 * next_rip is consumed on VMRUN as the return address pushed on the
	 * stack for injected soft exceptions/interrupts.  If nrips is exposed
	 * to L1, take it verbatim from vmcb12.  If nrips is supported in
	 * hardware but not exposed to L1, stuff the actual L2 RIP to emulate
	 * what a nrips=0 CPU would do (L1 is responsible for advancing RIP
	 * prior to injecting the event).
	 */
	if (guest_cpu_cap_has(vcpu, X86_FEATURE_NRIPS))
		vmcb02->control.next_rip    = svm->nested.ctl.next_rip;
	else if (boot_cpu_has(X86_FEATURE_NRIPS))
		vmcb02->control.next_rip    = vmcb12_rip;

	svm->nmi_l1_to_l2 = is_evtinj_nmi(vmcb02->control.event_inj);
	if (is_evtinj_soft(vmcb02->control.event_inj)) {
		svm->soft_int_injected = true;
		svm->soft_int_csbase = vmcb12_csbase;
		svm->soft_int_old_rip = vmcb12_rip;
		if (guest_cpu_cap_has(vcpu, X86_FEATURE_NRIPS))
			svm->soft_int_next_rip = svm->nested.ctl.next_rip;
		else
			svm->soft_int_next_rip = vmcb12_rip;
	}

	/* LBR_CTL_ENABLE_MASK is controlled by svm_update_lbrv() */

	if (!nested_vmcb_needs_vls_intercept(svm))
		vmcb02->control.virt_ext |= VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_PAUSEFILTER))
		pause_count12 = svm->nested.ctl.pause_filter_count;
	else
		pause_count12 = 0;
	if (guest_cpu_cap_has(vcpu, X86_FEATURE_PFTHRESHOLD))
		pause_thresh12 = svm->nested.ctl.pause_filter_thresh;
	else
		pause_thresh12 = 0;
	if (kvm_pause_in_guest(svm->vcpu.kvm)) {
		/* use guest values since host doesn't intercept PAUSE */
		vmcb02->control.pause_filter_count = pause_count12;
		vmcb02->control.pause_filter_thresh = pause_thresh12;

	} else {
		/* start from host values otherwise */
		vmcb02->control.pause_filter_count = vmcb01->control.pause_filter_count;
		vmcb02->control.pause_filter_thresh = vmcb01->control.pause_filter_thresh;

		/* ... but ensure filtering is disabled if so requested.  */
		if (vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_PAUSE)) {
			if (!pause_count12)
				vmcb02->control.pause_filter_count = 0;
			if (!pause_thresh12)
				vmcb02->control.pause_filter_thresh = 0;
		}
	}

	/*
	 * Merge guest and host intercepts - must be called with vcpu in
	 * guest-mode to take effect.
	 */
	recalc_intercepts(svm);
}

static void nested_svm_copy_common_state(struct vmcb *from_vmcb, struct vmcb *to_vmcb)
{
	/*
	 * Some VMCB state is shared between L1 and L2 and thus has to be
	 * moved at the time of nested vmrun and vmexit.
	 *
	 * VMLOAD/VMSAVE state would also belong in this category, but KVM
	 * always performs VMLOAD and VMSAVE from the VMCB01.
	 */
	to_vmcb->save.spec_ctrl = from_vmcb->save.spec_ctrl;
}

int enter_svm_guest_mode(struct kvm_vcpu *vcpu, u64 vmcb12_gpa,
			 struct vmcb *vmcb12, bool from_vmrun)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;

	trace_kvm_nested_vmenter(svm->vmcb->save.rip,
				 vmcb12_gpa,
				 vmcb12->save.rip,
				 vmcb12->control.int_ctl,
				 vmcb12->control.event_inj,
				 vmcb12->control.nested_ctl,
				 vmcb12->control.nested_cr3,
				 vmcb12->save.cr3,
				 KVM_ISA_SVM);

	trace_kvm_nested_intercepts(vmcb12->control.intercepts[INTERCEPT_CR] & 0xffff,
				    vmcb12->control.intercepts[INTERCEPT_CR] >> 16,
				    vmcb12->control.intercepts[INTERCEPT_EXCEPTION],
				    vmcb12->control.intercepts[INTERCEPT_WORD3],
				    vmcb12->control.intercepts[INTERCEPT_WORD4],
				    vmcb12->control.intercepts[INTERCEPT_WORD5]);


	svm->nested.vmcb12_gpa = vmcb12_gpa;

	WARN_ON(svm->vmcb == svm->nested.vmcb02.ptr);

	nested_svm_copy_common_state(svm->vmcb01.ptr, svm->nested.vmcb02.ptr);

	svm_switch_vmcb(svm, &svm->nested.vmcb02);
	nested_vmcb02_prepare_control(svm, vmcb12->save.rip, vmcb12->save.cs.base);
	nested_vmcb02_prepare_save(svm, vmcb12);

	ret = nested_svm_load_cr3(&svm->vcpu, svm->nested.save.cr3,
				  nested_npt_enabled(svm), from_vmrun);
	if (ret)
		return ret;

	if (!from_vmrun)
		kvm_make_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);

	svm_set_gif(svm, true);

	if (kvm_vcpu_apicv_active(vcpu))
		kvm_make_request(KVM_REQ_APICV_UPDATE, vcpu);

	nested_svm_hv_update_vm_vp_ids(vcpu);

	return 0;
}

int nested_svm_vmrun(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;
	struct vmcb *vmcb12;
	struct kvm_host_map map;
	u64 vmcb12_gpa;
	struct vmcb *vmcb01 = svm->vmcb01.ptr;

	if (!svm->nested.hsave_msr) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	if (is_smm(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	/* This fails when VP assist page is enabled but the supplied GPA is bogus */
	ret = kvm_hv_verify_vp_assist(vcpu);
	if (ret) {
		kvm_inject_gp(vcpu, 0);
		return ret;
	}

	vmcb12_gpa = svm->vmcb->save.rax;
	ret = kvm_vcpu_map(vcpu, gpa_to_gfn(vmcb12_gpa), &map);
	if (ret == -EINVAL) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	} else if (ret) {
		return kvm_skip_emulated_instruction(vcpu);
	}

	ret = kvm_skip_emulated_instruction(vcpu);

	vmcb12 = map.hva;

	if (WARN_ON_ONCE(!svm->nested.initialized))
		return -EINVAL;

	nested_copy_vmcb_control_to_cache(svm, &vmcb12->control);
	nested_copy_vmcb_save_to_cache(svm, &vmcb12->save);

	if (!nested_vmcb_check_save(vcpu) ||
	    !nested_vmcb_check_controls(vcpu)) {
		vmcb12->control.exit_code    = SVM_EXIT_ERR;
		vmcb12->control.exit_code_hi = 0;
		vmcb12->control.exit_info_1  = 0;
		vmcb12->control.exit_info_2  = 0;
		goto out;
	}

	/*
	 * Since vmcb01 is not in use, we can use it to store some of the L1
	 * state.
	 */
	vmcb01->save.efer   = vcpu->arch.efer;
	vmcb01->save.cr0    = kvm_read_cr0(vcpu);
	vmcb01->save.cr4    = vcpu->arch.cr4;
	vmcb01->save.rflags = kvm_get_rflags(vcpu);
	vmcb01->save.rip    = kvm_rip_read(vcpu);

	if (!npt_enabled)
		vmcb01->save.cr3 = kvm_read_cr3(vcpu);

	svm->nested.nested_run_pending = 1;

	if (enter_svm_guest_mode(vcpu, vmcb12_gpa, vmcb12, true))
		goto out_exit_err;

	if (nested_svm_merge_msrpm(vcpu))
		goto out;

out_exit_err:
	svm->nested.nested_run_pending = 0;
	svm->nmi_l1_to_l2 = false;
	svm->soft_int_injected = false;

	svm->vmcb->control.exit_code    = SVM_EXIT_ERR;
	svm->vmcb->control.exit_code_hi = 0;
	svm->vmcb->control.exit_info_1  = 0;
	svm->vmcb->control.exit_info_2  = 0;

	nested_svm_vmexit(svm);

out:
	kvm_vcpu_unmap(vcpu, &map);

	return ret;
}

/* Copy state save area fields which are handled by VMRUN */
void svm_copy_vmrun_state(struct vmcb_save_area *to_save,
			  struct vmcb_save_area *from_save)
{
	to_save->es = from_save->es;
	to_save->cs = from_save->cs;
	to_save->ss = from_save->ss;
	to_save->ds = from_save->ds;
	to_save->gdtr = from_save->gdtr;
	to_save->idtr = from_save->idtr;
	to_save->rflags = from_save->rflags | X86_EFLAGS_FIXED;
	to_save->efer = from_save->efer;
	to_save->cr0 = from_save->cr0;
	to_save->cr3 = from_save->cr3;
	to_save->cr4 = from_save->cr4;
	to_save->rax = from_save->rax;
	to_save->rsp = from_save->rsp;
	to_save->rip = from_save->rip;
	to_save->cpl = 0;

	if (kvm_cpu_cap_has(X86_FEATURE_SHSTK)) {
		to_save->s_cet  = from_save->s_cet;
		to_save->isst_addr = from_save->isst_addr;
		to_save->ssp = from_save->ssp;
	}
}

void svm_copy_vmloadsave_state(struct vmcb *to_vmcb, struct vmcb *from_vmcb)
{
	to_vmcb->save.fs = from_vmcb->save.fs;
	to_vmcb->save.gs = from_vmcb->save.gs;
	to_vmcb->save.tr = from_vmcb->save.tr;
	to_vmcb->save.ldtr = from_vmcb->save.ldtr;
	to_vmcb->save.kernel_gs_base = from_vmcb->save.kernel_gs_base;
	to_vmcb->save.star = from_vmcb->save.star;
	to_vmcb->save.lstar = from_vmcb->save.lstar;
	to_vmcb->save.cstar = from_vmcb->save.cstar;
	to_vmcb->save.sfmask = from_vmcb->save.sfmask;
	to_vmcb->save.sysenter_cs = from_vmcb->save.sysenter_cs;
	to_vmcb->save.sysenter_esp = from_vmcb->save.sysenter_esp;
	to_vmcb->save.sysenter_eip = from_vmcb->save.sysenter_eip;
}

int nested_svm_vmexit(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct vmcb *vmcb01 = svm->vmcb01.ptr;
	struct vmcb *vmcb02 = svm->nested.vmcb02.ptr;
	struct vmcb *vmcb12;
	struct kvm_host_map map;
	int rc;

	rc = kvm_vcpu_map(vcpu, gpa_to_gfn(svm->nested.vmcb12_gpa), &map);
	if (rc) {
		if (rc == -EINVAL)
			kvm_inject_gp(vcpu, 0);
		return 1;
	}

	vmcb12 = map.hva;

	/* Exit Guest-Mode */
	leave_guest_mode(vcpu);
	svm->nested.vmcb12_gpa = 0;
	WARN_ON_ONCE(svm->nested.nested_run_pending);

	kvm_clear_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);

	/* in case we halted in L2 */
	kvm_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);

	/* Give the current vmcb to the guest */

	vmcb12->save.es     = vmcb02->save.es;
	vmcb12->save.cs     = vmcb02->save.cs;
	vmcb12->save.ss     = vmcb02->save.ss;
	vmcb12->save.ds     = vmcb02->save.ds;
	vmcb12->save.gdtr   = vmcb02->save.gdtr;
	vmcb12->save.idtr   = vmcb02->save.idtr;
	vmcb12->save.efer   = svm->vcpu.arch.efer;
	vmcb12->save.cr0    = kvm_read_cr0(vcpu);
	vmcb12->save.cr3    = kvm_read_cr3(vcpu);
	vmcb12->save.cr2    = vmcb02->save.cr2;
	vmcb12->save.cr4    = svm->vcpu.arch.cr4;
	vmcb12->save.rflags = kvm_get_rflags(vcpu);
	vmcb12->save.rip    = kvm_rip_read(vcpu);
	vmcb12->save.rsp    = kvm_rsp_read(vcpu);
	vmcb12->save.rax    = kvm_rax_read(vcpu);
	vmcb12->save.dr7    = vmcb02->save.dr7;
	vmcb12->save.dr6    = svm->vcpu.arch.dr6;
	vmcb12->save.cpl    = vmcb02->save.cpl;

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK)) {
		vmcb12->save.s_cet	= vmcb02->save.s_cet;
		vmcb12->save.isst_addr	= vmcb02->save.isst_addr;
		vmcb12->save.ssp	= vmcb02->save.ssp;
	}

	vmcb12->control.int_state         = vmcb02->control.int_state;
	vmcb12->control.exit_code         = vmcb02->control.exit_code;
	vmcb12->control.exit_code_hi      = vmcb02->control.exit_code_hi;
	vmcb12->control.exit_info_1       = vmcb02->control.exit_info_1;
	vmcb12->control.exit_info_2       = vmcb02->control.exit_info_2;

	if (vmcb12->control.exit_code != SVM_EXIT_ERR)
		nested_save_pending_event_to_vmcb12(svm, vmcb12);

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_NRIPS))
		vmcb12->control.next_rip  = vmcb02->control.next_rip;

	vmcb12->control.int_ctl           = svm->nested.ctl.int_ctl;
	vmcb12->control.event_inj         = svm->nested.ctl.event_inj;
	vmcb12->control.event_inj_err     = svm->nested.ctl.event_inj_err;

	if (!kvm_pause_in_guest(vcpu->kvm)) {
		vmcb01->control.pause_filter_count = vmcb02->control.pause_filter_count;
		vmcb_mark_dirty(vmcb01, VMCB_INTERCEPTS);

	}

	/*
	 * Invalidate bus_lock_rip unless KVM is still waiting for the guest
	 * to make forward progress before re-enabling bus lock detection.
	 */
	if (!vmcb02->control.bus_lock_counter)
		svm->nested.ctl.bus_lock_rip = INVALID_GPA;

	nested_svm_copy_common_state(svm->nested.vmcb02.ptr, svm->vmcb01.ptr);

	kvm_nested_vmexit_handle_ibrs(vcpu);

	svm_switch_vmcb(svm, &svm->vmcb01);

	/*
	 * Rules for synchronizing int_ctl bits from vmcb02 to vmcb01:
	 *
	 * V_IRQ, V_IRQ_VECTOR, V_INTR_PRIO_MASK, V_IGN_TPR:  If L1 doesn't
	 * intercept interrupts, then KVM will use vmcb02's V_IRQ (and related
	 * flags) to detect interrupt windows for L1 IRQs (even if L1 uses
	 * virtual interrupt masking).  Raise KVM_REQ_EVENT to ensure that
	 * KVM re-requests an interrupt window if necessary, which implicitly
	 * copies this bits from vmcb02 to vmcb01.
	 *
	 * V_TPR: If L1 doesn't use virtual interrupt masking, then L1's vTPR
	 * is stored in vmcb02, but its value doesn't need to be copied from/to
	 * vmcb01 because it is copied from/to the virtual APIC's TPR register
	 * on each VM entry/exit.
	 *
	 * V_GIF: If nested vGIF is not used, KVM uses vmcb02's V_GIF for L1's
	 * V_GIF.  However, GIF is architecturally clear on each VM exit, thus
	 * there is no need to copy V_GIF from vmcb02 to vmcb01.
	 */
	if (!nested_exit_on_intr(svm))
		kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);

	if (unlikely(guest_cpu_cap_has(vcpu, X86_FEATURE_LBRV) &&
		     (svm->nested.ctl.virt_ext & LBR_CTL_ENABLE_MASK)))
		svm_copy_lbrs(vmcb12, vmcb02);
	else
		svm_copy_lbrs(vmcb01, vmcb02);

	svm_update_lbrv(vcpu);

	if (vnmi) {
		if (vmcb02->control.int_ctl & V_NMI_BLOCKING_MASK)
			vmcb01->control.int_ctl |= V_NMI_BLOCKING_MASK;
		else
			vmcb01->control.int_ctl &= ~V_NMI_BLOCKING_MASK;

		if (vcpu->arch.nmi_pending) {
			vcpu->arch.nmi_pending--;
			vmcb01->control.int_ctl |= V_NMI_PENDING_MASK;
		} else {
			vmcb01->control.int_ctl &= ~V_NMI_PENDING_MASK;
		}
	}

	/*
	 * On vmexit the  GIF is set to false and
	 * no event can be injected in L1.
	 */
	svm_set_gif(svm, false);
	vmcb01->control.exit_int_info = 0;

	svm->vcpu.arch.tsc_offset = svm->vcpu.arch.l1_tsc_offset;
	if (vmcb01->control.tsc_offset != svm->vcpu.arch.tsc_offset) {
		vmcb01->control.tsc_offset = svm->vcpu.arch.tsc_offset;
		vmcb_mark_dirty(vmcb01, VMCB_INTERCEPTS);
	}

	if (kvm_caps.has_tsc_control &&
	    vcpu->arch.tsc_scaling_ratio != vcpu->arch.l1_tsc_scaling_ratio) {
		vcpu->arch.tsc_scaling_ratio = vcpu->arch.l1_tsc_scaling_ratio;
		svm_write_tsc_multiplier(vcpu);
	}

	svm->nested.ctl.nested_cr3 = 0;

	/*
	 * Restore processor state that had been saved in vmcb01
	 */
	kvm_set_rflags(vcpu, vmcb01->save.rflags);
	svm_set_efer(vcpu, vmcb01->save.efer);
	svm_set_cr0(vcpu, vmcb01->save.cr0 | X86_CR0_PE);
	svm_set_cr4(vcpu, vmcb01->save.cr4);
	kvm_rax_write(vcpu, vmcb01->save.rax);
	kvm_rsp_write(vcpu, vmcb01->save.rsp);
	kvm_rip_write(vcpu, vmcb01->save.rip);

	svm->vcpu.arch.dr7 = DR7_FIXED_1;
	kvm_update_dr7(&svm->vcpu);

	trace_kvm_nested_vmexit_inject(vmcb12->control.exit_code,
				       vmcb12->control.exit_info_1,
				       vmcb12->control.exit_info_2,
				       vmcb12->control.exit_int_info,
				       vmcb12->control.exit_int_info_err,
				       KVM_ISA_SVM);

	kvm_vcpu_unmap(vcpu, &map);

	nested_svm_transition_tlb_flush(vcpu);

	nested_svm_uninit_mmu_context(vcpu);

	rc = nested_svm_load_cr3(vcpu, vmcb01->save.cr3, false, true);
	if (rc)
		return 1;

	/*
	 * Drop what we picked up for L2 via svm_complete_interrupts() so it
	 * doesn't end up in L1.
	 */
	svm->vcpu.arch.nmi_injected = false;
	kvm_clear_exception_queue(vcpu);
	kvm_clear_interrupt_queue(vcpu);

	/*
	 * If we are here following the completion of a VMRUN that
	 * is being single-stepped, queue the pending #DB intercept
	 * right now so that it an be accounted for before we execute
	 * L1's next instruction.
	 */
	if (unlikely(vmcb01->save.rflags & X86_EFLAGS_TF))
		kvm_queue_exception(&(svm->vcpu), DB_VECTOR);

	/*
	 * Un-inhibit the AVIC right away, so that other vCPUs can start
	 * to benefit from it right away.
	 */
	if (kvm_apicv_activated(vcpu->kvm))
		__kvm_vcpu_update_apicv(vcpu);

	return 0;
}

static void nested_svm_triple_fault(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_SHUTDOWN))
		return;

	kvm_clear_request(KVM_REQ_TRIPLE_FAULT, vcpu);
	nested_svm_simple_vmexit(to_svm(vcpu), SVM_EXIT_SHUTDOWN);
}

int svm_allocate_nested(struct vcpu_svm *svm)
{
	struct page *vmcb02_page;

	if (svm->nested.initialized)
		return 0;

	vmcb02_page = snp_safe_alloc_page();
	if (!vmcb02_page)
		return -ENOMEM;
	svm->nested.vmcb02.ptr = page_address(vmcb02_page);
	svm->nested.vmcb02.pa = __sme_set(page_to_pfn(vmcb02_page) << PAGE_SHIFT);

	svm->nested.msrpm = svm_vcpu_alloc_msrpm();
	if (!svm->nested.msrpm)
		goto err_free_vmcb02;

	svm->nested.initialized = true;
	return 0;

err_free_vmcb02:
	__free_page(vmcb02_page);
	return -ENOMEM;
}

void svm_free_nested(struct vcpu_svm *svm)
{
	if (!svm->nested.initialized)
		return;

	if (WARN_ON_ONCE(svm->vmcb != svm->vmcb01.ptr))
		svm_switch_vmcb(svm, &svm->vmcb01);

	svm_vcpu_free_msrpm(svm->nested.msrpm);
	svm->nested.msrpm = NULL;

	__free_page(virt_to_page(svm->nested.vmcb02.ptr));
	svm->nested.vmcb02.ptr = NULL;

	/*
	 * When last_vmcb12_gpa matches the current vmcb12 gpa,
	 * some vmcb12 fields are not loaded if they are marked clean
	 * in the vmcb12, since in this case they are up to date already.
	 *
	 * When the vmcb02 is freed, this optimization becomes invalid.
	 */
	svm->nested.last_vmcb12_gpa = INVALID_GPA;

	svm->nested.initialized = false;
}

void svm_leave_nested(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (is_guest_mode(vcpu)) {
		svm->nested.nested_run_pending = 0;
		svm->nested.vmcb12_gpa = INVALID_GPA;

		leave_guest_mode(vcpu);

		svm_switch_vmcb(svm, &svm->vmcb01);

		nested_svm_uninit_mmu_context(vcpu);
		vmcb_mark_all_dirty(svm->vmcb);

		if (kvm_apicv_activated(vcpu->kvm))
			kvm_make_request(KVM_REQ_APICV_UPDATE, vcpu);
	}

	kvm_clear_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);
}

static int nested_svm_exit_handled_msr(struct vcpu_svm *svm)
{
	gpa_t base = svm->nested.ctl.msrpm_base_pa;
	int write, bit_nr;
	u8 value, mask;
	u32 msr;

	if (!(vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_MSR_PROT)))
		return NESTED_EXIT_HOST;

	msr    = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	bit_nr = svm_msrpm_bit_nr(msr);
	write  = svm->vmcb->control.exit_info_1 & 1;

	if (bit_nr < 0)
		return NESTED_EXIT_DONE;

	if (kvm_vcpu_read_guest(&svm->vcpu, base + bit_nr / BITS_PER_BYTE,
				&value, sizeof(value)))
		return NESTED_EXIT_DONE;

	mask = BIT(write) << (bit_nr & (BITS_PER_BYTE - 1));
	return (value & mask) ? NESTED_EXIT_DONE : NESTED_EXIT_HOST;
}

static int nested_svm_intercept_ioio(struct vcpu_svm *svm)
{
	unsigned port, size, iopm_len;
	u16 val, mask;
	u8 start_bit;
	u64 gpa;

	if (!(vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_IOIO_PROT)))
		return NESTED_EXIT_HOST;

	port = svm->vmcb->control.exit_info_1 >> 16;
	size = (svm->vmcb->control.exit_info_1 & SVM_IOIO_SIZE_MASK) >>
		SVM_IOIO_SIZE_SHIFT;
	gpa  = svm->nested.ctl.iopm_base_pa + (port / 8);
	start_bit = port % 8;
	iopm_len = (start_bit + size > 8) ? 2 : 1;
	mask = (0xf >> (4 - size)) << start_bit;
	val = 0;

	if (kvm_vcpu_read_guest(&svm->vcpu, gpa, &val, iopm_len))
		return NESTED_EXIT_DONE;

	return (val & mask) ? NESTED_EXIT_DONE : NESTED_EXIT_HOST;
}

static int nested_svm_intercept(struct vcpu_svm *svm)
{
	u32 exit_code = svm->vmcb->control.exit_code;
	int vmexit = NESTED_EXIT_HOST;

	switch (exit_code) {
	case SVM_EXIT_MSR:
		vmexit = nested_svm_exit_handled_msr(svm);
		break;
	case SVM_EXIT_IOIO:
		vmexit = nested_svm_intercept_ioio(svm);
		break;
	case SVM_EXIT_READ_CR0 ... SVM_EXIT_WRITE_CR8: {
		if (vmcb12_is_intercept(&svm->nested.ctl, exit_code))
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_READ_DR0 ... SVM_EXIT_WRITE_DR7: {
		if (vmcb12_is_intercept(&svm->nested.ctl, exit_code))
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 0x1f: {
		/*
		 * Host-intercepted exceptions have been checked already in
		 * nested_svm_exit_special.  There is nothing to do here,
		 * the vmexit is injected by svm_check_nested_events.
		 */
		vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_ERR: {
		vmexit = NESTED_EXIT_DONE;
		break;
	}
	default: {
		if (vmcb12_is_intercept(&svm->nested.ctl, exit_code))
			vmexit = NESTED_EXIT_DONE;
	}
	}

	return vmexit;
}

int nested_svm_exit_handled(struct vcpu_svm *svm)
{
	int vmexit;

	vmexit = nested_svm_intercept(svm);

	if (vmexit == NESTED_EXIT_DONE)
		nested_svm_vmexit(svm);

	return vmexit;
}

int nested_svm_check_permissions(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.efer & EFER_SVME) || !is_paging(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	if (to_svm(vcpu)->vmcb->save.cpl) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	return 0;
}

static bool nested_svm_is_exception_vmexit(struct kvm_vcpu *vcpu, u8 vector,
					   u32 error_code)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return (svm->nested.ctl.intercepts[INTERCEPT_EXCEPTION] & BIT(vector));
}

static void nested_svm_inject_exception_vmexit(struct kvm_vcpu *vcpu)
{
	struct kvm_queued_exception *ex = &vcpu->arch.exception_vmexit;
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;

	vmcb->control.exit_code = SVM_EXIT_EXCP_BASE + ex->vector;
	vmcb->control.exit_code_hi = 0;

	if (ex->has_error_code)
		vmcb->control.exit_info_1 = ex->error_code;

	/*
	 * EXITINFO2 is undefined for all exception intercepts other
	 * than #PF.
	 */
	if (ex->vector == PF_VECTOR) {
		if (ex->has_payload)
			vmcb->control.exit_info_2 = ex->payload;
		else
			vmcb->control.exit_info_2 = vcpu->arch.cr2;
	} else if (ex->vector == DB_VECTOR) {
		/* See kvm_check_and_inject_events().  */
		kvm_deliver_exception_payload(vcpu, ex);

		if (vcpu->arch.dr7 & DR7_GD) {
			vcpu->arch.dr7 &= ~DR7_GD;
			kvm_update_dr7(vcpu);
		}
	} else {
		WARN_ON(ex->has_payload);
	}

	nested_svm_vmexit(svm);
}

static inline bool nested_exit_on_init(struct vcpu_svm *svm)
{
	return vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_INIT);
}

static int svm_check_nested_events(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	struct vcpu_svm *svm = to_svm(vcpu);
	/*
	 * Only a pending nested run blocks a pending exception.  If there is a
	 * previously injected event, the pending exception occurred while said
	 * event was being delivered and thus needs to be handled.
	 */
	bool block_nested_exceptions = svm->nested.nested_run_pending;
	/*
	 * New events (not exceptions) are only recognized at instruction
	 * boundaries.  If an event needs reinjection, then KVM is handling a
	 * VM-Exit that occurred _during_ instruction execution; new events are
	 * blocked until the instruction completes.
	 */
	bool block_nested_events = block_nested_exceptions ||
				   kvm_event_needs_reinjection(vcpu);

	if (lapic_in_kernel(vcpu) &&
	    test_bit(KVM_APIC_INIT, &apic->pending_events)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_init(svm))
			return 0;
		nested_svm_simple_vmexit(svm, SVM_EXIT_INIT);
		return 0;
	}

	if (vcpu->arch.exception_vmexit.pending) {
		if (block_nested_exceptions)
                        return -EBUSY;
		nested_svm_inject_exception_vmexit(vcpu);
		return 0;
	}

	if (vcpu->arch.exception.pending) {
		if (block_nested_exceptions)
			return -EBUSY;
		return 0;
	}

#ifdef CONFIG_KVM_SMM
	if (vcpu->arch.smi_pending && !svm_smi_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_smi(svm))
			return 0;
		nested_svm_simple_vmexit(svm, SVM_EXIT_SMI);
		return 0;
	}
#endif

	if (vcpu->arch.nmi_pending && !svm_nmi_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_nmi(svm))
			return 0;
		nested_svm_simple_vmexit(svm, SVM_EXIT_NMI);
		return 0;
	}

	if (kvm_cpu_has_interrupt(vcpu) && !svm_interrupt_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_intr(svm))
			return 0;
		trace_kvm_nested_intr_vmexit(svm->vmcb->save.rip);
		nested_svm_simple_vmexit(svm, SVM_EXIT_INTR);
		return 0;
	}

	return 0;
}

int nested_svm_exit_special(struct vcpu_svm *svm)
{
	u32 exit_code = svm->vmcb->control.exit_code;
	struct kvm_vcpu *vcpu = &svm->vcpu;

	switch (exit_code) {
	case SVM_EXIT_INTR:
	case SVM_EXIT_NMI:
	case SVM_EXIT_NPF:
		return NESTED_EXIT_HOST;
	case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 0x1f: {
		u32 excp_bits = 1 << (exit_code - SVM_EXIT_EXCP_BASE);

		if (svm->vmcb01.ptr->control.intercepts[INTERCEPT_EXCEPTION] &
		    excp_bits)
			return NESTED_EXIT_HOST;
		else if (exit_code == SVM_EXIT_EXCP_BASE + PF_VECTOR &&
			 svm->vcpu.arch.apf.host_apf_flags)
			/* Trap async PF even if not shadowing */
			return NESTED_EXIT_HOST;
		break;
	}
	case SVM_EXIT_VMMCALL:
		/* Hyper-V L2 TLB flush hypercall is handled by L0 */
		if (guest_hv_cpuid_has_l2_tlb_flush(vcpu) &&
		    nested_svm_l2_tlb_flush_enabled(vcpu) &&
		    kvm_hv_is_tlb_flush_hcall(vcpu))
			return NESTED_EXIT_HOST;
		break;
	default:
		break;
	}

	return NESTED_EXIT_CONTINUE;
}

void nested_svm_update_tsc_ratio_msr(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	vcpu->arch.tsc_scaling_ratio =
		kvm_calc_nested_tsc_multiplier(vcpu->arch.l1_tsc_scaling_ratio,
					       svm->tsc_ratio_msr);
	svm_write_tsc_multiplier(vcpu);
}

/* Inverse operation of nested_copy_vmcb_control_to_cache(). asid is copied too. */
static void nested_copy_vmcb_cache_to_control(struct vmcb_control_area *dst,
					      struct vmcb_ctrl_area_cached *from)
{
	unsigned int i;

	memset(dst, 0, sizeof(struct vmcb_control_area));

	for (i = 0; i < MAX_INTERCEPT; i++)
		dst->intercepts[i] = from->intercepts[i];

	dst->iopm_base_pa         = from->iopm_base_pa;
	dst->msrpm_base_pa        = from->msrpm_base_pa;
	dst->tsc_offset           = from->tsc_offset;
	dst->asid                 = from->asid;
	dst->tlb_ctl              = from->tlb_ctl;
	dst->int_ctl              = from->int_ctl;
	dst->int_vector           = from->int_vector;
	dst->int_state            = from->int_state;
	dst->exit_code            = from->exit_code;
	dst->exit_code_hi         = from->exit_code_hi;
	dst->exit_info_1          = from->exit_info_1;
	dst->exit_info_2          = from->exit_info_2;
	dst->exit_int_info        = from->exit_int_info;
	dst->exit_int_info_err    = from->exit_int_info_err;
	dst->nested_ctl           = from->nested_ctl;
	dst->event_inj            = from->event_inj;
	dst->event_inj_err        = from->event_inj_err;
	dst->next_rip             = from->next_rip;
	dst->nested_cr3           = from->nested_cr3;
	dst->virt_ext              = from->virt_ext;
	dst->pause_filter_count   = from->pause_filter_count;
	dst->pause_filter_thresh  = from->pause_filter_thresh;
	/* 'clean' and 'hv_enlightenments' are not changed by KVM */
}

static int svm_get_nested_state(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				u32 user_data_size)
{
	struct vcpu_svm *svm;
	struct vmcb_control_area *ctl;
	unsigned long r;
	struct kvm_nested_state kvm_state = {
		.flags = 0,
		.format = KVM_STATE_NESTED_FORMAT_SVM,
		.size = sizeof(kvm_state),
	};
	struct vmcb __user *user_vmcb = (struct vmcb __user *)
		&user_kvm_nested_state->data.svm[0];

	if (!vcpu)
		return kvm_state.size + KVM_STATE_NESTED_SVM_VMCB_SIZE;

	svm = to_svm(vcpu);

	if (user_data_size < kvm_state.size)
		goto out;

	/* First fill in the header and copy it out.  */
	if (is_guest_mode(vcpu)) {
		kvm_state.hdr.svm.vmcb_pa = svm->nested.vmcb12_gpa;
		kvm_state.size += KVM_STATE_NESTED_SVM_VMCB_SIZE;
		kvm_state.flags |= KVM_STATE_NESTED_GUEST_MODE;

		if (svm->nested.nested_run_pending)
			kvm_state.flags |= KVM_STATE_NESTED_RUN_PENDING;
	}

	if (gif_set(svm))
		kvm_state.flags |= KVM_STATE_NESTED_GIF_SET;

	if (copy_to_user(user_kvm_nested_state, &kvm_state, sizeof(kvm_state)))
		return -EFAULT;

	if (!is_guest_mode(vcpu))
		goto out;

	/*
	 * Copy over the full size of the VMCB rather than just the size
	 * of the structs.
	 */
	if (clear_user(user_vmcb, KVM_STATE_NESTED_SVM_VMCB_SIZE))
		return -EFAULT;

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	nested_copy_vmcb_cache_to_control(ctl, &svm->nested.ctl);
	r = copy_to_user(&user_vmcb->control, ctl,
			 sizeof(user_vmcb->control));
	kfree(ctl);
	if (r)
		return -EFAULT;

	if (copy_to_user(&user_vmcb->save, &svm->vmcb01.ptr->save,
			 sizeof(user_vmcb->save)))
		return -EFAULT;
out:
	return kvm_state.size;
}

static int svm_set_nested_state(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				struct kvm_nested_state *kvm_state)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb __user *user_vmcb = (struct vmcb __user *)
		&user_kvm_nested_state->data.svm[0];
	struct vmcb_control_area *ctl;
	struct vmcb_save_area *save;
	struct vmcb_save_area_cached save_cached;
	struct vmcb_ctrl_area_cached ctl_cached;
	unsigned long cr0;
	int ret;

	BUILD_BUG_ON(sizeof(struct vmcb_control_area) + sizeof(struct vmcb_save_area) >
		     KVM_STATE_NESTED_SVM_VMCB_SIZE);

	if (kvm_state->format != KVM_STATE_NESTED_FORMAT_SVM)
		return -EINVAL;

	if (kvm_state->flags & ~(KVM_STATE_NESTED_GUEST_MODE |
				 KVM_STATE_NESTED_RUN_PENDING |
				 KVM_STATE_NESTED_GIF_SET))
		return -EINVAL;

	/*
	 * If in guest mode, vcpu->arch.efer actually refers to the L2 guest's
	 * EFER.SVME, but EFER.SVME still has to be 1 for VMRUN to succeed.
	 */
	if (!(vcpu->arch.efer & EFER_SVME)) {
		/* GIF=1 and no guest mode are required if SVME=0.  */
		if (kvm_state->flags != KVM_STATE_NESTED_GIF_SET)
			return -EINVAL;
	}

	/* SMM temporarily disables SVM, so we cannot be in guest mode.  */
	if (is_smm(vcpu) && (kvm_state->flags & KVM_STATE_NESTED_GUEST_MODE))
		return -EINVAL;

	if (!(kvm_state->flags & KVM_STATE_NESTED_GUEST_MODE)) {
		svm_leave_nested(vcpu);
		svm_set_gif(svm, !!(kvm_state->flags & KVM_STATE_NESTED_GIF_SET));
		return 0;
	}

	if (!page_address_valid(vcpu, kvm_state->hdr.svm.vmcb_pa))
		return -EINVAL;
	if (kvm_state->size < sizeof(*kvm_state) + KVM_STATE_NESTED_SVM_VMCB_SIZE)
		return -EINVAL;

	ctl = memdup_user(&user_vmcb->control, sizeof(*ctl));
	if (IS_ERR(ctl))
		return PTR_ERR(ctl);

	save = memdup_user(&user_vmcb->save, sizeof(*save));
	if (IS_ERR(save)) {
		kfree(ctl);
		return PTR_ERR(save);
	}

	ret = -EINVAL;
	__nested_copy_vmcb_control_to_cache(vcpu, &ctl_cached, ctl);
	if (!__nested_vmcb_check_controls(vcpu, &ctl_cached))
		goto out_free;

	/*
	 * Processor state contains L2 state.  Check that it is
	 * valid for guest mode (see nested_vmcb_check_save).
	 */
	cr0 = kvm_read_cr0(vcpu);
        if (((cr0 & X86_CR0_CD) == 0) && (cr0 & X86_CR0_NW))
		goto out_free;

	/*
	 * Validate host state saved from before VMRUN (see
	 * nested_svm_check_permissions).
	 */
	__nested_copy_vmcb_save_to_cache(&save_cached, save);
	if (!(save->cr0 & X86_CR0_PG) ||
	    !(save->cr0 & X86_CR0_PE) ||
	    (save->rflags & X86_EFLAGS_VM) ||
	    !__nested_vmcb_check_save(vcpu, &save_cached))
		goto out_free;


	/*
	 * All checks done, we can enter guest mode. Userspace provides
	 * vmcb12.control, which will be combined with L1 and stored into
	 * vmcb02, and the L1 save state which we store in vmcb01.
	 * L2 registers if needed are moved from the current VMCB to VMCB02.
	 */

	if (is_guest_mode(vcpu))
		svm_leave_nested(vcpu);
	else
		svm->nested.vmcb02.ptr->save = svm->vmcb01.ptr->save;

	svm_set_gif(svm, !!(kvm_state->flags & KVM_STATE_NESTED_GIF_SET));

	svm->nested.nested_run_pending =
		!!(kvm_state->flags & KVM_STATE_NESTED_RUN_PENDING);

	svm->nested.vmcb12_gpa = kvm_state->hdr.svm.vmcb_pa;

	svm_copy_vmrun_state(&svm->vmcb01.ptr->save, save);
	nested_copy_vmcb_control_to_cache(svm, ctl);

	svm_switch_vmcb(svm, &svm->nested.vmcb02);
	nested_vmcb02_prepare_control(svm, svm->vmcb->save.rip, svm->vmcb->save.cs.base);

	/*
	 * While the nested guest CR3 is already checked and set by
	 * KVM_SET_SREGS, it was set when nested state was yet loaded,
	 * thus MMU might not be initialized correctly.
	 * Set it again to fix this.
	 */

	ret = nested_svm_load_cr3(&svm->vcpu, vcpu->arch.cr3,
				  nested_npt_enabled(svm), false);
	if (WARN_ON_ONCE(ret))
		goto out_free;

	svm->nested.force_msr_bitmap_recalc = true;

	kvm_make_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);
	ret = 0;
out_free:
	kfree(save);
	kfree(ctl);

	return ret;
}

static bool svm_get_nested_state_pages(struct kvm_vcpu *vcpu)
{
	if (WARN_ON(!is_guest_mode(vcpu)))
		return true;

	if (!vcpu->arch.pdptrs_from_userspace &&
	    !nested_npt_enabled(to_svm(vcpu)) && is_pae_paging(vcpu))
		/*
		 * Reload the guest's PDPTRs since after a migration
		 * the guest CR3 might be restored prior to setting the nested
		 * state which can lead to a load of wrong PDPTRs.
		 */
		if (CC(!load_pdptrs(vcpu, vcpu->arch.cr3)))
			return false;

	if (!nested_svm_merge_msrpm(vcpu)) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return false;
	}

	if (kvm_hv_verify_vp_assist(vcpu))
		return false;

	return true;
}

struct kvm_x86_nested_ops svm_nested_ops = {
	.leave_nested = svm_leave_nested,
	.is_exception_vmexit = nested_svm_is_exception_vmexit,
	.check_events = svm_check_nested_events,
	.triple_fault = nested_svm_triple_fault,
	.get_nested_state_pages = svm_get_nested_state_pages,
	.get_state = svm_get_nested_state,
	.set_state = svm_set_nested_state,
	.hv_inject_synthetic_vmexit_post_tlb_flush = svm_hv_inject_synthetic_vmexit_post_tlb_flush,
};

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

#define pr_fmt(fmt) "SVM: " fmt

#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/kernel.h>

#include <asm/msr-index.h>
#include <asm/debugreg.h>

#include "kvm_emulate.h"
#include "trace.h"
#include "mmu.h"
#include "x86.h"
#include "cpuid.h"
#include "lapic.h"
#include "svm.h"

#define CC KVM_NESTED_VMENTER_CONSISTENCY_CHECK

static void nested_svm_inject_npf_exit(struct kvm_vcpu *vcpu,
				       struct x86_exception *fault)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm->vmcb->control.exit_code != SVM_EXIT_NPF) {
		/*
		 * TODO: track the cause of the nested page fault, and
		 * correctly fill in the high bits of exit_info_1.
		 */
		svm->vmcb->control.exit_code = SVM_EXIT_NPF;
		svm->vmcb->control.exit_code_hi = 0;
		svm->vmcb->control.exit_info_1 = (1ULL << 32);
		svm->vmcb->control.exit_info_2 = fault->address;
	}

	svm->vmcb->control.exit_info_1 &= ~0xffffffffULL;
	svm->vmcb->control.exit_info_1 |= fault->error_code;

	nested_svm_vmexit(svm);
}

static void svm_inject_page_fault_nested(struct kvm_vcpu *vcpu, struct x86_exception *fault)
{
       struct vcpu_svm *svm = to_svm(vcpu);
       WARN_ON(!is_guest_mode(vcpu));

       if (vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_EXCEPTION_OFFSET + PF_VECTOR) &&
	   !svm->nested.nested_run_pending) {
               svm->vmcb->control.exit_code = SVM_EXIT_EXCP_BASE + PF_VECTOR;
               svm->vmcb->control.exit_code_hi = 0;
               svm->vmcb->control.exit_info_1 = fault->error_code;
               svm->vmcb->control.exit_info_2 = fault->address;
               nested_svm_vmexit(svm);
       } else {
               kvm_inject_page_fault(vcpu, fault);
       }
}

static u64 nested_svm_get_tdp_pdptr(struct kvm_vcpu *vcpu, int index)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr3 = svm->nested.ctl.nested_cr3;
	u64 pdpte;
	int ret;

	ret = kvm_vcpu_read_guest_page(vcpu, gpa_to_gfn(cr3), &pdpte,
				       offset_in_page(cr3) + index * 8, 8);
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
	kvm_init_shadow_npt_mmu(vcpu, X86_CR0_PG, svm->vmcb01.ptr->save.cr4,
				svm->vmcb01.ptr->save.efer,
				svm->nested.ctl.nested_cr3);
	vcpu->arch.mmu->get_guest_pgd     = nested_svm_get_tdp_cr3;
	vcpu->arch.mmu->get_pdptr         = nested_svm_get_tdp_pdptr;
	vcpu->arch.mmu->inject_page_fault = nested_svm_inject_npf_exit;
	reset_shadow_zero_bits_mask(vcpu, vcpu->arch.mmu);
	vcpu->arch.walk_mmu              = &vcpu->arch.nested_mmu;
}

static void nested_svm_uninit_mmu_context(struct kvm_vcpu *vcpu)
{
	vcpu->arch.mmu = &vcpu->arch.root_mmu;
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;
}

void recalc_intercepts(struct vcpu_svm *svm)
{
	struct vmcb_control_area *c, *h, *g;
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
		/* We only want the cr8 intercept bits of L1 */
		vmcb_clr_intercept(c, INTERCEPT_CR8_READ);
		vmcb_clr_intercept(c, INTERCEPT_CR8_WRITE);

		/*
		 * Once running L2 with HF_VINTR_MASK, EFLAGS.IF does not
		 * affect any interrupt we may want to inject; therefore,
		 * interrupt window vmexits are irrelevant to L0.
		 */
		vmcb_clr_intercept(c, INTERCEPT_VINTR);
	}

	/* We don't want to see VMMCALLs from a nested guest */
	vmcb_clr_intercept(c, INTERCEPT_VMMCALL);

	for (i = 0; i < MAX_INTERCEPT; i++)
		c->intercepts[i] |= g->intercepts[i];
}

static void copy_vmcb_control_area(struct vmcb_control_area *dst,
				   struct vmcb_control_area *from)
{
	unsigned int i;

	for (i = 0; i < MAX_INTERCEPT; i++)
		dst->intercepts[i] = from->intercepts[i];

	dst->iopm_base_pa         = from->iopm_base_pa;
	dst->msrpm_base_pa        = from->msrpm_base_pa;
	dst->tsc_offset           = from->tsc_offset;
	/* asid not copied, it is handled manually for svm->vmcb.  */
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
	dst->nested_cr3           = from->nested_cr3;
	dst->virt_ext              = from->virt_ext;
	dst->pause_filter_count   = from->pause_filter_count;
	dst->pause_filter_thresh  = from->pause_filter_thresh;
}

static bool nested_svm_vmrun_msrpm(struct vcpu_svm *svm)
{
	/*
	 * This function merges the msr permission bitmaps of kvm and the
	 * nested vmcb. It is optimized in that it only merges the parts where
	 * the kvm msr permission bitmap may contain zero bits
	 */
	int i;

	if (!(vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_MSR_PROT)))
		return true;

	for (i = 0; i < MSRPM_OFFSETS; i++) {
		u32 value, p;
		u64 offset;

		if (msrpm_offsets[i] == 0xffffffff)
			break;

		p      = msrpm_offsets[i];
		offset = svm->nested.ctl.msrpm_base_pa + (p * 4);

		if (kvm_vcpu_read_guest(&svm->vcpu, offset, &value, 4))
			return false;

		svm->nested.msrpm[p] = svm->msrpm[p] | value;
	}

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

static bool nested_vmcb_check_controls(struct kvm_vcpu *vcpu,
				       struct vmcb_control_area *control)
{
	if (CC(!vmcb_is_intercept(control, INTERCEPT_VMRUN)))
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

	return true;
}

static bool nested_vmcb_check_cr3_cr4(struct kvm_vcpu *vcpu,
				      struct vmcb_save_area *save)
{
	/*
	 * These checks are also performed by KVM_SET_SREGS,
	 * except that EFER.LMA is not checked by SVM against
	 * CR0.PG && EFER.LME.
	 */
	if ((save->efer & EFER_LME) && (save->cr0 & X86_CR0_PG)) {
		if (CC(!(save->cr4 & X86_CR4_PAE)) ||
		    CC(!(save->cr0 & X86_CR0_PE)) ||
		    CC(kvm_vcpu_is_illegal_gpa(vcpu, save->cr3)))
			return false;
	}

	if (CC(!kvm_is_valid_cr4(vcpu, save->cr4)))
		return false;

	return true;
}

/* Common checks that apply to both L1 and L2 state.  */
static bool nested_vmcb_valid_sregs(struct kvm_vcpu *vcpu,
				    struct vmcb_save_area *save)
{
	/*
	 * FIXME: these should be done after copying the fields,
	 * to avoid TOC/TOU races.  For these save area checks
	 * the possible damage is limited since kvm_set_cr0 and
	 * kvm_set_cr4 handle failure; EFER_SVME is an exception
	 * so it is force-set later in nested_prepare_vmcb_save.
	 */
	if (CC(!(save->efer & EFER_SVME)))
		return false;

	if (CC((save->cr0 & X86_CR0_CD) == 0 && (save->cr0 & X86_CR0_NW)) ||
	    CC(save->cr0 & ~0xffffffffULL))
		return false;

	if (CC(!kvm_dr6_valid(save->dr6)) || CC(!kvm_dr7_valid(save->dr7)))
		return false;

	if (!nested_vmcb_check_cr3_cr4(vcpu, save))
		return false;

	if (CC(!kvm_valid_efer(vcpu, save->efer)))
		return false;

	return true;
}

static void nested_load_control_from_vmcb12(struct vcpu_svm *svm,
					    struct vmcb_control_area *control)
{
	copy_vmcb_control_area(&svm->nested.ctl, control);

	/* Copy it here because nested_svm_check_controls will check it.  */
	svm->nested.ctl.asid           = control->asid;
	svm->nested.ctl.msrpm_base_pa &= ~0x0fffULL;
	svm->nested.ctl.iopm_base_pa  &= ~0x0fffULL;
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
	if (!(svm->nested.ctl.int_ctl & V_INTR_MASKING_MASK) &&
	    svm_is_intercept(svm, INTERCEPT_VINTR)) {
		/*
		 * In order to request an interrupt window, L0 is usurping
		 * svm->vmcb->control.int_ctl and possibly setting V_IRQ
		 * even if it was clear in L1's VMCB.  Restoring it would be
		 * wrong.  However, in this case V_IRQ will remain true until
		 * interrupt_window_interception calls svm_clear_vintr and
		 * restores int_ctl.  We can just leave it aside.
		 */
		mask &= ~V_IRQ_MASK;
	}
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
		nr = vcpu->arch.exception.nr;
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

static inline bool nested_npt_enabled(struct vcpu_svm *svm)
{
	return svm->nested.ctl.nested_ctl & SVM_NESTED_CTL_NP_ENABLE;
}

/*
 * Load guest's/host's cr3 on nested vmentry or vmexit. @nested_npt is true
 * if we are emulating VM-Entry into a guest with NPT enabled.
 */
static int nested_svm_load_cr3(struct kvm_vcpu *vcpu, unsigned long cr3,
			       bool nested_npt)
{
	if (CC(kvm_vcpu_is_illegal_gpa(vcpu, cr3)))
		return -EINVAL;

	if (!nested_npt && is_pae_paging(vcpu) &&
	    (cr3 != kvm_read_cr3(vcpu) || pdptrs_changed(vcpu))) {
		if (CC(!load_pdptrs(vcpu, vcpu->arch.walk_mmu, cr3)))
			return -EINVAL;
	}

	/*
	 * TODO: optimize unconditional TLB flush/MMU sync here and in
	 * kvm_init_shadow_npt_mmu().
	 */
	if (!nested_npt)
		kvm_mmu_new_pgd(vcpu, cr3, false, false);

	vcpu->arch.cr3 = cr3;
	kvm_register_mark_available(vcpu, VCPU_EXREG_CR3);

	kvm_init_mmu(vcpu, false);

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

	nested_vmcb02_compute_g_pat(svm);

	/* Load the nested guest state */
	if (svm->nested.vmcb12_gpa != svm->nested.last_vmcb12_gpa) {
		new_vmcb12 = true;
		svm->nested.last_vmcb12_gpa = svm->nested.vmcb12_gpa;
	}

	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_SEG))) {
		svm->vmcb->save.es = vmcb12->save.es;
		svm->vmcb->save.cs = vmcb12->save.cs;
		svm->vmcb->save.ss = vmcb12->save.ss;
		svm->vmcb->save.ds = vmcb12->save.ds;
		svm->vmcb->save.cpl = vmcb12->save.cpl;
		vmcb_mark_dirty(svm->vmcb, VMCB_SEG);
	}

	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_DT))) {
		svm->vmcb->save.gdtr = vmcb12->save.gdtr;
		svm->vmcb->save.idtr = vmcb12->save.idtr;
		vmcb_mark_dirty(svm->vmcb, VMCB_DT);
	}

	kvm_set_rflags(&svm->vcpu, vmcb12->save.rflags | X86_EFLAGS_FIXED);

	/*
	 * Force-set EFER_SVME even though it is checked earlier on the
	 * VMCB12, because the guest can flip the bit between the check
	 * and now.  Clearing EFER_SVME would call svm_free_nested.
	 */
	svm_set_efer(&svm->vcpu, vmcb12->save.efer | EFER_SVME);

	svm_set_cr0(&svm->vcpu, vmcb12->save.cr0);
	svm_set_cr4(&svm->vcpu, vmcb12->save.cr4);

	svm->vcpu.arch.cr2 = vmcb12->save.cr2;

	kvm_rax_write(&svm->vcpu, vmcb12->save.rax);
	kvm_rsp_write(&svm->vcpu, vmcb12->save.rsp);
	kvm_rip_write(&svm->vcpu, vmcb12->save.rip);

	/* In case we don't even reach vcpu_run, the fields are not updated */
	svm->vmcb->save.rax = vmcb12->save.rax;
	svm->vmcb->save.rsp = vmcb12->save.rsp;
	svm->vmcb->save.rip = vmcb12->save.rip;

	/* These bits will be set properly on the first execution when new_vmc12 is true */
	if (unlikely(new_vmcb12 || vmcb_is_dirty(vmcb12, VMCB_DR))) {
		svm->vmcb->save.dr7 = vmcb12->save.dr7 | DR7_FIXED_1;
		svm->vcpu.arch.dr6  = vmcb12->save.dr6 | DR6_ACTIVE_LOW;
		vmcb_mark_dirty(svm->vmcb, VMCB_DR);
	}
}

static void nested_vmcb02_prepare_control(struct vcpu_svm *svm)
{
	const u32 mask = V_INTR_MASKING_MASK | V_GIF_ENABLE_MASK | V_GIF_MASK;

	/*
	 * Filled at exit: exit_code, exit_code_hi, exit_info_1, exit_info_2,
	 * exit_int_info, exit_int_info_err, next_rip, insn_len, insn_bytes.
	 */

	/*
	 * Also covers avic_vapic_bar, avic_backing_page, avic_logical_id,
	 * avic_physical_id.
	 */
	WARN_ON(svm->vmcb01.ptr->control.int_ctl & AVIC_ENABLE_MASK);

	/* Copied from vmcb01.  msrpm_base can be overwritten later.  */
	svm->vmcb->control.nested_ctl = svm->vmcb01.ptr->control.nested_ctl;
	svm->vmcb->control.iopm_base_pa = svm->vmcb01.ptr->control.iopm_base_pa;
	svm->vmcb->control.msrpm_base_pa = svm->vmcb01.ptr->control.msrpm_base_pa;

	/* Done at vmrun: asid.  */

	/* Also overwritten later if necessary.  */
	svm->vmcb->control.tlb_ctl = TLB_CONTROL_DO_NOTHING;

	/* nested_cr3.  */
	if (nested_npt_enabled(svm))
		nested_svm_init_mmu_context(&svm->vcpu);

	svm->vmcb->control.tsc_offset = svm->vcpu.arch.tsc_offset =
		svm->vcpu.arch.l1_tsc_offset + svm->nested.ctl.tsc_offset;

	svm->vmcb->control.int_ctl             =
		(svm->nested.ctl.int_ctl & ~mask) |
		(svm->vmcb01.ptr->control.int_ctl & mask);

	svm->vmcb->control.virt_ext            = svm->nested.ctl.virt_ext;
	svm->vmcb->control.int_vector          = svm->nested.ctl.int_vector;
	svm->vmcb->control.int_state           = svm->nested.ctl.int_state;
	svm->vmcb->control.event_inj           = svm->nested.ctl.event_inj;
	svm->vmcb->control.event_inj_err       = svm->nested.ctl.event_inj_err;

	svm->vmcb->control.pause_filter_count  = svm->nested.ctl.pause_filter_count;
	svm->vmcb->control.pause_filter_thresh = svm->nested.ctl.pause_filter_thresh;

	/* Enter Guest-Mode */
	enter_guest_mode(&svm->vcpu);

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
			 struct vmcb *vmcb12)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;

	trace_kvm_nested_vmrun(svm->vmcb->save.rip, vmcb12_gpa,
			       vmcb12->save.rip,
			       vmcb12->control.int_ctl,
			       vmcb12->control.event_inj,
			       vmcb12->control.nested_ctl);

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
	nested_vmcb02_prepare_control(svm);
	nested_vmcb02_prepare_save(svm, vmcb12);

	ret = nested_svm_load_cr3(&svm->vcpu, vmcb12->save.cr3,
				  nested_npt_enabled(svm));
	if (ret)
		return ret;

	if (!npt_enabled)
		vcpu->arch.mmu->inject_page_fault = svm_inject_page_fault_nested;

	svm_set_gif(svm, true);

	return 0;
}

int nested_svm_vmrun(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;
	struct vmcb *vmcb12;
	struct kvm_host_map map;
	u64 vmcb12_gpa;

	++vcpu->stat.nested_run;

	if (is_smm(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
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

	nested_load_control_from_vmcb12(svm, &vmcb12->control);

	if (!nested_vmcb_valid_sregs(vcpu, &vmcb12->save) ||
	    !nested_vmcb_check_controls(vcpu, &svm->nested.ctl)) {
		vmcb12->control.exit_code    = SVM_EXIT_ERR;
		vmcb12->control.exit_code_hi = 0;
		vmcb12->control.exit_info_1  = 0;
		vmcb12->control.exit_info_2  = 0;
		goto out;
	}


	/* Clear internal status */
	kvm_clear_exception_queue(vcpu);
	kvm_clear_interrupt_queue(vcpu);

	/*
	 * Since vmcb01 is not in use, we can use it to store some of the L1
	 * state.
	 */
	svm->vmcb01.ptr->save.efer   = vcpu->arch.efer;
	svm->vmcb01.ptr->save.cr0    = kvm_read_cr0(vcpu);
	svm->vmcb01.ptr->save.cr4    = vcpu->arch.cr4;
	svm->vmcb01.ptr->save.rflags = kvm_get_rflags(vcpu);
	svm->vmcb01.ptr->save.rip    = kvm_rip_read(vcpu);

	if (!npt_enabled)
		svm->vmcb01.ptr->save.cr3 = kvm_read_cr3(vcpu);

	svm->nested.nested_run_pending = 1;

	if (enter_svm_guest_mode(vcpu, vmcb12_gpa, vmcb12))
		goto out_exit_err;

	if (nested_svm_vmrun_msrpm(svm))
		goto out;

out_exit_err:
	svm->nested.nested_run_pending = 0;

	svm->vmcb->control.exit_code    = SVM_EXIT_ERR;
	svm->vmcb->control.exit_code_hi = 0;
	svm->vmcb->control.exit_info_1  = 0;
	svm->vmcb->control.exit_info_2  = 0;

	nested_svm_vmexit(svm);

out:
	kvm_vcpu_unmap(vcpu, &map, true);

	return ret;
}

void nested_svm_vmloadsave(struct vmcb *from_vmcb, struct vmcb *to_vmcb)
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
	struct vmcb *vmcb12;
	struct vmcb *vmcb = svm->vmcb;
	struct kvm_host_map map;
	int rc;

	/* Triple faults in L2 should never escape. */
	WARN_ON_ONCE(kvm_check_request(KVM_REQ_TRIPLE_FAULT, vcpu));

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
	svm->vcpu.arch.mp_state = KVM_MP_STATE_RUNNABLE;

	/* Give the current vmcb to the guest */

	vmcb12->save.es     = vmcb->save.es;
	vmcb12->save.cs     = vmcb->save.cs;
	vmcb12->save.ss     = vmcb->save.ss;
	vmcb12->save.ds     = vmcb->save.ds;
	vmcb12->save.gdtr   = vmcb->save.gdtr;
	vmcb12->save.idtr   = vmcb->save.idtr;
	vmcb12->save.efer   = svm->vcpu.arch.efer;
	vmcb12->save.cr0    = kvm_read_cr0(vcpu);
	vmcb12->save.cr3    = kvm_read_cr3(vcpu);
	vmcb12->save.cr2    = vmcb->save.cr2;
	vmcb12->save.cr4    = svm->vcpu.arch.cr4;
	vmcb12->save.rflags = kvm_get_rflags(vcpu);
	vmcb12->save.rip    = kvm_rip_read(vcpu);
	vmcb12->save.rsp    = kvm_rsp_read(vcpu);
	vmcb12->save.rax    = kvm_rax_read(vcpu);
	vmcb12->save.dr7    = vmcb->save.dr7;
	vmcb12->save.dr6    = svm->vcpu.arch.dr6;
	vmcb12->save.cpl    = vmcb->save.cpl;

	vmcb12->control.int_state         = vmcb->control.int_state;
	vmcb12->control.exit_code         = vmcb->control.exit_code;
	vmcb12->control.exit_code_hi      = vmcb->control.exit_code_hi;
	vmcb12->control.exit_info_1       = vmcb->control.exit_info_1;
	vmcb12->control.exit_info_2       = vmcb->control.exit_info_2;

	if (vmcb12->control.exit_code != SVM_EXIT_ERR)
		nested_save_pending_event_to_vmcb12(svm, vmcb12);

	if (svm->nrips_enabled)
		vmcb12->control.next_rip  = vmcb->control.next_rip;

	vmcb12->control.int_ctl           = svm->nested.ctl.int_ctl;
	vmcb12->control.tlb_ctl           = svm->nested.ctl.tlb_ctl;
	vmcb12->control.event_inj         = svm->nested.ctl.event_inj;
	vmcb12->control.event_inj_err     = svm->nested.ctl.event_inj_err;

	vmcb12->control.pause_filter_count =
		svm->vmcb->control.pause_filter_count;
	vmcb12->control.pause_filter_thresh =
		svm->vmcb->control.pause_filter_thresh;

	nested_svm_copy_common_state(svm->nested.vmcb02.ptr, svm->vmcb01.ptr);

	svm_switch_vmcb(svm, &svm->vmcb01);
	WARN_ON_ONCE(svm->vmcb->control.exit_code != SVM_EXIT_VMRUN);

	/*
	 * On vmexit the  GIF is set to false and
	 * no event can be injected in L1.
	 */
	svm_set_gif(svm, false);
	svm->vmcb->control.exit_int_info = 0;

	svm->vcpu.arch.tsc_offset = svm->vcpu.arch.l1_tsc_offset;
	if (svm->vmcb->control.tsc_offset != svm->vcpu.arch.tsc_offset) {
		svm->vmcb->control.tsc_offset = svm->vcpu.arch.tsc_offset;
		vmcb_mark_dirty(svm->vmcb, VMCB_INTERCEPTS);
	}

	svm->nested.ctl.nested_cr3 = 0;

	/*
	 * Restore processor state that had been saved in vmcb01
	 */
	kvm_set_rflags(vcpu, svm->vmcb->save.rflags);
	svm_set_efer(vcpu, svm->vmcb->save.efer);
	svm_set_cr0(vcpu, svm->vmcb->save.cr0 | X86_CR0_PE);
	svm_set_cr4(vcpu, svm->vmcb->save.cr4);
	kvm_rax_write(vcpu, svm->vmcb->save.rax);
	kvm_rsp_write(vcpu, svm->vmcb->save.rsp);
	kvm_rip_write(vcpu, svm->vmcb->save.rip);

	svm->vcpu.arch.dr7 = DR7_FIXED_1;
	kvm_update_dr7(&svm->vcpu);

	trace_kvm_nested_vmexit_inject(vmcb12->control.exit_code,
				       vmcb12->control.exit_info_1,
				       vmcb12->control.exit_info_2,
				       vmcb12->control.exit_int_info,
				       vmcb12->control.exit_int_info_err,
				       KVM_ISA_SVM);

	kvm_vcpu_unmap(vcpu, &map, true);

	nested_svm_uninit_mmu_context(vcpu);

	rc = nested_svm_load_cr3(vcpu, svm->vmcb->save.cr3, false);
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
	if (unlikely(svm->vmcb->save.rflags & X86_EFLAGS_TF))
		kvm_queue_exception(&(svm->vcpu), DB_VECTOR);

	return 0;
}

static void nested_svm_triple_fault(struct kvm_vcpu *vcpu)
{
	nested_svm_simple_vmexit(to_svm(vcpu), SVM_EXIT_SHUTDOWN);
}

int svm_allocate_nested(struct vcpu_svm *svm)
{
	struct page *vmcb02_page;

	if (svm->nested.initialized)
		return 0;

	vmcb02_page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!vmcb02_page)
		return -ENOMEM;
	svm->nested.vmcb02.ptr = page_address(vmcb02_page);
	svm->nested.vmcb02.pa = __sme_set(page_to_pfn(vmcb02_page) << PAGE_SHIFT);

	svm->nested.msrpm = svm_vcpu_alloc_msrpm();
	if (!svm->nested.msrpm)
		goto err_free_vmcb02;
	svm_vcpu_init_msrpm(&svm->vcpu, svm->nested.msrpm);

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

	svm_vcpu_free_msrpm(svm->nested.msrpm);
	svm->nested.msrpm = NULL;

	__free_page(virt_to_page(svm->nested.vmcb02.ptr));
	svm->nested.vmcb02.ptr = NULL;

	svm->nested.initialized = false;
}

/*
 * Forcibly leave nested mode in order to be able to reset the VCPU later on.
 */
void svm_leave_nested(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;

	if (is_guest_mode(vcpu)) {
		svm->nested.nested_run_pending = 0;
		leave_guest_mode(vcpu);

		svm_switch_vmcb(svm, &svm->nested.vmcb02);

		nested_svm_uninit_mmu_context(vcpu);
		vmcb_mark_all_dirty(svm->vmcb);
	}

	kvm_clear_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);
}

static int nested_svm_exit_handled_msr(struct vcpu_svm *svm)
{
	u32 offset, msr, value;
	int write, mask;

	if (!(vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_MSR_PROT)))
		return NESTED_EXIT_HOST;

	msr    = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	offset = svm_msrpm_offset(msr);
	write  = svm->vmcb->control.exit_info_1 & 1;
	mask   = 1 << ((2 * (msr & 0xf)) + write);

	if (offset == MSR_INVALID)
		return NESTED_EXIT_DONE;

	/* Offset is in 32 bit units but need in 8 bit units */
	offset *= 4;

	if (kvm_vcpu_read_guest(&svm->vcpu, svm->nested.ctl.msrpm_base_pa + offset, &value, 4))
		return NESTED_EXIT_DONE;

	return (value & mask) ? NESTED_EXIT_DONE : NESTED_EXIT_HOST;
}

static int nested_svm_intercept_ioio(struct vcpu_svm *svm)
{
	unsigned port, size, iopm_len;
	u16 val, mask;
	u8 start_bit;
	u64 gpa;

	if (!(vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_IOIO_PROT)))
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
		if (vmcb_is_intercept(&svm->nested.ctl, exit_code))
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_READ_DR0 ... SVM_EXIT_WRITE_DR7: {
		if (vmcb_is_intercept(&svm->nested.ctl, exit_code))
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
		if (vmcb_is_intercept(&svm->nested.ctl, exit_code))
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

static bool nested_exit_on_exception(struct vcpu_svm *svm)
{
	unsigned int nr = svm->vcpu.arch.exception.nr;

	return (svm->nested.ctl.intercepts[INTERCEPT_EXCEPTION] & BIT(nr));
}

static void nested_svm_inject_exception_vmexit(struct vcpu_svm *svm)
{
	unsigned int nr = svm->vcpu.arch.exception.nr;

	svm->vmcb->control.exit_code = SVM_EXIT_EXCP_BASE + nr;
	svm->vmcb->control.exit_code_hi = 0;

	if (svm->vcpu.arch.exception.has_error_code)
		svm->vmcb->control.exit_info_1 = svm->vcpu.arch.exception.error_code;

	/*
	 * EXITINFO2 is undefined for all exception intercepts other
	 * than #PF.
	 */
	if (nr == PF_VECTOR) {
		if (svm->vcpu.arch.exception.nested_apf)
			svm->vmcb->control.exit_info_2 = svm->vcpu.arch.apf.nested_apf_token;
		else if (svm->vcpu.arch.exception.has_payload)
			svm->vmcb->control.exit_info_2 = svm->vcpu.arch.exception.payload;
		else
			svm->vmcb->control.exit_info_2 = svm->vcpu.arch.cr2;
	} else if (nr == DB_VECTOR) {
		/* See inject_pending_event.  */
		kvm_deliver_exception_payload(&svm->vcpu);
		if (svm->vcpu.arch.dr7 & DR7_GD) {
			svm->vcpu.arch.dr7 &= ~DR7_GD;
			kvm_update_dr7(&svm->vcpu);
		}
	} else
		WARN_ON(svm->vcpu.arch.exception.has_payload);

	nested_svm_vmexit(svm);
}

static inline bool nested_exit_on_init(struct vcpu_svm *svm)
{
	return vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_INIT);
}

static int svm_check_nested_events(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	bool block_nested_events =
		kvm_event_needs_reinjection(vcpu) || svm->nested.nested_run_pending;
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (lapic_in_kernel(vcpu) &&
	    test_bit(KVM_APIC_INIT, &apic->pending_events)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_init(svm))
			return 0;
		nested_svm_simple_vmexit(svm, SVM_EXIT_INIT);
		return 0;
	}

	if (vcpu->arch.exception.pending) {
		/*
		 * Only a pending nested run can block a pending exception.
		 * Otherwise an injected NMI/interrupt should either be
		 * lost or delivered to the nested hypervisor in the EXITINTINFO
		 * vmcb field, while delivering the pending exception.
		 */
		if (svm->nested.nested_run_pending)
                        return -EBUSY;
		if (!nested_exit_on_exception(svm))
			return 0;
		nested_svm_inject_exception_vmexit(svm);
		return 0;
	}

	if (vcpu->arch.smi_pending && !svm_smi_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_smi(svm))
			return 0;
		nested_svm_simple_vmexit(svm, SVM_EXIT_SMI);
		return 0;
	}

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
	default:
		break;
	}

	return NESTED_EXIT_CONTINUE;
}

static int svm_get_nested_state(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				u32 user_data_size)
{
	struct vcpu_svm *svm;
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
	if (copy_to_user(&user_vmcb->control, &svm->nested.ctl,
			 sizeof(user_vmcb->control)))
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
	int ret;
	u32 cr0;

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
		svm_leave_nested(svm);
		svm_set_gif(svm, !!(kvm_state->flags & KVM_STATE_NESTED_GIF_SET));
		return 0;
	}

	if (!page_address_valid(vcpu, kvm_state->hdr.svm.vmcb_pa))
		return -EINVAL;
	if (kvm_state->size < sizeof(*kvm_state) + KVM_STATE_NESTED_SVM_VMCB_SIZE)
		return -EINVAL;

	ret  = -ENOMEM;
	ctl  = kzalloc(sizeof(*ctl),  GFP_KERNEL_ACCOUNT);
	save = kzalloc(sizeof(*save), GFP_KERNEL_ACCOUNT);
	if (!ctl || !save)
		goto out_free;

	ret = -EFAULT;
	if (copy_from_user(ctl, &user_vmcb->control, sizeof(*ctl)))
		goto out_free;
	if (copy_from_user(save, &user_vmcb->save, sizeof(*save)))
		goto out_free;

	ret = -EINVAL;
	if (!nested_vmcb_check_controls(vcpu, ctl))
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
	if (!(save->cr0 & X86_CR0_PG) ||
	    !(save->cr0 & X86_CR0_PE) ||
	    (save->rflags & X86_EFLAGS_VM) ||
	    !nested_vmcb_valid_sregs(vcpu, save))
		goto out_free;

	/*
	 * All checks done, we can enter guest mode. Userspace provides
	 * vmcb12.control, which will be combined with L1 and stored into
	 * vmcb02, and the L1 save state which we store in vmcb01.
	 * L2 registers if needed are moved from the current VMCB to VMCB02.
	 */

	svm->nested.nested_run_pending =
		!!(kvm_state->flags & KVM_STATE_NESTED_RUN_PENDING);

	svm->nested.vmcb12_gpa = kvm_state->hdr.svm.vmcb_pa;
	if (svm->current_vmcb == &svm->vmcb01)
		svm->nested.vmcb02.ptr->save = svm->vmcb01.ptr->save;

	svm->vmcb01.ptr->save.es = save->es;
	svm->vmcb01.ptr->save.cs = save->cs;
	svm->vmcb01.ptr->save.ss = save->ss;
	svm->vmcb01.ptr->save.ds = save->ds;
	svm->vmcb01.ptr->save.gdtr = save->gdtr;
	svm->vmcb01.ptr->save.idtr = save->idtr;
	svm->vmcb01.ptr->save.rflags = save->rflags | X86_EFLAGS_FIXED;
	svm->vmcb01.ptr->save.efer = save->efer;
	svm->vmcb01.ptr->save.cr0 = save->cr0;
	svm->vmcb01.ptr->save.cr3 = save->cr3;
	svm->vmcb01.ptr->save.cr4 = save->cr4;
	svm->vmcb01.ptr->save.rax = save->rax;
	svm->vmcb01.ptr->save.rsp = save->rsp;
	svm->vmcb01.ptr->save.rip = save->rip;
	svm->vmcb01.ptr->save.cpl = 0;

	nested_load_control_from_vmcb12(svm, ctl);

	svm_switch_vmcb(svm, &svm->nested.vmcb02);

	nested_vmcb02_prepare_control(svm);

	kvm_make_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);
	ret = 0;
out_free:
	kfree(save);
	kfree(ctl);

	return ret;
}

static bool svm_get_nested_state_pages(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (WARN_ON(!is_guest_mode(vcpu)))
		return true;

	if (nested_svm_load_cr3(&svm->vcpu, vcpu->arch.cr3,
				nested_npt_enabled(svm)))
		return false;

	if (!nested_svm_vmrun_msrpm(svm)) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return false;
	}

	return true;
}

struct kvm_x86_nested_ops svm_nested_ops = {
	.check_events = svm_check_nested_events,
	.triple_fault = nested_svm_triple_fault,
	.get_nested_state_pages = svm_get_nested_state_pages,
	.get_state = svm_get_nested_state,
	.set_state = svm_set_nested_state,
};

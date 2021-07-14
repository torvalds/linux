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
	struct vmcb *hsave = svm->nested.hsave;

	WARN_ON(mmu_is_nested(vcpu));

	vcpu->arch.mmu = &vcpu->arch.guest_mmu;
	kvm_init_shadow_npt_mmu(vcpu, X86_CR0_PG, hsave->save.cr4, hsave->save.efer,
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
	h = &svm->nested.hsave->control;
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

static bool svm_get_nested_state_pages(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (WARN_ON(!is_guest_mode(vcpu)))
		return true;

	if (!nested_svm_vmrun_msrpm(svm)) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return false;
	}

	return true;
}

static bool nested_vmcb_check_controls(struct vmcb_control_area *control)
{
	if ((vmcb_is_intercept(control, INTERCEPT_VMRUN)) == 0)
		return false;

	if (control->asid == 0)
		return false;

	if ((control->nested_ctl & SVM_NESTED_CTL_NP_ENABLE) &&
	    !npt_enabled)
		return false;

	return true;
}

static bool nested_vmcb_check_save(struct vcpu_svm *svm, struct vmcb *vmcb12)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	bool vmcb12_lma;

	/*
	 * FIXME: these should be done after copying the fields,
	 * to avoid TOC/TOU races.  For these save area checks
	 * the possible damage is limited since kvm_set_cr0 and
	 * kvm_set_cr4 handle failure; EFER_SVME is an exception
	 * so it is force-set later in nested_prepare_vmcb_save.
	 */
	if ((vmcb12->save.efer & EFER_SVME) == 0)
		return false;

	if (((vmcb12->save.cr0 & X86_CR0_CD) == 0) && (vmcb12->save.cr0 & X86_CR0_NW))
		return false;

	if (!kvm_dr6_valid(vmcb12->save.dr6) || !kvm_dr7_valid(vmcb12->save.dr7))
		return false;

	vmcb12_lma = (vmcb12->save.efer & EFER_LME) && (vmcb12->save.cr0 & X86_CR0_PG);

	if (vmcb12_lma) {
		if (!(vmcb12->save.cr4 & X86_CR4_PAE) ||
		    !(vmcb12->save.cr0 & X86_CR0_PE) ||
		    (vmcb12->save.cr3 & vcpu->arch.cr3_lm_rsvd_bits))
			return false;
	}
	if (kvm_valid_cr4(&svm->vcpu, vmcb12->save.cr4))
		return false;

	return true;
}

static void load_nested_vmcb_control(struct vcpu_svm *svm,
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
 * they can be copied back into the nested_vmcb.
 */
void sync_nested_vmcb_control(struct vcpu_svm *svm)
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
static void nested_vmcb_save_pending_event(struct vcpu_svm *svm,
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
	if (cr3 & rsvd_bits(cpuid_maxphyaddr(vcpu), 63))
		return -EINVAL;

	if (!nested_npt && is_pae_paging(vcpu) &&
	    (cr3 != kvm_read_cr3(vcpu) || pdptrs_changed(vcpu))) {
		if (!load_pdptrs(vcpu, vcpu->arch.walk_mmu, cr3))
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

static void nested_prepare_vmcb_save(struct vcpu_svm *svm, struct vmcb *vmcb12)
{
	/* Load the nested guest state */
	svm->vmcb->save.es = vmcb12->save.es;
	svm->vmcb->save.cs = vmcb12->save.cs;
	svm->vmcb->save.ss = vmcb12->save.ss;
	svm->vmcb->save.ds = vmcb12->save.ds;
	svm->vmcb->save.gdtr = vmcb12->save.gdtr;
	svm->vmcb->save.idtr = vmcb12->save.idtr;
	kvm_set_rflags(&svm->vcpu, vmcb12->save.rflags);

	/*
	 * Force-set EFER_SVME even though it is checked earlier on the
	 * VMCB12, because the guest can flip the bit between the check
	 * and now.  Clearing EFER_SVME would call svm_free_nested.
	 */
	svm_set_efer(&svm->vcpu, vmcb12->save.efer | EFER_SVME);

	svm_set_cr0(&svm->vcpu, vmcb12->save.cr0);
	svm_set_cr4(&svm->vcpu, vmcb12->save.cr4);
	svm->vmcb->save.cr2 = svm->vcpu.arch.cr2 = vmcb12->save.cr2;
	kvm_rax_write(&svm->vcpu, vmcb12->save.rax);
	kvm_rsp_write(&svm->vcpu, vmcb12->save.rsp);
	kvm_rip_write(&svm->vcpu, vmcb12->save.rip);

	/* In case we don't even reach vcpu_run, the fields are not updated */
	svm->vmcb->save.rax = vmcb12->save.rax;
	svm->vmcb->save.rsp = vmcb12->save.rsp;
	svm->vmcb->save.rip = vmcb12->save.rip;
	svm->vmcb->save.dr7 = vmcb12->save.dr7;
	svm->vcpu.arch.dr6  = vmcb12->save.dr6;
	svm->vmcb->save.cpl = vmcb12->save.cpl;
}

static void nested_prepare_vmcb_control(struct vcpu_svm *svm)
{
	const u32 int_ctl_vmcb01_bits =
		V_INTR_MASKING_MASK | V_GIF_MASK | V_GIF_ENABLE_MASK;

	const u32 int_ctl_vmcb12_bits = V_TPR_MASK | V_IRQ_INJECTION_BITS_MASK;

	if (nested_npt_enabled(svm))
		nested_svm_init_mmu_context(&svm->vcpu);

	svm->vmcb->control.tsc_offset = svm->vcpu.arch.tsc_offset =
		svm->vcpu.arch.l1_tsc_offset + svm->nested.ctl.tsc_offset;

	svm->vmcb->control.int_ctl =
		(svm->nested.ctl.int_ctl & int_ctl_vmcb12_bits) |
		(svm->nested.hsave->control.int_ctl & int_ctl_vmcb01_bits);

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
	 * Merge guest and host intercepts - must be called  with vcpu in
	 * guest-mode to take affect here
	 */
	recalc_intercepts(svm);

	vmcb_mark_all_dirty(svm->vmcb);
}

int enter_svm_guest_mode(struct vcpu_svm *svm, u64 vmcb12_gpa,
			 struct vmcb *vmcb12)
{
	int ret;

	svm->nested.vmcb12_gpa = vmcb12_gpa;
	nested_prepare_vmcb_save(svm, vmcb12);
	nested_prepare_vmcb_control(svm);

	ret = nested_svm_load_cr3(&svm->vcpu, vmcb12->save.cr3,
				  nested_npt_enabled(svm));
	if (ret)
		return ret;

	if (!npt_enabled)
		svm->vcpu.arch.mmu->inject_page_fault = svm_inject_page_fault_nested;

	svm_set_gif(svm, true);

	return 0;
}

int nested_svm_vmrun(struct vcpu_svm *svm)
{
	int ret;
	struct vmcb *vmcb12;
	struct vmcb *hsave = svm->nested.hsave;
	struct vmcb *vmcb = svm->vmcb;
	struct kvm_host_map map;
	u64 vmcb12_gpa;

	if (is_smm(&svm->vcpu)) {
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
		return 1;
	}

	vmcb12_gpa = svm->vmcb->save.rax;
	ret = kvm_vcpu_map(&svm->vcpu, gpa_to_gfn(vmcb12_gpa), &map);
	if (ret == -EINVAL) {
		kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	} else if (ret) {
		return kvm_skip_emulated_instruction(&svm->vcpu);
	}

	ret = kvm_skip_emulated_instruction(&svm->vcpu);

	vmcb12 = map.hva;

	if (WARN_ON_ONCE(!svm->nested.initialized))
		return -EINVAL;

	load_nested_vmcb_control(svm, &vmcb12->control);

	if (!nested_vmcb_check_save(svm, vmcb12) ||
	    !nested_vmcb_check_controls(&svm->nested.ctl)) {
		vmcb12->control.exit_code    = SVM_EXIT_ERR;
		vmcb12->control.exit_code_hi = 0;
		vmcb12->control.exit_info_1  = 0;
		vmcb12->control.exit_info_2  = 0;
		goto out;
	}

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

	/* Clear internal status */
	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	/*
	 * Save the old vmcb, so we don't need to pick what we save, but can
	 * restore everything when a VMEXIT occurs
	 */
	hsave->save.es     = vmcb->save.es;
	hsave->save.cs     = vmcb->save.cs;
	hsave->save.ss     = vmcb->save.ss;
	hsave->save.ds     = vmcb->save.ds;
	hsave->save.gdtr   = vmcb->save.gdtr;
	hsave->save.idtr   = vmcb->save.idtr;
	hsave->save.efer   = svm->vcpu.arch.efer;
	hsave->save.cr0    = kvm_read_cr0(&svm->vcpu);
	hsave->save.cr4    = svm->vcpu.arch.cr4;
	hsave->save.rflags = kvm_get_rflags(&svm->vcpu);
	hsave->save.rip    = kvm_rip_read(&svm->vcpu);
	hsave->save.rsp    = vmcb->save.rsp;
	hsave->save.rax    = vmcb->save.rax;
	if (npt_enabled)
		hsave->save.cr3    = vmcb->save.cr3;
	else
		hsave->save.cr3    = kvm_read_cr3(&svm->vcpu);

	copy_vmcb_control_area(&hsave->control, &vmcb->control);

	svm->nested.nested_run_pending = 1;

	if (enter_svm_guest_mode(svm, vmcb12_gpa, vmcb12))
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
	kvm_vcpu_unmap(&svm->vcpu, &map, true);

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
	int rc;
	struct vmcb *vmcb12;
	struct vmcb *hsave = svm->nested.hsave;
	struct vmcb *vmcb = svm->vmcb;
	struct kvm_host_map map;

	rc = kvm_vcpu_map(&svm->vcpu, gpa_to_gfn(svm->nested.vmcb12_gpa), &map);
	if (rc) {
		if (rc == -EINVAL)
			kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	}

	vmcb12 = map.hva;

	/* Exit Guest-Mode */
	leave_guest_mode(&svm->vcpu);
	svm->nested.vmcb12_gpa = 0;
	WARN_ON_ONCE(svm->nested.nested_run_pending);

	kvm_clear_request(KVM_REQ_GET_NESTED_STATE_PAGES, &svm->vcpu);

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
	vmcb12->save.cr0    = kvm_read_cr0(&svm->vcpu);
	vmcb12->save.cr3    = kvm_read_cr3(&svm->vcpu);
	vmcb12->save.cr2    = vmcb->save.cr2;
	vmcb12->save.cr4    = svm->vcpu.arch.cr4;
	vmcb12->save.rflags = kvm_get_rflags(&svm->vcpu);
	vmcb12->save.rip    = kvm_rip_read(&svm->vcpu);
	vmcb12->save.rsp    = kvm_rsp_read(&svm->vcpu);
	vmcb12->save.rax    = kvm_rax_read(&svm->vcpu);
	vmcb12->save.dr7    = vmcb->save.dr7;
	vmcb12->save.dr6    = svm->vcpu.arch.dr6;
	vmcb12->save.cpl    = vmcb->save.cpl;

	vmcb12->control.int_state         = vmcb->control.int_state;
	vmcb12->control.exit_code         = vmcb->control.exit_code;
	vmcb12->control.exit_code_hi      = vmcb->control.exit_code_hi;
	vmcb12->control.exit_info_1       = vmcb->control.exit_info_1;
	vmcb12->control.exit_info_2       = vmcb->control.exit_info_2;

	if (vmcb12->control.exit_code != SVM_EXIT_ERR)
		nested_vmcb_save_pending_event(svm, vmcb12);

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

	/* Restore the original control entries */
	copy_vmcb_control_area(&vmcb->control, &hsave->control);

	/* On vmexit the  GIF is set to false */
	svm_set_gif(svm, false);

	svm->vmcb->control.tsc_offset = svm->vcpu.arch.tsc_offset =
		svm->vcpu.arch.l1_tsc_offset;

	svm->nested.ctl.nested_cr3 = 0;

	/* Restore selected save entries */
	svm->vmcb->save.es = hsave->save.es;
	svm->vmcb->save.cs = hsave->save.cs;
	svm->vmcb->save.ss = hsave->save.ss;
	svm->vmcb->save.ds = hsave->save.ds;
	svm->vmcb->save.gdtr = hsave->save.gdtr;
	svm->vmcb->save.idtr = hsave->save.idtr;
	kvm_set_rflags(&svm->vcpu, hsave->save.rflags);
	svm_set_efer(&svm->vcpu, hsave->save.efer);
	svm_set_cr0(&svm->vcpu, hsave->save.cr0 | X86_CR0_PE);
	svm_set_cr4(&svm->vcpu, hsave->save.cr4);
	kvm_rax_write(&svm->vcpu, hsave->save.rax);
	kvm_rsp_write(&svm->vcpu, hsave->save.rsp);
	kvm_rip_write(&svm->vcpu, hsave->save.rip);
	svm->vmcb->save.dr7 = 0;
	svm->vmcb->save.cpl = 0;
	svm->vmcb->control.exit_int_info = 0;

	vmcb_mark_all_dirty(svm->vmcb);

	trace_kvm_nested_vmexit_inject(vmcb12->control.exit_code,
				       vmcb12->control.exit_info_1,
				       vmcb12->control.exit_info_2,
				       vmcb12->control.exit_int_info,
				       vmcb12->control.exit_int_info_err,
				       KVM_ISA_SVM);

	kvm_vcpu_unmap(&svm->vcpu, &map, true);

	nested_svm_uninit_mmu_context(&svm->vcpu);

	rc = nested_svm_load_cr3(&svm->vcpu, hsave->save.cr3, false);
	if (rc)
		return 1;

	if (npt_enabled)
		svm->vmcb->save.cr3 = hsave->save.cr3;

	/*
	 * Drop what we picked up for L2 via svm_complete_interrupts() so it
	 * doesn't end up in L1.
	 */
	svm->vcpu.arch.nmi_injected = false;
	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	return 0;
}

int svm_allocate_nested(struct vcpu_svm *svm)
{
	struct page *hsave_page;

	if (svm->nested.initialized)
		return 0;

	hsave_page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!hsave_page)
		return -ENOMEM;
	svm->nested.hsave = page_address(hsave_page);

	svm->nested.msrpm = svm_vcpu_alloc_msrpm();
	if (!svm->nested.msrpm)
		goto err_free_hsave;
	svm_vcpu_init_msrpm(&svm->vcpu, svm->nested.msrpm);

	svm->nested.initialized = true;
	return 0;

err_free_hsave:
	__free_page(hsave_page);
	return -ENOMEM;
}

void svm_free_nested(struct vcpu_svm *svm)
{
	if (!svm->nested.initialized)
		return;

	svm_vcpu_free_msrpm(svm->nested.msrpm);
	svm->nested.msrpm = NULL;

	__free_page(virt_to_page(svm->nested.hsave));
	svm->nested.hsave = NULL;

	svm->nested.initialized = false;
}

/*
 * Forcibly leave nested mode in order to be able to reset the VCPU later on.
 */
void svm_leave_nested(struct vcpu_svm *svm)
{
	if (is_guest_mode(&svm->vcpu)) {
		struct vmcb *hsave = svm->nested.hsave;
		struct vmcb *vmcb = svm->vmcb;

		svm->nested.nested_run_pending = 0;
		leave_guest_mode(&svm->vcpu);
		copy_vmcb_control_area(&vmcb->control, &hsave->control);
		nested_svm_uninit_mmu_context(&svm->vcpu);
	}

	kvm_clear_request(KVM_REQ_GET_NESTED_STATE_PAGES, &svm->vcpu);
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

int nested_svm_check_permissions(struct vcpu_svm *svm)
{
	if (!(svm->vcpu.arch.efer & EFER_SVME) ||
	    !is_paging(&svm->vcpu)) {
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
		return 1;
	}

	if (svm->vmcb->save.cpl) {
		kvm_inject_gp(&svm->vcpu, 0);
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

static void nested_svm_smi(struct vcpu_svm *svm)
{
	svm->vmcb->control.exit_code = SVM_EXIT_SMI;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;

	nested_svm_vmexit(svm);
}

static void nested_svm_nmi(struct vcpu_svm *svm)
{
	svm->vmcb->control.exit_code = SVM_EXIT_NMI;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;

	nested_svm_vmexit(svm);
}

static void nested_svm_intr(struct vcpu_svm *svm)
{
	trace_kvm_nested_intr_vmexit(svm->vmcb->save.rip);

	svm->vmcb->control.exit_code   = SVM_EXIT_INTR;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;

	nested_svm_vmexit(svm);
}

static inline bool nested_exit_on_init(struct vcpu_svm *svm)
{
	return vmcb_is_intercept(&svm->nested.ctl, INTERCEPT_INIT);
}

static void nested_svm_init(struct vcpu_svm *svm)
{
	svm->vmcb->control.exit_code   = SVM_EXIT_INIT;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;

	nested_svm_vmexit(svm);
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
		nested_svm_init(svm);
		return 0;
	}

	if (vcpu->arch.exception.pending) {
		if (block_nested_events)
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
		nested_svm_smi(svm);
		return 0;
	}

	if (vcpu->arch.nmi_pending && !svm_nmi_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_nmi(svm))
			return 0;
		nested_svm_nmi(svm);
		return 0;
	}

	if (kvm_cpu_has_interrupt(vcpu) && !svm_interrupt_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_intr(svm))
			return 0;
		nested_svm_intr(svm);
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

		if (get_host_vmcb(svm)->control.intercepts[INTERCEPT_EXCEPTION] &
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
	if (copy_to_user(&user_vmcb->save, &svm->nested.hsave->save,
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
	struct vmcb *hsave = svm->nested.hsave;
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
	ctl  = kzalloc(sizeof(*ctl),  GFP_KERNEL);
	save = kzalloc(sizeof(*save), GFP_KERNEL);
	if (!ctl || !save)
		goto out_free;

	ret = -EFAULT;
	if (copy_from_user(ctl, &user_vmcb->control, sizeof(*ctl)))
		goto out_free;
	if (copy_from_user(save, &user_vmcb->save, sizeof(*save)))
		goto out_free;

	ret = -EINVAL;
	if (!nested_vmcb_check_controls(ctl))
		goto out_free;

	/*
	 * Processor state contains L2 state.  Check that it is
	 * valid for guest mode (see nested_vmcb_checks).
	 */
	cr0 = kvm_read_cr0(vcpu);
        if (((cr0 & X86_CR0_CD) == 0) && (cr0 & X86_CR0_NW))
		goto out_free;

	/*
	 * Validate host state saved from before VMRUN (see
	 * nested_svm_check_permissions).
	 * TODO: validate reserved bits for all saved state.
	 */
	if (!(save->cr0 & X86_CR0_PG))
		goto out_free;
	if (!(save->efer & EFER_SVME))
		goto out_free;

	/*
	 * All checks done, we can enter guest mode.  L1 control fields
	 * come from the nested save state.  Guest state is already
	 * in the registers, the save area of the nested state instead
	 * contains saved L1 state.
	 */
	copy_vmcb_control_area(&hsave->control, &svm->vmcb->control);
	hsave->save = *save;

	svm->nested.vmcb12_gpa = kvm_state->hdr.svm.vmcb_pa;
	load_nested_vmcb_control(svm, ctl);
	nested_prepare_vmcb_control(svm);

	kvm_make_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu);
	ret = 0;
out_free:
	kfree(save);
	kfree(ctl);

	return ret;
}

struct kvm_x86_nested_ops svm_nested_ops = {
	.check_events = svm_check_nested_events,
	.get_nested_state_pages = svm_get_nested_state_pages,
	.get_state = svm_get_nested_state,
	.set_state = svm_set_nested_state,
};

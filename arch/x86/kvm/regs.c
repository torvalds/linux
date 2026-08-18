// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm_host.h>

#include "lapic.h"
#include "mmu.h"
#include "regs.h"
#include "x86.h"

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu)
{
	/* Can't read the RIP when guest state is protected, just return 0 */
	if (vcpu->arch.guest_state_protected)
		return 0;

	if (is_64_bit_mode(vcpu))
		return kvm_rip_read(vcpu);
	return (u32)(kvm_get_segment_base(vcpu, VCPU_SREG_CS) +
		     kvm_rip_read(vcpu));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_linear_rip);

bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip)
{
	return kvm_get_linear_rip(vcpu) == linear_rip;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_is_linear_rip);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu)
{
	unsigned long rflags;

	rflags = kvm_x86_call(get_rflags)(vcpu);
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		rflags &= ~X86_EFLAGS_TF;
	return rflags;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_rflags);

void __kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP &&
	    kvm_is_linear_rip(vcpu, vcpu->arch.singlestep_rip))
		rflags |= X86_EFLAGS_TF;
	kvm_x86_call(set_rflags)(vcpu, rflags);
}

void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	__kvm_set_rflags(vcpu, rflags);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_rflags);

static void __get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->arch.emulate_regs_need_sync_to_vcpu) {
		/*
		 * We are here if userspace calls get_regs() in the middle of
		 * instruction emulation. Registers state needs to be copied
		 * back from emulation context to vcpu. Userspace shouldn't do
		 * that usually, but some bad designed PV devices (vmware
		 * backdoor interface) need this to work
		 */
		emulator_writeback_register_cache(vcpu->arch.emulate_ctxt);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;
	}
	regs->rax = kvm_rax_read_raw(vcpu);
	regs->rbx = kvm_rbx_read_raw(vcpu);
	regs->rcx = kvm_rcx_read_raw(vcpu);
	regs->rdx = kvm_rdx_read_raw(vcpu);
	regs->rsi = kvm_rsi_read_raw(vcpu);
	regs->rdi = kvm_rdi_read_raw(vcpu);
	regs->rsp = kvm_rsp_read(vcpu);
	regs->rbp = kvm_rbp_read_raw(vcpu);
#ifdef CONFIG_X86_64
	regs->r8 = kvm_r8_read_raw(vcpu);
	regs->r9 = kvm_r9_read_raw(vcpu);
	regs->r10 = kvm_r10_read_raw(vcpu);
	regs->r11 = kvm_r11_read_raw(vcpu);
	regs->r12 = kvm_r12_read_raw(vcpu);
	regs->r13 = kvm_r13_read_raw(vcpu);
	regs->r14 = kvm_r14_read_raw(vcpu);
	regs->r15 = kvm_r15_read_raw(vcpu);
#endif

	regs->rip = kvm_rip_read(vcpu);
	regs->rflags = kvm_get_rflags(vcpu);
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__get_regs(vcpu, regs);
	vcpu_put(vcpu);
	return 0;
}

static void __set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu->arch.emulate_regs_need_sync_from_vcpu = true;
	vcpu->arch.emulate_regs_need_sync_to_vcpu = false;

	kvm_rax_write_raw(vcpu, regs->rax);
	kvm_rbx_write_raw(vcpu, regs->rbx);
	kvm_rcx_write_raw(vcpu, regs->rcx);
	kvm_rdx_write_raw(vcpu, regs->rdx);
	kvm_rsi_write_raw(vcpu, regs->rsi);
	kvm_rdi_write_raw(vcpu, regs->rdi);
	kvm_rsp_write(vcpu, regs->rsp);
	kvm_rbp_write_raw(vcpu, regs->rbp);
#ifdef CONFIG_X86_64
	kvm_r8_write_raw(vcpu, regs->r8);
	kvm_r9_write_raw(vcpu, regs->r9);
	kvm_r10_write_raw(vcpu, regs->r10);
	kvm_r11_write_raw(vcpu, regs->r11);
	kvm_r12_write_raw(vcpu, regs->r12);
	kvm_r13_write_raw(vcpu, regs->r13);
	kvm_r14_write_raw(vcpu, regs->r14);
	kvm_r15_write_raw(vcpu, regs->r15);
#endif

	kvm_rip_write(vcpu, regs->rip);
	kvm_set_rflags(vcpu, regs->rflags | X86_EFLAGS_FIXED);

	vcpu->arch.exception.pending = false;
	vcpu->arch.exception_vmexit.pending = false;

	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__set_regs(vcpu, regs);
	vcpu_put(vcpu);
	return 0;
}

static inline u64 pdptr_rsvd_bits(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.reserved_gpa_bits | rsvd_bits(5, 8) | rsvd_bits(1, 2);
}

/*
 * Load the pae pdptrs.  Return 1 if they are all valid, 0 otherwise.
 */
int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	struct kvm_pagewalk *w = &vcpu->arch.gva_walk;
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	gpa_t real_gpa;
	int i;
	int ret;
	u64 pdpte[ARRAY_SIZE(vcpu->arch.pdptrs)];

	/*
	 * If the MMU is nested, CR3 holds an L2 GPA and needs to be translated
	 * to an L1 GPA.
	 */
	real_gpa = kvm_translate_gpa(vcpu, w, gfn_to_gpa(pdpt_gfn),
				     PFERR_USER_MASK | PFERR_WRITE_MASK |
				     PFERR_GUEST_PAGE_MASK, NULL, 0);
	if (real_gpa == INVALID_GPA)
		return 0;

	/* Note the offset, PDPTRs are 32 byte aligned when using PAE paging. */
	ret = kvm_vcpu_read_guest_page(vcpu, gpa_to_gfn(real_gpa), pdpte,
				       cr3 & GENMASK(11, 5), sizeof(pdpte));
	if (ret < 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(pdpte); ++i) {
		if ((pdpte[i] & PT_PRESENT_MASK) &&
		    (pdpte[i] & pdptr_rsvd_bits(vcpu))) {
			return 0;
		}
	}

	/*
	 * Marking VCPU_REG_PDPTR dirty doesn't work for !tdp_enabled.
	 * Shadow page roots need to be reconstructed instead.
	 */
	if (!tdp_enabled && memcmp(vcpu->arch.pdptrs, pdpte, sizeof(vcpu->arch.pdptrs)))
		kvm_mmu_free_roots(vcpu->kvm, &vcpu->arch.root_mmu,
				   KVM_MMU_ROOT_CURRENT);

	memcpy(vcpu->arch.pdptrs, pdpte, sizeof(vcpu->arch.pdptrs));
	kvm_register_mark_dirty(vcpu, VCPU_REG_PDPTR);
	kvm_make_request(KVM_REQ_LOAD_MMU_PGD, vcpu);
	vcpu->arch.pdptrs_from_userspace = false;

	return 1;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(load_pdptrs);

static bool kvm_is_valid_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
#ifdef CONFIG_X86_64
	if (cr0 & 0xffffffff00000000UL)
		return false;
#endif

	if ((cr0 & X86_CR0_NW) && !(cr0 & X86_CR0_CD))
		return false;

	if ((cr0 & X86_CR0_PG) && !(cr0 & X86_CR0_PE))
		return false;

	return kvm_x86_call(is_valid_cr0)(vcpu, cr0);
}

void kvm_post_set_cr0(struct kvm_vcpu *vcpu, unsigned long old_cr0, unsigned long cr0)
{
	/*
	 * CR0.WP is incorporated into the MMU role, but only for non-nested,
	 * indirect shadow MMUs.  If paging is disabled, no updates are needed
	 * as there are no permission bits to emulate.  If TDP is enabled, the
	 * MMU's metadata needs to be updated, e.g. so that emulating guest
	 * translations does the right thing, but there's no need to unload the
	 * root as CR0.WP doesn't affect SPTEs.
	 */
	if ((cr0 ^ old_cr0) == X86_CR0_WP) {
		if (!(cr0 & X86_CR0_PG))
			return;

		if (tdp_enabled) {
			kvm_init_mmu(vcpu);
			return;
		}
	}

	if ((cr0 ^ old_cr0) & X86_CR0_PG) {
		/*
		 * Clearing CR0.PG is defined to flush the TLB from the guest's
		 * perspective.
		 */
		if (!(cr0 & X86_CR0_PG))
			kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
		/*
		 * Check for async #PF completion events when enabling paging,
		 * as the vCPU may have previously encountered async #PFs (it's
		 * entirely legal for the guest to toggle paging on/off without
		 * waiting for the async #PF queue to drain).
		 */
		else if (kvm_pv_async_pf_enabled(vcpu))
			kvm_make_request(KVM_REQ_APF_READY, vcpu);
	}

	if ((cr0 ^ old_cr0) & KVM_MMU_CR0_ROLE_BITS)
		kvm_mmu_reset_context(vcpu);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_post_set_cr0);

int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	unsigned long old_cr0 = kvm_read_cr0(vcpu);

	if (!kvm_is_valid_cr0(vcpu, cr0))
		return 1;

	cr0 |= X86_CR0_ET;

	/* Write to CR0 reserved bits are ignored, even on Intel. */
	cr0 &= ~CR0_RESERVED_BITS;

#ifdef CONFIG_X86_64
	if ((vcpu->arch.efer & EFER_LME) && !is_paging(vcpu) &&
	    (cr0 & X86_CR0_PG)) {
		int cs_db, cs_l;

		if (!is_pae(vcpu))
			return 1;
		kvm_x86_call(get_cs_db_l_bits)(vcpu, &cs_db, &cs_l);
		if (cs_l)
			return 1;
	}
#endif
	if (!(vcpu->arch.efer & EFER_LME) && (cr0 & X86_CR0_PG) &&
	    is_pae(vcpu) && ((cr0 ^ old_cr0) & X86_CR0_PDPTR_BITS) &&
	    !load_pdptrs(vcpu, kvm_read_cr3(vcpu)))
		return 1;

	if (!(cr0 & X86_CR0_PG) &&
	    (is_64_bit_mode(vcpu) || kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE)))
		return 1;

	if (!(cr0 & X86_CR0_WP) && kvm_is_cr4_bit_set(vcpu, X86_CR4_CET))
		return 1;

	kvm_x86_call(set_cr0)(vcpu, cr0);

	kvm_post_set_cr0(vcpu, old_cr0, cr0);

	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_cr0);

void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw)
{
	(void)kvm_set_cr0(vcpu, kvm_read_cr0_bits(vcpu, ~0x0eul) | (msw & 0x0f));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_lmsw);

int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	bool skip_tlb_flush = false;
	unsigned long pcid = 0;
#ifdef CONFIG_X86_64
	if (kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE)) {
		skip_tlb_flush = cr3 & X86_CR3_PCID_NOFLUSH;
		cr3 &= ~X86_CR3_PCID_NOFLUSH;
		pcid = cr3 & X86_CR3_PCID_MASK;
	}
#endif

	/* PDPTRs are always reloaded for PAE paging. */
	if (cr3 == kvm_read_cr3(vcpu) && !is_pae_paging(vcpu))
		goto handle_tlb_flush;

	/*
	 * Do not condition the GPA check on long mode, this helper is used to
	 * stuff CR3, e.g. for RSM emulation, and there is no guarantee that
	 * the current vCPU mode is accurate.
	 */
	if (!kvm_vcpu_is_legal_cr3(vcpu, cr3))
		return 1;

	if (is_pae_paging(vcpu) && !load_pdptrs(vcpu, cr3))
		return 1;

	if (cr3 != kvm_read_cr3(vcpu))
		kvm_mmu_new_pgd(vcpu, cr3);

	vcpu->arch.cr3 = cr3;
	kvm_register_mark_dirty(vcpu, VCPU_REG_CR3);
	/* Do not call post_set_cr3, we do not get here for confidential guests.  */

handle_tlb_flush:
	/*
	 * A load of CR3 that flushes the TLB flushes only the current PCID,
	 * even if PCID is disabled, in which case PCID=0 is flushed.  It's a
	 * moot point in the end because _disabling_ PCID will flush all PCIDs,
	 * and it's impossible to use a non-zero PCID when PCID is disabled,
	 * i.e. only PCID=0 can be relevant.
	 */
	if (!skip_tlb_flush)
		kvm_invalidate_pcid(vcpu, pcid);

	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_cr3);

static bool kvm_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	return __kvm_is_valid_cr4(vcpu, cr4) &&
	       kvm_x86_call(is_valid_cr4)(vcpu, cr4);
}

void kvm_post_set_cr4(struct kvm_vcpu *vcpu, unsigned long old_cr4, unsigned long cr4)
{
	if ((cr4 ^ old_cr4) & KVM_MMU_CR4_ROLE_BITS)
		kvm_mmu_reset_context(vcpu);

	/*
	 * If CR4.PCIDE is changed 0 -> 1, there is no need to flush the TLB
	 * according to the SDM; however, stale prev_roots could be reused
	 * incorrectly in the future after a MOV to CR3 with NOFLUSH=1, so we
	 * free them all.  This is *not* a superset of KVM_REQ_TLB_FLUSH_GUEST
	 * or KVM_REQ_TLB_FLUSH_CURRENT, because the hardware TLB is not flushed,
	 * so fall through.
	 */
	if (!tdp_enabled &&
	    (cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE))
		kvm_mmu_unload(vcpu);

	/*
	 * The TLB has to be flushed for all PCIDs if any of the following
	 * (architecturally required) changes happen:
	 * - CR4.PCIDE is changed from 1 to 0
	 * - CR4.PGE is toggled
	 *
	 * This is a superset of KVM_REQ_TLB_FLUSH_CURRENT.
	 */
	if (((cr4 ^ old_cr4) & X86_CR4_PGE) ||
	    (!(cr4 & X86_CR4_PCIDE) && (old_cr4 & X86_CR4_PCIDE)))
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);

	/*
	 * The TLB has to be flushed for the current PCID if any of the
	 * following (architecturally required) changes happen:
	 * - CR4.SMEP is changed from 0 to 1
	 * - CR4.PAE is toggled
	 */
	else if (((cr4 ^ old_cr4) & X86_CR4_PAE) ||
		 ((cr4 & X86_CR4_SMEP) && !(old_cr4 & X86_CR4_SMEP)))
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);

}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_post_set_cr4);

int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long old_cr4 = kvm_read_cr4(vcpu);

	if (!kvm_is_valid_cr4(vcpu, cr4))
		return 1;

	if (is_long_mode(vcpu)) {
		if (!(cr4 & X86_CR4_PAE))
			return 1;
		if ((cr4 ^ old_cr4) & X86_CR4_LA57)
			return 1;
	} else if (is_paging(vcpu) && (cr4 & X86_CR4_PAE)
		   && ((cr4 ^ old_cr4) & X86_CR4_PDPTR_BITS)
		   && !load_pdptrs(vcpu, kvm_read_cr3(vcpu)))
		return 1;

	if ((cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE)) {
		/* PCID can not be enabled when cr3[11:0]!=000H or EFER.LMA=0 */
		if ((kvm_read_cr3(vcpu) & X86_CR3_PCID_MASK) || !is_long_mode(vcpu))
			return 1;
	}

	if ((cr4 & X86_CR4_CET) && !kvm_is_cr0_bit_set(vcpu, X86_CR0_WP))
		return 1;

	kvm_x86_call(set_cr4)(vcpu, cr4);

	kvm_post_set_cr4(vcpu, old_cr4, cr4);

	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_cr4);

int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	if (cr8 & CR8_RESERVED_BITS)
		return 1;
	if (lapic_in_kernel(vcpu))
		kvm_lapic_set_tpr(vcpu, cr8);
	else
		vcpu->arch.cr8 = cr8;
	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_cr8);

unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu)
{
	if (lapic_in_kernel(vcpu))
		return kvm_lapic_get_cr8(vcpu);
	else
		return vcpu->arch.cr8;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_cr8);

static void __get_sregs_common(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct desc_ptr dt;

	if (vcpu->arch.guest_state_protected)
		goto skip_protected_regs;

	kvm_handle_exception_payload_quirk(vcpu);

	kvm_get_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_get_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_get_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_get_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_get_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_get_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_get_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_get_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_x86_call(get_idt)(vcpu, &dt);
	sregs->idt.limit = dt.size;
	sregs->idt.base = dt.address;
	kvm_x86_call(get_gdt)(vcpu, &dt);
	sregs->gdt.limit = dt.size;
	sregs->gdt.base = dt.address;

	sregs->cr2 = vcpu->arch.cr2;
	sregs->cr3 = kvm_read_cr3(vcpu);

skip_protected_regs:
	sregs->cr0 = kvm_read_cr0(vcpu);
	sregs->cr4 = kvm_read_cr4(vcpu);
	sregs->cr8 = kvm_get_cr8(vcpu);
	sregs->efer = vcpu->arch.efer;
	sregs->apic_base = vcpu->arch.apic_base;
}

static void __get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	__get_sregs_common(vcpu, sregs);

	if (vcpu->arch.guest_state_protected)
		return;

	if (vcpu->arch.interrupt.injected && !vcpu->arch.interrupt.soft)
		set_bit(vcpu->arch.interrupt.nr,
			(unsigned long *)sregs->interrupt_bitmap);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__get_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return 0;
}

void kvm_vcpu_ioctl_x86_get_sregs2(struct kvm_vcpu *vcpu,
				   struct kvm_sregs2 *sregs2)
{
	int i;

	__get_sregs_common(vcpu, (struct kvm_sregs *)sregs2);

	if (vcpu->arch.guest_state_protected)
		return;

	if (is_pae_paging(vcpu)) {
		kvm_vcpu_srcu_read_lock(vcpu);
		for (i = 0 ; i < 4 ; i++)
			sregs2->pdptrs[i] = kvm_pdptr_read(vcpu, i);
		sregs2->flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;
		kvm_vcpu_srcu_read_unlock(vcpu);
	}
}

static bool kvm_is_valid_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	if ((sregs->efer & EFER_LME) && (sregs->cr0 & X86_CR0_PG)) {
		/*
		 * When EFER.LME and CR0.PG are set, the processor is in
		 * 64-bit mode (though maybe in a 32-bit code segment).
		 * CR4.PAE and EFER.LMA must be set.
		 */
		if (!(sregs->cr4 & X86_CR4_PAE) || !(sregs->efer & EFER_LMA))
			return false;
		if (!kvm_vcpu_is_legal_cr3(vcpu, sregs->cr3))
			return false;
	} else {
		/*
		 * Not in 64-bit mode: EFER.LMA is clear and the code
		 * segment cannot be 64-bit.
		 */
		if (sregs->efer & EFER_LMA || sregs->cs.l)
			return false;
	}

	return kvm_is_valid_cr4(vcpu, sregs->cr4) &&
	       kvm_is_valid_cr0(vcpu, sregs->cr0) &&
	       kvm_valid_efer(vcpu, sregs->efer);
}

static int __set_sregs_common(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs,
			      int *mmu_reset_needed, bool update_pdptrs)
{
	int idx;
	struct desc_ptr dt;

	if (!kvm_is_valid_sregs(vcpu, sregs))
		return -EINVAL;

	if (kvm_apic_set_base(vcpu, sregs->apic_base, true))
		return -EINVAL;

	if (vcpu->arch.guest_state_protected)
		return 0;

	dt.size = sregs->idt.limit;
	dt.address = sregs->idt.base;
	kvm_x86_call(set_idt)(vcpu, &dt);
	dt.size = sregs->gdt.limit;
	dt.address = sregs->gdt.base;
	kvm_x86_call(set_gdt)(vcpu, &dt);

	vcpu->arch.cr2 = sregs->cr2;
	*mmu_reset_needed |= kvm_read_cr3(vcpu) != sregs->cr3;
	vcpu->arch.cr3 = sregs->cr3;
	kvm_register_mark_dirty(vcpu, VCPU_REG_CR3);
	kvm_x86_call(post_set_cr3)(vcpu, sregs->cr3);

	*mmu_reset_needed |= vcpu->arch.efer != sregs->efer;
	kvm_x86_call(set_efer)(vcpu, sregs->efer);

	*mmu_reset_needed |= kvm_read_cr0(vcpu) != sregs->cr0;
	kvm_x86_call(set_cr0)(vcpu, sregs->cr0);

	*mmu_reset_needed |= kvm_read_cr4(vcpu) != sregs->cr4;
	kvm_x86_call(set_cr4)(vcpu, sregs->cr4);

	if (update_pdptrs) {
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		if (is_pae_paging(vcpu)) {
			load_pdptrs(vcpu, kvm_read_cr3(vcpu));
			*mmu_reset_needed = 1;
		}
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
	}

	kvm_set_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_set_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_set_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_set_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_set_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_set_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_set_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_set_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_set_cr8(vcpu, sregs->cr8);

	/* Older userspace won't unhalt the vcpu on reset. */
	if (kvm_vcpu_is_bsp(vcpu) && kvm_rip_read(vcpu) == 0xfff0 &&
	    sregs->cs.selector == 0xf000 && sregs->cs.base == 0xffff0000 &&
	    !is_protmode(vcpu))
		kvm_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);

	return 0;
}

static int __set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	int pending_vec, max_bits;
	int mmu_reset_needed = 0;
	int ret = __set_sregs_common(vcpu, sregs, &mmu_reset_needed, true);

	if (ret)
		return ret;

	if (mmu_reset_needed) {
		kvm_mmu_reset_context(vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
	}

	max_bits = KVM_NR_INTERRUPTS;
	pending_vec = find_first_bit(
		(const unsigned long *)sregs->interrupt_bitmap, max_bits);

	if (pending_vec < max_bits) {
		kvm_queue_interrupt(vcpu, pending_vec, false);
		pr_debug("Set back pending irq %d\n", pending_vec);
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	}
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int ret;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	ret = __set_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return ret;
}

int kvm_vcpu_ioctl_x86_set_sregs2(struct kvm_vcpu *vcpu,
				  struct kvm_sregs2 *sregs2)
{
	int mmu_reset_needed = 0;
	bool valid_pdptrs = sregs2->flags & KVM_SREGS2_FLAGS_PDPTRS_VALID;
	bool pae = (sregs2->cr0 & X86_CR0_PG) && (sregs2->cr4 & X86_CR4_PAE) &&
		!(sregs2->efer & EFER_LMA);
	int i, ret;

	if (sregs2->flags & ~KVM_SREGS2_FLAGS_PDPTRS_VALID)
		return -EINVAL;

	if (valid_pdptrs && (!pae || vcpu->arch.guest_state_protected))
		return -EINVAL;

	ret = __set_sregs_common(vcpu, (struct kvm_sregs *)sregs2,
				 &mmu_reset_needed, !valid_pdptrs);
	if (ret)
		return ret;

	if (valid_pdptrs) {
		for (i = 0; i < 4 ; i++)
			kvm_pdptr_write(vcpu, i, sregs2->pdptrs[i]);

		kvm_register_mark_dirty(vcpu, VCPU_REG_PDPTR);
		mmu_reset_needed = 1;
		vcpu->arch.pdptrs_from_userspace = true;
	}
	if (mmu_reset_needed) {
		kvm_mmu_reset_context(vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
	}
	return 0;
}

void kvm_run_sync_regs_to_user(struct kvm_vcpu *vcpu)
{
	BUILD_BUG_ON(sizeof(struct kvm_sync_regs) > SYNC_REGS_SIZE_BYTES);

	if (vcpu->run->kvm_valid_regs & KVM_SYNC_X86_REGS)
		__get_regs(vcpu, &vcpu->run->s.regs.regs);

	if (vcpu->run->kvm_valid_regs & KVM_SYNC_X86_SREGS)
		__get_sregs(vcpu, &vcpu->run->s.regs.sregs);
}

int kvm_run_sync_regs_from_user(struct kvm_vcpu *vcpu)
{
	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_REGS) {
		__set_regs(vcpu, &vcpu->run->s.regs.regs);
		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_REGS;
	}

	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_SREGS) {
		struct kvm_sregs sregs = vcpu->run->s.regs.sregs;

		if (__set_sregs(vcpu, &sregs))
			return -EINVAL;

		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_SREGS;
	}

	return 0;
}

void kvm_update_dr0123(struct kvm_vcpu *vcpu)
{
	int i;

	if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)) {
		for (i = 0; i < KVM_NR_DB_REGS; i++)
			vcpu->arch.eff_db[i] = vcpu->arch.db[i];
	}
}

void kvm_update_dr7(struct kvm_vcpu *vcpu)
{
	unsigned long dr7;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		dr7 = vcpu->arch.guest_debug_dr7;
	else
		dr7 = vcpu->arch.dr7;
	kvm_x86_call(set_dr7)(vcpu, dr7);
	vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_BP_ENABLED;
	if (dr7 & DR7_BP_EN_MASK)
		vcpu->arch.switch_db_regs |= KVM_DEBUGREG_BP_ENABLED;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_update_dr7);

static u64 kvm_dr6_fixed(struct kvm_vcpu *vcpu)
{
	u64 fixed = DR6_FIXED_1;

	if (!guest_cpu_cap_has(vcpu, X86_FEATURE_RTM))
		fixed |= DR6_RTM;

	if (!guest_cpu_cap_has(vcpu, X86_FEATURE_BUS_LOCK_DETECT))
		fixed |= DR6_BUS_LOCK;
	return fixed;
}

int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
{
	size_t size = ARRAY_SIZE(vcpu->arch.db);

	switch (dr) {
	case 0 ... 3:
		vcpu->arch.db[array_index_nospec(dr, size)] = val;
		if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP))
			vcpu->arch.eff_db[dr] = val;
		break;
	case 4:
	case 6:
		if (!kvm_dr6_valid(val))
			return 1; /* #GP */
		vcpu->arch.dr6 = (val & DR6_VOLATILE) | kvm_dr6_fixed(vcpu);
		break;
	case 5:
	default: /* 7 */
		if (!kvm_dr7_valid(val))
			return 1; /* #GP */
		vcpu->arch.dr7 = (val & DR7_VOLATILE) | DR7_FIXED_1;
		kvm_update_dr7(vcpu);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_dr);

unsigned long kvm_get_dr(struct kvm_vcpu *vcpu, int dr)
{
	size_t size = ARRAY_SIZE(vcpu->arch.db);

	switch (dr) {
	case 0 ... 3:
		return vcpu->arch.db[array_index_nospec(dr, size)];
	case 4:
	case 6:
		return vcpu->arch.dr6;
	case 5:
	default: /* 7 */
		return vcpu->arch.dr7;
	}
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_dr);

int kvm_vcpu_ioctl_x86_get_debugregs(struct kvm_vcpu *vcpu,
				     struct kvm_debugregs *dbgregs)
{
	unsigned int i;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	kvm_handle_exception_payload_quirk(vcpu);

	memset(dbgregs, 0, sizeof(*dbgregs));

	BUILD_BUG_ON(ARRAY_SIZE(vcpu->arch.db) != ARRAY_SIZE(dbgregs->db));
	for (i = 0; i < ARRAY_SIZE(vcpu->arch.db); i++)
		dbgregs->db[i] = vcpu->arch.db[i];

	dbgregs->dr6 = vcpu->arch.dr6;
	dbgregs->dr7 = vcpu->arch.dr7;
	return 0;
}

int kvm_vcpu_ioctl_x86_set_debugregs(struct kvm_vcpu *vcpu,
				     struct kvm_debugregs *dbgregs)
{
	unsigned int i;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	if (dbgregs->flags)
		return -EINVAL;

	if (!kvm_dr6_valid(dbgregs->dr6))
		return -EINVAL;
	if (!kvm_dr7_valid(dbgregs->dr7))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.db); i++)
		vcpu->arch.db[i] = dbgregs->db[i];

	kvm_update_dr0123(vcpu);
	vcpu->arch.dr6 = dbgregs->dr6;
	vcpu->arch.dr7 = dbgregs->dr7;
	kvm_update_dr7(vcpu);

	return 0;
}

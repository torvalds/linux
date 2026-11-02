// SPDX-License-Identifier: GPL-2.0
#include <linux/kvm_host.h>

#include <arm64/kvm_emulate.h>

#include <faultin.h>

#include "trace.h"

static inline bool kvm_s390_cur_gmap_fault_is_write(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.sae_block.hai.pic & PGM_INT_CODE_MASK) == PGM_PROTECTION ||
	       vcpu->arch.sae_block.hai.teid.fsi == TEID_FSI_STORE;
}

/*
 * user_mem_abort() - handle a dat fault for the gmap of a vcpu
 *
 * Return: 0 on success, < 0 in case of error.
 * Context: The mm lock must not be held before calling. May sleep.
 */
static int user_mem_abort(struct kvm_vcpu *vcpu, gpa_t fault_ipa,
			  struct kvm_memory_slot *slot, hva_t hva)
{
	struct guest_fault f = { };
	int ret;

	if (kvm_s390_cur_gmap_fault_is_write(vcpu))
		f.write_attempt = FOLL_WRITE;
	f.gfn = gpa_to_gfn(fault_ipa);

	ret = kvm_s390_faultin_gfn(vcpu, NULL, &f);
	if (ret <= 0)
		return ret;
	if (ret == PGM_ADDRESSING)
		/*
		 * Without the relevant sysregs we cannot do anything for now.
		 * Go back to userspace with an error. TODO sysreg handling
		 */
		return -ENOEXEC;
	KVM_BUG_ON(ret, vcpu->kvm);
	return -EINVAL;
}

static int kvm_handle_pic(struct kvm_vcpu *vcpu, bool *translation)
{
	switch (kvm_vcpu_fault_pic(vcpu)) {
	/* expected cases: */
	case PGM_ASCE_TYPE:
	case PGM_REGION_FIRST_TRANS:
	case PGM_REGION_SECOND_TRANS:
	case PGM_REGION_THIRD_TRANS:
	case PGM_SEGMENT_TRANSLATION:
	case PGM_PAGE_TRANSLATION:
		*translation = true;
		break;
	case PGM_PROTECTION:
		break;
	/* unexpected cases: */
	case 0:
		KVM_BUG(1, vcpu->kvm, "On MMU fault path but no fault occurred");
		return -EFAULT;
	default:
		KVM_BUG(1, vcpu->kvm, "Unexpected program interrupt 0x%x, TEID 0x%016lx",
			vcpu->arch.sae_block.hai.pic, vcpu->arch.sae_block.hai.teid.val);
		send_sig(SIGSEGV, current, 0);
		return -EFAULT;
	}

	return 0;
}

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu)
{
	struct kvm_memory_slot *memslot;
	bool translation = false;
	phys_addr_t fault_ipa;
	unsigned long hva;
	unsigned long esr;
	bool write_fault;
	bool guest_size_err;
	bool writable;
	bool is_iabt;
	int ret;
	gfn_t gfn;

	esr = kvm_vcpu_get_esr(vcpu);
	fault_ipa = kvm_vcpu_get_fault_ipa(vcpu);
	is_iabt = kvm_vcpu_trap_is_iabt(vcpu);
	guest_size_err = vcpu->arch.sae_block.icptr == SAE_ICPTR_GUEST_ADDRESS_SIZE;

	if (guest_size_err) {
		translation = true;
	} else {
		ret = kvm_handle_pic(vcpu, &translation);
		if (ret)
			return ret;
	}

	if (translation) {
		/*
		 * For both cases:
		 * Without the relevant sysregs we cannot do anything for now.
		 * Go back to userspace with an error. TODO sysreg handling
		 */
		if (fault_ipa >= BIT_ULL(get_kvm_ipa_limit()))
			return -ENOEXEC;

		if (fault_ipa >= vcpu->kvm->arch.guest_phys_size)
			return -ENOEXEC;
	}

	trace_kvm_guest_fault(*vcpu_pc(vcpu), kvm_vcpu_get_esr(vcpu),
		      kvm_vcpu_get_hfar(vcpu), fault_ipa);

	kvm_vcpu_srcu_read_lock(vcpu);

	gfn = fault_ipa >> PAGE_SHIFT;

	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writable);
	write_fault = kvm_s390_cur_gmap_fault_is_write(vcpu);
	if (kvm_is_error_hva(hva) || (write_fault && !writable)) {
		ret = -ENOEXEC;
		/*
		 * The guest has put either its instructions or its page-tables
		 * somewhere it shouldn't have. Userspace won't be able to do
		 * anything about this (there's no syndrome for a start).
		 *
		 * Without the relevant sysregs we cannot do anything for now.
		 * Go back to userspace with an error. TODO sysreg handling
		 */
		if (is_iabt)
			goto out_unlock;

		if (kvm_vcpu_abt_iss1tw(vcpu)) {
			/*
			 * Without the relevant sysregs we cannot do anything for now.
			 * Go back to userspace with an error. TODO sysreg handling
			 */
			goto out_unlock;
		}

		/*
		 * Check for a cache maintenance operation. Assume the guest is
		 * cautious and skip instruction
		 */
		if (kvm_is_error_hva(hva) && esr_dabt_is_cm(esr)) {
			kvm_incr_pc(vcpu);
			ret = 1;
			goto out_unlock;
		}

		/*
		 * The IPA is reported as [MAX:12], so we need to
		 * complement it with the bottom 12 bits from the
		 * faulting VA. This is always 12 bits, irrespective
		 * of the page size.
		 */
		fault_ipa |= kvm_vcpu_get_hfar(vcpu) & ((1 << 12) - 1);
		ret = io_mem_abort(vcpu, fault_ipa);
		goto out_unlock;
	}

	/* Userspace should not be able to register out-of-bounds IPAs */
	VM_BUG_ON(fault_ipa >= vcpu->kvm->arch.guest_phys_size);
	/*
	 * Proper guest size faults have been injected.
	 * In theory it's fine to have device memory higher than MSL,
	 * even if not currently possible, but that would have been handled above.
	 * So if we get here with a guest size intercept, we have a bug somewhere.
	 */
	VM_BUG_ON(guest_size_err);

	ret = user_mem_abort(vcpu, fault_ipa, memslot, hva);
	if (!ret)
		ret = 1;
out_unlock:
	kvm_vcpu_srcu_read_unlock(vcpu);
	return ret;
}

long kvm_arch_vcpu_pre_fault_memory(struct kvm_vcpu *vcpu, struct kvm_pre_fault_memory *range)
{
	struct guest_fault f = { .gfn = gpa_to_gfn(range->gpa), };
	gpa_t end;
	int rc;

	rc = kvm_s390_faultin_gfn(vcpu, NULL, &f);
	if (rc == PGM_ADDRESSING)
		return -ENOENT;
	if (rc > 0)
		return -EIO;
	if (rc < 0)
		return rc;

	if (f.ptep)
		return PAGE_SIZE;

	end = ALIGN(range->gpa + PAGE_SIZE, f.crste_region3 ? _REGION3_SIZE : HPAGE_SIZE);
	return min(range->size, end - range->gpa);
}

int kvm_arch_pre_fault_allowed(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_initialized(vcpu))
		return -ENOEXEC;

	return 0;
}

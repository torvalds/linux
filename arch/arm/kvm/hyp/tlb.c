// SPDX-License-Identifier: GPL-2.0-only
/*
 * Original code:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * Mostly rewritten in C by Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

/**
 * Flush per-VMID TLBs
 *
 * __kvm_tlb_flush_vmid(struct kvm *kvm);
 *
 * We rely on the hardware to broadcast the TLB invalidation to all CPUs
 * inside the inner-shareable domain (which is the case for all v7
 * implementations).  If we come across a non-IS SMP implementation, we'll
 * have to use an IPI based mechanism. Until then, we stick to the simple
 * hardware assisted version.
 *
 * As v7 does not support flushing per IPA, just nuke the whole TLB
 * instead, ignoring the ipa value.
 */
void __hyp_text __kvm_tlb_flush_vmid(struct kvm *kvm)
{
	dsb(ishst);

	/* Switch to requested VMID */
	kvm = kern_hyp_va(kvm);
	write_sysreg(kvm_get_vttbr(kvm), VTTBR);
	isb();

	write_sysreg(0, TLBIALLIS);
	dsb(ish);
	isb();

	write_sysreg(0, VTTBR);
}

void __hyp_text __kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa)
{
	__kvm_tlb_flush_vmid(kvm);
}

void __hyp_text __kvm_tlb_flush_local_vmid(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(kern_hyp_va(vcpu)->kvm);

	/* Switch to requested VMID */
	write_sysreg(kvm_get_vttbr(kvm), VTTBR);
	isb();

	write_sysreg(0, TLBIALL);
	dsb(nsh);
	isb();

	write_sysreg(0, VTTBR);
}

void __hyp_text __kvm_flush_vm_context(void)
{
	write_sysreg(0, TLBIALLNSNHIS);
	write_sysreg(0, ICIALLUIS);
	dsb(ish);
}

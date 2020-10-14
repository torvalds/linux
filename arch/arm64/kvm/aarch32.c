// SPDX-License-Identifier: GPL-2.0-only
/*
 * (not much of an) Emulation layer for 32bit guests.
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * based on arch/arm/kvm/emulate.c
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/bits.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

#define DFSR_FSC_EXTABT_LPAE	0x10
#define DFSR_FSC_EXTABT_nLPAE	0x08
#define DFSR_LPAE		BIT(9)

static bool pre_fault_synchronize(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	if (vcpu->arch.sysregs_loaded_on_cpu) {
		kvm_arch_vcpu_put(vcpu);
		return true;
	}

	preempt_enable();
	return false;
}

static void post_fault_synchronize(struct kvm_vcpu *vcpu, bool loaded)
{
	if (loaded) {
		kvm_arch_vcpu_load(vcpu, smp_processor_id());
		preempt_enable();
	}
}

void kvm_inject_undef32(struct kvm_vcpu *vcpu)
{
	vcpu->arch.flags |= (KVM_ARM64_EXCEPT_AA32_UND |
			     KVM_ARM64_PENDING_EXCEPTION);
}

/*
 * Modelled after TakeDataAbortException() and TakePrefetchAbortException
 * pseudocode.
 */
static void inject_abt32(struct kvm_vcpu *vcpu, bool is_pabt,
			 unsigned long addr)
{
	u32 *far, *fsr;
	bool is_lpae;
	bool loaded;

	loaded = pre_fault_synchronize(vcpu);

	if (is_pabt) {
		vcpu->arch.flags |= (KVM_ARM64_EXCEPT_AA32_IABT |
				     KVM_ARM64_PENDING_EXCEPTION);
		far = &vcpu_cp15(vcpu, c6_IFAR);
		fsr = &vcpu_cp15(vcpu, c5_IFSR);
	} else { /* !iabt */
		vcpu->arch.flags |= (KVM_ARM64_EXCEPT_AA32_DABT |
				     KVM_ARM64_PENDING_EXCEPTION);
		far = &vcpu_cp15(vcpu, c6_DFAR);
		fsr = &vcpu_cp15(vcpu, c5_DFSR);
	}

	*far = addr;

	/* Give the guest an IMPLEMENTATION DEFINED exception */
	is_lpae = (vcpu_cp15(vcpu, c2_TTBCR) >> 31);
	if (is_lpae) {
		*fsr = DFSR_LPAE | DFSR_FSC_EXTABT_LPAE;
	} else {
		/* no need to shuffle FS[4] into DFSR[10] as its 0 */
		*fsr = DFSR_FSC_EXTABT_nLPAE;
	}

	post_fault_synchronize(vcpu, loaded);
}

void kvm_inject_dabt32(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt32(vcpu, false, addr);
}

void kvm_inject_pabt32(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt32(vcpu, true, addr);
}

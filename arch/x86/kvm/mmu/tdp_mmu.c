// SPDX-License-Identifier: GPL-2.0

#include "tdp_mmu.h"

static bool __read_mostly tdp_mmu_enabled = false;

static bool is_tdp_mmu_enabled(void)
{
#ifdef CONFIG_X86_64
	return tdp_enabled && READ_ONCE(tdp_mmu_enabled);
#else
	return false;
#endif /* CONFIG_X86_64 */
}

/* Initializes the TDP MMU for the VM, if enabled. */
void kvm_mmu_init_tdp_mmu(struct kvm *kvm)
{
	if (!is_tdp_mmu_enabled())
		return;

	/* This should not be changed for the lifetime of the VM. */
	kvm->arch.tdp_mmu_enabled = true;
}

void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm)
{
	if (!kvm->arch.tdp_mmu_enabled)
		return;
}

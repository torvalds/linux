// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <asm/tlb.h>
#include <asm/kvm_csr.h>

/*
 * kvm_flush_tlb_all() - Flush all root TLB entries for guests.
 *
 * Invalidate all entries including GVA-->GPA and GPA-->HPA mappings.
 */
void kvm_flush_tlb_all(void)
{
	unsigned long flags;

	local_irq_save(flags);
	invtlb_all(INVTLB_ALLGID, 0, 0);
	local_irq_restore(flags);
}

void kvm_flush_tlb_gpa(struct kvm_vcpu *vcpu, unsigned long gpa)
{
	lockdep_assert_irqs_disabled();
	gpa &= (PAGE_MASK << 1);
	invtlb(INVTLB_GID_ADDR, read_csr_gstat() & CSR_GSTAT_GID, gpa);
}

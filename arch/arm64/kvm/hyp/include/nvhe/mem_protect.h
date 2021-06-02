/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#ifndef __KVM_NVHE_MEM_PROTECT__
#define __KVM_NVHE_MEM_PROTECT__
#include <linux/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_pgtable.h>
#include <asm/virt.h>
#include <nvhe/spinlock.h>

struct host_kvm {
	struct kvm_arch arch;
	struct kvm_pgtable pgt;
	struct kvm_pgtable_mm_ops mm_ops;
	hyp_spinlock_t lock;
};
extern struct host_kvm host_kvm;

int __pkvm_prot_finalize(void);
int __pkvm_mark_hyp(phys_addr_t start, phys_addr_t end);

int kvm_host_prepare_stage2(void *mem_pgt_pool, void *dev_pgt_pool);
void handle_host_mem_abort(struct kvm_cpu_context *host_ctxt);

static __always_inline void __load_host_stage2(void)
{
	if (static_branch_likely(&kvm_protected_mode_initialized))
		__load_stage2(&host_kvm.arch.mmu, host_kvm.arch.vtcr);
	else
		write_sysreg(0, vttbr_el2);
}
#endif /* __KVM_NVHE_MEM_PROTECT__ */

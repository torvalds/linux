/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, <yu.liu@freescale.com>
 *
 * Description:
 * This file is derived from arch/powerpc/include/asm/kvm_44x.h,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_KVM_E500_H__
#define __ASM_KVM_E500_H__

#include <linux/kvm_host.h>

#define BOOKE_INTERRUPT_SIZE 36

#define E500_PID_NUM   3
#define E500_TLB_NUM   2

#define E500_TLB_VALID 1
#define E500_TLB_DIRTY 2

struct tlbe_ref {
	pfn_t pfn;
	unsigned int flags; /* E500_TLB_* */
};

struct tlbe_priv {
	struct tlbe_ref ref; /* TLB0 only -- TLB1 uses tlb_refs */
};

struct vcpu_id_table;

struct kvmppc_e500_tlb_params {
	int entries, ways, sets;
};

struct kvmppc_vcpu_e500 {
	/* Unmodified copy of the guest's TLB -- shared with host userspace. */
	struct kvm_book3e_206_tlb_entry *gtlb_arch;

	/* Starting entry number in gtlb_arch[] */
	int gtlb_offset[E500_TLB_NUM];

	/* KVM internal information associated with each guest TLB entry */
	struct tlbe_priv *gtlb_priv[E500_TLB_NUM];

	struct kvmppc_e500_tlb_params gtlb_params[E500_TLB_NUM];

	unsigned int gtlb_nv[E500_TLB_NUM];

	/*
	 * information associated with each host TLB entry --
	 * TLB1 only for now.  If/when guest TLB1 entries can be
	 * mapped with host TLB0, this will be used for that too.
	 *
	 * We don't want to use this for guest TLB0 because then we'd
	 * have the overhead of doing the translation again even if
	 * the entry is still in the guest TLB (e.g. we swapped out
	 * and back, and our host TLB entries got evicted).
	 */
	struct tlbe_ref *tlb_refs[E500_TLB_NUM];
	unsigned int host_tlb1_nv;

	u32 host_pid[E500_PID_NUM];
	u32 pid[E500_PID_NUM];
	u32 svr;

	/* vcpu id table */
	struct vcpu_id_table *idt;

	u32 l1csr0;
	u32 l1csr1;
	u32 hid0;
	u32 hid1;
	u32 tlb0cfg;
	u32 tlb1cfg;
	u64 mcar;

	struct page **shared_tlb_pages;
	int num_shared_tlb_pages;

	struct kvm_vcpu vcpu;
};

static inline struct kvmppc_vcpu_e500 *to_e500(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_e500, vcpu);
}

#endif /* __ASM_KVM_E500_H__ */

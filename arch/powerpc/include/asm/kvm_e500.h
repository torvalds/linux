/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
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

struct tlbe{
	u32 mas1;
	u32 mas2;
	u32 mas3;
	u32 mas7;
};

struct kvmppc_vcpu_e500 {
	/* Unmodified copy of the guest's TLB. */
	struct tlbe *guest_tlb[E500_TLB_NUM];
	/* TLB that's actually used when the guest is running. */
	struct tlbe *shadow_tlb[E500_TLB_NUM];
	/* Pages which are referenced in the shadow TLB. */
	struct page **shadow_pages[E500_TLB_NUM];

	unsigned int guest_tlb_size[E500_TLB_NUM];
	unsigned int shadow_tlb_size[E500_TLB_NUM];
	unsigned int guest_tlb_nv[E500_TLB_NUM];

	u32 host_pid[E500_PID_NUM];
	u32 pid[E500_PID_NUM];

	u32 mas0;
	u32 mas1;
	u32 mas2;
	u32 mas3;
	u32 mas4;
	u32 mas5;
	u32 mas6;
	u32 mas7;
	u32 l1csr1;
	u32 hid0;
	u32 hid1;

	struct kvm_vcpu vcpu;
};

static inline struct kvmppc_vcpu_e500 *to_e500(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_e500, vcpu);
}

#endif /* __ASM_KVM_E500_H__ */

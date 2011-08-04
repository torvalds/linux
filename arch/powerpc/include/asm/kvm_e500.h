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

struct tlbe{
	u32 mas1;
	u32 mas2;
	u32 mas3;
	u32 mas7;
};

#define E500_TLB_VALID 1
#define E500_TLB_DIRTY 2

struct tlbe_priv {
	pfn_t pfn;
	unsigned int flags; /* E500_TLB_* */
};

struct vcpu_id_table;

struct kvmppc_vcpu_e500 {
	/* Unmodified copy of the guest's TLB. */
	struct tlbe *gtlb_arch[E500_TLB_NUM];

	/* KVM internal information associated with each guest TLB entry */
	struct tlbe_priv *gtlb_priv[E500_TLB_NUM];

	unsigned int gtlb_size[E500_TLB_NUM];
	unsigned int gtlb_nv[E500_TLB_NUM];

	u32 host_pid[E500_PID_NUM];
	u32 pid[E500_PID_NUM];
	u32 svr;

	u32 mas0;
	u32 mas1;
	u32 mas2;
	u32 mas3;
	u32 mas4;
	u32 mas5;
	u32 mas6;
	u32 mas7;

	/* vcpu id table */
	struct vcpu_id_table *idt;

	u32 l1csr0;
	u32 l1csr1;
	u32 hid0;
	u32 hid1;
	u32 tlb0cfg;
	u32 tlb1cfg;
	u64 mcar;

	struct kvm_vcpu vcpu;
};

static inline struct kvmppc_vcpu_e500 *to_e500(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_e500, vcpu);
}

#endif /* __ASM_KVM_E500_H__ */

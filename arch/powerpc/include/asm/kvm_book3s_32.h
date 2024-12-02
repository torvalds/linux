/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_32_H__
#define __ASM_KVM_BOOK3S_32_H__

static inline struct kvmppc_book3s_shadow_vcpu *svcpu_get(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.shadow_vcpu;
}

static inline void svcpu_put(struct kvmppc_book3s_shadow_vcpu *svcpu)
{
}

#define PTE_SIZE	12
#define VSID_ALL	0
#define SR_INVALID	0x00000001	/* VSID 1 should always be unused */
#define SR_KP		0x20000000
#define PTE_V		0x80000000
#define PTE_SEC		0x00000040
#define PTE_M		0x00000010
#define PTE_R		0x00000100
#define PTE_C		0x00000080

#define SID_SHIFT	28
#define ESID_MASK	0xf0000000
#define VSID_MASK	0x00fffffff0000000ULL
#define VPN_SHIFT	12

#endif /* __ASM_KVM_BOOK3S_32_H__ */

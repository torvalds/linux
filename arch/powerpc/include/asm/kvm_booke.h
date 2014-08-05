/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOKE_H__
#define __ASM_KVM_BOOKE_H__

#include <linux/types.h>
#include <linux/kvm_host.h>

/* LPIDs we support with this build -- runtime limit may be lower */
#define KVMPPC_NR_LPIDS                        64

#define KVMPPC_INST_EHPRIV		0x7c00021c
#define EHPRIV_OC_SHIFT			11
/* "ehpriv 1" : ehpriv with OC = 1 is used for debug emulation */
#define EHPRIV_OC_DEBUG			1
#define KVMPPC_INST_EHPRIV_DEBUG	(KVMPPC_INST_EHPRIV | \
					 (EHPRIV_OC_DEBUG << EHPRIV_OC_SHIFT))

static inline void kvmppc_set_gpr(struct kvm_vcpu *vcpu, int num, ulong val)
{
	vcpu->arch.gpr[num] = val;
}

static inline ulong kvmppc_get_gpr(struct kvm_vcpu *vcpu, int num)
{
	return vcpu->arch.gpr[num];
}

static inline void kvmppc_set_cr(struct kvm_vcpu *vcpu, u32 val)
{
	vcpu->arch.cr = val;
}

static inline u32 kvmppc_get_cr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr;
}

static inline void kvmppc_set_xer(struct kvm_vcpu *vcpu, u32 val)
{
	vcpu->arch.xer = val;
}

static inline u32 kvmppc_get_xer(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.xer;
}

static inline bool kvmppc_need_byteswap(struct kvm_vcpu *vcpu)
{
	/* XXX Would need to check TLB entry */
	return false;
}

static inline void kvmppc_set_ctr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.ctr = val;
}

static inline ulong kvmppc_get_ctr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.ctr;
}

static inline void kvmppc_set_lr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.lr = val;
}

static inline ulong kvmppc_get_lr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.lr;
}

static inline void kvmppc_set_pc(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.pc = val;
}

static inline ulong kvmppc_get_pc(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.pc;
}

static inline ulong kvmppc_get_fault_dar(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault_dear;
}

static inline bool kvmppc_supports_magic_page(struct kvm_vcpu *vcpu)
{
	/* Magic page is only supported on e500v2 */
#ifdef CONFIG_KVM_E500V2
	return true;
#else
	return false;
#endif
}
#endif /* __ASM_KVM_BOOKE_H__ */

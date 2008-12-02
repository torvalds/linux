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
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __KVM_POWERPC_TLB_H__
#define __KVM_POWERPC_TLB_H__

#include <linux/kvm_host.h>
#include <asm/mmu-44x.h>

extern int kvmppc_44x_tlb_index(struct kvm_vcpu *vcpu, gva_t eaddr,
                                unsigned int pid, unsigned int as);
extern int kvmppc_44x_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);
extern int kvmppc_44x_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);

extern int kvmppc_44x_emul_tlbsx(struct kvm_vcpu *vcpu, u8 rt, u8 ra, u8 rb,
                                 u8 rc);
extern int kvmppc_44x_emul_tlbwe(struct kvm_vcpu *vcpu, u8 ra, u8 rs, u8 ws);

/* TLB helper functions */
static inline unsigned int get_tlb_size(const struct kvmppc_44x_tlbe *tlbe)
{
	return (tlbe->word0 >> 4) & 0xf;
}

static inline gva_t get_tlb_eaddr(const struct kvmppc_44x_tlbe *tlbe)
{
	return tlbe->word0 & 0xfffffc00;
}

static inline gva_t get_tlb_bytes(const struct kvmppc_44x_tlbe *tlbe)
{
	unsigned int pgsize = get_tlb_size(tlbe);
	return 1 << 10 << (pgsize << 1);
}

static inline gva_t get_tlb_end(const struct kvmppc_44x_tlbe *tlbe)
{
	return get_tlb_eaddr(tlbe) + get_tlb_bytes(tlbe) - 1;
}

static inline u64 get_tlb_raddr(const struct kvmppc_44x_tlbe *tlbe)
{
	u64 word1 = tlbe->word1;
	return ((word1 & 0xf) << 32) | (word1 & 0xfffffc00);
}

static inline unsigned int get_tlb_tid(const struct kvmppc_44x_tlbe *tlbe)
{
	return tlbe->tid & 0xff;
}

static inline unsigned int get_tlb_ts(const struct kvmppc_44x_tlbe *tlbe)
{
	return (tlbe->word0 >> 8) & 0x1;
}

static inline unsigned int get_tlb_v(const struct kvmppc_44x_tlbe *tlbe)
{
	return (tlbe->word0 >> 9) & 0x1;
}

static inline unsigned int get_mmucr_stid(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.mmucr & 0xff;
}

static inline unsigned int get_mmucr_sts(const struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.mmucr >> 16) & 0x1;
}

static inline gpa_t tlb_xlate(struct kvmppc_44x_tlbe *tlbe, gva_t eaddr)
{
	unsigned int pgmask = get_tlb_bytes(tlbe) - 1;

	return get_tlb_raddr(tlbe) | (eaddr & pgmask);
}

#endif /* __KVM_POWERPC_TLB_H__ */

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
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __ASM_44X_H__
#define __ASM_44X_H__

#include <linux/kvm_host.h>

/* XXX Can't include mmu-44x.h because it redefines struct mm_context. */
#define PPC44x_TLB_SIZE 64

struct kvmppc_vcpu_44x {
	/* Unmodified copy of the guest's TLB. */
	struct kvmppc_44x_tlbe guest_tlb[PPC44x_TLB_SIZE];
	/* TLB that's actually used when the guest is running. */
	struct kvmppc_44x_tlbe shadow_tlb[PPC44x_TLB_SIZE];
	/* Pages which are referenced in the shadow TLB. */
	struct page *shadow_pages[PPC44x_TLB_SIZE];

	/* Track which TLB entries we've modified in the current exit. */
	u8 shadow_tlb_mod[PPC44x_TLB_SIZE];

	struct kvm_vcpu vcpu;
};

static inline struct kvmppc_vcpu_44x *to_44x(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_44x, vcpu);
}

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 new_pid);

#endif /* __ASM_44X_H__ */

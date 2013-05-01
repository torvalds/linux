/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
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
 */

#ifndef __ARM_KVM_MMU_H__
#define __ARM_KVM_MMU_H__

int create_hyp_mappings(void *from, void *to);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
void free_hyp_pmds(void);

int kvm_alloc_stage2_pgd(struct kvm *kvm);
void kvm_free_stage2_pgd(struct kvm *kvm);
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size);

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run);

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);

phys_addr_t kvm_mmu_get_httbr(void);
int kvm_mmu_init(void);
void kvm_clear_hyp_idmap(void);

static inline bool kvm_is_write_fault(unsigned long hsr)
{
	unsigned long hsr_ec = hsr >> HSR_EC_SHIFT;
	if (hsr_ec == HSR_EC_IABT)
		return false;
	else if ((hsr & HSR_ISV) && !(hsr & HSR_WNR))
		return false;
	else
		return true;
}

#endif /* __ARM_KVM_MMU_H__ */

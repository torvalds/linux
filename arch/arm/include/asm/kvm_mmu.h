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

#include <asm/cacheflush.h>
#include <asm/pgalloc.h>

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

static inline void kvm_set_pte(pte_t *pte, pte_t new_pte)
{
	pte_val(*pte) = new_pte;
	/*
	 * flush_pmd_entry just takes a void pointer and cleans the necessary
	 * cache entries, so we can reuse the function for ptes.
	 */
	flush_pmd_entry(pte);
}

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

static inline void kvm_clean_pgd(pgd_t *pgd)
{
	clean_dcache_area(pgd, PTRS_PER_S2_PGD * sizeof(pgd_t));
}

static inline void kvm_clean_pmd_entry(pmd_t *pmd)
{
	clean_pmd_entry(pmd);
}

static inline void kvm_clean_pte(pte_t *pte)
{
	clean_pte_table(pte);
}

static inline void kvm_set_s2pte_writable(pte_t *pte)
{
	pte_val(*pte) |= L_PTE_S2_RDWR;
}

struct kvm;

static inline void coherent_icache_guest_page(struct kvm *kvm, gfn_t gfn)
{
	/*
	 * If we are going to insert an instruction page and the icache is
	 * either VIPT or PIPT, there is a potential problem where the host
	 * (or another VM) may have used the same page as this guest, and we
	 * read incorrect data from the icache.  If we're using a PIPT cache,
	 * we can invalidate just that page, but if we are using a VIPT cache
	 * we need to invalidate the entire icache - damn shame - as written
	 * in the ARM ARM (DDI 0406C.b - Page B3-1393).
	 *
	 * VIVT caches are tagged using both the ASID and the VMID and doesn't
	 * need any kind of flushing (DDI 0406C.b - Page B3-1392).
	 */
	if (icache_is_pipt()) {
		unsigned long hva = gfn_to_hva(kvm, gfn);
		__cpuc_coherent_user_range(hva, hva + PAGE_SIZE);
	} else if (!icache_is_vivt_asid_tagged()) {
		/* any kind of VIPT cache */
		__flush_icache_all();
	}
}

#endif /* __ARM_KVM_MMU_H__ */

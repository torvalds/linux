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

#include <asm/memory.h>
#include <asm/page.h>

/*
 * We directly use the kernel VA for the HYP, as we can directly share
 * the mapping (HTTBR "covers" TTBR1).
 */
#define HYP_PAGE_OFFSET_MASK	UL(~0)
#define HYP_PAGE_OFFSET		PAGE_OFFSET
#define KERN_TO_HYP(kva)	(kva)

/*
 * Our virtual mapping for the boot-time MMU-enable code. Must be
 * shared across all the page-tables. Conveniently, we use the vectors
 * page, where no kernel data will ever be shared with HYP.
 */
#define TRAMPOLINE_VA		UL(CONFIG_VECTORS_BASE)

#ifndef __ASSEMBLY__

#include <asm/cacheflush.h>
#include <asm/pgalloc.h>

int create_hyp_mappings(void *from, void *to);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
void free_boot_hyp_pgd(void);
void free_hyp_pgds(void);

int kvm_alloc_stage2_pgd(struct kvm *kvm);
void kvm_free_stage2_pgd(struct kvm *kvm);
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable);

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run);

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);

phys_addr_t kvm_mmu_get_httbr(void);
phys_addr_t kvm_mmu_get_boot_httbr(void);
phys_addr_t kvm_get_idmap_vector(void);
int kvm_mmu_init(void);
void kvm_clear_hyp_idmap(void);

static inline void kvm_set_pmd(pmd_t *pmd, pmd_t new_pmd)
{
	*pmd = new_pmd;
	flush_pmd_entry(pmd);
}

static inline void kvm_set_pte(pte_t *pte, pte_t new_pte)
{
	*pte = new_pte;
	/*
	 * flush_pmd_entry just takes a void pointer and cleans the necessary
	 * cache entries, so we can reuse the function for ptes.
	 */
	flush_pmd_entry(pte);
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

static inline void kvm_set_s2pmd_writable(pmd_t *pmd)
{
	pmd_val(*pmd) |= L_PMD_S2_RDWR;
}

/* Open coded p*d_addr_end that can deal with 64bit addresses */
#define kvm_pgd_addr_end(addr, end)					\
({	u64 __boundary = ((addr) + PGDIR_SIZE) & PGDIR_MASK;		\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

#define kvm_pud_addr_end(addr,end)		(end)

#define kvm_pmd_addr_end(addr, end)					\
({	u64 __boundary = ((addr) + PMD_SIZE) & PMD_MASK;		\
	(__boundary - 1 < (end) - 1)? __boundary: (end);		\
})

static inline bool kvm_page_empty(void *ptr)
{
	struct page *ptr_page = virt_to_page(ptr);
	return page_count(ptr_page) == 1;
}


#define kvm_pte_table_empty(ptep) kvm_page_empty(ptep)
#define kvm_pmd_table_empty(pmdp) kvm_page_empty(pmdp)
#define kvm_pud_table_empty(pudp) (0)


struct kvm;

#define kvm_flush_dcache_to_poc(a,l)	__cpuc_flush_dcache_area((a), (l))

static inline bool vcpu_has_cache_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.cp15[c1_SCTLR] & 0b101) == 0b101;
}

static inline void coherent_cache_guest_page(struct kvm_vcpu *vcpu, hva_t hva,
					     unsigned long size)
{
	if (!vcpu_has_cache_enabled(vcpu))
		kvm_flush_dcache_to_poc((void *)hva, size);
	
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
		__cpuc_coherent_user_range(hva, hva + size);
	} else if (!icache_is_vivt_asid_tagged()) {
		/* any kind of VIPT cache */
		__flush_icache_all();
	}
}

#define kvm_virt_to_phys(x)		virt_to_idmap((unsigned long)(x))

void stage2_flush_vm(struct kvm *kvm);

#endif	/* !__ASSEMBLY__ */

#endif /* __ARM_KVM_MMU_H__ */

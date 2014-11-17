/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_KVM_MMU_H__
#define __ARM64_KVM_MMU_H__

#include <asm/page.h>
#include <asm/memory.h>

/*
 * As we only have the TTBR0_EL2 register, we cannot express
 * "negative" addresses. This makes it impossible to directly share
 * mappings with the kernel.
 *
 * Instead, give the HYP mode its own VA region at a fixed offset from
 * the kernel by just masking the top bits (which are all ones for a
 * kernel address).
 */
#define HYP_PAGE_OFFSET_SHIFT	VA_BITS
#define HYP_PAGE_OFFSET_MASK	((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET		(PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)

/*
 * Our virtual mapping for the idmap-ed MMU-enable code. Must be
 * shared across all the page-tables. Conveniently, we use the last
 * possible page, where no kernel mapping will ever exist.
 */
#define TRAMPOLINE_VA		(HYP_PAGE_OFFSET_MASK & PAGE_MASK)

/*
 * KVM_MMU_CACHE_MIN_PAGES is the number of stage2 page table translation
 * levels in addition to the PGD and potentially the PUD which are
 * pre-allocated (we pre-allocate the fake PGD and the PUD when the Stage-2
 * tables use one level of tables less than the kernel.
 */
#ifdef CONFIG_ARM64_64K_PAGES
#define KVM_MMU_CACHE_MIN_PAGES	1
#else
#define KVM_MMU_CACHE_MIN_PAGES	2
#endif

#ifdef __ASSEMBLY__

/*
 * Convert a kernel VA into a HYP VA.
 * reg: VA to be converted.
 */
.macro kern_hyp_va	reg
	and	\reg, \reg, #HYP_PAGE_OFFSET_MASK
.endm

#else

#include <asm/pgalloc.h>
#include <asm/cachetype.h>
#include <asm/cacheflush.h>

#define KERN_TO_HYP(kva)	((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)

/*
 * We currently only support a 40bit IPA.
 */
#define KVM_PHYS_SHIFT	(40)
#define KVM_PHYS_SIZE	(1UL << KVM_PHYS_SHIFT)
#define KVM_PHYS_MASK	(KVM_PHYS_SIZE - 1UL)

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

#define	kvm_set_pte(ptep, pte)		set_pte(ptep, pte)
#define	kvm_set_pmd(pmdp, pmd)		set_pmd(pmdp, pmd)

static inline void kvm_clean_pgd(pgd_t *pgd) {}
static inline void kvm_clean_pmd(pmd_t *pmd) {}
static inline void kvm_clean_pmd_entry(pmd_t *pmd) {}
static inline void kvm_clean_pte(pte_t *pte) {}
static inline void kvm_clean_pte_entry(pte_t *pte) {}

static inline void kvm_set_s2pte_writable(pte_t *pte)
{
	pte_val(*pte) |= PTE_S2_RDWR;
}

static inline void kvm_set_s2pmd_writable(pmd_t *pmd)
{
	pmd_val(*pmd) |= PMD_S2_RDWR;
}

#define kvm_pgd_addr_end(addr, end)	pgd_addr_end(addr, end)
#define kvm_pud_addr_end(addr, end)	pud_addr_end(addr, end)
#define kvm_pmd_addr_end(addr, end)	pmd_addr_end(addr, end)

/*
 * In the case where PGDIR_SHIFT is larger than KVM_PHYS_SHIFT, we can address
 * the entire IPA input range with a single pgd entry, and we would only need
 * one pgd entry.  Note that in this case, the pgd is actually not used by
 * the MMU for Stage-2 translations, but is merely a fake pgd used as a data
 * structure for the kernel pgtable macros to work.
 */
#if PGDIR_SHIFT > KVM_PHYS_SHIFT
#define PTRS_PER_S2_PGD_SHIFT	0
#else
#define PTRS_PER_S2_PGD_SHIFT	(KVM_PHYS_SHIFT - PGDIR_SHIFT)
#endif
#define PTRS_PER_S2_PGD		(1 << PTRS_PER_S2_PGD_SHIFT)
#define S2_PGD_ORDER		get_order(PTRS_PER_S2_PGD * sizeof(pgd_t))

/*
 * If we are concatenating first level stage-2 page tables, we would have less
 * than or equal to 16 pointers in the fake PGD, because that's what the
 * architecture allows.  In this case, (4 - CONFIG_ARM64_PGTABLE_LEVELS)
 * represents the first level for the host, and we add 1 to go to the next
 * level (which uses contatenation) for the stage-2 tables.
 */
#if PTRS_PER_S2_PGD <= 16
#define KVM_PREALLOC_LEVEL	(4 - CONFIG_ARM64_PGTABLE_LEVELS + 1)
#else
#define KVM_PREALLOC_LEVEL	(0)
#endif

/**
 * kvm_prealloc_hwpgd - allocate inital table for VTTBR
 * @kvm:	The KVM struct pointer for the VM.
 * @pgd:	The kernel pseudo pgd
 *
 * When the kernel uses more levels of page tables than the guest, we allocate
 * a fake PGD and pre-populate it to point to the next-level page table, which
 * will be the real initial page table pointed to by the VTTBR.
 *
 * When KVM_PREALLOC_LEVEL==2, we allocate a single page for the PMD and
 * the kernel will use folded pud.  When KVM_PREALLOC_LEVEL==1, we
 * allocate 2 consecutive PUD pages.
 */
static inline int kvm_prealloc_hwpgd(struct kvm *kvm, pgd_t *pgd)
{
	unsigned int i;
	unsigned long hwpgd;

	if (KVM_PREALLOC_LEVEL == 0)
		return 0;

	hwpgd = __get_free_pages(GFP_KERNEL | __GFP_ZERO, PTRS_PER_S2_PGD_SHIFT);
	if (!hwpgd)
		return -ENOMEM;

	for (i = 0; i < PTRS_PER_S2_PGD; i++) {
		if (KVM_PREALLOC_LEVEL == 1)
			pgd_populate(NULL, pgd + i,
				     (pud_t *)hwpgd + i * PTRS_PER_PUD);
		else if (KVM_PREALLOC_LEVEL == 2)
			pud_populate(NULL, pud_offset(pgd, 0) + i,
				     (pmd_t *)hwpgd + i * PTRS_PER_PMD);
	}

	return 0;
}

static inline void *kvm_get_hwpgd(struct kvm *kvm)
{
	pgd_t *pgd = kvm->arch.pgd;
	pud_t *pud;

	if (KVM_PREALLOC_LEVEL == 0)
		return pgd;

	pud = pud_offset(pgd, 0);
	if (KVM_PREALLOC_LEVEL == 1)
		return pud;

	BUG_ON(KVM_PREALLOC_LEVEL != 2);
	return pmd_offset(pud, 0);
}

static inline void kvm_free_hwpgd(struct kvm *kvm)
{
	if (KVM_PREALLOC_LEVEL > 0) {
		unsigned long hwpgd = (unsigned long)kvm_get_hwpgd(kvm);
		free_pages(hwpgd, PTRS_PER_S2_PGD_SHIFT);
	}
}

static inline bool kvm_page_empty(void *ptr)
{
	struct page *ptr_page = virt_to_page(ptr);
	return page_count(ptr_page) == 1;
}

#define kvm_pte_table_empty(kvm, ptep) kvm_page_empty(ptep)

#ifdef __PAGETABLE_PMD_FOLDED
#define kvm_pmd_table_empty(kvm, pmdp) (0)
#else
#define kvm_pmd_table_empty(kvm, pmdp) \
	(kvm_page_empty(pmdp) && (!(kvm) || KVM_PREALLOC_LEVEL < 2))
#endif

#ifdef __PAGETABLE_PUD_FOLDED
#define kvm_pud_table_empty(kvm, pudp) (0)
#else
#define kvm_pud_table_empty(kvm, pudp) \
	(kvm_page_empty(pudp) && (!(kvm) || KVM_PREALLOC_LEVEL < 1))
#endif


struct kvm;

#define kvm_flush_dcache_to_poc(a,l)	__flush_dcache_area((a), (l))

static inline bool vcpu_has_cache_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu_sys_reg(vcpu, SCTLR_EL1) & 0b101) == 0b101;
}

static inline void coherent_cache_guest_page(struct kvm_vcpu *vcpu, hva_t hva,
					     unsigned long size,
					     bool ipa_uncached)
{
	if (!vcpu_has_cache_enabled(vcpu) || ipa_uncached)
		kvm_flush_dcache_to_poc((void *)hva, size);

	if (!icache_is_aliasing()) {		/* PIPT */
		flush_icache_range(hva, hva + size);
	} else if (!icache_is_aivivt()) {	/* non ASID-tagged VIVT */
		/* any kind of VIPT cache */
		__flush_icache_all();
	}
}

#define kvm_virt_to_phys(x)		__virt_to_phys((unsigned long)(x))

void stage2_flush_vm(struct kvm *kvm);

#endif /* __ASSEMBLY__ */
#endif /* __ARM64_KVM_MMU_H__ */

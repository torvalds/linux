/*
 * Copyright (C) 2016 - ARM Ltd
 *
 * stage2 page table helpers
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

#ifndef __ARM64_S2_PGTABLE_H_
#define __ARM64_S2_PGTABLE_H_

#include <linux/hugetlb.h>
#include <asm/pgtable.h>

/*
 * PGDIR_SHIFT determines the size a top-level page table entry can map
 * and depends on the number of levels in the page table. Compute the
 * PGDIR_SHIFT for a given number of levels.
 */
#define pt_levels_pgdir_shift(lvls)	ARM64_HW_PGTABLE_LEVEL_SHIFT(4 - (lvls))

/*
 * The hardware supports concatenation of up to 16 tables at stage2 entry
 * level and we use the feature whenever possible, which means we resolve 4
 * additional bits of address at the entry level.
 *
 * This implies, the total number of page table levels required for
 * IPA_SHIFT at stage2 expected by the hardware can be calculated using
 * the same logic used for the (non-collapsable) stage1 page tables but for
 * (IPA_SHIFT - 4).
 */
#define stage2_pgtable_levels(ipa)	ARM64_HW_PGTABLE_LEVELS((ipa) - 4)
#define kvm_stage2_levels(kvm)		VTCR_EL2_LVLS(kvm->arch.vtcr)

/* stage2_pgdir_shift() is the size mapped by top-level stage2 entry for the VM */
#define stage2_pgdir_shift(kvm)		pt_levels_pgdir_shift(kvm_stage2_levels(kvm))
#define stage2_pgdir_size(kvm)		(1ULL << stage2_pgdir_shift(kvm))
#define stage2_pgdir_mask(kvm)		~(stage2_pgdir_size(kvm) - 1)

/*
 * The number of PTRS across all concatenated stage2 tables given by the
 * number of bits resolved at the initial level.
 * If we force more levels than necessary, we may have (stage2_pgdir_shift > IPA),
 * in which case, stage2_pgd_ptrs will have one entry.
 */
#define pgd_ptrs_shift(ipa, pgdir_shift)	\
	((ipa) > (pgdir_shift) ? ((ipa) - (pgdir_shift)) : 0)
#define __s2_pgd_ptrs(ipa, lvls)		\
	(1 << (pgd_ptrs_shift((ipa), pt_levels_pgdir_shift(lvls))))
#define __s2_pgd_size(ipa, lvls)	(__s2_pgd_ptrs((ipa), (lvls)) * sizeof(pgd_t))

#define stage2_pgd_ptrs(kvm)		__s2_pgd_ptrs(kvm_phys_shift(kvm), kvm_stage2_levels(kvm))
#define stage2_pgd_size(kvm)		__s2_pgd_size(kvm_phys_shift(kvm), kvm_stage2_levels(kvm))

/*
 * kvm_mmmu_cache_min_pages() is the number of pages required to install
 * a stage-2 translation. We pre-allocate the entry level page table at
 * the VM creation.
 */
#define kvm_mmu_cache_min_pages(kvm)	(kvm_stage2_levels(kvm) - 1)

/* Stage2 PUD definitions when the level is present */
static inline bool kvm_stage2_has_pud(struct kvm *kvm)
{
	return (CONFIG_PGTABLE_LEVELS > 3) && (kvm_stage2_levels(kvm) > 3);
}

#define S2_PUD_SHIFT			ARM64_HW_PGTABLE_LEVEL_SHIFT(1)
#define S2_PUD_SIZE			(1UL << S2_PUD_SHIFT)
#define S2_PUD_MASK			(~(S2_PUD_SIZE - 1))

static inline bool stage2_pgd_none(struct kvm *kvm, pgd_t pgd)
{
	if (kvm_stage2_has_pud(kvm))
		return pgd_none(pgd);
	else
		return 0;
}

static inline void stage2_pgd_clear(struct kvm *kvm, pgd_t *pgdp)
{
	if (kvm_stage2_has_pud(kvm))
		pgd_clear(pgdp);
}

static inline bool stage2_pgd_present(struct kvm *kvm, pgd_t pgd)
{
	if (kvm_stage2_has_pud(kvm))
		return pgd_present(pgd);
	else
		return 1;
}

static inline void stage2_pgd_populate(struct kvm *kvm, pgd_t *pgd, pud_t *pud)
{
	if (kvm_stage2_has_pud(kvm))
		pgd_populate(NULL, pgd, pud);
}

static inline pud_t *stage2_pud_offset(struct kvm *kvm,
				       pgd_t *pgd, unsigned long address)
{
	if (kvm_stage2_has_pud(kvm))
		return pud_offset(pgd, address);
	else
		return (pud_t *)pgd;
}

static inline void stage2_pud_free(struct kvm *kvm, pud_t *pud)
{
	if (kvm_stage2_has_pud(kvm))
		pud_free(NULL, pud);
}

static inline bool stage2_pud_table_empty(struct kvm *kvm, pud_t *pudp)
{
	if (kvm_stage2_has_pud(kvm))
		return kvm_page_empty(pudp);
	else
		return false;
}

static inline phys_addr_t
stage2_pud_addr_end(struct kvm *kvm, phys_addr_t addr, phys_addr_t end)
{
	if (kvm_stage2_has_pud(kvm)) {
		phys_addr_t boundary = (addr + S2_PUD_SIZE) & S2_PUD_MASK;

		return (boundary - 1 < end - 1) ? boundary : end;
	} else {
		return end;
	}
}

/* Stage2 PMD definitions when the level is present */
static inline bool kvm_stage2_has_pmd(struct kvm *kvm)
{
	return (CONFIG_PGTABLE_LEVELS > 2) && (kvm_stage2_levels(kvm) > 2);
}

#define S2_PMD_SHIFT			ARM64_HW_PGTABLE_LEVEL_SHIFT(2)
#define S2_PMD_SIZE			(1UL << S2_PMD_SHIFT)
#define S2_PMD_MASK			(~(S2_PMD_SIZE - 1))

static inline bool stage2_pud_none(struct kvm *kvm, pud_t pud)
{
	if (kvm_stage2_has_pmd(kvm))
		return pud_none(pud);
	else
		return 0;
}

static inline void stage2_pud_clear(struct kvm *kvm, pud_t *pud)
{
	if (kvm_stage2_has_pmd(kvm))
		pud_clear(pud);
}

static inline bool stage2_pud_present(struct kvm *kvm, pud_t pud)
{
	if (kvm_stage2_has_pmd(kvm))
		return pud_present(pud);
	else
		return 1;
}

static inline void stage2_pud_populate(struct kvm *kvm, pud_t *pud, pmd_t *pmd)
{
	if (kvm_stage2_has_pmd(kvm))
		pud_populate(NULL, pud, pmd);
}

static inline pmd_t *stage2_pmd_offset(struct kvm *kvm,
				       pud_t *pud, unsigned long address)
{
	if (kvm_stage2_has_pmd(kvm))
		return pmd_offset(pud, address);
	else
		return (pmd_t *)pud;
}

static inline void stage2_pmd_free(struct kvm *kvm, pmd_t *pmd)
{
	if (kvm_stage2_has_pmd(kvm))
		pmd_free(NULL, pmd);
}

static inline bool stage2_pud_huge(struct kvm *kvm, pud_t pud)
{
	if (kvm_stage2_has_pmd(kvm))
		return pud_huge(pud);
	else
		return 0;
}

static inline bool stage2_pmd_table_empty(struct kvm *kvm, pmd_t *pmdp)
{
	if (kvm_stage2_has_pmd(kvm))
		return kvm_page_empty(pmdp);
	else
		return 0;
}

static inline phys_addr_t
stage2_pmd_addr_end(struct kvm *kvm, phys_addr_t addr, phys_addr_t end)
{
	if (kvm_stage2_has_pmd(kvm)) {
		phys_addr_t boundary = (addr + S2_PMD_SIZE) & S2_PMD_MASK;

		return (boundary - 1 < end - 1) ? boundary : end;
	} else {
		return end;
	}
}

static inline bool stage2_pte_table_empty(struct kvm *kvm, pte_t *ptep)
{
	return kvm_page_empty(ptep);
}

static inline unsigned long stage2_pgd_index(struct kvm *kvm, phys_addr_t addr)
{
	return (((addr) >> stage2_pgdir_shift(kvm)) & (stage2_pgd_ptrs(kvm) - 1));
}

static inline phys_addr_t
stage2_pgd_addr_end(struct kvm *kvm, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t boundary = (addr + stage2_pgdir_size(kvm)) & stage2_pgdir_mask(kvm);

	return (boundary - 1 < end - 1) ? boundary : end;
}

#endif	/* __ARM64_S2_PGTABLE_H_ */

#ifndef _ASM_ARM64_VMALLOC_H
#define _ASM_ARM64_VMALLOC_H

#include <asm/page.h>
#include <asm/pgtable.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return pud_sect_supported();
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return true;
}

#define arch_vmap_pte_range_map_size arch_vmap_pte_range_map_size
static inline unsigned long arch_vmap_pte_range_map_size(unsigned long addr,
						unsigned long end, u64 pfn,
						unsigned int max_page_shift)
{
	/*
	 * If the block is at least CONT_PTE_SIZE in size, and is naturally
	 * aligned in both virtual and physical space, then we can pte-map the
	 * block using the PTE_CONT bit for more efficient use of the TLB.
	 */
	if (max_page_shift < CONT_PTE_SHIFT)
		return PAGE_SIZE;

	if (end - addr < CONT_PTE_SIZE)
		return PAGE_SIZE;

	if (!IS_ALIGNED(addr, CONT_PTE_SIZE))
		return PAGE_SIZE;

	if (!IS_ALIGNED(PFN_PHYS(pfn), CONT_PTE_SIZE))
		return PAGE_SIZE;

	return CONT_PTE_SIZE;
}

#define arch_vmap_pte_range_unmap_size arch_vmap_pte_range_unmap_size
static inline unsigned long arch_vmap_pte_range_unmap_size(unsigned long addr,
							   pte_t *ptep)
{
	/*
	 * The caller handles alignment so it's sufficient just to check
	 * PTE_CONT.
	 */
	return pte_valid_cont(__ptep_get(ptep)) ? CONT_PTE_SIZE : PAGE_SIZE;
}

#define arch_vmap_pte_supported_shift arch_vmap_pte_supported_shift
static inline int arch_vmap_pte_supported_shift(unsigned long size)
{
	if (size >= CONT_PTE_SIZE)
		return CONT_PTE_SHIFT;

	return PAGE_SHIFT;
}

#endif

#define arch_vmap_pgprot_tagged arch_vmap_pgprot_tagged
static inline pgprot_t arch_vmap_pgprot_tagged(pgprot_t prot)
{
	return pgprot_tagged(prot);
}

#endif /* _ASM_ARM64_VMALLOC_H */

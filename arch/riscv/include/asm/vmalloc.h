#ifndef _ASM_RISCV_VMALLOC_H
#define _ASM_RISCV_VMALLOC_H

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define IOREMAP_MAX_ORDER (PUD_SHIFT)

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return true;
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return true;
}

#ifdef CONFIG_RISCV_ISA_SVNAPOT
#include <linux/pgtable.h>

#define arch_vmap_pte_range_map_size arch_vmap_pte_range_map_size
static inline unsigned long arch_vmap_pte_range_map_size(unsigned long addr, unsigned long end,
							 u64 pfn, unsigned int max_page_shift)
{
	unsigned long map_size = PAGE_SIZE;
	unsigned long size, order;

	if (!has_svnapot())
		return map_size;

	for_each_napot_order_rev(order) {
		if (napot_cont_shift(order) > max_page_shift)
			continue;

		size = napot_cont_size(order);
		if (end - addr < size)
			continue;

		if (!IS_ALIGNED(addr, size))
			continue;

		if (!IS_ALIGNED(PFN_PHYS(pfn), size))
			continue;

		map_size = size;
		break;
	}

	return map_size;
}

#define arch_vmap_pte_supported_shift arch_vmap_pte_supported_shift
static inline int arch_vmap_pte_supported_shift(unsigned long size)
{
	int shift = PAGE_SHIFT;
	unsigned long order;

	if (!has_svnapot())
		return shift;

	WARN_ON_ONCE(size >= PMD_SIZE);

	for_each_napot_order_rev(order) {
		if (napot_cont_size(order) > size)
			continue;

		if (!IS_ALIGNED(size, napot_cont_size(order)))
			continue;

		shift = napot_cont_shift(order);
		break;
	}

	return shift;
}

#endif /* CONFIG_RISCV_ISA_SVNAPOT */
#endif /* CONFIG_HAVE_ARCH_HUGE_VMAP */
#endif /* _ASM_RISCV_VMALLOC_H */

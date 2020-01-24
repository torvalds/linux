#ifndef _ASM_POWERPC_PTE_WALK_H
#define _ASM_POWERPC_PTE_WALK_H

#include <linux/sched.h>

/* Don't use this directly */
extern pte_t *__find_linux_pte(pgd_t *pgdir, unsigned long ea,
			       bool *is_thp, unsigned *hshift);

static inline pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea,
				    bool *is_thp, unsigned *hshift)
{
	pte_t *pte;

	VM_WARN(!arch_irqs_disabled(), "%s called with irq enabled\n", __func__);
	pte = __find_linux_pte(pgdir, ea, is_thp, hshift);

#if defined(CONFIG_DEBUG_VM) &&						\
	!(defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE))
	/*
	 * We should not find huge page if these configs are not enabled.
	 */
	if (hshift)
		WARN_ON(*hshift);
#endif
	return pte;
}

static inline pte_t *find_init_mm_pte(unsigned long ea, unsigned *hshift)
{
	pgd_t *pgdir = init_mm.pgd;
	return __find_linux_pte(pgdir, ea, NULL, hshift);
}
/*
 * This is what we should always use. Any other lockless page table lookup needs
 * careful audit against THP split.
 */
static inline pte_t *find_current_mm_pte(pgd_t *pgdir, unsigned long ea,
					 bool *is_thp, unsigned *hshift)
{
	pte_t *pte;

	VM_WARN(!arch_irqs_disabled(), "%s called with irq enabled\n", __func__);
	VM_WARN(pgdir != current->mm->pgd,
		"%s lock less page table lookup called on wrong mm\n", __func__);
	pte = __find_linux_pte(pgdir, ea, is_thp, hshift);

#if defined(CONFIG_DEBUG_VM) &&						\
	!(defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE))
	/*
	 * We should not find huge page if these configs are not enabled.
	 */
	if (hshift)
		WARN_ON(*hshift);
#endif
	return pte;
}

#endif /* _ASM_POWERPC_PTE_WALK_H */

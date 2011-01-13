#include <linux/mm.h>
#include <linux/slab.h>

#define PGALLOC_GFP GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO

static struct kmem_cache *pgd_cachep;
#if PAGETABLE_LEVELS > 2
static struct kmem_cache *pmd_cachep;
#endif

void pgd_ctor(void *x)
{
	pgd_t *pgd = x;

	memcpy(pgd + USER_PTRS_PER_PGD,
	       swapper_pg_dir + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}

void pgtable_cache_init(void)
{
	pgd_cachep = kmem_cache_create("pgd_cache",
				       PTRS_PER_PGD * (1<<PTE_MAGNITUDE),
				       PAGE_SIZE, SLAB_PANIC, pgd_ctor);
#if PAGETABLE_LEVELS > 2
	pmd_cachep = kmem_cache_create("pmd_cache",
				       PTRS_PER_PMD * (1<<PTE_MAGNITUDE),
				       PAGE_SIZE, SLAB_PANIC, NULL);
#endif
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgd_cachep, PGALLOC_GFP);
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(pgd_cachep, pgd);
}

#if PAGETABLE_LEVELS > 2
void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((unsigned long)pmd));
}

pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return kmem_cache_alloc(pmd_cachep, PGALLOC_GFP);
}

void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(pmd_cachep, pmd);
}
#endif /* PAGETABLE_LEVELS > 2 */

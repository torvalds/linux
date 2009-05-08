/*
 * xtensa mmu stuff
 *
 * Extracted from init.c
 */
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/cache.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/page.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

void __init paging_init(void)
{
	memset(swapper_pg_dir, 0, PAGE_SIZE);
}

/*
 * Flush the mmu and reset associated register to default values.
 */
void __init init_mmu(void)
{
	/* Writing zeros to the <t>TLBCFG special registers ensure
	 * that valid values exist in the register.  For existing
	 * PGSZID<w> fields, zero selects the first element of the
	 * page-size array.  For nonexistent PGSZID<w> fields, zero is
	 * the best value to write.  Also, when changing PGSZID<w>
	 * fields, the corresponding TLB must be flushed.
	 */
	set_itlbcfg_register(0);
	set_dtlbcfg_register(0);
	flush_tlb_all();

	/* Set rasid register to a known value. */

	set_rasid_register(ASID_USER_FIRST);

	/* Set PTEVADDR special register to the start of the page
	 * table, which is in kernel mappable space (ie. not
	 * statically mapped).  This register's value is undefined on
	 * reset.
	 */
	set_ptevaddr_register(PGTABLE_START);
}

struct kmem_cache *pgtable_cache __read_mostly;

static void pgd_ctor(void *addr)
{
	pte_t *ptep = (pte_t *)addr;
	int i;

	for (i = 0; i < 1024; i++, ptep++)
		pte_clear(NULL, 0, ptep);

}

void __init pgtable_cache_init(void)
{
	pgtable_cache = kmem_cache_create("pgd",
			PAGE_SIZE, PAGE_SIZE,
			SLAB_HWCACHE_ALIGN,
			pgd_ctor);
}

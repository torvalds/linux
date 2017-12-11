// SPDX-License-Identifier: GPL-2.0
/*
 * xtensa mmu stuff
 *
 * Extracted from init.c
 */
#include <linux/bootmem.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/cache.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/initialize_mmu.h>
#include <asm/io.h>

#if defined(CONFIG_HIGHMEM)
static void * __init init_pmd(unsigned long vaddr, unsigned long n_pages)
{
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);
	pte_t *pte;
	unsigned long i;

	n_pages = ALIGN(n_pages, PTRS_PER_PTE);

	pr_debug("%s: vaddr: 0x%08lx, n_pages: %ld\n",
		 __func__, vaddr, n_pages);

	pte = alloc_bootmem_low_pages(n_pages * sizeof(pte_t));

	for (i = 0; i < n_pages; ++i)
		pte_clear(NULL, 0, pte + i);

	for (i = 0; i < n_pages; i += PTRS_PER_PTE, ++pmd) {
		pte_t *cur_pte = pte + i;

		BUG_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd(((unsigned long)cur_pte) & PAGE_MASK));
		BUG_ON(cur_pte != pte_offset_kernel(pmd, 0));
		pr_debug("%s: pmd: 0x%p, pte: 0x%p\n",
			 __func__, pmd, cur_pte);
	}
	return pte;
}

static void __init fixedrange_init(void)
{
	init_pmd(__fix_to_virt(0), __end_of_fixed_addresses);
}
#endif

void __init paging_init(void)
{
	memset(swapper_pg_dir, 0, PAGE_SIZE);
#ifdef CONFIG_HIGHMEM
	fixedrange_init();
	pkmap_page_table = init_pmd(PKMAP_BASE, LAST_PKMAP);
	kmap_init();
#endif
}

/*
 * Flush the mmu and reset associated register to default values.
 */
void init_mmu(void)
{
#if !(XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY)
	/*
	 * Writing zeros to the instruction and data TLBCFG special
	 * registers ensure that valid values exist in the register.
	 *
	 * For existing PGSZID<w> fields, zero selects the first element
	 * of the page-size array.  For nonexistent PGSZID<w> fields,
	 * zero is the best value to write.  Also, when changing PGSZID<w>
	 * fields, the corresponding TLB must be flushed.
	 */
	set_itlbcfg_register(0);
	set_dtlbcfg_register(0);
#endif
#if XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY && defined(CONFIG_OF)
	/*
	 * Update the IO area mapping in case xtensa_kio_paddr has changed
	 */
	write_dtlb_entry(__pte(xtensa_kio_paddr + CA_WRITEBACK),
			XCHAL_KIO_CACHED_VADDR + 6);
	write_itlb_entry(__pte(xtensa_kio_paddr + CA_WRITEBACK),
			XCHAL_KIO_CACHED_VADDR + 6);
	write_dtlb_entry(__pte(xtensa_kio_paddr + CA_BYPASS),
			XCHAL_KIO_BYPASS_VADDR + 6);
	write_itlb_entry(__pte(xtensa_kio_paddr + CA_BYPASS),
			XCHAL_KIO_BYPASS_VADDR + 6);
#endif

	local_flush_tlb_all();

	/* Set rasid register to a known value. */

	set_rasid_register(ASID_INSERT(ASID_USER_FIRST));

	/* Set PTEVADDR special register to the start of the page
	 * table, which is in kernel mappable space (ie. not
	 * statically mapped).  This register's value is undefined on
	 * reset.
	 */
	set_ptevaddr_register(PGTABLE_START);
}

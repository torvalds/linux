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
static void * __init init_pmd(unsigned long vaddr)
{
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);

	if (pmd_none(*pmd)) {
		unsigned i;
		pte_t *pte = alloc_bootmem_low_pages(PAGE_SIZE);

		for (i = 0; i < 1024; i++)
			pte_clear(NULL, 0, pte + i);

		set_pmd(pmd, __pmd(((unsigned long)pte) & PAGE_MASK));
		BUG_ON(pte != pte_offset_kernel(pmd, 0));
		pr_debug("%s: vaddr: 0x%08lx, pmd: 0x%p, pte: 0x%p\n",
			 __func__, vaddr, pmd, pte);
		return pte;
	} else {
		return pte_offset_kernel(pmd, 0);
	}
}

static void __init fixedrange_init(void)
{
	BUILD_BUG_ON(FIXADDR_SIZE > PMD_SIZE);
	init_pmd(__fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK);
}
#endif

void __init paging_init(void)
{
	memset(swapper_pg_dir, 0, PAGE_SIZE);
#ifdef CONFIG_HIGHMEM
	fixedrange_init();
	pkmap_page_table = init_pmd(PKMAP_BASE);
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

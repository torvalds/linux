#include <linux/vmalloc.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>

/* called with the page_table_lock held */
static inline void 
remap_area_pte(pte_t * pte, unsigned long address, unsigned long size, 
	       unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;
	unsigned long pfn;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	if (address >= end)
		BUG();
	pfn = phys_addr >> PAGE_SHIFT;
	do {
		if (!pte_none(*pte)) {
			printk("remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, pfn_pte(pfn, 
				     __pgprot(_PAGE_VALID | _PAGE_ASM | 
				              _PAGE_KRE | _PAGE_KWE | flags)));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

/* called with the page_table_lock held */
static inline int 
remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size, 
	       unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	if (address >= end)
		BUG();
	do {
		pte_t * pte = pte_alloc_kernel(&init_mm, pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, 
				     address + phys_addr, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

int
__alpha_remap_area_pages(unsigned long address, unsigned long phys_addr,
			 unsigned long size, unsigned long flags)
{
	pgd_t * dir;
	int error = 0;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	if (address >= end)
		BUG();
	spin_lock(&init_mm.page_table_lock);
	do {
		pmd_t *pmd;
		pmd = pmd_alloc(&init_mm, dir, address);
		error = -ENOMEM;
		if (!pmd)
			break;
		if (remap_area_pmd(pmd, address, end - address,
			           phys_addr + address, flags))
			break;
		error = 0;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	spin_unlock(&init_mm.page_table_lock);
	return error;
}


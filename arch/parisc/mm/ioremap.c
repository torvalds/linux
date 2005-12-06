/*
 * arch/parisc/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001 Helge Deller <deller@gmx.de>
 */

#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/pgalloc.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	if (address >= end)
		BUG();
	do {
		if (!pte_none(*pte)) {
			printk(KERN_ERR "remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, mk_pte_phys(phys_addr, __pgprot(_PAGE_PRESENT | _PAGE_RW | 
					_PAGE_DIRTY | _PAGE_ACCESSED | flags)));
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
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
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

#if (USE_HPPA_IOREMAP)
static int remap_area_pages(unsigned long address, unsigned long phys_addr,
				 unsigned long size, unsigned long flags)
{
	int error;
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	if (address >= end)
		BUG();
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
	flush_tlb_all();
	return error;
}
#endif /* USE_HPPA_IOREMAP */

#ifdef CONFIG_DEBUG_IOREMAP
static unsigned long last = 0;

void gsc_bad_addr(unsigned long addr)
{
	if (time_after(jiffies, last + HZ*10)) {
		printk("gsc_foo() called with bad address 0x%lx\n", addr);
		dump_stack();
		last = jiffies;
	}
}
EXPORT_SYMBOL(gsc_bad_addr);

void __raw_bad_addr(const volatile void __iomem *addr)
{
	if (time_after(jiffies, last + HZ*10)) {
		printk("__raw_foo() called with bad address 0x%p\n", addr);
		dump_stack();
		last = jiffies;
	}
}
EXPORT_SYMBOL(__raw_bad_addr);
#endif

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
#if !(USE_HPPA_IOREMAP)

	unsigned long end = phys_addr + size - 1;
	/* Support EISA addresses */
	if ((phys_addr >= 0x00080000 && end < 0x000fffff)
			|| (phys_addr >= 0x00500000 && end < 0x03bfffff)) {
		phys_addr |= 0xfc000000;
	}

#ifdef CONFIG_DEBUG_IOREMAP
	return (void __iomem *)(phys_addr - (0x1UL << NYBBLE_SHIFT));
#else
	return (void __iomem *)phys_addr;
#endif

#else
	void * addr;
	struct vm_struct * area;
	unsigned long offset, last_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);
	   
		for(page = virt_to_page(t_addr); page <= virt_to_page(t_end); page++)
			if(!PageReserved(page))
				return NULL;
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, flags)) {
		vfree(addr);
		return NULL;
	}
	return (void __iomem *) (offset + (char *)addr);
#endif
}

void iounmap(void __iomem *addr)
{
#if !(USE_HPPA_IOREMAP)
	return;
#else
	if (addr > high_memory)
		return vfree((void *) (PAGE_MASK & (unsigned long __force) addr));
#endif
}

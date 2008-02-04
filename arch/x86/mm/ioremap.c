/*
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>

enum ioremap_mode {
	IOR_MODE_UNCACHED,
	IOR_MODE_CACHED,
};

#ifdef CONFIG_X86_64

unsigned long __phys_addr(unsigned long x)
{
	if (x >= __START_KERNEL_map)
		return x - __START_KERNEL_map + phys_base;
	return x - PAGE_OFFSET;
}
EXPORT_SYMBOL(__phys_addr);

#endif

int page_is_ram(unsigned long pagenr)
{
	unsigned long addr, end;
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		/*
		 * Not usable memory:
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		addr = (e820.map[i].addr + PAGE_SIZE-1) >> PAGE_SHIFT;
		end = (e820.map[i].addr + e820.map[i].size) >> PAGE_SHIFT;

		/*
		 * Sanity check: Some BIOSen report areas as RAM that
		 * are not. Notably the 640->1Mb area, which is the
		 * PCI BIOS area.
		 */
		if (addr >= (BIOS_BEGIN >> PAGE_SHIFT) &&
		    end < (BIOS_END >> PAGE_SHIFT))
			continue;

		if ((pagenr >= addr) && (pagenr < end))
			return 1;
	}
	return 0;
}

/*
 * Fix up the linear direct mapping of the kernel to avoid cache attribute
 * conflicts.
 */
static int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			       enum ioremap_mode mode)
{
	unsigned long nrpages = size >> PAGE_SHIFT;
	int err;

	switch (mode) {
	case IOR_MODE_UNCACHED:
	default:
		err = set_memory_uc(vaddr, nrpages);
		break;
	case IOR_MODE_CACHED:
		err = set_memory_wb(vaddr, nrpages);
		break;
	}

	return err;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
static void __iomem *__ioremap(unsigned long phys_addr, unsigned long size,
			       enum ioremap_mode mode)
{
	unsigned long pfn, offset, last_addr, vaddr;
	struct vm_struct *area;
	pgprot_t prot;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Don't remap the low PCI/ISA area, it's always mapped..
	 */
	if (phys_addr >= ISA_START_ADDRESS && last_addr < ISA_END_ADDRESS)
		return (__force void __iomem *)phys_to_virt(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	for (pfn = phys_addr >> PAGE_SHIFT; pfn < max_pfn_mapped &&
	     (pfn << PAGE_SHIFT) < last_addr; pfn++) {
		if (page_is_ram(pfn) && pfn_valid(pfn) &&
		    !PageReserved(pfn_to_page(pfn)))
			return NULL;
	}

	switch (mode) {
	case IOR_MODE_UNCACHED:
	default:
		prot = PAGE_KERNEL_NOCACHE;
		break;
	case IOR_MODE_CACHED:
		prot = PAGE_KERNEL;
		break;
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, phys_addr, prot)) {
		remove_vm_area((void *)(vaddr & PAGE_MASK));
		return NULL;
	}

	if (ioremap_change_attr(vaddr, size, mode) < 0) {
		vunmap(area->addr);
		return NULL;
	}

	return (void __iomem *) (vaddr + offset);
}

/**
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In particular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_nocache(unsigned long phys_addr, unsigned long size)
{
	return __ioremap(phys_addr, size, IOR_MODE_UNCACHED);
}
EXPORT_SYMBOL(ioremap_nocache);

void __iomem *ioremap_cache(unsigned long phys_addr, unsigned long size)
{
	return __ioremap(phys_addr, size, IOR_MODE_CACHED);
}
EXPORT_SYMBOL(ioremap_cache);

/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
	struct vm_struct *p, *o;

	if ((void __force *)addr <= high_memory)
		return;

	/*
	 * __ioremap special-cases the PCI/ISA range by not instantiating a
	 * vm_area and by simply returning an address into the kernel mapping
	 * of ISA space.   So handle that here.
	 */
	if (addr >= phys_to_virt(ISA_START_ADDRESS) &&
	    addr < phys_to_virt(ISA_END_ADDRESS))
		return;

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	/* Use the vm area unlocked, assuming the caller
	   ensures there isn't another iounmap for the same address
	   in parallel. Reuse of the virtual address is prevented by
	   leaving it in the global lists until we're done with it.
	   cpa takes care of the direct mappings. */
	read_lock(&vmlist_lock);
	for (p = vmlist; p; p = p->next) {
		if (p->addr == addr)
			break;
	}
	read_unlock(&vmlist_lock);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	/* Finally remove it */
	o = remove_vm_area((void *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}
EXPORT_SYMBOL(iounmap);

#ifdef CONFIG_X86_32

int __initdata early_ioremap_debug;

static int __init early_ioremap_debug_setup(char *str)
{
	early_ioremap_debug = 1;

	return 0;
}
early_param("early_ioremap_debug", early_ioremap_debug_setup);

static __initdata int after_paging_init;
static __initdata unsigned long bm_pte[1024]
				__attribute__((aligned(PAGE_SIZE)));

static inline unsigned long * __init early_ioremap_pgd(unsigned long addr)
{
	return (unsigned long *)swapper_pg_dir + ((addr >> 22) & 1023);
}

static inline unsigned long * __init early_ioremap_pte(unsigned long addr)
{
	return bm_pte + ((addr >> PAGE_SHIFT) & 1023);
}

void __init early_ioremap_init(void)
{
	unsigned long *pgd;

	if (early_ioremap_debug)
		printk(KERN_INFO "early_ioremap_init()\n");

	pgd = early_ioremap_pgd(fix_to_virt(FIX_BTMAP_BEGIN));
	*pgd = __pa(bm_pte) | _PAGE_TABLE;
	memset(bm_pte, 0, sizeof(bm_pte));
	/*
	 * The boot-ioremap range spans multiple pgds, for which
	 * we are not prepared:
	 */
	if (pgd != early_ioremap_pgd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pgd %p != %p\n",
		       pgd, early_ioremap_pgd(fix_to_virt(FIX_BTMAP_END)));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
		       fix_to_virt(FIX_BTMAP_BEGIN));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_END):   %08lx\n",
		       fix_to_virt(FIX_BTMAP_END));

		printk(KERN_WARNING "FIX_BTMAP_END:       %d\n", FIX_BTMAP_END);
		printk(KERN_WARNING "FIX_BTMAP_BEGIN:     %d\n",
		       FIX_BTMAP_BEGIN);
	}
}

void __init early_ioremap_clear(void)
{
	unsigned long *pgd;

	if (early_ioremap_debug)
		printk(KERN_INFO "early_ioremap_clear()\n");

	pgd = early_ioremap_pgd(fix_to_virt(FIX_BTMAP_BEGIN));
	*pgd = 0;
	paravirt_release_pt(__pa(pgd) >> PAGE_SHIFT);
	__flush_tlb_all();
}

void __init early_ioremap_reset(void)
{
	enum fixed_addresses idx;
	unsigned long *pte, phys, addr;

	after_paging_init = 1;
	for (idx = FIX_BTMAP_BEGIN; idx >= FIX_BTMAP_END; idx--) {
		addr = fix_to_virt(idx);
		pte = early_ioremap_pte(addr);
		if (*pte & _PAGE_PRESENT) {
			phys = *pte & PAGE_MASK;
			set_fixmap(idx, phys);
		}
	}
}

static void __init __early_set_fixmap(enum fixed_addresses idx,
				   unsigned long phys, pgprot_t flags)
{
	unsigned long *pte, addr = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr);
	if (pgprot_val(flags))
		*pte = (phys & PAGE_MASK) | pgprot_val(flags);
	else
		*pte = 0;
	__flush_tlb_one(addr);
}

static inline void __init early_set_fixmap(enum fixed_addresses idx,
					unsigned long phys)
{
	if (after_paging_init)
		set_fixmap(idx, phys);
	else
		__early_set_fixmap(idx, phys, PAGE_KERNEL);
}

static inline void __init early_clear_fixmap(enum fixed_addresses idx)
{
	if (after_paging_init)
		clear_fixmap(idx);
	else
		__early_set_fixmap(idx, 0, __pgprot(0));
}


int __initdata early_ioremap_nested;

static int __init check_early_ioremap_leak(void)
{
	if (!early_ioremap_nested)
		return 0;

	printk(KERN_WARNING
	       "Debug warning: early ioremap leak of %d areas detected.\n",
	       early_ioremap_nested);
	printk(KERN_WARNING
	       "please boot with early_ioremap_debug and report the dmesg.\n");
	WARN_ON(1);

	return 1;
}
late_initcall(check_early_ioremap_leak);

void __init *early_ioremap(unsigned long phys_addr, unsigned long size)
{
	unsigned long offset, last_addr;
	unsigned int nrpages, nesting;
	enum fixed_addresses idx0, idx;

	WARN_ON(system_state != SYSTEM_BOOTING);

	nesting = early_ioremap_nested;
	if (early_ioremap_debug) {
		printk(KERN_INFO "early_ioremap(%08lx, %08lx) [%d] => ",
		       phys_addr, size, nesting);
		dump_stack();
	}

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr) {
		WARN_ON(1);
		return NULL;
	}

	if (nesting >= FIX_BTMAPS_NESTING) {
		WARN_ON(1);
		return NULL;
	}
	early_ioremap_nested++;
	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr) - phys_addr;

	/*
	 * Mappings have to fit in the FIX_BTMAP area.
	 */
	nrpages = size >> PAGE_SHIFT;
	if (nrpages > NR_FIX_BTMAPS) {
		WARN_ON(1);
		return NULL;
	}

	/*
	 * Ok, go for it..
	 */
	idx0 = FIX_BTMAP_BEGIN - NR_FIX_BTMAPS*nesting;
	idx = idx0;
	while (nrpages > 0) {
		early_set_fixmap(idx, phys_addr);
		phys_addr += PAGE_SIZE;
		--idx;
		--nrpages;
	}
	if (early_ioremap_debug)
		printk(KERN_CONT "%08lx + %08lx\n", offset, fix_to_virt(idx0));

	return (void *) (offset + fix_to_virt(idx0));
}

void __init early_iounmap(void *addr, unsigned long size)
{
	unsigned long virt_addr;
	unsigned long offset;
	unsigned int nrpages;
	enum fixed_addresses idx;
	unsigned int nesting;

	nesting = --early_ioremap_nested;
	WARN_ON(nesting < 0);

	if (early_ioremap_debug) {
		printk(KERN_INFO "early_iounmap(%p, %08lx) [%d]\n", addr,
		       size, nesting);
		dump_stack();
	}

	virt_addr = (unsigned long)addr;
	if (virt_addr < fix_to_virt(FIX_BTMAP_BEGIN)) {
		WARN_ON(1);
		return;
	}
	offset = virt_addr & ~PAGE_MASK;
	nrpages = PAGE_ALIGN(offset + size - 1) >> PAGE_SHIFT;

	idx = FIX_BTMAP_BEGIN - NR_FIX_BTMAPS*nesting;
	while (nrpages > 0) {
		early_clear_fixmap(idx);
		--idx;
		--nrpages;
	}
}

void __this_fixmap_does_not_exist(void)
{
	WARN_ON(1);
}

#endif /* CONFIG_X86_32 */

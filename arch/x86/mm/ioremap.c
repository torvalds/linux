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
#include <linux/mmiotrace.h>

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/pat.h>

#ifdef CONFIG_X86_64

unsigned long __phys_addr(unsigned long x)
{
	if (x >= __START_KERNEL_map)
		return x - __START_KERNEL_map + phys_base;
	return x - PAGE_OFFSET;
}
EXPORT_SYMBOL(__phys_addr);

static inline int phys_addr_valid(unsigned long addr)
{
	return addr < (1UL << boot_cpu_data.x86_phys_bits);
}

#else

static inline int phys_addr_valid(unsigned long addr)
{
	return 1;
}

#endif

int page_is_ram(unsigned long pagenr)
{
	resource_size_t addr, end;
	int i;

	/*
	 * A special case is the first 4Kb of memory;
	 * This is a BIOS owned area, not kernel ram, but generally
	 * not listed as such in the E820 table.
	 */
	if (pagenr == 0)
		return 0;

	/*
	 * Second special case: Some BIOSen report the PC BIOS
	 * area (640->1Mb) as ram even though it is not.
	 */
	if (pagenr >= (BIOS_BEGIN >> PAGE_SHIFT) &&
		    pagenr < (BIOS_END >> PAGE_SHIFT))
		return 0;

	for (i = 0; i < e820.nr_map; i++) {
		/*
		 * Not usable memory:
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		addr = (e820.map[i].addr + PAGE_SIZE-1) >> PAGE_SHIFT;
		end = (e820.map[i].addr + e820.map[i].size) >> PAGE_SHIFT;


		if ((pagenr >= addr) && (pagenr < end))
			return 1;
	}
	return 0;
}

/*
 * Fix up the linear direct mapping of the kernel to avoid cache attribute
 * conflicts.
 */
int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			       unsigned long prot_val)
{
	unsigned long nrpages = size >> PAGE_SHIFT;
	int err;

	switch (prot_val) {
	case _PAGE_CACHE_UC:
	default:
		err = _set_memory_uc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_WC:
		err = _set_memory_wc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_WB:
		err = _set_memory_wb(vaddr, nrpages);
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
static void __iomem *__ioremap_caller(resource_size_t phys_addr,
		unsigned long size, unsigned long prot_val, void *caller)
{
	unsigned long pfn, offset, vaddr;
	resource_size_t last_addr;
	const resource_size_t unaligned_phys_addr = phys_addr;
	const unsigned long unaligned_size = size;
	struct vm_struct *area;
	unsigned long new_prot_val;
	pgprot_t prot;
	int retval;
	void __iomem *ret_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	if (!phys_addr_valid(phys_addr)) {
		printk(KERN_WARNING "ioremap: invalid physical address %llx\n",
		       (unsigned long long)phys_addr);
		WARN_ON_ONCE(1);
		return NULL;
	}

	/*
	 * Don't remap the low PCI/ISA area, it's always mapped..
	 */
	if (is_ISA_range(phys_addr, last_addr))
		return (__force void __iomem *)phys_to_virt(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	for (pfn = phys_addr >> PAGE_SHIFT;
				(pfn << PAGE_SHIFT) < (last_addr & PAGE_MASK);
				pfn++) {

		int is_ram = page_is_ram(pfn);

		if (is_ram && pfn_valid(pfn) && !PageReserved(pfn_to_page(pfn)))
			return NULL;
		WARN_ON_ONCE(is_ram);
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	retval = reserve_memtype(phys_addr, (u64)phys_addr + size,
						prot_val, &new_prot_val);
	if (retval) {
		pr_debug("Warning: reserve_memtype returned %d\n", retval);
		return NULL;
	}

	if (prot_val != new_prot_val) {
		/*
		 * Do not fallback to certain memory types with certain
		 * requested type:
		 * - request is uc-, return cannot be write-back
		 * - request is uc-, return cannot be write-combine
		 * - request is write-combine, return cannot be write-back
		 */
		if ((prot_val == _PAGE_CACHE_UC_MINUS &&
		     (new_prot_val == _PAGE_CACHE_WB ||
		      new_prot_val == _PAGE_CACHE_WC)) ||
		    (prot_val == _PAGE_CACHE_WC &&
		     new_prot_val == _PAGE_CACHE_WB)) {
			pr_debug(
		"ioremap error for 0x%llx-0x%llx, requested 0x%lx, got 0x%lx\n",
				(unsigned long long)phys_addr,
				(unsigned long long)(phys_addr + size),
				prot_val, new_prot_val);
			free_memtype(phys_addr, phys_addr + size);
			return NULL;
		}
		prot_val = new_prot_val;
	}

	switch (prot_val) {
	case _PAGE_CACHE_UC:
	default:
		prot = PAGE_KERNEL_NOCACHE;
		break;
	case _PAGE_CACHE_UC_MINUS:
		prot = PAGE_KERNEL_UC_MINUS;
		break;
	case _PAGE_CACHE_WC:
		prot = PAGE_KERNEL_WC;
		break;
	case _PAGE_CACHE_WB:
		prot = PAGE_KERNEL;
		break;
	}

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, phys_addr, prot)) {
		free_memtype(phys_addr, phys_addr + size);
		free_vm_area(area);
		return NULL;
	}

	if (ioremap_change_attr(vaddr, size, prot_val) < 0) {
		free_memtype(phys_addr, phys_addr + size);
		vunmap(area->addr);
		return NULL;
	}

	ret_addr = (void __iomem *) (vaddr + offset);
	mmiotrace_ioremap(unaligned_phys_addr, unaligned_size, ret_addr);

	return ret_addr;
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
void __iomem *ioremap_nocache(resource_size_t phys_addr, unsigned long size)
{
	/*
	 * Ideally, this should be:
	 *	pat_enabled ? _PAGE_CACHE_UC : _PAGE_CACHE_UC_MINUS;
	 *
	 * Till we fix all X drivers to use ioremap_wc(), we will use
	 * UC MINUS.
	 */
	unsigned long val = _PAGE_CACHE_UC_MINUS;

	return __ioremap_caller(phys_addr, size, val,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_nocache);

/**
 * ioremap_wc	-	map memory into CPU space write combined
 * @offset:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write combining.
 * Write combining allows faster writes to some hardware devices.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wc(unsigned long phys_addr, unsigned long size)
{
	if (pat_enabled)
		return __ioremap_caller(phys_addr, size, _PAGE_CACHE_WC,
					__builtin_return_address(0));
	else
		return ioremap_nocache(phys_addr, size);
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_cache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_WB,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

static void __iomem *ioremap_default(resource_size_t phys_addr,
					unsigned long size)
{
	unsigned long flags;
	void *ret;
	int err;

	/*
	 * - WB for WB-able memory and no other conflicting mappings
	 * - UC_MINUS for non-WB-able memory with no other conflicting mappings
	 * - Inherit from confliting mappings otherwise
	 */
	err = reserve_memtype(phys_addr, phys_addr + size, -1, &flags);
	if (err < 0)
		return NULL;

	ret = (void *) __ioremap_caller(phys_addr, size, flags,
					__builtin_return_address(0));

	free_memtype(phys_addr, phys_addr + size);
	return (void __iomem *)ret;
}

void __iomem *ioremap_prot(resource_size_t phys_addr, unsigned long size,
				unsigned long prot_val)
{
	return __ioremap_caller(phys_addr, size, (prot_val & _PAGE_CACHE_MASK),
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_prot);

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
	if ((void __force *)addr >= phys_to_virt(ISA_START_ADDRESS) &&
	    (void __force *)addr < phys_to_virt(ISA_END_ADDRESS))
		return;

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	mmiotrace_iounmap(addr);

	/* Use the vm area unlocked, assuming the caller
	   ensures there isn't another iounmap for the same address
	   in parallel. Reuse of the virtual address is prevented by
	   leaving it in the global lists until we're done with it.
	   cpa takes care of the direct mappings. */
	read_lock(&vmlist_lock);
	for (p = vmlist; p; p = p->next) {
		if (p->addr == (void __force *)addr)
			break;
	}
	read_unlock(&vmlist_lock);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	free_memtype(p->phys_addr, p->phys_addr + get_vm_area_size(p));

	/* Finally remove it */
	o = remove_vm_area((void __force *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}
EXPORT_SYMBOL(iounmap);

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
void *xlate_dev_mem_ptr(unsigned long phys)
{
	void *addr;
	unsigned long start = phys & PAGE_MASK;

	/* If page is RAM, we can use __va. Otherwise ioremap and unmap. */
	if (page_is_ram(start >> PAGE_SHIFT))
		return __va(phys);

	addr = (void __force *)ioremap_default(start, PAGE_SIZE);
	if (addr)
		addr = (void *)((unsigned long)addr | (phys & ~PAGE_MASK));

	return addr;
}

void unxlate_dev_mem_ptr(unsigned long phys, void *addr)
{
	if (page_is_ram(phys >> PAGE_SHIFT))
		return;

	iounmap((void __iomem *)((unsigned long)addr & PAGE_MASK));
	return;
}

int __initdata early_ioremap_debug;

static int __init early_ioremap_debug_setup(char *str)
{
	early_ioremap_debug = 1;

	return 0;
}
early_param("early_ioremap_debug", early_ioremap_debug_setup);

static __initdata int after_paging_init;
static pte_t bm_pte[PAGE_SIZE/sizeof(pte_t)] __page_aligned_bss;

static inline pmd_t * __init early_ioremap_pmd(unsigned long addr)
{
	/* Don't assume we're using swapper_pg_dir at this point */
	pgd_t *base = __va(read_cr3());
	pgd_t *pgd = &base[pgd_index(addr)];
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return pmd;
}

static inline pte_t * __init early_ioremap_pte(unsigned long addr)
{
	return &bm_pte[pte_index(addr)];
}

void __init early_ioremap_init(void)
{
	pmd_t *pmd;

	if (early_ioremap_debug)
		printk(KERN_INFO "early_ioremap_init()\n");

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	memset(bm_pte, 0, sizeof(bm_pte));
	pmd_populate_kernel(&init_mm, pmd, bm_pte);

	/*
	 * The boot-ioremap range spans multiple pmds, for which
	 * we are not prepared:
	 */
	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pmd %p != %p\n",
		       pmd, early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
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
	pmd_t *pmd;

	if (early_ioremap_debug)
		printk(KERN_INFO "early_ioremap_clear()\n");

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	pmd_clear(pmd);
	paravirt_release_pte(__pa(bm_pte) >> PAGE_SHIFT);
	__flush_tlb_all();
}

void __init early_ioremap_reset(void)
{
	enum fixed_addresses idx;
	unsigned long addr, phys;
	pte_t *pte;

	after_paging_init = 1;
	for (idx = FIX_BTMAP_BEGIN; idx >= FIX_BTMAP_END; idx--) {
		addr = fix_to_virt(idx);
		pte = early_ioremap_pte(addr);
		if (pte_present(*pte)) {
			phys = pte_val(*pte) & PAGE_MASK;
			set_fixmap(idx, phys);
		}
	}
}

static void __init __early_set_fixmap(enum fixed_addresses idx,
				   unsigned long phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr);

	if (pgprot_val(flags))
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(&init_mm, addr, pte);
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
	int nesting;

	nesting = --early_ioremap_nested;
	if (WARN_ON(nesting < 0))
		return;

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

// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 *
 * Hacked for ARM by Phil Blundell <philb@gnu.org>
 * Hacked to allow all architectures to build, and various cleanups
 * by Russell King
 *
 * This allows a driver to remap an arbitrary region of bus memory into
 * virtual space.  One should *only* use readl, writel, memcpy_toio and
 * so on with such remapped areas.
 *
 * Because the ARM only has a 32-bit address space we can't address the
 * whole of the (physical) PCI space at once.  PCI huge-mode addressing
 * allows us to circumvent this restriction by splitting PCI space into
 * two 2GB chunks and mapping only one at a time into processor memory.
 * We use MMU protection domains to trap any attempt to access the bank
 * that is not currently mapped.  (This isn't fully implemented yet.)
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/memblock.h>

#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/cacheflush.h>
#include <asm/early_ioremap.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/set_memory.h>
#include <asm/system_info.h>

#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include "mm.h"


LIST_HEAD(static_vmlist);

static struct static_vm *find_static_vm_paddr(phys_addr_t paddr,
			size_t size, unsigned int mtype)
{
	struct static_vm *svm;
	struct vm_struct *vm;

	list_for_each_entry(svm, &static_vmlist, list) {
		vm = &svm->vm;
		if (!(vm->flags & VM_ARM_STATIC_MAPPING))
			continue;
		if ((vm->flags & VM_ARM_MTYPE_MASK) != VM_ARM_MTYPE(mtype))
			continue;

		if (vm->phys_addr > paddr ||
			paddr + size - 1 > vm->phys_addr + vm->size - 1)
			continue;

		return svm;
	}

	return NULL;
}

struct static_vm *find_static_vm_vaddr(void *vaddr)
{
	struct static_vm *svm;
	struct vm_struct *vm;

	list_for_each_entry(svm, &static_vmlist, list) {
		vm = &svm->vm;

		/* static_vmlist is ascending order */
		if (vm->addr > vaddr)
			break;

		if (vm->addr <= vaddr && vm->addr + vm->size > vaddr)
			return svm;
	}

	return NULL;
}

void __init add_static_vm_early(struct static_vm *svm)
{
	struct static_vm *curr_svm;
	struct vm_struct *vm;
	void *vaddr;

	vm = &svm->vm;
	vm_area_add_early(vm);
	vaddr = vm->addr;

	list_for_each_entry(curr_svm, &static_vmlist, list) {
		vm = &curr_svm->vm;

		if (vm->addr > vaddr)
			break;
	}
	list_add_tail(&svm->list, &curr_svm->list);
}

int ioremap_page(unsigned long virt, unsigned long phys,
		 const struct mem_type *mtype)
{
	return vmap_page_range(virt, virt + PAGE_SIZE, phys,
			       __pgprot(mtype->prot_pte));
}
EXPORT_SYMBOL(ioremap_page);

void __check_vmalloc_seq(struct mm_struct *mm)
{
	int seq;

	do {
		seq = atomic_read(&init_mm.context.vmalloc_seq);
		memcpy(pgd_offset(mm, VMALLOC_START),
		       pgd_offset_k(VMALLOC_START),
		       sizeof(pgd_t) * (pgd_index(VMALLOC_END) -
					pgd_index(VMALLOC_START)));
		/*
		 * Use a store-release so that other CPUs that observe the
		 * counter's new value are guaranteed to see the results of the
		 * memcpy as well.
		 */
		atomic_set_release(&mm->context.vmalloc_seq, seq);
	} while (seq != atomic_read(&init_mm.context.vmalloc_seq));
}

#if !defined(CONFIG_SMP) && !defined(CONFIG_ARM_LPAE)
/*
 * Section support is unsafe on SMP - If you iounmap and ioremap a region,
 * the other CPUs will not see this change until their next context switch.
 * Meanwhile, (eg) if an interrupt comes in on one of those other CPUs
 * which requires the new ioremap'd region to be referenced, the CPU will
 * reference the _old_ region.
 *
 * Note that get_vm_area_caller() allocates a guard 4K page, so we need to
 * mask the size back to 1MB aligned or we will overflow in the loop below.
 */
static void unmap_area_sections(unsigned long virt, unsigned long size)
{
	unsigned long addr = virt, end = virt + (size & ~(SZ_1M - 1));
	pmd_t *pmdp = pmd_off_k(addr);

	do {
		pmd_t pmd = *pmdp;

		if (!pmd_none(pmd)) {
			/*
			 * Clear the PMD from the page table, and
			 * increment the vmalloc sequence so others
			 * notice this change.
			 *
			 * Note: this is still racy on SMP machines.
			 */
			pmd_clear(pmdp);
			atomic_inc_return_release(&init_mm.context.vmalloc_seq);

			/*
			 * Free the page table, if there was one.
			 */
			if ((pmd_val(pmd) & PMD_TYPE_MASK) == PMD_TYPE_TABLE)
				pte_free_kernel(&init_mm, pmd_page_vaddr(pmd));
		}

		addr += PMD_SIZE;
		pmdp += 2;
	} while (addr < end);

	/*
	 * Ensure that the active_mm is up to date - we want to
	 * catch any use-after-iounmap cases.
	 */
	check_vmalloc_seq(current->active_mm);

	flush_tlb_kernel_range(virt, end);
}

static int
remap_area_sections(unsigned long virt, unsigned long pfn,
		    size_t size, const struct mem_type *type)
{
	unsigned long addr = virt, end = virt + size;
	pmd_t *pmd = pmd_off_k(addr);

	/*
	 * Remove and free any PTE-based mapping, and
	 * sync the current kernel mapping.
	 */
	unmap_area_sections(virt, size);

	do {
		pmd[0] = __pmd(__pfn_to_phys(pfn) | type->prot_sect);
		pfn += SZ_1M >> PAGE_SHIFT;
		pmd[1] = __pmd(__pfn_to_phys(pfn) | type->prot_sect);
		pfn += SZ_1M >> PAGE_SHIFT;
		flush_pmd_entry(pmd);

		addr += PMD_SIZE;
		pmd += 2;
	} while (addr < end);

	return 0;
}

static int
remap_area_supersections(unsigned long virt, unsigned long pfn,
			 size_t size, const struct mem_type *type)
{
	unsigned long addr = virt, end = virt + size;
	pmd_t *pmd = pmd_off_k(addr);

	/*
	 * Remove and free any PTE-based mapping, and
	 * sync the current kernel mapping.
	 */
	unmap_area_sections(virt, size);
	do {
		unsigned long super_pmd_val, i;

		super_pmd_val = __pfn_to_phys(pfn) | type->prot_sect |
				PMD_SECT_SUPER;
		super_pmd_val |= ((pfn >> (32 - PAGE_SHIFT)) & 0xf) << 20;

		for (i = 0; i < 8; i++) {
			pmd[0] = __pmd(super_pmd_val);
			pmd[1] = __pmd(super_pmd_val);
			flush_pmd_entry(pmd);

			addr += PMD_SIZE;
			pmd += 2;
		}

		pfn += SUPERSECTION_SIZE >> PAGE_SHIFT;
	} while (addr < end);

	return 0;
}
#endif

static void __iomem * __arm_ioremap_pfn_caller(unsigned long pfn,
	unsigned long offset, size_t size, unsigned int mtype, void *caller)
{
	const struct mem_type *type;
	int err;
	unsigned long addr;
	struct vm_struct *area;
	phys_addr_t paddr = __pfn_to_phys(pfn);

#ifndef CONFIG_ARM_LPAE
	/*
	 * High mappings must be supersection aligned
	 */
	if (pfn >= 0x100000 && (paddr & ~SUPERSECTION_MASK))
		return NULL;
#endif

	type = get_mem_type(mtype);
	if (!type)
		return NULL;

	/*
	 * Page align the mapping size, taking account of any offset.
	 */
	size = PAGE_ALIGN(offset + size);

	/*
	 * Try to reuse one of the static mapping whenever possible.
	 */
	if (size && !(sizeof(phys_addr_t) == 4 && pfn >= 0x100000)) {
		struct static_vm *svm;

		svm = find_static_vm_paddr(paddr, size, mtype);
		if (svm) {
			addr = (unsigned long)svm->vm.addr;
			addr += paddr - svm->vm.phys_addr;
			return (void __iomem *) (offset + addr);
		}
	}

	/*
	 * Don't allow RAM to be mapped with mismatched attributes - this
	 * causes problems with ARMv6+
	 */
	if (WARN_ON(memblock_is_map_memory(PFN_PHYS(pfn)) &&
		    mtype != MT_MEMORY_RW))
		return NULL;

	area = get_vm_area_caller(size, VM_IOREMAP, caller);
 	if (!area)
 		return NULL;
 	addr = (unsigned long)area->addr;
	area->phys_addr = paddr;

#if !defined(CONFIG_SMP) && !defined(CONFIG_ARM_LPAE)
	if (DOMAIN_IO == 0 &&
	    (((cpu_architecture() >= CPU_ARCH_ARMv6) && (get_cr() & CR_XP)) ||
	       cpu_is_xsc3()) && pfn >= 0x100000 &&
	       !((paddr | size | addr) & ~SUPERSECTION_MASK)) {
		area->flags |= VM_ARM_SECTION_MAPPING;
		err = remap_area_supersections(addr, pfn, size, type);
	} else if (!((paddr | size | addr) & ~PMD_MASK)) {
		area->flags |= VM_ARM_SECTION_MAPPING;
		err = remap_area_sections(addr, pfn, size, type);
	} else
#endif
		err = ioremap_page_range(addr, addr + size, paddr,
					 __pgprot(type->prot_pte));

	if (err) {
 		vunmap((void *)addr);
 		return NULL;
 	}

	flush_cache_vmap(addr, addr + size);
	return (void __iomem *) (offset + addr);
}

void __iomem *__arm_ioremap_caller(phys_addr_t phys_addr, size_t size,
	unsigned int mtype, void *caller)
{
	phys_addr_t last_addr;
 	unsigned long offset = phys_addr & ~PAGE_MASK;
 	unsigned long pfn = __phys_to_pfn(phys_addr);

 	/*
 	 * Don't allow wraparound or zero size
	 */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	return __arm_ioremap_pfn_caller(pfn, offset, size, mtype,
			caller);
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
void __iomem *
__arm_ioremap_pfn(unsigned long pfn, unsigned long offset, size_t size,
		  unsigned int mtype)
{
	return __arm_ioremap_pfn_caller(pfn, offset, size, mtype,
					__builtin_return_address(0));
}
EXPORT_SYMBOL(__arm_ioremap_pfn);

void __iomem * (*arch_ioremap_caller)(phys_addr_t, size_t,
				      unsigned int, void *) =
	__arm_ioremap_caller;

void __iomem *ioremap(resource_size_t res_cookie, size_t size)
{
	return arch_ioremap_caller(res_cookie, size, MT_DEVICE,
				   __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_cache(resource_size_t res_cookie, size_t size)
{
	return arch_ioremap_caller(res_cookie, size, MT_DEVICE_CACHED,
				   __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

void __iomem *ioremap_wc(resource_size_t res_cookie, size_t size)
{
	return arch_ioremap_caller(res_cookie, size, MT_DEVICE_WC,
				   __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wc);

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space as memory. Needed when the kernel wants to execute
 * code in external memory. This is needed for reprogramming source
 * clocks that would affect normal memory for example. Please see
 * CONFIG_GENERIC_ALLOCATOR for allocating external memory.
 */
void __iomem *
__arm_ioremap_exec(phys_addr_t phys_addr, size_t size, bool cached)
{
	unsigned int mtype;

	if (cached)
		mtype = MT_MEMORY_RWX;
	else
		mtype = MT_MEMORY_RWX_NONCACHED;

	return __arm_ioremap_caller(phys_addr, size, mtype,
			__builtin_return_address(0));
}

void __arm_iomem_set_ro(void __iomem *ptr, size_t size)
{
	set_memory_ro((unsigned long)ptr, PAGE_ALIGN(size) / PAGE_SIZE);
}

void *arch_memremap_wb(phys_addr_t phys_addr, size_t size)
{
	return (__force void *)arch_ioremap_caller(phys_addr, size,
						   MT_MEMORY_RW,
						   __builtin_return_address(0));
}

void iounmap(volatile void __iomem *io_addr)
{
	void *addr = (void *)(PAGE_MASK & (unsigned long)io_addr);
	struct static_vm *svm;

	/* If this is a static mapping, we must leave it alone */
	svm = find_static_vm_vaddr(addr);
	if (svm)
		return;

#if !defined(CONFIG_SMP) && !defined(CONFIG_ARM_LPAE)
	{
		struct vm_struct *vm;

		vm = find_vm_area(addr);

		/*
		 * If this is a section based mapping we need to handle it
		 * specially as the VM subsystem does not know how to handle
		 * such a beast.
		 */
		if (vm && (vm->flags & VM_ARM_SECTION_MAPPING))
			unmap_area_sections((unsigned long)vm->addr, vm->size);
	}
#endif

	vunmap(addr);
}
EXPORT_SYMBOL(iounmap);

#if defined(CONFIG_PCI) || IS_ENABLED(CONFIG_PCMCIA)
static int pci_ioremap_mem_type = MT_DEVICE;

void pci_ioremap_set_mem_type(int mem_type)
{
	pci_ioremap_mem_type = mem_type;
}

int pci_remap_iospace(const struct resource *res, phys_addr_t phys_addr)
{
	unsigned long vaddr = (unsigned long)PCI_IOBASE + res->start;

	if (!(res->flags & IORESOURCE_IO))
		return -EINVAL;

	if (res->end > IO_SPACE_LIMIT)
		return -EINVAL;

	return vmap_page_range(vaddr, vaddr + resource_size(res), phys_addr,
			       __pgprot(get_mem_type(pci_ioremap_mem_type)->prot_pte));
}
EXPORT_SYMBOL(pci_remap_iospace);

void __iomem *pci_remap_cfgspace(resource_size_t res_cookie, size_t size)
{
	return arch_ioremap_caller(res_cookie, size, MT_UNCACHED,
				   __builtin_return_address(0));
}
EXPORT_SYMBOL_GPL(pci_remap_cfgspace);
#endif

/*
 * Must be called after early_fixmap_init
 */
void __init early_ioremap_init(void)
{
	early_ioremap_setup();
}

bool arch_memremap_can_ram_remap(resource_size_t offset, size_t size,
				 unsigned long flags)
{
	unsigned long pfn = PHYS_PFN(offset);

	return memblock_is_map_memory(pfn);
}

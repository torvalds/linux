// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/mmap.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/cpufeature.h>
#include <asm/page.h>

static pgprot_t protection_map[16] __ro_after_init = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_READONLY,
	[VM_WRITE | VM_READ]				= PAGE_READONLY,
	/* PAGE_EXECONLY if Enhanced PAN */
	[VM_EXEC]					= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	/* PAGE_EXECONLY if Enhanced PAN */
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_EXEC
};

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This might go
 * away in the future.
 */
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	/*
	 * Check whether addr is covered by a memory region without the
	 * MEMBLOCK_NOMAP attribute, and whether that region covers the
	 * entire range. In theory, this could lead to false negatives
	 * if the range is covered by distinct but adjacent memory regions
	 * that only differ in other attributes. However, few of such
	 * attributes have been defined, and it is debatable whether it
	 * follows that /dev/mem read() calls should be able traverse
	 * such boundaries.
	 */
	return memblock_is_region_memory(addr, size) &&
	       memblock_is_map_memory(addr);
}

/*
 * Do not allow /dev/mem mappings beyond the supported physical range.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(((pfn << PAGE_SHIFT) + size) & ~PHYS_MASK);
}

static int __init adjust_protection_map(void)
{
	/*
	 * With Enhanced PAN we can honour the execute-only permissions as
	 * there is no PAN override with such mappings.
	 */
	if (cpus_have_cap(ARM64_HAS_EPAN)) {
		protection_map[VM_EXEC] = PAGE_EXECONLY;
		protection_map[VM_EXEC | VM_SHARED] = PAGE_EXECONLY;
	}

	if (lpa2_is_enabled())
		for (int i = 0; i < ARRAY_SIZE(protection_map); i++)
			pgprot_val(protection_map[i]) &= ~PTE_SHARED;

	return 0;
}
arch_initcall(adjust_protection_map);

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	pteval_t prot = pgprot_val(protection_map[vm_flags &
				   (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)]);

	if (vm_flags & VM_ARM64_BTI)
		prot |= PTE_GP;

	/*
	 * There are two conditions required for returning a Normal Tagged
	 * memory type: (1) the user requested it via PROT_MTE passed to
	 * mmap() or mprotect() and (2) the corresponding vma supports MTE. We
	 * register (1) as VM_MTE in the vma->vm_flags and (2) as
	 * VM_MTE_ALLOWED. Note that the latter can only be set during the
	 * mmap() call since mprotect() does not accept MAP_* flags.
	 * Checking for VM_MTE only is sufficient since arch_validate_flags()
	 * does not permit (VM_MTE & !VM_MTE_ALLOWED).
	 */
	if (vm_flags & VM_MTE)
		prot |= PTE_ATTRINDX(MT_NORMAL_TAGGED);

	return __pgprot(prot);
}
EXPORT_SYMBOL(vm_get_page_prot);

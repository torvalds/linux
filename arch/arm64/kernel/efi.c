// SPDX-License-Identifier: GPL-2.0-only
/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 2.4
 *
 * Copyright (C) 2013, 2014 Linaro Ltd.
 */

#include <linux/efi.h>
#include <linux/init.h>

#include <asm/efi.h>

static bool region_is_misaligned(const efi_memory_desc_t *md)
{
	if (PAGE_SIZE == EFI_PAGE_SIZE)
		return false;
	return !PAGE_ALIGNED(md->phys_addr) ||
	       !PAGE_ALIGNED(md->num_pages << EFI_PAGE_SHIFT);
}

/*
 * Only regions of type EFI_RUNTIME_SERVICES_CODE need to be
 * executable, everything else can be mapped with the XN bits
 * set. Also take the new (optional) RO/XP bits into account.
 */
static __init pteval_t create_mapping_protection(efi_memory_desc_t *md)
{
	u64 attr = md->attribute;
	u32 type = md->type;

	if (type == EFI_MEMORY_MAPPED_IO)
		return PROT_DEVICE_nGnRE;

	if (region_is_misaligned(md)) {
		static bool __initdata code_is_misaligned;

		/*
		 * Regions that are not aligned to the OS page size cannot be
		 * mapped with strict permissions, as those might interfere
		 * with the permissions that are needed by the adjacent
		 * region's mapping. However, if we haven't encountered any
		 * misaligned runtime code regions so far, we can safely use
		 * non-executable permissions for non-code regions.
		 */
		code_is_misaligned |= (type == EFI_RUNTIME_SERVICES_CODE);

		return code_is_misaligned ? pgprot_val(PAGE_KERNEL_EXEC)
					  : pgprot_val(PAGE_KERNEL);
	}

	/* R-- */
	if ((attr & (EFI_MEMORY_XP | EFI_MEMORY_RO)) ==
	    (EFI_MEMORY_XP | EFI_MEMORY_RO))
		return pgprot_val(PAGE_KERNEL_RO);

	/* R-X */
	if (attr & EFI_MEMORY_RO)
		return pgprot_val(PAGE_KERNEL_ROX);

	/* RW- */
	if (((attr & (EFI_MEMORY_RP | EFI_MEMORY_WP | EFI_MEMORY_XP)) ==
	     EFI_MEMORY_XP) ||
	    type != EFI_RUNTIME_SERVICES_CODE)
		return pgprot_val(PAGE_KERNEL);

	/* RWX */
	return pgprot_val(PAGE_KERNEL_EXEC);
}

/* we will fill this structure from the stub, so don't put it in .bss */
struct screen_info screen_info __section(".data");
EXPORT_SYMBOL(screen_info);

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	pteval_t prot_val = create_mapping_protection(md);
	bool page_mappings_only = (md->type == EFI_RUNTIME_SERVICES_CODE ||
				   md->type == EFI_RUNTIME_SERVICES_DATA);

	/*
	 * If this region is not aligned to the page size used by the OS, the
	 * mapping will be rounded outwards, and may end up sharing a page
	 * frame with an adjacent runtime memory region. Given that the page
	 * table descriptor covering the shared page will be rewritten when the
	 * adjacent region gets mapped, we must avoid block mappings here so we
	 * don't have to worry about splitting them when that happens.
	 */
	if (region_is_misaligned(md))
		page_mappings_only = true;

	create_pgd_mapping(mm, md->phys_addr, md->virt_addr,
			   md->num_pages << EFI_PAGE_SHIFT,
			   __pgprot(prot_val | PTE_NG), page_mappings_only);
	return 0;
}

static int __init set_permissions(pte_t *ptep, unsigned long addr, void *data)
{
	efi_memory_desc_t *md = data;
	pte_t pte = READ_ONCE(*ptep);

	if (md->attribute & EFI_MEMORY_RO)
		pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));
	if (md->attribute & EFI_MEMORY_XP)
		pte = set_pte_bit(pte, __pgprot(PTE_PXN));
	set_pte(ptep, pte);
	return 0;
}

int __init efi_set_mapping_permissions(struct mm_struct *mm,
				       efi_memory_desc_t *md)
{
	BUG_ON(md->type != EFI_RUNTIME_SERVICES_CODE &&
	       md->type != EFI_RUNTIME_SERVICES_DATA);

	if (region_is_misaligned(md))
		return 0;

	/*
	 * Calling apply_to_page_range() is only safe on regions that are
	 * guaranteed to be mapped down to pages. Since we are only called
	 * for regions that have been mapped using efi_create_mapping() above
	 * (and this is checked by the generic Memory Attributes table parsing
	 * routines), there is no need to check that again here.
	 */
	return apply_to_page_range(mm, md->virt_addr,
				   md->num_pages << EFI_PAGE_SHIFT,
				   set_permissions, md);
}

/*
 * UpdateCapsule() depends on the system being shutdown via
 * ResetSystem().
 */
bool efi_poweroff_required(void)
{
	return efi_enabled(EFI_RUNTIME_SERVICES);
}

asmlinkage efi_status_t efi_handle_corrupted_x18(efi_status_t s, const char *f)
{
	pr_err_ratelimited(FW_BUG "register x18 corrupted by EFI %s\n", f);
	return s;
}

DEFINE_SPINLOCK(efi_rt_lock);

asmlinkage u64 *efi_rt_stack_top __ro_after_init;

/* EFI requires 8 KiB of stack space for runtime services */
static_assert(THREAD_SIZE >= SZ_8K);

static int __init arm64_efi_rt_init(void)
{
	void *p;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	p = __vmalloc_node(THREAD_SIZE, THREAD_ALIGN, GFP_KERNEL,
			   NUMA_NO_NODE, &&l);
l:	if (!p) {
		pr_warn("Failed to allocate EFI runtime stack\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return -ENOMEM;
	}

	efi_rt_stack_top = p + THREAD_SIZE;
	return 0;
}
core_initcall(arm64_efi_rt_init);

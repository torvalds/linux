// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 * Adapted from arch/arm64/kernel/efi.c
 */

#include <linux/efi.h>
#include <linux/init.h>

#include <asm/efi.h>
#include <asm/pgtable.h>
#include <asm/pgtable-bits.h>

/*
 * Only regions of type EFI_RUNTIME_SERVICES_CODE need to be
 * executable, everything else can be mapped with the XN bits
 * set. Also take the new (optional) RO/XP bits into account.
 */
static __init pgprot_t efimem_to_pgprot_map(efi_memory_desc_t *md)
{
	u64 attr = md->attribute;
	u32 type = md->type;

	if (type == EFI_MEMORY_MAPPED_IO)
		return PAGE_KERNEL;

	/* R-- */
	if ((attr & (EFI_MEMORY_XP | EFI_MEMORY_RO)) ==
	    (EFI_MEMORY_XP | EFI_MEMORY_RO))
		return PAGE_KERNEL_READ;

	/* R-X */
	if (attr & EFI_MEMORY_RO)
		return PAGE_KERNEL_READ_EXEC;

	/* RW- */
	if (((attr & (EFI_MEMORY_RP | EFI_MEMORY_WP | EFI_MEMORY_XP)) ==
	     EFI_MEMORY_XP) ||
	    type != EFI_RUNTIME_SERVICES_CODE)
		return PAGE_KERNEL;

	/* RWX */
	return PAGE_KERNEL_EXEC;
}

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	pgprot_t prot = __pgprot(pgprot_val(efimem_to_pgprot_map(md)) &
				~(_PAGE_GLOBAL));
	int i;

	/* RISC-V maps one page at a time */
	for (i = 0; i < md->num_pages; i++)
		create_pgd_mapping(mm->pgd, md->virt_addr + i * PAGE_SIZE,
				   md->phys_addr + i * PAGE_SIZE,
				   PAGE_SIZE, prot);
	return 0;
}

static int __init set_permissions(pte_t *ptep, unsigned long addr, void *data)
{
	efi_memory_desc_t *md = data;
	pte_t pte = READ_ONCE(*ptep);
	unsigned long val;

	if (md->attribute & EFI_MEMORY_RO) {
		val = pte_val(pte) & ~_PAGE_WRITE;
		val |= _PAGE_READ;
		pte = __pte(val);
	}
	if (md->attribute & EFI_MEMORY_XP) {
		val = pte_val(pte) & ~_PAGE_EXEC;
		pte = __pte(val);
	}
	set_pte(ptep, pte);

	return 0;
}

int __init efi_set_mapping_permissions(struct mm_struct *mm,
				       efi_memory_desc_t *md,
				       bool ignored)
{
	BUG_ON(md->type != EFI_RUNTIME_SERVICES_CODE &&
	       md->type != EFI_RUNTIME_SERVICES_DATA);

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

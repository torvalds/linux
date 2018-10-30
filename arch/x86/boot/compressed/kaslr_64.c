// SPDX-License-Identifier: GPL-2.0
/*
 * This code is used on x86_64 to create page table identity mappings on
 * demand by building up a new set of page tables (or appending to the
 * existing ones), and then switching over to them when ready.
 *
 * Copyright (C) 2015-2016  Yinghai Lu
 * Copyright (C)      2016  Kees Cook
 */

/*
 * Since we're dealing with identity mappings, physical and virtual
 * addresses are the same, so override these defines which are ultimately
 * used by the headers in misc.h.
 */
#define __pa(x)  ((unsigned long)(x))
#define __va(x)  ((void *)((unsigned long)(x)))

/* No PAGE_TABLE_ISOLATION support needed either: */
#undef CONFIG_PAGE_TABLE_ISOLATION

#include "misc.h"

/* These actually do the work of building the kernel identity maps. */
#include <asm/init.h>
#include <asm/pgtable.h>
/* Use the static base for this part of the boot process */
#undef __PAGE_OFFSET
#define __PAGE_OFFSET __PAGE_OFFSET_BASE
#include "../../mm/ident_map.c"

/* Used by pgtable.h asm code to force instruction serialization. */
unsigned long __force_order;

/* Used to track our page table allocation area. */
struct alloc_pgt_data {
	unsigned char *pgt_buf;
	unsigned long pgt_buf_size;
	unsigned long pgt_buf_offset;
};

/*
 * Allocates space for a page table entry, using struct alloc_pgt_data
 * above. Besides the local callers, this is used as the allocation
 * callback in mapping_info below.
 */
static void *alloc_pgt_page(void *context)
{
	struct alloc_pgt_data *pages = (struct alloc_pgt_data *)context;
	unsigned char *entry;

	/* Validate there is space available for a new page. */
	if (pages->pgt_buf_offset >= pages->pgt_buf_size) {
		debug_putstr("out of pgt_buf in " __FILE__ "!?\n");
		debug_putaddr(pages->pgt_buf_offset);
		debug_putaddr(pages->pgt_buf_size);
		return NULL;
	}

	entry = pages->pgt_buf + pages->pgt_buf_offset;
	pages->pgt_buf_offset += PAGE_SIZE;

	return entry;
}

/* Used to track our allocated page tables. */
static struct alloc_pgt_data pgt_data;

/* The top level page table entry pointer. */
static unsigned long top_level_pgt;

phys_addr_t physical_mask = (1ULL << __PHYSICAL_MASK_SHIFT) - 1;

/*
 * Mapping information structure passed to kernel_ident_mapping_init().
 * Due to relocation, pointers must be assigned at run time not build time.
 */
static struct x86_mapping_info mapping_info;

/* Locates and clears a region for a new top level page table. */
void initialize_identity_maps(void)
{
	/* If running as an SEV guest, the encryption mask is required. */
	set_sev_encryption_mask();

	/* Exclude the encryption mask from __PHYSICAL_MASK */
	physical_mask &= ~sme_me_mask;

	/* Init mapping_info with run-time function/buffer pointers. */
	mapping_info.alloc_pgt_page = alloc_pgt_page;
	mapping_info.context = &pgt_data;
	mapping_info.page_flag = __PAGE_KERNEL_LARGE_EXEC | sme_me_mask;
	mapping_info.kernpg_flag = _KERNPG_TABLE;

	/*
	 * It should be impossible for this not to already be true,
	 * but since calling this a second time would rewind the other
	 * counters, let's just make sure this is reset too.
	 */
	pgt_data.pgt_buf_offset = 0;

	/*
	 * If we came here via startup_32(), cr3 will be _pgtable already
	 * and we must append to the existing area instead of entirely
	 * overwriting it.
	 *
	 * With 5-level paging, we use '_pgtable' to allocate the p4d page table,
	 * the top-level page table is allocated separately.
	 *
	 * p4d_offset(top_level_pgt, 0) would cover both the 4- and 5-level
	 * cases. On 4-level paging it's equal to 'top_level_pgt'.
	 */
	top_level_pgt = read_cr3_pa();
	if (p4d_offset((pgd_t *)top_level_pgt, 0) == (p4d_t *)_pgtable) {
		debug_putstr("booted via startup_32()\n");
		pgt_data.pgt_buf = _pgtable + BOOT_INIT_PGT_SIZE;
		pgt_data.pgt_buf_size = BOOT_PGT_SIZE - BOOT_INIT_PGT_SIZE;
		memset(pgt_data.pgt_buf, 0, pgt_data.pgt_buf_size);
	} else {
		debug_putstr("booted via startup_64()\n");
		pgt_data.pgt_buf = _pgtable;
		pgt_data.pgt_buf_size = BOOT_PGT_SIZE;
		memset(pgt_data.pgt_buf, 0, pgt_data.pgt_buf_size);
		top_level_pgt = (unsigned long)alloc_pgt_page(&pgt_data);
	}
}

/*
 * Adds the specified range to what will become the new identity mappings.
 * Once all ranges have been added, the new mapping is activated by calling
 * finalize_identity_maps() below.
 */
void add_identity_map(unsigned long start, unsigned long size)
{
	unsigned long end = start + size;

	/* Align boundary to 2M. */
	start = round_down(start, PMD_SIZE);
	end = round_up(end, PMD_SIZE);
	if (start >= end)
		return;

	/* Build the mapping. */
	kernel_ident_mapping_init(&mapping_info, (pgd_t *)top_level_pgt,
				  start, end);
}

/*
 * This switches the page tables to the new level4 that has been built
 * via calls to add_identity_map() above. If booted via startup_32(),
 * this is effectively a no-op.
 */
void finalize_identity_maps(void)
{
	write_cr3(top_level_pgt);
}

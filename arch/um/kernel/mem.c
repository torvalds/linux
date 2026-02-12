// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <as-layout.h>
#include <init.h>
#include <kern.h>
#include <kern_util.h>
#include <mem_user.h>
#include <os.h>
#include <um_malloc.h>
#include <linux/sched/task.h>
#include <linux/kasan.h>

#ifdef CONFIG_KASAN
void __init kasan_init(void)
{
	/*
	 * kasan_map_memory will map all of the required address space and
	 * the host machine will allocate physical memory as necessary.
	 */
	kasan_map_memory((void *)KASAN_SHADOW_START, KASAN_SHADOW_SIZE);
	init_task.kasan_depth = 0;
	/*
	 * Since kasan_init() is called before main(),
	 * KASAN is initialized but the enablement is deferred after
	 * jump_label_init(). See arch_mm_preinit().
	 */
}

static void (*kasan_init_ptr)(void)
__section(".kasan_init") __used
= kasan_init;
#endif

/* allocated in paging_init, zeroed in mem_init, and unchanged thereafter */
unsigned long *empty_zero_page = NULL;
EXPORT_SYMBOL(empty_zero_page);

/*
 * Initialized during boot, and readonly for initializing page tables
 * afterwards
 */
pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* Initialized at boot time, and readonly after that */
int kmalloc_ok = 0;

/* Used during early boot */
static unsigned long brk_end;

void __init arch_mm_preinit(void)
{
	/* Safe to call after jump_label_init(). Enables KASAN. */
	kasan_init_generic();

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* Map in the area just after the brk now that kmalloc is about
	 * to be turned on.
	 */
	brk_end = PAGE_ALIGN((unsigned long) sbrk(0));
	map_memory(brk_end, __pa(brk_end), uml_reserved - brk_end, 1, 1, 0);
	memblock_free((void *)brk_end, uml_reserved - brk_end);
	uml_reserved = brk_end;
	min_low_pfn = PFN_UP(__pa(uml_reserved));
	max_pfn = max_low_pfn;
}

void __init mem_init(void)
{
	kmalloc_ok = 1;
}

void __init arch_zone_limits_init(unsigned long *max_zone_pfns)
{
	max_zone_pfns[ZONE_NORMAL] = high_physmem >> PAGE_SHIFT;
}

void __init paging_init(void)
{
	empty_zero_page = (unsigned long *) memblock_alloc_low(PAGE_SIZE,
							       PAGE_SIZE);
	if (!empty_zero_page)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
}

/*
 * This can't do anything because nothing in the kernel image can be freed
 * since it's not in kernel physical memory.
 */

void free_initmem(void)
{
}

/* Allocate and free page tables. */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = __pgd_alloc(mm, 0);

	if (pgd)
		memcpy(pgd + USER_PTRS_PER_PGD,
		       swapper_pg_dir + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));

	return pgd;
}

void *uml_kmalloc(int size, int flags)
{
	return kmalloc(size, flags);
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY,
	[VM_EXEC]					= PAGE_READONLY,
	[VM_EXEC | VM_READ]				= PAGE_READONLY,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED
};
DECLARE_VM_GET_PAGE_PROT

void mark_rodata_ro(void)
{
	unsigned long rodata_start = PFN_ALIGN(__start_rodata);
	unsigned long rodata_end = PFN_ALIGN(__end_rodata);

	os_protect_memory((void *)rodata_start, rodata_end - rodata_start, 1, 0, 0);
}

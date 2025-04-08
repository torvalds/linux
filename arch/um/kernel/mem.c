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

#ifdef CONFIG_KASAN
int kasan_um_is_ready;
void kasan_init(void)
{
	/*
	 * kasan_map_memory will map all of the required address space and
	 * the host machine will allocate physical memory as necessary.
	 */
	kasan_map_memory((void *)KASAN_SHADOW_START, KASAN_SHADOW_SIZE);
	init_task.kasan_depth = 0;
	kasan_um_is_ready = true;
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
	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* Map in the area just after the brk now that kmalloc is about
	 * to be turned on.
	 */
	brk_end = (unsigned long) UML_ROUND_UP(sbrk(0));
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

#if IS_ENABLED(CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA)
/*
 * Create a page table and place a pointer to it in a middle page
 * directory entry.
 */
static void __init one_page_table_init(pmd_t *pmd)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = (pte_t *) memblock_alloc_low(PAGE_SIZE,
							  PAGE_SIZE);
		if (!pte)
			panic("%s: Failed to allocate %lu bytes align=%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);

		set_pmd(pmd, __pmd(_KERNPG_TABLE +
					   (unsigned long) __pa(pte)));
		BUG_ON(pte != pte_offset_kernel(pmd, 0));
	}
}

static void __init one_md_table_init(pud_t *pud)
{
#if CONFIG_PGTABLE_LEVELS > 2
	pmd_t *pmd_table = (pmd_t *) memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
	if (!pmd_table)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	set_pud(pud, __pud(_KERNPG_TABLE + (unsigned long) __pa(pmd_table)));
	BUG_ON(pmd_table != pmd_offset(pud, 0));
#endif
}

static void __init one_ud_table_init(p4d_t *p4d)
{
#if CONFIG_PGTABLE_LEVELS > 3
	pud_t *pud_table = (pud_t *) memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
	if (!pud_table)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	set_p4d(p4d, __p4d(_KERNPG_TABLE + (unsigned long) __pa(pud_table)));
	BUG_ON(pud_table != pud_offset(p4d, 0));
#endif
}

static void __init fixrange_init(unsigned long start, unsigned long end,
				 pgd_t *pgd_base)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	int i, j;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr < end); pgd++, i++) {
		p4d = p4d_offset(pgd, vaddr);
		if (p4d_none(*p4d))
			one_ud_table_init(p4d);
		pud = pud_offset(p4d, vaddr);
		if (pud_none(*pud))
			one_md_table_init(pud);
		pmd = pmd_offset(pud, vaddr);
		for (; (j < PTRS_PER_PMD) && (vaddr < end); pmd++, j++) {
			one_page_table_init(pmd);
			vaddr += PMD_SIZE;
		}
		j = 0;
	}
}

static void __init fixaddr_user_init( void)
{
	long size = FIXADDR_USER_END - FIXADDR_USER_START;
	pte_t *pte;
	phys_t p;
	unsigned long v, vaddr = FIXADDR_USER_START;

	if (!size)
		return;

	fixrange_init( FIXADDR_USER_START, FIXADDR_USER_END, swapper_pg_dir);
	v = (unsigned long) memblock_alloc_low(size, PAGE_SIZE);
	if (!v)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, size, PAGE_SIZE);

	memcpy((void *) v , (void *) FIXADDR_USER_START, size);
	p = __pa(v);
	for ( ; size > 0; size -= PAGE_SIZE, vaddr += PAGE_SIZE,
		      p += PAGE_SIZE) {
		pte = virt_to_kpte(vaddr);
		pte_set_val(*pte, p, PAGE_READONLY);
	}
}
#endif

void __init paging_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

	empty_zero_page = (unsigned long *) memblock_alloc_low(PAGE_SIZE,
							       PAGE_SIZE);
	if (!empty_zero_page)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	max_zone_pfn[ZONE_NORMAL] = end_iomem >> PAGE_SHIFT;
	free_area_init(max_zone_pfn);

#if IS_ENABLED(CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA)
	fixaddr_user_init();
#endif
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

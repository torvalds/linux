/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on arch/m68k/mm/init.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/execmem.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/cpuinfo.h>
#include <asm/processor.h>

pgd_t *pgd_current;

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses of available kernel virtual memory.
 */
void __init paging_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

	pagetable_init();
	pgd_current = swapper_pg_dir;

	max_zone_pfn[ZONE_NORMAL] = max_low_pfn;

	/* pass the memory from the bootmem allocator to the main allocator */
	free_area_init(max_zone_pfn);

	flush_dcache_range((unsigned long)empty_zero_page,
			(unsigned long)empty_zero_page + PAGE_SIZE);
}

void __init mem_init(void)
{
	unsigned long end_mem   = memory_end; /* this must not include
						kernel stack at top */

	end_mem &= PAGE_MASK;
	high_memory = __va(end_mem);

	/* this will put all memory onto the freelists */
	memblock_free_all();
}

void __init mmu_init(void)
{
	flush_tlb_all();
}

pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
pte_t invalid_pte_table[PTRS_PER_PTE] __aligned(PAGE_SIZE);
static struct page *kuser_page[1];
static struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.pages = kuser_page,
};

static int alloc_kuser_page(void)
{
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;
	unsigned long vpage;

	vpage = get_zeroed_page(GFP_ATOMIC);
	if (!vpage)
		return -ENOMEM;

	/* Copy kuser helpers */
	memcpy((void *)vpage, __kuser_helper_start, kuser_sz);

	flush_icache_range(vpage, vpage + KUSER_SIZE);
	kuser_page[0] = virt_to_page(vpage);

	return 0;
}
arch_initcall(alloc_kuser_page);

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	mmap_write_lock(mm);

	/* Map kuser helpers to user space address */
	vma = _install_special_mapping(mm, KUSER_BASE, KUSER_SIZE,
				      VM_READ | VM_EXEC | VM_MAYREAD |
				      VM_MAYEXEC, &vdso_mapping);

	mmap_write_unlock(mm);

	return IS_ERR(vma) ? PTR_ERR(vma) : 0;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	return (vma->vm_start == KUSER_BASE) ? "[kuser]" : NULL;
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= MKP(0, 0, 0),
	[VM_READ]					= MKP(0, 0, 1),
	[VM_WRITE]					= MKP(0, 0, 0),
	[VM_WRITE | VM_READ]				= MKP(0, 0, 1),
	[VM_EXEC]					= MKP(1, 0, 0),
	[VM_EXEC | VM_READ]				= MKP(1, 0, 1),
	[VM_EXEC | VM_WRITE]				= MKP(1, 0, 0),
	[VM_EXEC | VM_WRITE | VM_READ]			= MKP(1, 0, 1),
	[VM_SHARED]					= MKP(0, 0, 0),
	[VM_SHARED | VM_READ]				= MKP(0, 0, 1),
	[VM_SHARED | VM_WRITE]				= MKP(0, 1, 0),
	[VM_SHARED | VM_WRITE | VM_READ]		= MKP(0, 1, 1),
	[VM_SHARED | VM_EXEC]				= MKP(1, 0, 0),
	[VM_SHARED | VM_EXEC | VM_READ]			= MKP(1, 0, 1),
	[VM_SHARED | VM_EXEC | VM_WRITE]		= MKP(1, 1, 0),
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= MKP(1, 1, 1)
};
DECLARE_VM_GET_PAGE_PROT

#ifdef CONFIG_EXECMEM
static struct execmem_info execmem_info __ro_after_init;

struct execmem_info __init *execmem_arch_setup(void)
{
	execmem_info = (struct execmem_info){
		.ranges = {
			[EXECMEM_DEFAULT] = {
				.start	= MODULES_VADDR,
				.end	= MODULES_END,
				.pgprot	= PAGE_KERNEL_EXEC,
				.alignment = 1,
			},
		},
	};

	return &execmem_info;
}
#endif /* CONFIG_EXECMEM */

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

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
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
	unsigned long zones_size[MAX_NR_ZONES];

	memset(zones_size, 0, sizeof(zones_size));

	pagetable_init();
	pgd_current = swapper_pg_dir;

	zones_size[ZONE_NORMAL] = max_mapnr;

	/* pass the memory from the bootmem allocator to the main allocator */
	free_area_init(zones_size);

	flush_dcache_range((unsigned long)empty_zero_page,
			(unsigned long)empty_zero_page + PAGE_SIZE);
}

void __init mem_init(void)
{
	unsigned long end_mem   = memory_end; /* this must not include
						kernel stack at top */

	pr_debug("mem_init: start=%lx, end=%lx\n", memory_start, memory_end);

	end_mem &= PAGE_MASK;
	high_memory = __va(end_mem);

	/* this will put all memory onto the freelists */
	memblock_free_all();
	mem_init_print_info(NULL);
}

void __init mmu_init(void)
{
	flush_tlb_all();
}

void __ref free_initmem(void)
{
	free_initmem_default(-1);
}

#define __page_aligned(order) __aligned(PAGE_SIZE << (order))
pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned(PGD_ORDER);
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned(PTE_ORDER);
static struct page *kuser_page[1];

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
	int ret;

	down_write(&mm->mmap_sem);

	/* Map kuser helpers to user space address */
	ret = install_special_mapping(mm, KUSER_BASE, KUSER_SIZE,
				      VM_READ | VM_EXEC | VM_MAYREAD |
				      VM_MAYEXEC, kuser_page);

	up_write(&mm->mmap_sem);

	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	return (vma->vm_start == KUSER_BASE) ? "[kuser]" : NULL;
}

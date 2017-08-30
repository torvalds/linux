/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2014 Intel Corporation; author: H. Peter Anvin
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms and conditions of the GNU General Public License,
 *   version 2, as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *   more details.
 *
 * ----------------------------------------------------------------------- */

/*
 * The IRET instruction, when returning to a 16-bit segment, only
 * restores the bottom 16 bits of the user space stack pointer.  This
 * causes some 16-bit software to break, but it also leaks kernel state
 * to user space.
 *
 * This works around this by creating percpu "ministacks", each of which
 * is mapped 2^16 times 64K apart.  When we detect that the return SS is
 * on the LDT, we copy the IRET frame to the ministack and use the
 * relevant alias to return to userspace.  The ministacks are mapped
 * readonly, so if the IRET fault we promote #GP to #DF which is an IST
 * vector and thus has its own stack; we then do the fixup in the #DF
 * handler.
 *
 * This file sets up the ministacks and the related page tables.  The
 * actual ministack invocation is in entry_64.S.
 */

#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/random.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/setup.h>
#include <asm/espfix.h>
#include <asm/kaiser.h>

/*
 * Note: we only need 6*8 = 48 bytes for the espfix stack, but round
 * it up to a cache line to avoid unnecessary sharing.
 */
#define ESPFIX_STACK_SIZE	(8*8UL)
#define ESPFIX_STACKS_PER_PAGE	(PAGE_SIZE/ESPFIX_STACK_SIZE)

/* There is address space for how many espfix pages? */
#define ESPFIX_PAGE_SPACE	(1UL << (PGDIR_SHIFT-PAGE_SHIFT-16))

#define ESPFIX_MAX_CPUS		(ESPFIX_STACKS_PER_PAGE * ESPFIX_PAGE_SPACE)
#if CONFIG_NR_CPUS > ESPFIX_MAX_CPUS
# error "Need more than one PGD for the ESPFIX hack"
#endif

#define PGALLOC_GFP (GFP_KERNEL | __GFP_NOTRACK | __GFP_REPEAT | __GFP_ZERO)

/* This contains the *bottom* address of the espfix stack */
DEFINE_PER_CPU_READ_MOSTLY(unsigned long, espfix_stack);
DEFINE_PER_CPU_READ_MOSTLY(unsigned long, espfix_waddr);

/* Initialization mutex - should this be a spinlock? */
static DEFINE_MUTEX(espfix_init_mutex);

/* Page allocation bitmap - each page serves ESPFIX_STACKS_PER_PAGE CPUs */
#define ESPFIX_MAX_PAGES  DIV_ROUND_UP(CONFIG_NR_CPUS, ESPFIX_STACKS_PER_PAGE)
static void *espfix_pages[ESPFIX_MAX_PAGES];

static __page_aligned_bss pud_t espfix_pud_page[PTRS_PER_PUD]
	__aligned(PAGE_SIZE);

static unsigned int page_random, slot_random;

/*
 * This returns the bottom address of the espfix stack for a specific CPU.
 * The math allows for a non-power-of-two ESPFIX_STACK_SIZE, in which case
 * we have to account for some amount of padding at the end of each page.
 */
static inline unsigned long espfix_base_addr(unsigned int cpu)
{
	unsigned long page, slot;
	unsigned long addr;

	page = (cpu / ESPFIX_STACKS_PER_PAGE) ^ page_random;
	slot = (cpu + slot_random) % ESPFIX_STACKS_PER_PAGE;
	addr = (page << PAGE_SHIFT) + (slot * ESPFIX_STACK_SIZE);
	addr = (addr & 0xffffUL) | ((addr & ~0xffffUL) << 16);
	addr += ESPFIX_BASE_ADDR;
	return addr;
}

#define PTE_STRIDE        (65536/PAGE_SIZE)
#define ESPFIX_PTE_CLONES (PTRS_PER_PTE/PTE_STRIDE)
#define ESPFIX_PMD_CLONES PTRS_PER_PMD
#define ESPFIX_PUD_CLONES (65536/(ESPFIX_PTE_CLONES*ESPFIX_PMD_CLONES))

#define PGTABLE_PROT	  ((_KERNPG_TABLE & ~_PAGE_RW) | _PAGE_NX)

static void init_espfix_random(void)
{
	unsigned long rand;

	/*
	 * This is run before the entropy pools are initialized,
	 * but this is hopefully better than nothing.
	 */
	if (!arch_get_random_long(&rand)) {
		/* The constant is an arbitrary large prime */
		rand = rdtsc();
		rand *= 0xc345c6b72fd16123UL;
	}

	slot_random = rand % ESPFIX_STACKS_PER_PAGE;
	page_random = (rand / ESPFIX_STACKS_PER_PAGE)
		& (ESPFIX_PAGE_SPACE - 1);
}

void __init init_espfix_bsp(void)
{
	pgd_t *pgd_p;

	/* Install the espfix pud into the kernel page directory */
	pgd_p = &init_level4_pgt[pgd_index(ESPFIX_BASE_ADDR)];
	pgd_populate(&init_mm, pgd_p, (pud_t *)espfix_pud_page);
	/*
	 * Just copy the top-level PGD that is mapping the espfix
	 * area to ensure it is mapped into the shadow user page
	 * tables.
	 */
	if (IS_ENABLED(CONFIG_KAISER))
		set_pgd(native_get_shadow_pgd(pgd_p),
			__pgd(_KERNPG_TABLE | __pa((pud_t *)espfix_pud_page)));

	/* Randomize the locations */
	init_espfix_random();

	/* The rest is the same as for any other processor */
	init_espfix_ap(0);
}

void init_espfix_ap(int cpu)
{
	unsigned int page;
	unsigned long addr;
	pud_t pud, *pud_p;
	pmd_t pmd, *pmd_p;
	pte_t pte, *pte_p;
	int n, node;
	void *stack_page;
	pteval_t ptemask;

	/* We only have to do this once... */
	if (likely(per_cpu(espfix_stack, cpu)))
		return;		/* Already initialized */

	addr = espfix_base_addr(cpu);
	page = cpu/ESPFIX_STACKS_PER_PAGE;

	/* Did another CPU already set this up? */
	stack_page = ACCESS_ONCE(espfix_pages[page]);
	if (likely(stack_page))
		goto done;

	mutex_lock(&espfix_init_mutex);

	/* Did we race on the lock? */
	stack_page = ACCESS_ONCE(espfix_pages[page]);
	if (stack_page)
		goto unlock_done;

	node = cpu_to_node(cpu);
	ptemask = __supported_pte_mask;

	pud_p = &espfix_pud_page[pud_index(addr)];
	pud = *pud_p;
	if (!pud_present(pud)) {
		struct page *page = alloc_pages_node(node, PGALLOC_GFP, 0);

		pmd_p = (pmd_t *)page_address(page);
		pud = __pud(__pa(pmd_p) | (PGTABLE_PROT & ptemask));
		paravirt_alloc_pmd(&init_mm, __pa(pmd_p) >> PAGE_SHIFT);
		for (n = 0; n < ESPFIX_PUD_CLONES; n++)
			set_pud(&pud_p[n], pud);
	}

	pmd_p = pmd_offset(&pud, addr);
	pmd = *pmd_p;
	if (!pmd_present(pmd)) {
		struct page *page = alloc_pages_node(node, PGALLOC_GFP, 0);

		pte_p = (pte_t *)page_address(page);
		pmd = __pmd(__pa(pte_p) | (PGTABLE_PROT & ptemask));
		paravirt_alloc_pte(&init_mm, __pa(pte_p) >> PAGE_SHIFT);
		for (n = 0; n < ESPFIX_PMD_CLONES; n++)
			set_pmd(&pmd_p[n], pmd);
	}

	pte_p = pte_offset_kernel(&pmd, addr);
	stack_page = page_address(alloc_pages_node(node, GFP_KERNEL, 0));
	pte = __pte(__pa(stack_page) | (__PAGE_KERNEL_RO & ptemask));
	for (n = 0; n < ESPFIX_PTE_CLONES; n++)
		set_pte(&pte_p[n*PTE_STRIDE], pte);

	/* Job is done for this CPU and any CPU which shares this page */
	ACCESS_ONCE(espfix_pages[page]) = stack_page;

unlock_done:
	mutex_unlock(&espfix_init_mutex);
done:
	per_cpu(espfix_stack, cpu) = addr;
	per_cpu(espfix_waddr, cpu) = (unsigned long)stack_page
				      + (addr & ~PAGE_MASK);
}

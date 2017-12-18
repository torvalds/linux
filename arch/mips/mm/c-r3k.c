// SPDX-License-Identifier: GPL-2.0
/*
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 *
 * with a lot of changes to make this thing work for R3000s
 * Tx39XX R4k style caches added. HK
 * Copyright (C) 1998, 1999, 2000 Harald Koerfgen
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 * Copyright (C) 2001, 2004, 2007  Maciej W. Rozycki
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/isadep.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

static unsigned long icache_size, dcache_size;		/* Size in bytes */
static unsigned long icache_lsize, dcache_lsize;	/* Size in bytes */

unsigned long r3k_cache_size(unsigned long ca_flags)
{
	unsigned long flags, status, dummy, size;
	volatile unsigned long *p;

	p = (volatile unsigned long *) KSEG0;

	flags = read_c0_status();

	/* isolate cache space */
	write_c0_status((ca_flags|flags)&~ST0_IEC);

	*p = 0xa5a55a5a;
	dummy = *p;
	status = read_c0_status();

	if (dummy != 0xa5a55a5a || (status & ST0_CM)) {
		size = 0;
	} else {
		for (size = 128; size <= 0x40000; size <<= 1)
			*(p + size) = 0;
		*p = -1;
		for (size = 128;
		     (size <= 0x40000) && (*(p + size) == 0);
		     size <<= 1)
			;
		if (size > 0x40000)
			size = 0;
	}

	write_c0_status(flags);

	return size * sizeof(*p);
}

unsigned long r3k_cache_lsize(unsigned long ca_flags)
{
	unsigned long flags, status, lsize, i;
	volatile unsigned long *p;

	p = (volatile unsigned long *) KSEG0;

	flags = read_c0_status();

	/* isolate cache space */
	write_c0_status((ca_flags|flags)&~ST0_IEC);

	for (i = 0; i < 128; i++)
		*(p + i) = 0;
	*(volatile unsigned char *)p = 0;
	for (lsize = 1; lsize < 128; lsize <<= 1) {
		*(p + lsize);
		status = read_c0_status();
		if (!(status & ST0_CM))
			break;
	}
	for (i = 0; i < 128; i += lsize)
		*(volatile unsigned char *)(p + i) = 0;

	write_c0_status(flags);

	return lsize * sizeof(*p);
}

static void r3k_probe_cache(void)
{
	dcache_size = r3k_cache_size(ST0_ISC);
	if (dcache_size)
		dcache_lsize = r3k_cache_lsize(ST0_ISC);

	icache_size = r3k_cache_size(ST0_ISC|ST0_SWC);
	if (icache_size)
		icache_lsize = r3k_cache_lsize(ST0_ISC|ST0_SWC);
}

static void r3k_flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long size, i, flags;
	volatile unsigned char *p;

	size = end - start;
	if (size > icache_size || KSEGX(start) != KSEG0) {
		start = KSEG0;
		size = icache_size;
	}
	p = (char *)start;

	flags = read_c0_status();

	/* isolate cache space */
	write_c0_status((ST0_ISC|ST0_SWC|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 0x080) {
		asm(	"sb\t$0, 0x000(%0)\n\t"
			"sb\t$0, 0x004(%0)\n\t"
			"sb\t$0, 0x008(%0)\n\t"
			"sb\t$0, 0x00c(%0)\n\t"
			"sb\t$0, 0x010(%0)\n\t"
			"sb\t$0, 0x014(%0)\n\t"
			"sb\t$0, 0x018(%0)\n\t"
			"sb\t$0, 0x01c(%0)\n\t"
			"sb\t$0, 0x020(%0)\n\t"
			"sb\t$0, 0x024(%0)\n\t"
			"sb\t$0, 0x028(%0)\n\t"
			"sb\t$0, 0x02c(%0)\n\t"
			"sb\t$0, 0x030(%0)\n\t"
			"sb\t$0, 0x034(%0)\n\t"
			"sb\t$0, 0x038(%0)\n\t"
			"sb\t$0, 0x03c(%0)\n\t"
			"sb\t$0, 0x040(%0)\n\t"
			"sb\t$0, 0x044(%0)\n\t"
			"sb\t$0, 0x048(%0)\n\t"
			"sb\t$0, 0x04c(%0)\n\t"
			"sb\t$0, 0x050(%0)\n\t"
			"sb\t$0, 0x054(%0)\n\t"
			"sb\t$0, 0x058(%0)\n\t"
			"sb\t$0, 0x05c(%0)\n\t"
			"sb\t$0, 0x060(%0)\n\t"
			"sb\t$0, 0x064(%0)\n\t"
			"sb\t$0, 0x068(%0)\n\t"
			"sb\t$0, 0x06c(%0)\n\t"
			"sb\t$0, 0x070(%0)\n\t"
			"sb\t$0, 0x074(%0)\n\t"
			"sb\t$0, 0x078(%0)\n\t"
			"sb\t$0, 0x07c(%0)\n\t"
			: : "r" (p) );
		p += 0x080;
	}

	write_c0_status(flags);
}

static void r3k_flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long size, i, flags;
	volatile unsigned char *p;

	size = end - start;
	if (size > dcache_size || KSEGX(start) != KSEG0) {
		start = KSEG0;
		size = dcache_size;
	}
	p = (char *)start;

	flags = read_c0_status();

	/* isolate cache space */
	write_c0_status((ST0_ISC|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 0x080) {
		asm(	"sb\t$0, 0x000(%0)\n\t"
			"sb\t$0, 0x004(%0)\n\t"
			"sb\t$0, 0x008(%0)\n\t"
			"sb\t$0, 0x00c(%0)\n\t"
			"sb\t$0, 0x010(%0)\n\t"
			"sb\t$0, 0x014(%0)\n\t"
			"sb\t$0, 0x018(%0)\n\t"
			"sb\t$0, 0x01c(%0)\n\t"
			"sb\t$0, 0x020(%0)\n\t"
			"sb\t$0, 0x024(%0)\n\t"
			"sb\t$0, 0x028(%0)\n\t"
			"sb\t$0, 0x02c(%0)\n\t"
			"sb\t$0, 0x030(%0)\n\t"
			"sb\t$0, 0x034(%0)\n\t"
			"sb\t$0, 0x038(%0)\n\t"
			"sb\t$0, 0x03c(%0)\n\t"
			"sb\t$0, 0x040(%0)\n\t"
			"sb\t$0, 0x044(%0)\n\t"
			"sb\t$0, 0x048(%0)\n\t"
			"sb\t$0, 0x04c(%0)\n\t"
			"sb\t$0, 0x050(%0)\n\t"
			"sb\t$0, 0x054(%0)\n\t"
			"sb\t$0, 0x058(%0)\n\t"
			"sb\t$0, 0x05c(%0)\n\t"
			"sb\t$0, 0x060(%0)\n\t"
			"sb\t$0, 0x064(%0)\n\t"
			"sb\t$0, 0x068(%0)\n\t"
			"sb\t$0, 0x06c(%0)\n\t"
			"sb\t$0, 0x070(%0)\n\t"
			"sb\t$0, 0x074(%0)\n\t"
			"sb\t$0, 0x078(%0)\n\t"
			"sb\t$0, 0x07c(%0)\n\t"
			: : "r" (p) );
		p += 0x080;
	}

	write_c0_status(flags);
}

static inline void r3k_flush_cache_all(void)
{
}

static inline void r3k___flush_cache_all(void)
{
	r3k_flush_dcache_range(KSEG0, KSEG0 + dcache_size);
	r3k_flush_icache_range(KSEG0, KSEG0 + icache_size);
}

static void r3k_flush_cache_mm(struct mm_struct *mm)
{
}

static void r3k_flush_cache_range(struct vm_area_struct *vma,
				  unsigned long start, unsigned long end)
{
}

static void r3k_flush_cache_page(struct vm_area_struct *vma,
				 unsigned long addr, unsigned long pfn)
{
	unsigned long kaddr = KSEG0ADDR(pfn << PAGE_SHIFT);
	int exec = vma->vm_flags & VM_EXEC;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pr_debug("cpage[%08lx,%08lx]\n",
		 cpu_context(smp_processor_id(), mm), addr);

	/* No ASID => no such page in the cache.  */
	if (cpu_context(smp_processor_id(), mm) == 0)
		return;

	pgdp = pgd_offset(mm, addr);
	pudp = pud_offset(pgdp, addr);
	pmdp = pmd_offset(pudp, addr);
	ptep = pte_offset(pmdp, addr);

	/* Invalid => no such page in the cache.  */
	if (!(pte_val(*ptep) & _PAGE_PRESENT))
		return;

	r3k_flush_dcache_range(kaddr, kaddr + PAGE_SIZE);
	if (exec)
		r3k_flush_icache_range(kaddr, kaddr + PAGE_SIZE);
}

static void local_r3k_flush_data_cache_page(void *addr)
{
}

static void r3k_flush_data_cache_page(unsigned long addr)
{
}

static void r3k_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long flags;

	pr_debug("csigtramp[%08lx]\n", addr);

	flags = read_c0_status();

	write_c0_status(flags&~ST0_IEC);

	/* Fill the TLB to avoid an exception with caches isolated. */
	asm(	"lw\t$0, 0x000(%0)\n\t"
		"lw\t$0, 0x004(%0)\n\t"
		: : "r" (addr) );

	write_c0_status((ST0_ISC|ST0_SWC|flags)&~ST0_IEC);

	asm(	"sb\t$0, 0x000(%0)\n\t"
		"sb\t$0, 0x004(%0)\n\t"
		: : "r" (addr) );

	write_c0_status(flags);
}

static void r3k_flush_kernel_vmap_range(unsigned long vaddr, int size)
{
	BUG();
}

static void r3k_dma_cache_wback_inv(unsigned long start, unsigned long size)
{
	/* Catch bad driver code */
	BUG_ON(size == 0);

	iob();
	r3k_flush_dcache_range(start, start + size);
}

void r3k_cache_init(void)
{
	extern void build_clear_page(void);
	extern void build_copy_page(void);

	r3k_probe_cache();

	flush_cache_all = r3k_flush_cache_all;
	__flush_cache_all = r3k___flush_cache_all;
	flush_cache_mm = r3k_flush_cache_mm;
	flush_cache_range = r3k_flush_cache_range;
	flush_cache_page = r3k_flush_cache_page;
	flush_icache_range = r3k_flush_icache_range;
	local_flush_icache_range = r3k_flush_icache_range;
	__flush_icache_user_range = r3k_flush_icache_range;
	__local_flush_icache_user_range = r3k_flush_icache_range;

	__flush_kernel_vmap_range = r3k_flush_kernel_vmap_range;

	flush_cache_sigtramp = r3k_flush_cache_sigtramp;
	local_flush_data_cache_page = local_r3k_flush_data_cache_page;
	flush_data_cache_page = r3k_flush_data_cache_page;

	_dma_cache_wback_inv = r3k_dma_cache_wback_inv;
	_dma_cache_wback = r3k_dma_cache_wback_inv;
	_dma_cache_inv = r3k_dma_cache_wback_inv;

	printk("Primary instruction cache %ldkB, linesize %ld bytes.\n",
		icache_size >> 10, icache_lsize);
	printk("Primary data cache %ldkB, linesize %ld bytes.\n",
		dcache_size >> 10, dcache_lsize);

	build_clear_page();
	build_copy_page();
}

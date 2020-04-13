// SPDX-License-Identifier: GPL-2.0
/*
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995,2002 Pete Zaitcev (zaitcev@yahoo.com)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1999,2000 Anton Blanchard (anton@samba.org)
 */

#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/kdebug.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io-unit.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/bitext.h>
#include <asm/vaddrs.h>
#include <asm/cache.h>
#include <asm/traps.h>
#include <asm/oplib.h>
#include <asm/mbus.h>
#include <asm/page.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/io.h>

/* Now the cpu specific definitions. */
#include <asm/turbosparc.h>
#include <asm/tsunami.h>
#include <asm/viking.h>
#include <asm/swift.h>
#include <asm/leon.h>
#include <asm/mxcc.h>
#include <asm/ross.h>

#include "mm_32.h"

enum mbus_module srmmu_modtype;
static unsigned int hwbug_bitmask;
int vac_cache_size;
EXPORT_SYMBOL(vac_cache_size);
int vac_line_size;

extern struct resource sparc_iomap;

extern unsigned long last_valid_pfn;

static pgd_t *srmmu_swapper_pg_dir;

const struct sparc32_cachetlb_ops *sparc32_cachetlb_ops;
EXPORT_SYMBOL(sparc32_cachetlb_ops);

#ifdef CONFIG_SMP
const struct sparc32_cachetlb_ops *local_ops;

#define FLUSH_BEGIN(mm)
#define FLUSH_END
#else
#define FLUSH_BEGIN(mm) if ((mm)->context != NO_CONTEXT) {
#define FLUSH_END	}
#endif

int flush_page_for_dma_global = 1;

char *srmmu_name;

ctxd_t *srmmu_ctx_table_phys;
static ctxd_t *srmmu_context_table;

int viking_mxcc_present;
static DEFINE_SPINLOCK(srmmu_context_spinlock);

static int is_hypersparc;

static int srmmu_cache_pagetables;

/* these will be initialized in srmmu_nocache_calcsize() */
static unsigned long srmmu_nocache_size;
static unsigned long srmmu_nocache_end;

/* 1 bit <=> 256 bytes of nocache <=> 64 PTEs */
#define SRMMU_NOCACHE_BITMAP_SHIFT (PAGE_SHIFT - 4)

/* The context table is a nocache user with the biggest alignment needs. */
#define SRMMU_NOCACHE_ALIGN_MAX (sizeof(ctxd_t)*SRMMU_MAX_CONTEXTS)

void *srmmu_nocache_pool;
static struct bit_map srmmu_nocache_map;

static inline int srmmu_pmd_none(pmd_t pmd)
{ return !(pmd_val(pmd) & 0xFFFFFFF); }

/* XXX should we hyper_flush_whole_icache here - Anton */
static inline void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{
	pte_t pte;

	pte = __pte((SRMMU_ET_PTD | (__nocache_pa(pgdp) >> 4)));
	set_pte((pte_t *)ctxp, pte);
}

/*
 * Locations of MSI Registers.
 */
#define MSI_MBUS_ARBEN	0xe0001008	/* MBus Arbiter Enable register */

/*
 * Useful bits in the MSI Registers.
 */
#define MSI_ASYNC_MODE  0x80000000	/* Operate the MSI asynchronously */

static void msi_set_sync(void)
{
	__asm__ __volatile__ ("lda [%0] %1, %%g3\n\t"
			      "andn %%g3, %2, %%g3\n\t"
			      "sta %%g3, [%0] %1\n\t" : :
			      "r" (MSI_MBUS_ARBEN),
			      "i" (ASI_M_CTL), "r" (MSI_ASYNC_MODE) : "g3");
}

void pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	unsigned long ptp;	/* Physical address, shifted right by 4 */
	int i;

	ptp = __nocache_pa(ptep) >> 4;
	for (i = 0; i < PTRS_PER_PTE/SRMMU_REAL_PTRS_PER_PTE; i++) {
		set_pte((pte_t *)&pmdp->pmdv[i], __pte(SRMMU_ET_PTD | ptp));
		ptp += (SRMMU_REAL_PTRS_PER_PTE * sizeof(pte_t) >> 4);
	}
}

void pmd_populate(struct mm_struct *mm, pmd_t *pmdp, struct page *ptep)
{
	unsigned long ptp;	/* Physical address, shifted right by 4 */
	int i;

	ptp = page_to_pfn(ptep) << (PAGE_SHIFT-4);	/* watch for overflow */
	for (i = 0; i < PTRS_PER_PTE/SRMMU_REAL_PTRS_PER_PTE; i++) {
		set_pte((pte_t *)&pmdp->pmdv[i], __pte(SRMMU_ET_PTD | ptp));
		ptp += (SRMMU_REAL_PTRS_PER_PTE * sizeof(pte_t) >> 4);
	}
}

/* Find an entry in the third-level page table.. */
pte_t *pte_offset_kernel(pmd_t *dir, unsigned long address)
{
	void *pte;

	pte = __nocache_va((dir->pmdv[0] & SRMMU_PTD_PMASK) << 4);
	return (pte_t *) pte +
	    ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * size: bytes to allocate in the nocache area.
 * align: bytes, number to align at.
 * Returns the virtual address of the allocated area.
 */
static void *__srmmu_get_nocache(int size, int align)
{
	int offset;
	unsigned long addr;

	if (size < SRMMU_NOCACHE_BITMAP_SHIFT) {
		printk(KERN_ERR "Size 0x%x too small for nocache request\n",
		       size);
		size = SRMMU_NOCACHE_BITMAP_SHIFT;
	}
	if (size & (SRMMU_NOCACHE_BITMAP_SHIFT - 1)) {
		printk(KERN_ERR "Size 0x%x unaligned int nocache request\n",
		       size);
		size += SRMMU_NOCACHE_BITMAP_SHIFT - 1;
	}
	BUG_ON(align > SRMMU_NOCACHE_ALIGN_MAX);

	offset = bit_map_string_get(&srmmu_nocache_map,
				    size >> SRMMU_NOCACHE_BITMAP_SHIFT,
				    align >> SRMMU_NOCACHE_BITMAP_SHIFT);
	if (offset == -1) {
		printk(KERN_ERR "srmmu: out of nocache %d: %d/%d\n",
		       size, (int) srmmu_nocache_size,
		       srmmu_nocache_map.used << SRMMU_NOCACHE_BITMAP_SHIFT);
		return NULL;
	}

	addr = SRMMU_NOCACHE_VADDR + (offset << SRMMU_NOCACHE_BITMAP_SHIFT);
	return (void *)addr;
}

void *srmmu_get_nocache(int size, int align)
{
	void *tmp;

	tmp = __srmmu_get_nocache(size, align);

	if (tmp)
		memset(tmp, 0, size);

	return tmp;
}

void srmmu_free_nocache(void *addr, int size)
{
	unsigned long vaddr;
	int offset;

	vaddr = (unsigned long)addr;
	if (vaddr < SRMMU_NOCACHE_VADDR) {
		printk("Vaddr %lx is smaller than nocache base 0x%lx\n",
		    vaddr, (unsigned long)SRMMU_NOCACHE_VADDR);
		BUG();
	}
	if (vaddr + size > srmmu_nocache_end) {
		printk("Vaddr %lx is bigger than nocache end 0x%lx\n",
		    vaddr, srmmu_nocache_end);
		BUG();
	}
	if (!is_power_of_2(size)) {
		printk("Size 0x%x is not a power of 2\n", size);
		BUG();
	}
	if (size < SRMMU_NOCACHE_BITMAP_SHIFT) {
		printk("Size 0x%x is too small\n", size);
		BUG();
	}
	if (vaddr & (size - 1)) {
		printk("Vaddr %lx is not aligned to size 0x%x\n", vaddr, size);
		BUG();
	}

	offset = (vaddr - SRMMU_NOCACHE_VADDR) >> SRMMU_NOCACHE_BITMAP_SHIFT;
	size = size >> SRMMU_NOCACHE_BITMAP_SHIFT;

	bit_map_clear(&srmmu_nocache_map, offset, size);
}

static void srmmu_early_allocate_ptable_skeleton(unsigned long start,
						 unsigned long end);

/* Return how much physical memory we have.  */
static unsigned long __init probe_memory(void)
{
	unsigned long total = 0;
	int i;

	for (i = 0; sp_banks[i].num_bytes; i++)
		total += sp_banks[i].num_bytes;

	return total;
}

/*
 * Reserve nocache dynamically proportionally to the amount of
 * system RAM. -- Tomas Szepe <szepe@pinerecords.com>, June 2002
 */
static void __init srmmu_nocache_calcsize(void)
{
	unsigned long sysmemavail = probe_memory() / 1024;
	int srmmu_nocache_npages;

	srmmu_nocache_npages =
		sysmemavail / SRMMU_NOCACHE_ALCRATIO / 1024 * 256;

 /* P3 XXX The 4x overuse: corroborated by /proc/meminfo. */
	// if (srmmu_nocache_npages < 256) srmmu_nocache_npages = 256;
	if (srmmu_nocache_npages < SRMMU_MIN_NOCACHE_PAGES)
		srmmu_nocache_npages = SRMMU_MIN_NOCACHE_PAGES;

	/* anything above 1280 blows up */
	if (srmmu_nocache_npages > SRMMU_MAX_NOCACHE_PAGES)
		srmmu_nocache_npages = SRMMU_MAX_NOCACHE_PAGES;

	srmmu_nocache_size = srmmu_nocache_npages * PAGE_SIZE;
	srmmu_nocache_end = SRMMU_NOCACHE_VADDR + srmmu_nocache_size;
}

static void __init srmmu_nocache_init(void)
{
	void *srmmu_nocache_bitmap;
	unsigned int bitmap_bits;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long paddr, vaddr;
	unsigned long pteval;

	bitmap_bits = srmmu_nocache_size >> SRMMU_NOCACHE_BITMAP_SHIFT;

	srmmu_nocache_pool = memblock_alloc(srmmu_nocache_size,
					    SRMMU_NOCACHE_ALIGN_MAX);
	if (!srmmu_nocache_pool)
		panic("%s: Failed to allocate %lu bytes align=0x%x\n",
		      __func__, srmmu_nocache_size, SRMMU_NOCACHE_ALIGN_MAX);
	memset(srmmu_nocache_pool, 0, srmmu_nocache_size);

	srmmu_nocache_bitmap =
		memblock_alloc(BITS_TO_LONGS(bitmap_bits) * sizeof(long),
			       SMP_CACHE_BYTES);
	if (!srmmu_nocache_bitmap)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      BITS_TO_LONGS(bitmap_bits) * sizeof(long));
	bit_map_init(&srmmu_nocache_map, srmmu_nocache_bitmap, bitmap_bits);

	srmmu_swapper_pg_dir = __srmmu_get_nocache(SRMMU_PGD_TABLE_SIZE, SRMMU_PGD_TABLE_SIZE);
	memset(__nocache_fix(srmmu_swapper_pg_dir), 0, SRMMU_PGD_TABLE_SIZE);
	init_mm.pgd = srmmu_swapper_pg_dir;

	srmmu_early_allocate_ptable_skeleton(SRMMU_NOCACHE_VADDR, srmmu_nocache_end);

	paddr = __pa((unsigned long)srmmu_nocache_pool);
	vaddr = SRMMU_NOCACHE_VADDR;

	while (vaddr < srmmu_nocache_end) {
		pgd = pgd_offset_k(vaddr);
		p4d = p4d_offset(__nocache_fix(pgd), vaddr);
		pud = pud_offset(__nocache_fix(p4d), vaddr);
		pmd = pmd_offset(__nocache_fix(pgd), vaddr);
		pte = pte_offset_kernel(__nocache_fix(pmd), vaddr);

		pteval = ((paddr >> 4) | SRMMU_ET_PTE | SRMMU_PRIV);

		if (srmmu_cache_pagetables)
			pteval |= SRMMU_CACHE;

		set_pte(__nocache_fix(pte), __pte(pteval));

		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}

	flush_cache_all();
	flush_tlb_all();
}

pgd_t *get_pgd_fast(void)
{
	pgd_t *pgd = NULL;

	pgd = __srmmu_get_nocache(SRMMU_PGD_TABLE_SIZE, SRMMU_PGD_TABLE_SIZE);
	if (pgd) {
		pgd_t *init = pgd_offset_k(0);
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return pgd;
}

/*
 * Hardware needs alignment to 256 only, but we align to whole page size
 * to reduce fragmentation problems due to the buddy principle.
 * XXX Provide actual fragmentation statistics in /proc.
 *
 * Alignments up to the page size are the same for physical and virtual
 * addresses of the nocache area.
 */
pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	unsigned long pte;
	struct page *page;

	if ((pte = (unsigned long)pte_alloc_one_kernel(mm)) == 0)
		return NULL;
	page = pfn_to_page(__nocache_pa(pte) >> PAGE_SHIFT);
	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	return page;
}

void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	unsigned long p;

	pgtable_pte_page_dtor(pte);
	p = (unsigned long)page_address(pte);	/* Cached address (for test) */
	if (p == 0)
		BUG();
	p = page_to_pfn(pte) << PAGE_SHIFT;	/* Physical address */

	/* free non cached virtual address*/
	srmmu_free_nocache(__nocache_va(p), PTE_SIZE);
}

/* context handling - a dynamically sized pool is used */
#define NO_CONTEXT	-1

struct ctx_list {
	struct ctx_list *next;
	struct ctx_list *prev;
	unsigned int ctx_number;
	struct mm_struct *ctx_mm;
};

static struct ctx_list *ctx_list_pool;
static struct ctx_list ctx_free;
static struct ctx_list ctx_used;

/* At boot time we determine the number of contexts */
static int num_contexts;

static inline void remove_from_ctx_list(struct ctx_list *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline void add_to_ctx_list(struct ctx_list *head, struct ctx_list *entry)
{
	entry->next = head;
	(entry->prev = head->prev)->next = entry;
	head->prev = entry;
}
#define add_to_free_ctxlist(entry) add_to_ctx_list(&ctx_free, entry)
#define add_to_used_ctxlist(entry) add_to_ctx_list(&ctx_used, entry)


static inline void alloc_context(struct mm_struct *old_mm, struct mm_struct *mm)
{
	struct ctx_list *ctxp;

	ctxp = ctx_free.next;
	if (ctxp != &ctx_free) {
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		mm->context = ctxp->ctx_number;
		ctxp->ctx_mm = mm;
		return;
	}
	ctxp = ctx_used.next;
	if (ctxp->ctx_mm == old_mm)
		ctxp = ctxp->next;
	if (ctxp == &ctx_used)
		panic("out of mmu contexts");
	flush_cache_mm(ctxp->ctx_mm);
	flush_tlb_mm(ctxp->ctx_mm);
	remove_from_ctx_list(ctxp);
	add_to_used_ctxlist(ctxp);
	ctxp->ctx_mm->context = NO_CONTEXT;
	ctxp->ctx_mm = mm;
	mm->context = ctxp->ctx_number;
}

static inline void free_context(int context)
{
	struct ctx_list *ctx_old;

	ctx_old = ctx_list_pool + context;
	remove_from_ctx_list(ctx_old);
	add_to_free_ctxlist(ctx_old);
}

static void __init sparc_context_init(int numctx)
{
	int ctx;
	unsigned long size;

	size = numctx * sizeof(struct ctx_list);
	ctx_list_pool = memblock_alloc(size, SMP_CACHE_BYTES);
	if (!ctx_list_pool)
		panic("%s: Failed to allocate %lu bytes\n", __func__, size);

	for (ctx = 0; ctx < numctx; ctx++) {
		struct ctx_list *clist;

		clist = (ctx_list_pool + ctx);
		clist->ctx_number = ctx;
		clist->ctx_mm = NULL;
	}
	ctx_free.next = ctx_free.prev = &ctx_free;
	ctx_used.next = ctx_used.prev = &ctx_used;
	for (ctx = 0; ctx < numctx; ctx++)
		add_to_free_ctxlist(ctx_list_pool + ctx);
}

void switch_mm(struct mm_struct *old_mm, struct mm_struct *mm,
	       struct task_struct *tsk)
{
	unsigned long flags;

	if (mm->context == NO_CONTEXT) {
		spin_lock_irqsave(&srmmu_context_spinlock, flags);
		alloc_context(old_mm, mm);
		spin_unlock_irqrestore(&srmmu_context_spinlock, flags);
		srmmu_ctxd_set(&srmmu_context_table[mm->context], mm->pgd);
	}

	if (sparc_cpu_model == sparc_leon)
		leon_switch_mm();

	if (is_hypersparc)
		hyper_flush_whole_icache();

	srmmu_set_context(mm->context);
}

/* Low level IO area allocation on the SRMMU. */
static inline void srmmu_mapioaddr(unsigned long physaddr,
				   unsigned long virt_addr, int bus_type)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long tmp;

	physaddr &= PAGE_MASK;
	pgdp = pgd_offset_k(virt_addr);
	p4dp = p4d_offset(pgdp, virt_addr);
	pudp = pud_offset(p4dp, virt_addr);
	pmdp = pmd_offset(pudp, virt_addr);
	ptep = pte_offset_kernel(pmdp, virt_addr);
	tmp = (physaddr >> 4) | SRMMU_ET_PTE;

	/* I need to test whether this is consistent over all
	 * sun4m's.  The bus_type represents the upper 4 bits of
	 * 36-bit physical address on the I/O space lines...
	 */
	tmp |= (bus_type << 28);
	tmp |= SRMMU_PRIV;
	__flush_page_to_ram(virt_addr);
	set_pte(ptep, __pte(tmp));
}

void srmmu_mapiorange(unsigned int bus, unsigned long xpa,
		      unsigned long xva, unsigned int len)
{
	while (len != 0) {
		len -= PAGE_SIZE;
		srmmu_mapioaddr(xpa, xva, bus);
		xva += PAGE_SIZE;
		xpa += PAGE_SIZE;
	}
	flush_tlb_all();
}

static inline void srmmu_unmapioaddr(unsigned long virt_addr)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;


	pgdp = pgd_offset_k(virt_addr);
	p4dp = p4d_offset(pgdp, virt_addr);
	pudp = pud_offset(p4dp, virt_addr);
	pmdp = pmd_offset(pudp, virt_addr);
	ptep = pte_offset_kernel(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	__pte_clear(ptep);
}

void srmmu_unmapiorange(unsigned long virt_addr, unsigned int len)
{
	while (len != 0) {
		len -= PAGE_SIZE;
		srmmu_unmapioaddr(virt_addr);
		virt_addr += PAGE_SIZE;
	}
	flush_tlb_all();
}

/* tsunami.S */
extern void tsunami_flush_cache_all(void);
extern void tsunami_flush_cache_mm(struct mm_struct *mm);
extern void tsunami_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void tsunami_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void tsunami_flush_page_to_ram(unsigned long page);
extern void tsunami_flush_page_for_dma(unsigned long page);
extern void tsunami_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void tsunami_flush_tlb_all(void);
extern void tsunami_flush_tlb_mm(struct mm_struct *mm);
extern void tsunami_flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void tsunami_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void tsunami_setup_blockops(void);

/* swift.S */
extern void swift_flush_cache_all(void);
extern void swift_flush_cache_mm(struct mm_struct *mm);
extern void swift_flush_cache_range(struct vm_area_struct *vma,
				    unsigned long start, unsigned long end);
extern void swift_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void swift_flush_page_to_ram(unsigned long page);
extern void swift_flush_page_for_dma(unsigned long page);
extern void swift_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void swift_flush_tlb_all(void);
extern void swift_flush_tlb_mm(struct mm_struct *mm);
extern void swift_flush_tlb_range(struct vm_area_struct *vma,
				  unsigned long start, unsigned long end);
extern void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);

#if 0  /* P3: deadwood to debug precise flushes on Swift. */
void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cctx, ctx1;

	page &= PAGE_MASK;
	if ((ctx1 = vma->vm_mm->context) != -1) {
		cctx = srmmu_get_context();
/* Is context # ever different from current context? P3 */
		if (cctx != ctx1) {
			printk("flush ctx %02x curr %02x\n", ctx1, cctx);
			srmmu_set_context(ctx1);
			swift_flush_page(page);
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					"r" (page), "i" (ASI_M_FLUSH_PROBE));
			srmmu_set_context(cctx);
		} else {
			 /* Rm. prot. bits from virt. c. */
			/* swift_flush_cache_all(); */
			/* swift_flush_cache_page(vma, page); */
			swift_flush_page(page);

			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				"r" (page), "i" (ASI_M_FLUSH_PROBE));
			/* same as above: srmmu_flush_tlb_page() */
		}
	}
}
#endif

/*
 * The following are all MBUS based SRMMU modules, and therefore could
 * be found in a multiprocessor configuration.  On the whole, these
 * chips seems to be much more touchy about DVMA and page tables
 * with respect to cache coherency.
 */

/* viking.S */
extern void viking_flush_cache_all(void);
extern void viking_flush_cache_mm(struct mm_struct *mm);
extern void viking_flush_cache_range(struct vm_area_struct *vma, unsigned long start,
				     unsigned long end);
extern void viking_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void viking_flush_page_to_ram(unsigned long page);
extern void viking_flush_page_for_dma(unsigned long page);
extern void viking_flush_sig_insns(struct mm_struct *mm, unsigned long addr);
extern void viking_flush_page(unsigned long page);
extern void viking_mxcc_flush_page(unsigned long page);
extern void viking_flush_tlb_all(void);
extern void viking_flush_tlb_mm(struct mm_struct *mm);
extern void viking_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
				   unsigned long end);
extern void viking_flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long page);
extern void sun4dsmp_flush_tlb_all(void);
extern void sun4dsmp_flush_tlb_mm(struct mm_struct *mm);
extern void sun4dsmp_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
				   unsigned long end);
extern void sun4dsmp_flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long page);

/* hypersparc.S */
extern void hypersparc_flush_cache_all(void);
extern void hypersparc_flush_cache_mm(struct mm_struct *mm);
extern void hypersparc_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void hypersparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void hypersparc_flush_page_to_ram(unsigned long page);
extern void hypersparc_flush_page_for_dma(unsigned long page);
extern void hypersparc_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void hypersparc_flush_tlb_all(void);
extern void hypersparc_flush_tlb_mm(struct mm_struct *mm);
extern void hypersparc_flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void hypersparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void hypersparc_setup_blockops(void);

/*
 * NOTE: All of this startup code assumes the low 16mb (approx.) of
 *       kernel mappings are done with one single contiguous chunk of
 *       ram.  On small ram machines (classics mainly) we only get
 *       around 8mb mapped for us.
 */

static void __init early_pgtable_allocfail(char *type)
{
	prom_printf("inherit_prom_mappings: Cannot alloc kernel %s.\n", type);
	prom_halt();
}

static void __init srmmu_early_allocate_ptable_skeleton(unsigned long start,
							unsigned long end)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	while (start < end) {
		pgdp = pgd_offset_k(start);
		p4dp = p4d_offset(pgdp, start);
		pudp = pud_offset(p4dp, start);
		if (pud_none(*(pud_t *)__nocache_fix(pudp))) {
			pmdp = __srmmu_get_nocache(
			    SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(__nocache_fix(pmdp), 0, SRMMU_PMD_TABLE_SIZE);
			pud_set(__nocache_fix(pudp), pmdp);
		}
		pmdp = pmd_offset(__nocache_fix(pudp), start);
		if (srmmu_pmd_none(*(pmd_t *)__nocache_fix(pmdp))) {
			ptep = __srmmu_get_nocache(PTE_SIZE, PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(__nocache_fix(ptep), 0, PTE_SIZE);
			pmd_set(__nocache_fix(pmdp), ptep);
		}
		if (start > (0xffffffffUL - PMD_SIZE))
			break;
		start = (start + PMD_SIZE) & PMD_MASK;
	}
}

static void __init srmmu_allocate_ptable_skeleton(unsigned long start,
						  unsigned long end)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	while (start < end) {
		pgdp = pgd_offset_k(start);
		p4dp = p4d_offset(pgdp, start);
		pudp = pud_offset(p4dp, start);
		if (pud_none(*pudp)) {
			pmdp = __srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(pmdp, 0, SRMMU_PMD_TABLE_SIZE);
			pud_set((pud_t *)pgdp, pmdp);
		}
		pmdp = pmd_offset(pudp, start);
		if (srmmu_pmd_none(*pmdp)) {
			ptep = __srmmu_get_nocache(PTE_SIZE,
							     PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(ptep, 0, PTE_SIZE);
			pmd_set(pmdp, ptep);
		}
		if (start > (0xffffffffUL - PMD_SIZE))
			break;
		start = (start + PMD_SIZE) & PMD_MASK;
	}
}

/* These flush types are not available on all chips... */
static inline unsigned long srmmu_probe(unsigned long vaddr)
{
	unsigned long retval;

	if (sparc_cpu_model != sparc_leon) {

		vaddr &= PAGE_MASK;
		__asm__ __volatile__("lda [%1] %2, %0\n\t" :
				     "=r" (retval) :
				     "r" (vaddr | 0x400), "i" (ASI_M_FLUSH_PROBE));
	} else {
		retval = leon_swprobe(vaddr, NULL);
	}
	return retval;
}

/*
 * This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
static void __init srmmu_inherit_prom_mappings(unsigned long start,
					       unsigned long end)
{
	unsigned long probed;
	unsigned long addr;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	int what; /* 0 = normal-pte, 1 = pmd-level pte, 2 = pgd-level pte */

	while (start <= end) {
		if (start == 0)
			break; /* probably wrap around */
		if (start == 0xfef00000)
			start = KADB_DEBUGGER_BEGVM;
		probed = srmmu_probe(start);
		if (!probed) {
			/* continue probing until we find an entry */
			start += PAGE_SIZE;
			continue;
		}

		/* A red snapper, see what it really is. */
		what = 0;
		addr = start - PAGE_SIZE;

		if (!(start & ~(SRMMU_REAL_PMD_MASK))) {
			if (srmmu_probe(addr + SRMMU_REAL_PMD_SIZE) == probed)
				what = 1;
		}

		if (!(start & ~(SRMMU_PGDIR_MASK))) {
			if (srmmu_probe(addr + SRMMU_PGDIR_SIZE) == probed)
				what = 2;
		}

		pgdp = pgd_offset_k(start);
		p4dp = p4d_offset(pgdp, start);
		pudp = pud_offset(p4dp, start);
		if (what == 2) {
			*(pgd_t *)__nocache_fix(pgdp) = __pgd(probed);
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if (pud_none(*(pud_t *)__nocache_fix(pudp))) {
			pmdp = __srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE,
						   SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(__nocache_fix(pmdp), 0, SRMMU_PMD_TABLE_SIZE);
			pud_set(__nocache_fix(pudp), pmdp);
		}
		pmdp = pmd_offset(__nocache_fix(pgdp), start);
		if (srmmu_pmd_none(*(pmd_t *)__nocache_fix(pmdp))) {
			ptep = __srmmu_get_nocache(PTE_SIZE, PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(__nocache_fix(ptep), 0, PTE_SIZE);
			pmd_set(__nocache_fix(pmdp), ptep);
		}
		if (what == 1) {
			/* We bend the rule where all 16 PTPs in a pmd_t point
			 * inside the same PTE page, and we leak a perfectly
			 * good hardware PTE piece. Alternatives seem worse.
			 */
			unsigned int x;	/* Index of HW PMD in soft cluster */
			unsigned long *val;
			x = (start >> PMD_SHIFT) & 15;
			val = &pmdp->pmdv[x];
			*(unsigned long *)__nocache_fix(val) = probed;
			start += SRMMU_REAL_PMD_SIZE;
			continue;
		}
		ptep = pte_offset_kernel(__nocache_fix(pmdp), start);
		*(pte_t *)__nocache_fix(ptep) = __pte(probed);
		start += PAGE_SIZE;
	}
}

#define KERNEL_PTE(page_shifted) ((page_shifted)|SRMMU_CACHE|SRMMU_PRIV|SRMMU_VALID)

/* Create a third-level SRMMU 16MB page mapping. */
static void __init do_large_mapping(unsigned long vaddr, unsigned long phys_base)
{
	pgd_t *pgdp = pgd_offset_k(vaddr);
	unsigned long big_pte;

	big_pte = KERNEL_PTE(phys_base >> 4);
	*(pgd_t *)__nocache_fix(pgdp) = __pgd(big_pte);
}

/* Map sp_bank entry SP_ENTRY, starting at virtual address VBASE. */
static unsigned long __init map_spbank(unsigned long vbase, int sp_entry)
{
	unsigned long pstart = (sp_banks[sp_entry].base_addr & SRMMU_PGDIR_MASK);
	unsigned long vstart = (vbase & SRMMU_PGDIR_MASK);
	unsigned long vend = SRMMU_PGDIR_ALIGN(vbase + sp_banks[sp_entry].num_bytes);
	/* Map "low" memory only */
	const unsigned long min_vaddr = PAGE_OFFSET;
	const unsigned long max_vaddr = PAGE_OFFSET + SRMMU_MAXMEM;

	if (vstart < min_vaddr || vstart >= max_vaddr)
		return vstart;

	if (vend > max_vaddr || vend < min_vaddr)
		vend = max_vaddr;

	while (vstart < vend) {
		do_large_mapping(vstart, pstart);
		vstart += SRMMU_PGDIR_SIZE; pstart += SRMMU_PGDIR_SIZE;
	}
	return vstart;
}

static void __init map_kernel(void)
{
	int i;

	if (phys_base > 0) {
		do_large_mapping(PAGE_OFFSET, phys_base);
	}

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		map_spbank((unsigned long)__va(sp_banks[i].base_addr), i);
	}
}

void (*poke_srmmu)(void) = NULL;

void __init srmmu_paging_init(void)
{
	int i;
	phandle cpunode;
	char node_str[128];
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long pages_avail;

	init_mm.context = (unsigned long) NO_CONTEXT;
	sparc_iomap.start = SUN4M_IOBASE_VADDR;	/* 16MB of IOSPACE on all sun4m's. */

	if (sparc_cpu_model == sun4d)
		num_contexts = 65536; /* We know it is Viking */
	else {
		/* Find the number of contexts on the srmmu. */
		cpunode = prom_getchild(prom_root_node);
		num_contexts = 0;
		while (cpunode != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if (!strcmp(node_str, "cpu")) {
				num_contexts = prom_getintdefault(cpunode, "mmu-nctx", 0x8);
				break;
			}
			cpunode = prom_getsibling(cpunode);
		}
	}

	if (!num_contexts) {
		prom_printf("Something wrong, can't find cpu node in paging_init.\n");
		prom_halt();
	}

	pages_avail = 0;
	last_valid_pfn = bootmem_init(&pages_avail);

	srmmu_nocache_calcsize();
	srmmu_nocache_init();
	srmmu_inherit_prom_mappings(0xfe400000, (LINUX_OPPROM_ENDVM - PAGE_SIZE));
	map_kernel();

	/* ctx table has to be physically aligned to its size */
	srmmu_context_table = __srmmu_get_nocache(num_contexts * sizeof(ctxd_t), num_contexts * sizeof(ctxd_t));
	srmmu_ctx_table_phys = (ctxd_t *)__nocache_pa(srmmu_context_table);

	for (i = 0; i < num_contexts; i++)
		srmmu_ctxd_set((ctxd_t *)__nocache_fix(&srmmu_context_table[i]), srmmu_swapper_pg_dir);

	flush_cache_all();
	srmmu_set_ctable_ptr((unsigned long)srmmu_ctx_table_phys);
#ifdef CONFIG_SMP
	/* Stop from hanging here... */
	local_ops->tlb_all();
#else
	flush_tlb_all();
#endif
	poke_srmmu();

	srmmu_allocate_ptable_skeleton(sparc_iomap.start, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);

	srmmu_allocate_ptable_skeleton(
		__fix_to_virt(__end_of_fixed_addresses - 1), FIXADDR_TOP);
	srmmu_allocate_ptable_skeleton(PKMAP_BASE, PKMAP_END);

	pgd = pgd_offset_k(PKMAP_BASE);
	p4d = p4d_offset(pgd, PKMAP_BASE);
	pud = pud_offset(p4d, PKMAP_BASE);
	pmd = pmd_offset(pud, PKMAP_BASE);
	pte = pte_offset_kernel(pmd, PKMAP_BASE);
	pkmap_page_table = pte;

	flush_cache_all();
	flush_tlb_all();

	sparc_context_init(num_contexts);

	kmap_init();

	{
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long zholes_size[MAX_NR_ZONES];
		unsigned long npages;
		int znum;

		for (znum = 0; znum < MAX_NR_ZONES; znum++)
			zones_size[znum] = zholes_size[znum] = 0;

		npages = max_low_pfn - pfn_base;

		zones_size[ZONE_DMA] = npages;
		zholes_size[ZONE_DMA] = npages - pages_avail;

		npages = highend_pfn - max_low_pfn;
		zones_size[ZONE_HIGHMEM] = npages;
		zholes_size[ZONE_HIGHMEM] = npages - calc_highpages();

		free_area_init_node(0, zones_size, pfn_base, zholes_size);
	}
}

void mmu_info(struct seq_file *m)
{
	seq_printf(m,
		   "MMU type\t: %s\n"
		   "contexts\t: %d\n"
		   "nocache total\t: %ld\n"
		   "nocache used\t: %d\n",
		   srmmu_name,
		   num_contexts,
		   srmmu_nocache_size,
		   srmmu_nocache_map.used << SRMMU_NOCACHE_BITMAP_SHIFT);
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	unsigned long flags;

	if (mm->context != NO_CONTEXT) {
		flush_cache_mm(mm);
		srmmu_ctxd_set(&srmmu_context_table[mm->context], srmmu_swapper_pg_dir);
		flush_tlb_mm(mm);
		spin_lock_irqsave(&srmmu_context_spinlock, flags);
		free_context(mm->context);
		spin_unlock_irqrestore(&srmmu_context_spinlock, flags);
		mm->context = NO_CONTEXT;
	}
}

/* Init various srmmu chip types. */
static void __init srmmu_is_bad(void)
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

static void __init init_vac_layout(void)
{
	phandle nd;
	int cache_lines;
	char node_str[128];
#ifdef CONFIG_SMP
	int cpu = 0;
	unsigned long max_size = 0;
	unsigned long min_line_size = 0x10000000;
#endif

	nd = prom_getchild(prom_root_node);
	while ((nd = prom_getsibling(nd)) != 0) {
		prom_getstring(nd, "device_type", node_str, sizeof(node_str));
		if (!strcmp(node_str, "cpu")) {
			vac_line_size = prom_getint(nd, "cache-line-size");
			if (vac_line_size == -1) {
				prom_printf("can't determine cache-line-size, halting.\n");
				prom_halt();
			}
			cache_lines = prom_getint(nd, "cache-nlines");
			if (cache_lines == -1) {
				prom_printf("can't determine cache-nlines, halting.\n");
				prom_halt();
			}

			vac_cache_size = cache_lines * vac_line_size;
#ifdef CONFIG_SMP
			if (vac_cache_size > max_size)
				max_size = vac_cache_size;
			if (vac_line_size < min_line_size)
				min_line_size = vac_line_size;
			//FIXME: cpus not contiguous!!
			cpu++;
			if (cpu >= nr_cpu_ids || !cpu_online(cpu))
				break;
#else
			break;
#endif
		}
	}
	if (nd == 0) {
		prom_printf("No CPU nodes found, halting.\n");
		prom_halt();
	}
#ifdef CONFIG_SMP
	vac_cache_size = max_size;
	vac_line_size = min_line_size;
#endif
	printk("SRMMU: Using VAC size of %d bytes, line size %d bytes.\n",
	       (int)vac_cache_size, (int)vac_line_size);
}

static void poke_hypersparc(void)
{
	volatile unsigned long clear;
	unsigned long mreg = srmmu_get_mmureg();

	hyper_flush_unconditional_combined();

	mreg &= ~(HYPERSPARC_CWENABLE);
	mreg |= (HYPERSPARC_CENABLE | HYPERSPARC_WBENABLE);
	mreg |= (HYPERSPARC_CMODE);

	srmmu_set_mmureg(mreg);

#if 0 /* XXX I think this is bad news... -DaveM */
	hyper_clear_all_tags();
#endif

	put_ross_icr(HYPERSPARC_ICCR_FTD | HYPERSPARC_ICCR_ICE);
	hyper_flush_whole_icache();
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();
}

static const struct sparc32_cachetlb_ops hypersparc_ops = {
	.cache_all	= hypersparc_flush_cache_all,
	.cache_mm	= hypersparc_flush_cache_mm,
	.cache_page	= hypersparc_flush_cache_page,
	.cache_range	= hypersparc_flush_cache_range,
	.tlb_all	= hypersparc_flush_tlb_all,
	.tlb_mm		= hypersparc_flush_tlb_mm,
	.tlb_page	= hypersparc_flush_tlb_page,
	.tlb_range	= hypersparc_flush_tlb_range,
	.page_to_ram	= hypersparc_flush_page_to_ram,
	.sig_insns	= hypersparc_flush_sig_insns,
	.page_for_dma	= hypersparc_flush_page_for_dma,
};

static void __init init_hypersparc(void)
{
	srmmu_name = "ROSS HyperSparc";
	srmmu_modtype = HyperSparc;

	init_vac_layout();

	is_hypersparc = 1;
	sparc32_cachetlb_ops = &hypersparc_ops;

	poke_srmmu = poke_hypersparc;

	hypersparc_setup_blockops();
}

static void poke_swift(void)
{
	unsigned long mreg;

	/* Clear any crap from the cache or else... */
	swift_flush_cache_all();

	/* Enable I & D caches */
	mreg = srmmu_get_mmureg();
	mreg |= (SWIFT_IE | SWIFT_DE);
	/*
	 * The Swift branch folding logic is completely broken.  At
	 * trap time, if things are just right, if can mistakenly
	 * think that a trap is coming from kernel mode when in fact
	 * it is coming from user mode (it mis-executes the branch in
	 * the trap code).  So you see things like crashme completely
	 * hosing your machine which is completely unacceptable.  Turn
	 * this shit off... nice job Fujitsu.
	 */
	mreg &= ~(SWIFT_BF);
	srmmu_set_mmureg(mreg);
}

static const struct sparc32_cachetlb_ops swift_ops = {
	.cache_all	= swift_flush_cache_all,
	.cache_mm	= swift_flush_cache_mm,
	.cache_page	= swift_flush_cache_page,
	.cache_range	= swift_flush_cache_range,
	.tlb_all	= swift_flush_tlb_all,
	.tlb_mm		= swift_flush_tlb_mm,
	.tlb_page	= swift_flush_tlb_page,
	.tlb_range	= swift_flush_tlb_range,
	.page_to_ram	= swift_flush_page_to_ram,
	.sig_insns	= swift_flush_sig_insns,
	.page_for_dma	= swift_flush_page_for_dma,
};

#define SWIFT_MASKID_ADDR  0x10003018
static void __init init_swift(void)
{
	unsigned long swift_rev;

	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     "srl %0, 0x18, %0\n\t" :
			     "=r" (swift_rev) :
			     "r" (SWIFT_MASKID_ADDR), "i" (ASI_M_BYPASS));
	srmmu_name = "Fujitsu Swift";
	switch (swift_rev) {
	case 0x11:
	case 0x20:
	case 0x23:
	case 0x30:
		srmmu_modtype = Swift_lots_o_bugs;
		hwbug_bitmask |= (HWBUG_KERN_ACCBROKEN | HWBUG_KERN_CBITBROKEN);
		/*
		 * Gee george, I wonder why Sun is so hush hush about
		 * this hardware bug... really braindamage stuff going
		 * on here.  However I think we can find a way to avoid
		 * all of the workaround overhead under Linux.  Basically,
		 * any page fault can cause kernel pages to become user
		 * accessible (the mmu gets confused and clears some of
		 * the ACC bits in kernel ptes).  Aha, sounds pretty
		 * horrible eh?  But wait, after extensive testing it appears
		 * that if you use pgd_t level large kernel pte's (like the
		 * 4MB pages on the Pentium) the bug does not get tripped
		 * at all.  This avoids almost all of the major overhead.
		 * Welcome to a world where your vendor tells you to,
		 * "apply this kernel patch" instead of "sorry for the
		 * broken hardware, send it back and we'll give you
		 * properly functioning parts"
		 */
		break;
	case 0x25:
	case 0x31:
		srmmu_modtype = Swift_bad_c;
		hwbug_bitmask |= HWBUG_KERN_CBITBROKEN;
		/*
		 * You see Sun allude to this hardware bug but never
		 * admit things directly, they'll say things like,
		 * "the Swift chip cache problems" or similar.
		 */
		break;
	default:
		srmmu_modtype = Swift_ok;
		break;
	}

	sparc32_cachetlb_ops = &swift_ops;
	flush_page_for_dma_global = 0;

	/*
	 * Are you now convinced that the Swift is one of the
	 * biggest VLSI abortions of all time?  Bravo Fujitsu!
	 * Fujitsu, the !#?!%$'d up processor people.  I bet if
	 * you examined the microcode of the Swift you'd find
	 * XXX's all over the place.
	 */
	poke_srmmu = poke_swift;
}

static void turbosparc_flush_cache_all(void)
{
	flush_user_windows();
	turbosparc_idflash_clear();
}

static void turbosparc_flush_cache_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	flush_user_windows();
	turbosparc_idflash_clear();
	FLUSH_END
}

static void turbosparc_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(vma->vm_mm)
	flush_user_windows();
	turbosparc_idflash_clear();
	FLUSH_END
}

static void turbosparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	flush_user_windows();
	if (vma->vm_flags & VM_EXEC)
		turbosparc_flush_icache();
	turbosparc_flush_dcache();
	FLUSH_END
}

/* TurboSparc is copy-back, if we turn it on, but this does not work. */
static void turbosparc_flush_page_to_ram(unsigned long page)
{
#ifdef TURBOSPARC_WRITEBACK
	volatile unsigned long clear;

	if (srmmu_probe(page))
		turbosparc_flush_page_cache(page);
	clear = srmmu_get_fstatus();
#endif
}

static void turbosparc_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void turbosparc_flush_page_for_dma(unsigned long page)
{
	turbosparc_flush_dcache();
}

static void turbosparc_flush_tlb_all(void)
{
	srmmu_flush_whole_tlb();
}

static void turbosparc_flush_tlb_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	srmmu_flush_whole_tlb();
	FLUSH_END
}

static void turbosparc_flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(vma->vm_mm)
	srmmu_flush_whole_tlb();
	FLUSH_END
}

static void turbosparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	srmmu_flush_whole_tlb();
	FLUSH_END
}


static void poke_turbosparc(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long ccreg;

	/* Clear any crap from the cache or else... */
	turbosparc_flush_cache_all();
	/* Temporarily disable I & D caches */
	mreg &= ~(TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE);
	mreg &= ~(TURBOSPARC_PCENABLE);		/* Don't check parity */
	srmmu_set_mmureg(mreg);

	ccreg = turbosparc_get_ccreg();

#ifdef TURBOSPARC_WRITEBACK
	ccreg |= (TURBOSPARC_SNENABLE);		/* Do DVMA snooping in Dcache */
	ccreg &= ~(TURBOSPARC_uS2 | TURBOSPARC_WTENABLE);
			/* Write-back D-cache, emulate VLSI
			 * abortion number three, not number one */
#else
	/* For now let's play safe, optimize later */
	ccreg |= (TURBOSPARC_SNENABLE | TURBOSPARC_WTENABLE);
			/* Do DVMA snooping in Dcache, Write-thru D-cache */
	ccreg &= ~(TURBOSPARC_uS2);
			/* Emulate VLSI abortion number three, not number one */
#endif

	switch (ccreg & 7) {
	case 0: /* No SE cache */
	case 7: /* Test mode */
		break;
	default:
		ccreg |= (TURBOSPARC_SCENABLE);
	}
	turbosparc_set_ccreg(ccreg);

	mreg |= (TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE); /* I & D caches on */
	mreg |= (TURBOSPARC_ICSNOOP);		/* Icache snooping on */
	srmmu_set_mmureg(mreg);
}

static const struct sparc32_cachetlb_ops turbosparc_ops = {
	.cache_all	= turbosparc_flush_cache_all,
	.cache_mm	= turbosparc_flush_cache_mm,
	.cache_page	= turbosparc_flush_cache_page,
	.cache_range	= turbosparc_flush_cache_range,
	.tlb_all	= turbosparc_flush_tlb_all,
	.tlb_mm		= turbosparc_flush_tlb_mm,
	.tlb_page	= turbosparc_flush_tlb_page,
	.tlb_range	= turbosparc_flush_tlb_range,
	.page_to_ram	= turbosparc_flush_page_to_ram,
	.sig_insns	= turbosparc_flush_sig_insns,
	.page_for_dma	= turbosparc_flush_page_for_dma,
};

static void __init init_turbosparc(void)
{
	srmmu_name = "Fujitsu TurboSparc";
	srmmu_modtype = TurboSparc;
	sparc32_cachetlb_ops = &turbosparc_ops;
	poke_srmmu = poke_turbosparc;
}

static void poke_tsunami(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

static const struct sparc32_cachetlb_ops tsunami_ops = {
	.cache_all	= tsunami_flush_cache_all,
	.cache_mm	= tsunami_flush_cache_mm,
	.cache_page	= tsunami_flush_cache_page,
	.cache_range	= tsunami_flush_cache_range,
	.tlb_all	= tsunami_flush_tlb_all,
	.tlb_mm		= tsunami_flush_tlb_mm,
	.tlb_page	= tsunami_flush_tlb_page,
	.tlb_range	= tsunami_flush_tlb_range,
	.page_to_ram	= tsunami_flush_page_to_ram,
	.sig_insns	= tsunami_flush_sig_insns,
	.page_for_dma	= tsunami_flush_page_for_dma,
};

static void __init init_tsunami(void)
{
	/*
	 * Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	srmmu_name = "TI Tsunami";
	srmmu_modtype = Tsunami;
	sparc32_cachetlb_ops = &tsunami_ops;
	poke_srmmu = poke_tsunami;

	tsunami_setup_blockops();
}

static void poke_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	static int smp_catch;

	if (viking_mxcc_present) {
		unsigned long mxcc_control = mxcc_get_creg();

		mxcc_control |= (MXCC_CTL_ECE | MXCC_CTL_PRE | MXCC_CTL_MCE);
		mxcc_control &= ~(MXCC_CTL_RRC);
		mxcc_set_creg(mxcc_control);

		/*
		 * We don't need memory parity checks.
		 * XXX This is a mess, have to dig out later. ecd.
		viking_mxcc_turn_off_parity(&mreg, &mxcc_control);
		 */

		/* We do cache ptables on MXCC. */
		mreg |= VIKING_TCENABLE;
	} else {
		unsigned long bpreg;

		mreg &= ~(VIKING_TCENABLE);
		if (smp_catch++) {
			/* Must disable mixed-cmd mode here for other cpu's. */
			bpreg = viking_get_bpreg();
			bpreg &= ~(VIKING_ACTION_MIX);
			viking_set_bpreg(bpreg);

			/* Just in case PROM does something funny. */
			msi_set_sync();
		}
	}

	mreg |= VIKING_SPENABLE;
	mreg |= (VIKING_ICENABLE | VIKING_DCENABLE);
	mreg |= VIKING_SBENABLE;
	mreg &= ~(VIKING_ACENABLE);
	srmmu_set_mmureg(mreg);
}

static struct sparc32_cachetlb_ops viking_ops __ro_after_init = {
	.cache_all	= viking_flush_cache_all,
	.cache_mm	= viking_flush_cache_mm,
	.cache_page	= viking_flush_cache_page,
	.cache_range	= viking_flush_cache_range,
	.tlb_all	= viking_flush_tlb_all,
	.tlb_mm		= viking_flush_tlb_mm,
	.tlb_page	= viking_flush_tlb_page,
	.tlb_range	= viking_flush_tlb_range,
	.page_to_ram	= viking_flush_page_to_ram,
	.sig_insns	= viking_flush_sig_insns,
	.page_for_dma	= viking_flush_page_for_dma,
};

#ifdef CONFIG_SMP
/* On sun4d the cpu broadcasts local TLB flushes, so we can just
 * perform the local TLB flush and all the other cpus will see it.
 * But, unfortunately, there is a bug in the sun4d XBUS backplane
 * that requires that we add some synchronization to these flushes.
 *
 * The bug is that the fifo which keeps track of all the pending TLB
 * broadcasts in the system is an entry or two too small, so if we
 * have too many going at once we'll overflow that fifo and lose a TLB
 * flush resulting in corruption.
 *
 * Our workaround is to take a global spinlock around the TLB flushes,
 * which guarentees we won't ever have too many pending.  It's a big
 * hammer, but a semaphore like system to make sure we only have N TLB
 * flushes going at once will require SMP locking anyways so there's
 * no real value in trying any harder than this.
 */
static struct sparc32_cachetlb_ops viking_sun4d_smp_ops __ro_after_init = {
	.cache_all	= viking_flush_cache_all,
	.cache_mm	= viking_flush_cache_mm,
	.cache_page	= viking_flush_cache_page,
	.cache_range	= viking_flush_cache_range,
	.tlb_all	= sun4dsmp_flush_tlb_all,
	.tlb_mm		= sun4dsmp_flush_tlb_mm,
	.tlb_page	= sun4dsmp_flush_tlb_page,
	.tlb_range	= sun4dsmp_flush_tlb_range,
	.page_to_ram	= viking_flush_page_to_ram,
	.sig_insns	= viking_flush_sig_insns,
	.page_for_dma	= viking_flush_page_for_dma,
};
#endif

static void __init init_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */
	if (mreg & VIKING_MMODE) {
		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;
		msi_set_sync();

		/*
		 * We need this to make sure old viking takes no hits
		 * on it's cache for dma snoops to workaround the
		 * "load from non-cacheable memory" interrupt bug.
		 * This is only necessary because of the new way in
		 * which we use the IOMMU.
		 */
		viking_ops.page_for_dma = viking_flush_page;
#ifdef CONFIG_SMP
		viking_sun4d_smp_ops.page_for_dma = viking_flush_page;
#endif
		flush_page_for_dma_global = 0;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;
		srmmu_cache_pagetables = 1;
	}

	sparc32_cachetlb_ops = (const struct sparc32_cachetlb_ops *)
		&viking_ops;
#ifdef CONFIG_SMP
	if (sparc_cpu_model == sun4d)
		sparc32_cachetlb_ops = (const struct sparc32_cachetlb_ops *)
			&viking_sun4d_smp_ops;
#endif

	poke_srmmu = poke_viking;
}

/* Probe for the srmmu chip version. */
static void __init get_srmmu_type(void)
{
	unsigned long mreg, psr;
	unsigned long mod_typ, mod_rev, psr_typ, psr_vers;

	srmmu_modtype = SRMMU_INVAL_MOD;
	hwbug_bitmask = 0;

	mreg = srmmu_get_mmureg(); psr = get_psr();
	mod_typ = (mreg & 0xf0000000) >> 28;
	mod_rev = (mreg & 0x0f000000) >> 24;
	psr_typ = (psr >> 28) & 0xf;
	psr_vers = (psr >> 24) & 0xf;

	/* First, check for sparc-leon. */
	if (sparc_cpu_model == sparc_leon) {
		init_leon();
		return;
	}

	/* Second, check for HyperSparc or Cypress. */
	if (mod_typ == 1) {
		switch (mod_rev) {
		case 7:
			/* UP or MP Hypersparc */
			init_hypersparc();
			break;
		case 0:
		case 2:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		default:
			prom_printf("Sparc-Linux Cypress support does not longer exit.\n");
			prom_halt();
			break;
		}
		return;
	}

	/* Now Fujitsu TurboSparc. It might happen that it is
	 * in Swift emulation mode, so we will check later...
	 */
	if (psr_typ == 0 && psr_vers == 5) {
		init_turbosparc();
		return;
	}

	/* Next check for Fujitsu Swift. */
	if (psr_typ == 0 && psr_vers == 4) {
		phandle cpunode;
		char node_str[128];

		/* Look if it is not a TurboSparc emulating Swift... */
		cpunode = prom_getchild(prom_root_node);
		while ((cpunode = prom_getsibling(cpunode)) != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if (!strcmp(node_str, "cpu")) {
				if (!prom_getintdefault(cpunode, "psr-implementation", 1) &&
				    prom_getintdefault(cpunode, "psr-version", 1) == 5) {
					init_turbosparc();
					return;
				}
				break;
			}
		}

		init_swift();
		return;
	}

	/* Now the Viking family of srmmu. */
	if (psr_typ == 4 &&
	   ((psr_vers == 0) ||
	    ((psr_vers == 1) && (mod_typ == 0) && (mod_rev == 0)))) {
		init_viking();
		return;
	}

	/* Finally the Tsunami. */
	if (psr_typ == 4 && psr_vers == 1 && (mod_typ || mod_rev)) {
		init_tsunami();
		return;
	}

	/* Oh well */
	srmmu_is_bad();
}

#ifdef CONFIG_SMP
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) local_ops->page_for_dma, page);
	local_ops->page_for_dma(page);
}

static void smp_flush_cache_all(void)
{
	xc0((smpfunc_t) local_ops->cache_all);
	local_ops->cache_all();
}

static void smp_flush_tlb_all(void)
{
	xc0((smpfunc_t) local_ops->tlb_all);
	local_ops->tlb_all();
}

static void smp_flush_cache_mm(struct mm_struct *mm)
{
	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc1((smpfunc_t) local_ops->cache_mm, (unsigned long) mm);
		local_ops->cache_mm(mm);
	}
}

static void smp_flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask)) {
			xc1((smpfunc_t) local_ops->tlb_mm, (unsigned long) mm);
			if (atomic_read(&mm->mm_users) == 1 && current->active_mm == mm)
				cpumask_copy(mm_cpumask(mm),
					     cpumask_of(smp_processor_id()));
		}
		local_ops->tlb_mm(mm);
	}
}

static void smp_flush_cache_range(struct vm_area_struct *vma,
				  unsigned long start,
				  unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc3((smpfunc_t) local_ops->cache_range,
			    (unsigned long) vma, start, end);
		local_ops->cache_range(vma, start, end);
	}
}

static void smp_flush_tlb_range(struct vm_area_struct *vma,
				unsigned long start,
				unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc3((smpfunc_t) local_ops->tlb_range,
			    (unsigned long) vma, start, end);
		local_ops->tlb_range(vma, start, end);
	}
}

static void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc2((smpfunc_t) local_ops->cache_page,
			    (unsigned long) vma, page);
		local_ops->cache_page(vma, page);
	}
}

static void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc2((smpfunc_t) local_ops->tlb_page,
			    (unsigned long) vma, page);
		local_ops->tlb_page(vma, page);
	}
}

static void smp_flush_page_to_ram(unsigned long page)
{
	/* Current theory is that those who call this are the one's
	 * who have just dirtied their cache with the pages contents
	 * in kernel space, therefore we only run this on local cpu.
	 *
	 * XXX This experiment failed, research further... -DaveM
	 */
#if 1
	xc1((smpfunc_t) local_ops->page_to_ram, page);
#endif
	local_ops->page_to_ram(page);
}

static void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	cpumask_t cpu_mask;
	cpumask_copy(&cpu_mask, mm_cpumask(mm));
	cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
	if (!cpumask_empty(&cpu_mask))
		xc2((smpfunc_t) local_ops->sig_insns,
		    (unsigned long) mm, insn_addr);
	local_ops->sig_insns(mm, insn_addr);
}

static struct sparc32_cachetlb_ops smp_cachetlb_ops __ro_after_init = {
	.cache_all	= smp_flush_cache_all,
	.cache_mm	= smp_flush_cache_mm,
	.cache_page	= smp_flush_cache_page,
	.cache_range	= smp_flush_cache_range,
	.tlb_all	= smp_flush_tlb_all,
	.tlb_mm		= smp_flush_tlb_mm,
	.tlb_page	= smp_flush_tlb_page,
	.tlb_range	= smp_flush_tlb_range,
	.page_to_ram	= smp_flush_page_to_ram,
	.sig_insns	= smp_flush_sig_insns,
	.page_for_dma	= smp_flush_page_for_dma,
};
#endif

/* Load up routines and constants for sun4m and sun4d mmu */
void __init load_mmu(void)
{
	/* Functions */
	get_srmmu_type();

#ifdef CONFIG_SMP
	/* El switcheroo... */
	local_ops = sparc32_cachetlb_ops;

	if (sparc_cpu_model == sun4d || sparc_cpu_model == sparc_leon) {
		smp_cachetlb_ops.tlb_all = local_ops->tlb_all;
		smp_cachetlb_ops.tlb_mm = local_ops->tlb_mm;
		smp_cachetlb_ops.tlb_range = local_ops->tlb_range;
		smp_cachetlb_ops.tlb_page = local_ops->tlb_page;
	}

	if (poke_srmmu == poke_viking) {
		/* Avoid unnecessary cross calls. */
		smp_cachetlb_ops.cache_all = local_ops->cache_all;
		smp_cachetlb_ops.cache_mm = local_ops->cache_mm;
		smp_cachetlb_ops.cache_range = local_ops->cache_range;
		smp_cachetlb_ops.cache_page = local_ops->cache_page;

		smp_cachetlb_ops.page_to_ram = local_ops->page_to_ram;
		smp_cachetlb_ops.sig_insns = local_ops->sig_insns;
		smp_cachetlb_ops.page_for_dma = local_ops->page_for_dma;
	}

	/* It really is const after this point. */
	sparc32_cachetlb_ops = (const struct sparc32_cachetlb_ops *)
		&smp_cachetlb_ops;
#endif

	if (sparc_cpu_model != sun4d)
		ld_mmu_iommu();
#ifdef CONFIG_SMP
	if (sparc_cpu_model == sun4d)
		sun4d_init_smp();
	else if (sparc_cpu_model == sparc_leon)
		leon_init_smp();
	else
		sun4m_init_smp();
#endif
}

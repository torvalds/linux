/*
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995,2002 Pete Zaitcev (zaitcev@yahoo.com)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1999,2000 Anton Blanchard (anton@samba.org)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kdebug.h>
#include <linux/log2.h>
#include <linux/gfp.h>

#include <asm/bitext.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/traps.h>
#include <asm/smp.h>
#include <asm/mbus.h>
#include <asm/cache.h>
#include <asm/oplib.h>
#include <asm/asi.h>
#include <asm/msi.h>
#include <asm/mmu_context.h>
#include <asm/io-unit.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/mxcc.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>
#include <asm/turbosparc.h>
#include <asm/leon.h>

#include <asm/btfixup.h>

enum mbus_module srmmu_modtype;
static unsigned int hwbug_bitmask;
int vac_cache_size;
int vac_line_size;

extern struct resource sparc_iomap;

extern unsigned long last_valid_pfn;

extern unsigned long page_kernel;

static pgd_t *srmmu_swapper_pg_dir;

#ifdef CONFIG_SMP
#define FLUSH_BEGIN(mm)
#define FLUSH_END
#else
#define FLUSH_BEGIN(mm) if((mm)->context != NO_CONTEXT) {
#define FLUSH_END	}
#endif

BTFIXUPDEF_CALL(void, flush_page_for_dma, unsigned long)
#define flush_page_for_dma(page) BTFIXUP_CALL(flush_page_for_dma)(page)

int flush_page_for_dma_global = 1;

#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, local_flush_page_for_dma, unsigned long)
#define local_flush_page_for_dma(page) BTFIXUP_CALL(local_flush_page_for_dma)(page)
#endif

char *srmmu_name;

ctxd_t *srmmu_ctx_table_phys;
static ctxd_t *srmmu_context_table;

int viking_mxcc_present;
static DEFINE_SPINLOCK(srmmu_context_spinlock);

static int is_hypersparc;

/*
 * In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long srmmu_swap(unsigned long *addr, unsigned long value)
{
	__asm__ __volatile__("swap [%2], %0" : "=&r" (value) : "0" (value), "r" (addr));
	return value;
}

static inline void srmmu_set_pte(pte_t *ptep, pte_t pteval)
{
	srmmu_swap((unsigned long *)ptep, pte_val(pteval));
}

/* The very generic SRMMU page table operations. */
static inline int srmmu_device_memory(unsigned long x)
{
	return ((x & 0xF0000000) != 0);
}

static int srmmu_cache_pagetables;

/* these will be initialized in srmmu_nocache_calcsize() */
static unsigned long srmmu_nocache_size;
static unsigned long srmmu_nocache_end;

/* 1 bit <=> 256 bytes of nocache <=> 64 PTEs */
#define SRMMU_NOCACHE_BITMAP_SHIFT (PAGE_SHIFT - 4)

/* The context table is a nocache user with the biggest alignment needs. */
#define SRMMU_NOCACHE_ALIGN_MAX (sizeof(ctxd_t)*SRMMU_MAX_CONTEXTS)

void *srmmu_nocache_pool;
void *srmmu_nocache_bitmap;
static struct bit_map srmmu_nocache_map;

static unsigned long srmmu_pte_pfn(pte_t pte)
{
	if (srmmu_device_memory(pte_val(pte))) {
		/* Just return something that will cause
		 * pfn_valid() to return false.  This makes
		 * copy_one_pte() to just directly copy to
		 * PTE over.
		 */
		return ~0UL;
	}
	return (pte_val(pte) & SRMMU_PTE_PMASK) >> (PAGE_SHIFT-4);
}

static struct page *srmmu_pmd_page(pmd_t pmd)
{

	if (srmmu_device_memory(pmd_val(pmd)))
		BUG();
	return pfn_to_page((pmd_val(pmd) & SRMMU_PTD_PMASK) >> (PAGE_SHIFT-4));
}

static inline unsigned long srmmu_pgd_page(pgd_t pgd)
{ return srmmu_device_memory(pgd_val(pgd))?~0:(unsigned long)__nocache_va((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }


static inline int srmmu_pte_none(pte_t pte)
{ return !(pte_val(pte) & 0xFFFFFFF); }

static inline int srmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & SRMMU_ET_MASK) == SRMMU_ET_PTE); }

static inline void srmmu_pte_clear(pte_t *ptep)
{ srmmu_set_pte(ptep, __pte(0)); }

static inline int srmmu_pmd_none(pmd_t pmd)
{ return !(pmd_val(pmd) & 0xFFFFFFF); }

static inline int srmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static inline int srmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static inline void srmmu_pmd_clear(pmd_t *pmdp) {
	int i;
	for (i = 0; i < PTRS_PER_PTE/SRMMU_REAL_PTRS_PER_PTE; i++)
		srmmu_set_pte((pte_t *)&pmdp->pmdv[i], __pte(0));
}

static inline int srmmu_pgd_none(pgd_t pgd)          
{ return !(pgd_val(pgd) & 0xFFFFFFF); }

static inline int srmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static inline int srmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static inline void srmmu_pgd_clear(pgd_t * pgdp)
{ srmmu_set_pte((pte_t *)pgdp, __pte(0)); }

static inline pte_t srmmu_pte_wrprotect(pte_t pte)
{ return __pte(pte_val(pte) & ~SRMMU_WRITE);}

static inline pte_t srmmu_pte_mkclean(pte_t pte)
{ return __pte(pte_val(pte) & ~SRMMU_DIRTY);}

static inline pte_t srmmu_pte_mkold(pte_t pte)
{ return __pte(pte_val(pte) & ~SRMMU_REF);}

static inline pte_t srmmu_pte_mkwrite(pte_t pte)
{ return __pte(pte_val(pte) | SRMMU_WRITE);}

static inline pte_t srmmu_pte_mkdirty(pte_t pte)
{ return __pte(pte_val(pte) | SRMMU_DIRTY);}

static inline pte_t srmmu_pte_mkyoung(pte_t pte)
{ return __pte(pte_val(pte) | SRMMU_REF);}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t srmmu_mk_pte(struct page *page, pgprot_t pgprot)
{ return __pte((page_to_pfn(page) << (PAGE_SHIFT-4)) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ return __pte(((page) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{ return __pte(((page) >> 4) | (space << 28) | pgprot_val(pgprot)); }

/* XXX should we hyper_flush_whole_icache here - Anton */
static inline void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ srmmu_set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (__nocache_pa((unsigned long) pgdp) >> 4))); }

static inline void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ srmmu_set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (__nocache_pa((unsigned long) pmdp) >> 4))); }

static void srmmu_pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	unsigned long ptp;	/* Physical address, shifted right by 4 */
	int i;

	ptp = __nocache_pa((unsigned long) ptep) >> 4;
	for (i = 0; i < PTRS_PER_PTE/SRMMU_REAL_PTRS_PER_PTE; i++) {
		srmmu_set_pte((pte_t *)&pmdp->pmdv[i], SRMMU_ET_PTD | ptp);
		ptp += (SRMMU_REAL_PTRS_PER_PTE*sizeof(pte_t) >> 4);
	}
}

static void srmmu_pmd_populate(pmd_t *pmdp, struct page *ptep)
{
	unsigned long ptp;	/* Physical address, shifted right by 4 */
	int i;

	ptp = page_to_pfn(ptep) << (PAGE_SHIFT-4);	/* watch for overflow */
	for (i = 0; i < PTRS_PER_PTE/SRMMU_REAL_PTRS_PER_PTE; i++) {
		srmmu_set_pte((pte_t *)&pmdp->pmdv[i], SRMMU_ET_PTD | ptp);
		ptp += (SRMMU_REAL_PTRS_PER_PTE*sizeof(pte_t) >> 4);
	}
}

static inline pte_t srmmu_pte_modify(pte_t pte, pgprot_t newprot)
{ return __pte((pte_val(pte) & SRMMU_CHG_MASK) | pgprot_val(newprot)); }

/* to find an entry in a top-level page table... */
static inline pgd_t *srmmu_pgd_offset(struct mm_struct * mm, unsigned long address)
{ return mm->pgd + (address >> SRMMU_PGDIR_SHIFT); }

/* Find an entry in the second-level page table.. */
static inline pmd_t *srmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) srmmu_pgd_page(*dir) +
	    ((address >> PMD_SHIFT) & (PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static inline pte_t *srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	void *pte;

	pte = __nocache_va((dir->pmdv[0] & SRMMU_PTD_PMASK) << 4);
	return (pte_t *) pte +
	    ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

static unsigned long srmmu_swp_type(swp_entry_t entry)
{
	return (entry.val >> SRMMU_SWP_TYPE_SHIFT) & SRMMU_SWP_TYPE_MASK;
}

static unsigned long srmmu_swp_offset(swp_entry_t entry)
{
	return (entry.val >> SRMMU_SWP_OFF_SHIFT) & SRMMU_SWP_OFF_MASK;
}

static swp_entry_t srmmu_swp_entry(unsigned long type, unsigned long offset)
{
	return (swp_entry_t) {
		  (type & SRMMU_SWP_TYPE_MASK) << SRMMU_SWP_TYPE_SHIFT
		| (offset & SRMMU_SWP_OFF_MASK) << SRMMU_SWP_OFF_SHIFT };
}

/*
 * size: bytes to allocate in the nocache area.
 * align: bytes, number to align at.
 * Returns the virtual address of the allocated area.
 */
static unsigned long __srmmu_get_nocache(int size, int align)
{
	int offset;

	if (size < SRMMU_NOCACHE_BITMAP_SHIFT) {
		printk("Size 0x%x too small for nocache request\n", size);
		size = SRMMU_NOCACHE_BITMAP_SHIFT;
	}
	if (size & (SRMMU_NOCACHE_BITMAP_SHIFT-1)) {
		printk("Size 0x%x unaligned int nocache request\n", size);
		size += SRMMU_NOCACHE_BITMAP_SHIFT-1;
	}
	BUG_ON(align > SRMMU_NOCACHE_ALIGN_MAX);

	offset = bit_map_string_get(&srmmu_nocache_map,
		       			size >> SRMMU_NOCACHE_BITMAP_SHIFT,
					align >> SRMMU_NOCACHE_BITMAP_SHIFT);
	if (offset == -1) {
		printk("srmmu: out of nocache %d: %d/%d\n",
		    size, (int) srmmu_nocache_size,
		    srmmu_nocache_map.used << SRMMU_NOCACHE_BITMAP_SHIFT);
		return 0;
	}

	return (SRMMU_NOCACHE_VADDR + (offset << SRMMU_NOCACHE_BITMAP_SHIFT));
}

static unsigned long srmmu_get_nocache(int size, int align)
{
	unsigned long tmp;

	tmp = __srmmu_get_nocache(size, align);

	if (tmp)
		memset((void *)tmp, 0, size);

	return tmp;
}

static void srmmu_free_nocache(unsigned long vaddr, int size)
{
	int offset;

	if (vaddr < SRMMU_NOCACHE_VADDR) {
		printk("Vaddr %lx is smaller than nocache base 0x%lx\n",
		    vaddr, (unsigned long)SRMMU_NOCACHE_VADDR);
		BUG();
	}
	if (vaddr+size > srmmu_nocache_end) {
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
	if (vaddr & (size-1)) {
		printk("Vaddr %lx is not aligned to size 0x%x\n", vaddr, size);
		BUG();
	}

	offset = (vaddr - SRMMU_NOCACHE_VADDR) >> SRMMU_NOCACHE_BITMAP_SHIFT;
	size = size >> SRMMU_NOCACHE_BITMAP_SHIFT;

	bit_map_clear(&srmmu_nocache_map, offset, size);
}

static void srmmu_early_allocate_ptable_skeleton(unsigned long start,
						 unsigned long end);

extern unsigned long probe_memory(void);	/* in fault.c */

/*
 * Reserve nocache dynamically proportionally to the amount of
 * system RAM. -- Tomas Szepe <szepe@pinerecords.com>, June 2002
 */
static void srmmu_nocache_calcsize(void)
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
	unsigned int bitmap_bits;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long paddr, vaddr;
	unsigned long pteval;

	bitmap_bits = srmmu_nocache_size >> SRMMU_NOCACHE_BITMAP_SHIFT;

	srmmu_nocache_pool = __alloc_bootmem(srmmu_nocache_size,
		SRMMU_NOCACHE_ALIGN_MAX, 0UL);
	memset(srmmu_nocache_pool, 0, srmmu_nocache_size);

	srmmu_nocache_bitmap = __alloc_bootmem(bitmap_bits >> 3, SMP_CACHE_BYTES, 0UL);
	bit_map_init(&srmmu_nocache_map, srmmu_nocache_bitmap, bitmap_bits);

	srmmu_swapper_pg_dir = (pgd_t *)__srmmu_get_nocache(SRMMU_PGD_TABLE_SIZE, SRMMU_PGD_TABLE_SIZE);
	memset(__nocache_fix(srmmu_swapper_pg_dir), 0, SRMMU_PGD_TABLE_SIZE);
	init_mm.pgd = srmmu_swapper_pg_dir;

	srmmu_early_allocate_ptable_skeleton(SRMMU_NOCACHE_VADDR, srmmu_nocache_end);

	paddr = __pa((unsigned long)srmmu_nocache_pool);
	vaddr = SRMMU_NOCACHE_VADDR;

	while (vaddr < srmmu_nocache_end) {
		pgd = pgd_offset_k(vaddr);
		pmd = srmmu_pmd_offset(__nocache_fix(pgd), vaddr);
		pte = srmmu_pte_offset(__nocache_fix(pmd), vaddr);

		pteval = ((paddr >> 4) | SRMMU_ET_PTE | SRMMU_PRIV);

		if (srmmu_cache_pagetables)
			pteval |= SRMMU_CACHE;

		srmmu_set_pte(__nocache_fix(pte), __pte(pteval));

		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}

	flush_cache_all();
	flush_tlb_all();
}

static inline pgd_t *srmmu_get_pgd_fast(void)
{
	pgd_t *pgd = NULL;

	pgd = (pgd_t *)__srmmu_get_nocache(SRMMU_PGD_TABLE_SIZE, SRMMU_PGD_TABLE_SIZE);
	if (pgd) {
		pgd_t *init = pgd_offset_k(0);
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return pgd;
}

static void srmmu_free_pgd_fast(pgd_t *pgd)
{
	srmmu_free_nocache((unsigned long)pgd, SRMMU_PGD_TABLE_SIZE);
}

static pmd_t *srmmu_pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return (pmd_t *)srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
}

static void srmmu_pmd_free(pmd_t * pmd)
{
	srmmu_free_nocache((unsigned long)pmd, SRMMU_PMD_TABLE_SIZE);
}

/*
 * Hardware needs alignment to 256 only, but we align to whole page size
 * to reduce fragmentation problems due to the buddy principle.
 * XXX Provide actual fragmentation statistics in /proc.
 *
 * Alignments up to the page size are the same for physical and virtual
 * addresses of the nocache area.
 */
static pte_t *
srmmu_pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *)srmmu_get_nocache(PTE_SIZE, PTE_SIZE);
}

static pgtable_t
srmmu_pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long pte;
	struct page *page;

	if ((pte = (unsigned long)srmmu_pte_alloc_one_kernel(mm, address)) == 0)
		return NULL;
	page = pfn_to_page( __nocache_pa(pte) >> PAGE_SHIFT );
	pgtable_page_ctor(page);
	return page;
}

static void srmmu_free_pte_fast(pte_t *pte)
{
	srmmu_free_nocache((unsigned long)pte, PTE_SIZE);
}

static void srmmu_pte_free(pgtable_t pte)
{
	unsigned long p;

	pgtable_page_dtor(pte);
	p = (unsigned long)page_address(pte);	/* Cached address (for test) */
	if (p == 0)
		BUG();
	p = page_to_pfn(pte) << PAGE_SHIFT;	/* Physical address */
	p = (unsigned long) __nocache_va(p);	/* Nocached virtual */
	srmmu_free_nocache(p, PTE_SIZE);
}

/*
 */
static inline void alloc_context(struct mm_struct *old_mm, struct mm_struct *mm)
{
	struct ctx_list *ctxp;

	ctxp = ctx_free.next;
	if(ctxp != &ctx_free) {
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		mm->context = ctxp->ctx_number;
		ctxp->ctx_mm = mm;
		return;
	}
	ctxp = ctx_used.next;
	if(ctxp->ctx_mm == old_mm)
		ctxp = ctxp->next;
	if(ctxp == &ctx_used)
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


static void srmmu_switch_mm(struct mm_struct *old_mm, struct mm_struct *mm,
    struct task_struct *tsk, int cpu)
{
	if(mm->context == NO_CONTEXT) {
		spin_lock(&srmmu_context_spinlock);
		alloc_context(old_mm, mm);
		spin_unlock(&srmmu_context_spinlock);
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
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long tmp;

	physaddr &= PAGE_MASK;
	pgdp = pgd_offset_k(virt_addr);
	pmdp = srmmu_pmd_offset(pgdp, virt_addr);
	ptep = srmmu_pte_offset(pmdp, virt_addr);
	tmp = (physaddr >> 4) | SRMMU_ET_PTE;

	/*
	 * I need to test whether this is consistent over all
	 * sun4m's.  The bus_type represents the upper 4 bits of
	 * 36-bit physical address on the I/O space lines...
	 */
	tmp |= (bus_type << 28);
	tmp |= SRMMU_PRIV;
	__flush_page_to_ram(virt_addr);
	srmmu_set_pte(ptep, __pte(tmp));
}

static void srmmu_mapiorange(unsigned int bus, unsigned long xpa,
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
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_k(virt_addr);
	pmdp = srmmu_pmd_offset(pgdp, virt_addr);
	ptep = srmmu_pte_offset(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	srmmu_pte_clear(ptep);
}

static void srmmu_unmapiorange(unsigned long virt_addr, unsigned int len)
{
	while (len != 0) {
		len -= PAGE_SIZE;
		srmmu_unmapioaddr(virt_addr);
		virt_addr += PAGE_SIZE;
	}
	flush_tlb_all();
}

/*
 * On the SRMMU we do not have the problems with limited tlb entries
 * for mapping kernel pages, so we just take things from the free page
 * pool.  As a side effect we are putting a little too much pressure
 * on the gfp() subsystem.  This setup also makes the logic of the
 * iommu mapping code a lot easier as we can transparently handle
 * mappings on the kernel stack without any special code.
 */
struct thread_info *alloc_thread_info_node(struct task_struct *tsk, int node)
{
	struct thread_info *ret;

	ret = (struct thread_info *)__get_free_pages(GFP_KERNEL,
						     THREAD_INFO_ORDER);
#ifdef CONFIG_DEBUG_STACK_USAGE
	if (ret)
		memset(ret, 0, PAGE_SIZE << THREAD_INFO_ORDER);
#endif /* DEBUG_STACK_USAGE */

	return ret;
}

void free_thread_info(struct thread_info *ti)
{
	free_pages((unsigned long)ti, THREAD_INFO_ORDER);
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

/*
 * Workaround, until we find what's going on with Swift. When low on memory,
 * it sometimes loops in fault/handle_mm_fault incl. flush_tlb_page to find
 * out it is already in page tables/ fault again on the same instruction.
 * I really don't understand it, have checked it and contexts
 * are right, flush_tlb_all is done as well, and it faults again...
 * Strange. -jj
 *
 * The following code is a deadwood that may be necessary when
 * we start to make precise page flushes again. --zaitcev
 */
static void swift_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t *ptep)
{
#if 0
	static unsigned long last;
	unsigned int val;
	/* unsigned int n; */

	if (address == last) {
		val = srmmu_hwprobe(address);
		if (val != 0 && pte_val(*ptep) != val) {
			printk("swift_update_mmu_cache: "
			    "addr %lx put %08x probed %08x from %pf\n",
			    address, pte_val(*ptep), val,
			    __builtin_return_address(0));
			srmmu_flush_whole_tlb();
		}
	}
	last = address;
#endif
}

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

/* Cypress flushes. */
static void cypress_flush_cache_all(void)
{
	volatile unsigned long cypress_sucks;
	unsigned long faddr, tagval;

	flush_user_windows();
	for(faddr = 0; faddr < 0x10000; faddr += 0x20) {
		__asm__ __volatile__("lda [%1 + %2] %3, %0\n\t" :
				     "=r" (tagval) :
				     "r" (faddr), "r" (0x40000),
				     "i" (ASI_M_DATAC_TAG));

		/* If modified and valid, kick it. */
		if((tagval & 0x60) == 0x60)
			cypress_sucks = *(unsigned long *)(0xf0020000 + faddr);
	}
}

static void cypress_flush_cache_mm(struct mm_struct *mm)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long flags, faddr;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	local_irq_save(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	faddr = (0x10000 - 0x100);
	goto inside;
	do {
		faddr -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (faddr), "i" (ASI_M_FLUSH_CTX),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(faddr);
	srmmu_set_context(octx);
	local_irq_restore(flags);
	FLUSH_END
}

static void cypress_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	register unsigned long a, b, c, d, e, f, g;
	unsigned long flags, faddr;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	local_irq_save(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	start &= SRMMU_REAL_PMD_MASK;
	while(start < end) {
		faddr = (start + (0x10000 - 0x100));
		goto inside;
		do {
			faddr -= 0x100;
		inside:
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
					     "sta %%g0, [%0 + %2] %1\n\t"
					     "sta %%g0, [%0 + %3] %1\n\t"
					     "sta %%g0, [%0 + %4] %1\n\t"
					     "sta %%g0, [%0 + %5] %1\n\t"
					     "sta %%g0, [%0 + %6] %1\n\t"
					     "sta %%g0, [%0 + %7] %1\n\t"
					     "sta %%g0, [%0 + %8] %1\n\t" : :
					     "r" (faddr),
					     "i" (ASI_M_FLUSH_SEG),
					     "r" (a), "r" (b), "r" (c), "r" (d),
					     "r" (e), "r" (f), "r" (g));
		} while (faddr != start);
		start += SRMMU_REAL_PMD_SIZE;
	}
	srmmu_set_context(octx);
	local_irq_restore(flags);
	FLUSH_END
}

static void cypress_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags, line;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	local_irq_save(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
					     "sta %%g0, [%0 + %2] %1\n\t"
					     "sta %%g0, [%0 + %3] %1\n\t"
					     "sta %%g0, [%0 + %4] %1\n\t"
					     "sta %%g0, [%0 + %5] %1\n\t"
					     "sta %%g0, [%0 + %6] %1\n\t"
					     "sta %%g0, [%0 + %7] %1\n\t"
					     "sta %%g0, [%0 + %8] %1\n\t" : :
					     "r" (line),
					     "i" (ASI_M_FLUSH_PAGE),
					     "r" (a), "r" (b), "r" (c), "r" (d),
					     "r" (e), "r" (f), "r" (g));
	} while(line != page);
	srmmu_set_context(octx);
	local_irq_restore(flags);
	FLUSH_END
}

/* Cypress is copy-back, at least that is how we configure it. */
static void cypress_flush_page_to_ram(unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

/* Cypress is also IO cache coherent. */
static void cypress_flush_page_for_dma(unsigned long page)
{
}

/* Cypress has unified L2 VIPT, from which both instructions and data
 * are stored.  It does not have an onboard icache of any sort, therefore
 * no flush is necessary.
 */
static void cypress_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void cypress_flush_tlb_all(void)
{
	srmmu_flush_whole_tlb();
}

static void cypress_flush_tlb_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	__asm__ __volatile__(
	"lda	[%0] %3, %%g5\n\t"
	"sta	%2, [%0] %3\n\t"
	"sta	%%g0, [%1] %4\n\t"
	"sta	%%g5, [%0] %3\n"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (0x300), "r" (mm->context),
	  "i" (ASI_M_MMUREGS), "i" (ASI_M_FLUSH_PROBE)
	: "g5");
	FLUSH_END
}

static void cypress_flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long size;

	FLUSH_BEGIN(mm)
	start &= SRMMU_PGDIR_MASK;
	size = SRMMU_PGDIR_ALIGN(end) - start;
	__asm__ __volatile__(
		"lda	[%0] %5, %%g5\n\t"
		"sta	%1, [%0] %5\n"
		"1:\n\t"
		"subcc	%3, %4, %3\n\t"
		"bne	1b\n\t"
		" sta	%%g0, [%2 + %3] %6\n\t"
		"sta	%%g5, [%0] %5\n"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (mm->context), "r" (start | 0x200),
	  "r" (size), "r" (SRMMU_PGDIR_SIZE), "i" (ASI_M_MMUREGS),
	  "i" (ASI_M_FLUSH_PROBE)
	: "g5", "cc");
	FLUSH_END
}

static void cypress_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	FLUSH_BEGIN(mm)
	__asm__ __volatile__(
	"lda	[%0] %3, %%g5\n\t"
	"sta	%1, [%0] %3\n\t"
	"sta	%%g0, [%2] %4\n\t"
	"sta	%%g5, [%0] %3\n"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (mm->context), "r" (page & PAGE_MASK),
	  "i" (ASI_M_MMUREGS), "i" (ASI_M_FLUSH_PROBE)
	: "g5");
	FLUSH_END
}

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
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = pgd_offset_k(start);
		if(srmmu_pgd_none(*(pgd_t *)__nocache_fix(pgdp))) {
			pmdp = (pmd_t *) __srmmu_get_nocache(
			    SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(__nocache_fix(pmdp), 0, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(__nocache_fix(pgdp), pmdp);
		}
		pmdp = srmmu_pmd_offset(__nocache_fix(pgdp), start);
		if(srmmu_pmd_none(*(pmd_t *)__nocache_fix(pmdp))) {
			ptep = (pte_t *)__srmmu_get_nocache(PTE_SIZE, PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(__nocache_fix(ptep), 0, PTE_SIZE);
			srmmu_pmd_set(__nocache_fix(pmdp), ptep);
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
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = pgd_offset_k(start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = (pmd_t *)__srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(pmdp, 0, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = (pte_t *) __srmmu_get_nocache(PTE_SIZE,
							     PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(ptep, 0, PTE_SIZE);
			srmmu_pmd_set(pmdp, ptep);
		}
		if (start > (0xffffffffUL - PMD_SIZE))
			break;
		start = (start + PMD_SIZE) & PMD_MASK;
	}
}

/*
 * This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
static void __init srmmu_inherit_prom_mappings(unsigned long start,
					       unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int what = 0; /* 0 = normal-pte, 1 = pmd-level pte, 2 = pgd-level pte */
	unsigned long prompte;

	while(start <= end) {
		if (start == 0)
			break; /* probably wrap around */
		if(start == 0xfef00000)
			start = KADB_DEBUGGER_BEGVM;
		if(!(prompte = srmmu_hwprobe(start))) {
			start += PAGE_SIZE;
			continue;
		}
    
		/* A red snapper, see what it really is. */
		what = 0;
    
		if(!(start & ~(SRMMU_REAL_PMD_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_REAL_PMD_SIZE) == prompte)
				what = 1;
		}
    
		if(!(start & ~(SRMMU_PGDIR_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_PGDIR_SIZE) ==
			   prompte)
				what = 2;
		}
    
		pgdp = pgd_offset_k(start);
		if(what == 2) {
			*(pgd_t *)__nocache_fix(pgdp) = __pgd(prompte);
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if(srmmu_pgd_none(*(pgd_t *)__nocache_fix(pgdp))) {
			pmdp = (pmd_t *)__srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE, SRMMU_PMD_TABLE_SIZE);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(__nocache_fix(pmdp), 0, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(__nocache_fix(pgdp), pmdp);
		}
		pmdp = srmmu_pmd_offset(__nocache_fix(pgdp), start);
		if(srmmu_pmd_none(*(pmd_t *)__nocache_fix(pmdp))) {
			ptep = (pte_t *) __srmmu_get_nocache(PTE_SIZE,
							     PTE_SIZE);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(__nocache_fix(ptep), 0, PTE_SIZE);
			srmmu_pmd_set(__nocache_fix(pmdp), ptep);
		}
		if(what == 1) {
			/*
			 * We bend the rule where all 16 PTPs in a pmd_t point
			 * inside the same PTE page, and we leak a perfectly
			 * good hardware PTE piece. Alternatives seem worse.
			 */
			unsigned int x;	/* Index of HW PMD in soft cluster */
			x = (start >> PMD_SHIFT) & 15;
			*(unsigned long *)__nocache_fix(&pmdp->pmdv[x]) = prompte;
			start += SRMMU_REAL_PMD_SIZE;
			continue;
		}
		ptep = srmmu_pte_offset(__nocache_fix(pmdp), start);
		*(pte_t *)__nocache_fix(ptep) = __pte(prompte);
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

	while(vstart < vend) {
		do_large_mapping(vstart, pstart);
		vstart += SRMMU_PGDIR_SIZE; pstart += SRMMU_PGDIR_SIZE;
	}
	return vstart;
}

static inline void memprobe_error(char *msg)
{
	prom_printf(msg);
	prom_printf("Halting now...\n");
	prom_halt();
}

static inline void map_kernel(void)
{
	int i;

	if (phys_base > 0) {
		do_large_mapping(PAGE_OFFSET, phys_base);
	}

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		map_spbank((unsigned long)__va(sp_banks[i].base_addr), i);
	}

	BTFIXUPSET_SIMM13(user_ptrs_per_pgd, PAGE_OFFSET / SRMMU_PGDIR_SIZE);
}

/* Paging initialization on the Sparc Reference MMU. */
extern void sparc_context_init(int);

void (*poke_srmmu)(void) __cpuinitdata = NULL;

extern unsigned long bootmem_init(unsigned long *pages_avail);

void __init srmmu_paging_init(void)
{
	int i;
	phandle cpunode;
	char node_str[128];
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long pages_avail;

	sparc_iomap.start = SUN4M_IOBASE_VADDR;	/* 16MB of IOSPACE on all sun4m's. */

	if (sparc_cpu_model == sun4d)
		num_contexts = 65536; /* We know it is Viking */
	else {
		/* Find the number of contexts on the srmmu. */
		cpunode = prom_getchild(prom_root_node);
		num_contexts = 0;
		while(cpunode != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if(!strcmp(node_str, "cpu")) {
				num_contexts = prom_getintdefault(cpunode, "mmu-nctx", 0x8);
				break;
			}
			cpunode = prom_getsibling(cpunode);
		}
	}

	if(!num_contexts) {
		prom_printf("Something wrong, can't find cpu node in paging_init.\n");
		prom_halt();
	}

	pages_avail = 0;
	last_valid_pfn = bootmem_init(&pages_avail);

	srmmu_nocache_calcsize();
	srmmu_nocache_init();
        srmmu_inherit_prom_mappings(0xfe400000,(LINUX_OPPROM_ENDVM-PAGE_SIZE));
	map_kernel();

	/* ctx table has to be physically aligned to its size */
	srmmu_context_table = (ctxd_t *)__srmmu_get_nocache(num_contexts*sizeof(ctxd_t), num_contexts*sizeof(ctxd_t));
	srmmu_ctx_table_phys = (ctxd_t *)__nocache_pa((unsigned long)srmmu_context_table);

	for(i = 0; i < num_contexts; i++)
		srmmu_ctxd_set((ctxd_t *)__nocache_fix(&srmmu_context_table[i]), srmmu_swapper_pg_dir);

	flush_cache_all();
	srmmu_set_ctable_ptr((unsigned long)srmmu_ctx_table_phys);
#ifdef CONFIG_SMP
	/* Stop from hanging here... */
	local_flush_tlb_all();
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
	pmd = srmmu_pmd_offset(pgd, PKMAP_BASE);
	pte = srmmu_pte_offset(pmd, PKMAP_BASE);
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

static void srmmu_mmu_info(struct seq_file *m)
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

static void srmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

static void srmmu_destroy_context(struct mm_struct *mm)
{

	if(mm->context != NO_CONTEXT) {
		flush_cache_mm(mm);
		srmmu_ctxd_set(&srmmu_context_table[mm->context], srmmu_swapper_pg_dir);
		flush_tlb_mm(mm);
		spin_lock(&srmmu_context_spinlock);
		free_context(mm->context);
		spin_unlock(&srmmu_context_spinlock);
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
	while((nd = prom_getsibling(nd)) != 0) {
		prom_getstring(nd, "device_type", node_str, sizeof(node_str));
		if(!strcmp(node_str, "cpu")) {
			vac_line_size = prom_getint(nd, "cache-line-size");
			if (vac_line_size == -1) {
				prom_printf("can't determine cache-line-size, "
					    "halting.\n");
				prom_halt();
			}
			cache_lines = prom_getint(nd, "cache-nlines");
			if (cache_lines == -1) {
				prom_printf("can't determine cache-nlines, halting.\n");
				prom_halt();
			}

			vac_cache_size = cache_lines * vac_line_size;
#ifdef CONFIG_SMP
			if(vac_cache_size > max_size)
				max_size = vac_cache_size;
			if(vac_line_size < min_line_size)
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
	if(nd == 0) {
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

static void __cpuinit poke_hypersparc(void)
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

static void __init init_hypersparc(void)
{
	srmmu_name = "ROSS HyperSparc";
	srmmu_modtype = HyperSparc;

	init_vac_layout();

	is_hypersparc = 1;

	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_all, hypersparc_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, hypersparc_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, hypersparc_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, hypersparc_flush_cache_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, hypersparc_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, hypersparc_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, hypersparc_flush_tlb_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, hypersparc_flush_tlb_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, hypersparc_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, hypersparc_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, hypersparc_flush_page_for_dma, BTFIXUPCALL_NOP);


	poke_srmmu = poke_hypersparc;

	hypersparc_setup_blockops();
}

static void __cpuinit poke_cypress(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long faddr, tagval;
	volatile unsigned long cypress_sucks;
	volatile unsigned long clear;

	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	if (!(mreg & CYPRESS_CENABLE)) {
		for(faddr = 0x0; faddr < 0x10000; faddr += 20) {
			__asm__ __volatile__("sta %%g0, [%0 + %1] %2\n\t"
					     "sta %%g0, [%0] %2\n\t" : :
					     "r" (faddr), "r" (0x40000),
					     "i" (ASI_M_DATAC_TAG));
		}
	} else {
		for(faddr = 0; faddr < 0x10000; faddr += 0x20) {
			__asm__ __volatile__("lda [%1 + %2] %3, %0\n\t" :
					     "=r" (tagval) :
					     "r" (faddr), "r" (0x40000),
					     "i" (ASI_M_DATAC_TAG));

			/* If modified and valid, kick it. */
			if((tagval & 0x60) == 0x60)
				cypress_sucks = *(unsigned long *)
							(0xf0020000 + faddr);
		}
	}

	/* And one more, for our good neighbor, Mr. Broken Cypress. */
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	mreg |= (CYPRESS_CENABLE | CYPRESS_CMODE);
	srmmu_set_mmureg(mreg);
}

static void __init init_cypress_common(void)
{
	init_vac_layout();

	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_all, cypress_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, cypress_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, cypress_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, cypress_flush_cache_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, cypress_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, cypress_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, cypress_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, cypress_flush_tlb_range, BTFIXUPCALL_NORM);


	BTFIXUPSET_CALL(__flush_page_to_ram, cypress_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, cypress_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, cypress_flush_page_for_dma, BTFIXUPCALL_NOP);

	poke_srmmu = poke_cypress;
}

static void __init init_cypress_604(void)
{
	srmmu_name = "ROSS Cypress-604(UP)";
	srmmu_modtype = Cypress;
	init_cypress_common();
}

static void __init init_cypress_605(unsigned long mrev)
{
	srmmu_name = "ROSS Cypress-605(MP)";
	if(mrev == 0xe) {
		srmmu_modtype = Cypress_vE;
		hwbug_bitmask |= HWBUG_COPYBACK_BROKEN;
	} else {
		if(mrev == 0xd) {
			srmmu_modtype = Cypress_vD;
			hwbug_bitmask |= HWBUG_ASIFLUSH_BROKEN;
		} else {
			srmmu_modtype = Cypress;
		}
	}
	init_cypress_common();
}

static void __cpuinit poke_swift(void)
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

#define SWIFT_MASKID_ADDR  0x10003018
static void __init init_swift(void)
{
	unsigned long swift_rev;

	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     "srl %0, 0x18, %0\n\t" :
			     "=r" (swift_rev) :
			     "r" (SWIFT_MASKID_ADDR), "i" (ASI_M_BYPASS));
	srmmu_name = "Fujitsu Swift";
	switch(swift_rev) {
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

	BTFIXUPSET_CALL(flush_cache_all, swift_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, swift_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, swift_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, swift_flush_cache_range, BTFIXUPCALL_NORM);


	BTFIXUPSET_CALL(flush_tlb_all, swift_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, swift_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, swift_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, swift_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, swift_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, swift_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, swift_flush_page_for_dma, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(update_mmu_cache, swift_update_mmu_cache, BTFIXUPCALL_NORM);

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

	if (srmmu_hwprobe(page))
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


static void __cpuinit poke_turbosparc(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long ccreg;

	/* Clear any crap from the cache or else... */
	turbosparc_flush_cache_all();
	mreg &= ~(TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE); /* Temporarily disable I & D caches */
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
	turbosparc_set_ccreg (ccreg);

	mreg |= (TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE); /* I & D caches on */
	mreg |= (TURBOSPARC_ICSNOOP);		/* Icache snooping on */
	srmmu_set_mmureg(mreg);
}

static void __init init_turbosparc(void)
{
	srmmu_name = "Fujitsu TurboSparc";
	srmmu_modtype = TurboSparc;

	BTFIXUPSET_CALL(flush_cache_all, turbosparc_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, turbosparc_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, turbosparc_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, turbosparc_flush_cache_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, turbosparc_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, turbosparc_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, turbosparc_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, turbosparc_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, turbosparc_flush_page_to_ram, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_sig_insns, turbosparc_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, turbosparc_flush_page_for_dma, BTFIXUPCALL_NORM);

	poke_srmmu = poke_turbosparc;
}

static void __cpuinit poke_tsunami(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

static void __init init_tsunami(void)
{
	/*
	 * Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	srmmu_name = "TI Tsunami";
	srmmu_modtype = Tsunami;

	BTFIXUPSET_CALL(flush_cache_all, tsunami_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, tsunami_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, tsunami_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, tsunami_flush_cache_range, BTFIXUPCALL_NORM);


	BTFIXUPSET_CALL(flush_tlb_all, tsunami_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, tsunami_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, tsunami_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, tsunami_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, tsunami_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, tsunami_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, tsunami_flush_page_for_dma, BTFIXUPCALL_NORM);

	poke_srmmu = poke_tsunami;

	tsunami_setup_blockops();
}

static void __cpuinit poke_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	static int smp_catch;

	if(viking_mxcc_present) {
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
		if(smp_catch++) {
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

static void __init init_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */
	if(mreg & VIKING_MMODE) {
		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;
		msi_set_sync();

		BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);

		/*
		 * We need this to make sure old viking takes no hits
		 * on it's cache for dma snoops to workaround the
		 * "load from non-cacheable memory" interrupt bug.
		 * This is only necessary because of the new way in
		 * which we use the IOMMU.
		 */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page, BTFIXUPCALL_NORM);

		flush_page_for_dma_global = 0;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;

		srmmu_cache_pagetables = 1;

		/* MXCC vikings lack the DMA snooping bug. */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page_for_dma, BTFIXUPCALL_NOP);
	}

	BTFIXUPSET_CALL(flush_cache_all, viking_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, viking_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, viking_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, viking_flush_cache_range, BTFIXUPCALL_NORM);

#ifdef CONFIG_SMP
	if (sparc_cpu_model == sun4d) {
		BTFIXUPSET_CALL(flush_tlb_all, sun4dsmp_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, sun4dsmp_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, sun4dsmp_flush_tlb_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, sun4dsmp_flush_tlb_range, BTFIXUPCALL_NORM);
	} else
#endif
	{
		BTFIXUPSET_CALL(flush_tlb_all, viking_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, viking_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, viking_flush_tlb_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, viking_flush_tlb_range, BTFIXUPCALL_NORM);
	}

	BTFIXUPSET_CALL(__flush_page_to_ram, viking_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, viking_flush_sig_insns, BTFIXUPCALL_NOP);

	poke_srmmu = poke_viking;
}

#ifdef CONFIG_SPARC_LEON

void __init poke_leonsparc(void)
{
}

void __init init_leon(void)
{

	srmmu_name = "LEON";

	BTFIXUPSET_CALL(flush_cache_all, leon_flush_cache_all,
			BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, leon_flush_cache_all,
			BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, leon_flush_pcache_all,
			BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, leon_flush_cache_all,
			BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, leon_flush_dcache_all,
			BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, leon_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, leon_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, leon_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, leon_flush_tlb_all, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, leon_flush_cache_all,
			BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, leon_flush_cache_all, BTFIXUPCALL_NOP);

	poke_srmmu = poke_leonsparc;

	srmmu_cache_pagetables = 0;

	leon_flush_during_switch = leon_flush_needed();
}
#endif

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
	if(mod_typ == 1) {
		switch(mod_rev) {
		case 7:
			/* UP or MP Hypersparc */
			init_hypersparc();
			break;
		case 0:
		case 2:
			/* Uniprocessor Cypress */
			init_cypress_604();
			break;
		case 10:
		case 11:
		case 12:
			/* _REALLY OLD_ Cypress MP chips... */
		case 13:
		case 14:
		case 15:
			/* MP Cypress mmu/cache-controller */
			init_cypress_605(mod_rev);
			break;
		default:
			/* Some other Cypress revision, assume a 605. */
			init_cypress_605(mod_rev);
			break;
		}
		return;
	}
	
	/*
	 * Now Fujitsu TurboSparc. It might happen that it is
	 * in Swift emulation mode, so we will check later...
	 */
	if (psr_typ == 0 && psr_vers == 5) {
		init_turbosparc();
		return;
	}

	/* Next check for Fujitsu Swift. */
	if(psr_typ == 0 && psr_vers == 4) {
		phandle cpunode;
		char node_str[128];

		/* Look if it is not a TurboSparc emulating Swift... */
		cpunode = prom_getchild(prom_root_node);
		while((cpunode = prom_getsibling(cpunode)) != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if(!strcmp(node_str, "cpu")) {
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
	if(psr_typ == 4 &&
	   ((psr_vers == 0) ||
	    ((psr_vers == 1) && (mod_typ == 0) && (mod_rev == 0)))) {
		init_viking();
		return;
	}

	/* Finally the Tsunami. */
	if(psr_typ == 4 && psr_vers == 1 && (mod_typ || mod_rev)) {
		init_tsunami();
		return;
	}

	/* Oh well */
	srmmu_is_bad();
}

/* don't laugh, static pagetables */
static void srmmu_check_pgt_cache(int low, int high)
{
}

extern unsigned long spwin_mmu_patchme, fwin_mmu_patchme,
	tsetup_mmu_patchme, rtrap_mmu_patchme;

extern unsigned long spwin_srmmu_stackchk, srmmu_fwin_stackchk,
	tsetup_srmmu_stackchk, srmmu_rett_stackchk;

#ifdef CONFIG_SMP
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) BTFIXUP_CALL(local_flush_page_for_dma), page);
	local_flush_page_for_dma(page);
}

#endif

/* Load up routines and constants for sun4m and sun4d mmu */
void __init ld_mmu_srmmu(void)
{
	extern void ld_mmu_iommu(void);
	extern void ld_mmu_iounit(void);
	extern void ___xchg32_sun4md(void);

	BTFIXUPSET_SIMM13(pgdir_shift, SRMMU_PGDIR_SHIFT);
	BTFIXUPSET_SETHI(pgdir_size, SRMMU_PGDIR_SIZE);
	BTFIXUPSET_SETHI(pgdir_mask, SRMMU_PGDIR_MASK);

	BTFIXUPSET_SIMM13(ptrs_per_pmd, SRMMU_PTRS_PER_PMD);
	BTFIXUPSET_SIMM13(ptrs_per_pgd, SRMMU_PTRS_PER_PGD);

	BTFIXUPSET_INT(page_none, pgprot_val(SRMMU_PAGE_NONE));
	PAGE_SHARED = pgprot_val(SRMMU_PAGE_SHARED);
	BTFIXUPSET_INT(page_copy, pgprot_val(SRMMU_PAGE_COPY));
	BTFIXUPSET_INT(page_readonly, pgprot_val(SRMMU_PAGE_RDONLY));
	BTFIXUPSET_INT(page_kernel, pgprot_val(SRMMU_PAGE_KERNEL));
	page_kernel = pgprot_val(SRMMU_PAGE_KERNEL);

	/* Functions */
#ifndef CONFIG_SMP	
	BTFIXUPSET_CALL(___xchg32, ___xchg32_sun4md, BTFIXUPCALL_SWAPG1G2);
#endif
	BTFIXUPSET_CALL(do_check_pgt_cache, srmmu_check_pgt_cache, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(set_pte, srmmu_set_pte, BTFIXUPCALL_SWAPO0O1);
	BTFIXUPSET_CALL(switch_mm, srmmu_switch_mm, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pte_pfn, srmmu_pte_pfn, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_page, srmmu_pmd_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_page_vaddr, srmmu_pgd_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pte_present, srmmu_pte_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(pmd_bad, srmmu_pmd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_present, srmmu_pmd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(pgd_none, srmmu_pgd_none, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_bad, srmmu_pgd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_present, srmmu_pgd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(mk_pte, srmmu_mk_pte, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_phys, srmmu_mk_pte_phys, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_io, srmmu_mk_pte_io, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_set, srmmu_pgd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_set, srmmu_pmd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_populate, srmmu_pmd_populate, BTFIXUPCALL_NORM);
	
	BTFIXUPSET_INT(pte_modify_mask, SRMMU_CHG_MASK);
	BTFIXUPSET_CALL(pmd_offset, srmmu_pmd_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_offset_kernel, srmmu_pte_offset, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(free_pte_fast, srmmu_free_pte_fast, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_free, srmmu_pte_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc_one_kernel, srmmu_pte_alloc_one_kernel, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc_one, srmmu_pte_alloc_one, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(free_pmd_fast, srmmu_pmd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_alloc_one, srmmu_pmd_alloc_one, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(free_pgd_fast, srmmu_free_pgd_fast, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(get_pgd_fast, srmmu_get_pgd_fast, BTFIXUPCALL_NORM);

	BTFIXUPSET_HALF(pte_writei, SRMMU_WRITE);
	BTFIXUPSET_HALF(pte_dirtyi, SRMMU_DIRTY);
	BTFIXUPSET_HALF(pte_youngi, SRMMU_REF);
	BTFIXUPSET_HALF(pte_filei, SRMMU_FILE);
	BTFIXUPSET_HALF(pte_wrprotecti, SRMMU_WRITE);
	BTFIXUPSET_HALF(pte_mkcleani, SRMMU_DIRTY);
	BTFIXUPSET_HALF(pte_mkoldi, SRMMU_REF);
	BTFIXUPSET_CALL(pte_mkwrite, srmmu_pte_mkwrite, BTFIXUPCALL_ORINT(SRMMU_WRITE));
	BTFIXUPSET_CALL(pte_mkdirty, srmmu_pte_mkdirty, BTFIXUPCALL_ORINT(SRMMU_DIRTY));
	BTFIXUPSET_CALL(pte_mkyoung, srmmu_pte_mkyoung, BTFIXUPCALL_ORINT(SRMMU_REF));
	BTFIXUPSET_CALL(update_mmu_cache, srmmu_update_mmu_cache, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(destroy_context, srmmu_destroy_context, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(sparc_mapiorange, srmmu_mapiorange, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(sparc_unmapiorange, srmmu_unmapiorange, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__swp_type, srmmu_swp_type, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__swp_offset, srmmu_swp_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__swp_entry, srmmu_swp_entry, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(mmu_info, srmmu_mmu_info, BTFIXUPCALL_NORM);

	get_srmmu_type();

#ifdef CONFIG_SMP
	/* El switcheroo... */

	BTFIXUPCOPY_CALL(local_flush_cache_all, flush_cache_all);
	BTFIXUPCOPY_CALL(local_flush_cache_mm, flush_cache_mm);
	BTFIXUPCOPY_CALL(local_flush_cache_range, flush_cache_range);
	BTFIXUPCOPY_CALL(local_flush_cache_page, flush_cache_page);
	BTFIXUPCOPY_CALL(local_flush_tlb_all, flush_tlb_all);
	BTFIXUPCOPY_CALL(local_flush_tlb_mm, flush_tlb_mm);
	BTFIXUPCOPY_CALL(local_flush_tlb_range, flush_tlb_range);
	BTFIXUPCOPY_CALL(local_flush_tlb_page, flush_tlb_page);
	BTFIXUPCOPY_CALL(local_flush_page_to_ram, __flush_page_to_ram);
	BTFIXUPCOPY_CALL(local_flush_sig_insns, flush_sig_insns);
	BTFIXUPCOPY_CALL(local_flush_page_for_dma, flush_page_for_dma);

	BTFIXUPSET_CALL(flush_cache_all, smp_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, smp_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, smp_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, smp_flush_cache_page, BTFIXUPCALL_NORM);
	if (sparc_cpu_model != sun4d &&
	    sparc_cpu_model != sparc_leon) {
		BTFIXUPSET_CALL(flush_tlb_all, smp_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, smp_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, smp_flush_tlb_range, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, smp_flush_tlb_page, BTFIXUPCALL_NORM);
	}
	BTFIXUPSET_CALL(__flush_page_to_ram, smp_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, smp_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, smp_flush_page_for_dma, BTFIXUPCALL_NORM);

	if (poke_srmmu == poke_viking) {
		/* Avoid unnecessary cross calls. */
		BTFIXUPCOPY_CALL(flush_cache_all, local_flush_cache_all);
		BTFIXUPCOPY_CALL(flush_cache_mm, local_flush_cache_mm);
		BTFIXUPCOPY_CALL(flush_cache_range, local_flush_cache_range);
		BTFIXUPCOPY_CALL(flush_cache_page, local_flush_cache_page);
		BTFIXUPCOPY_CALL(__flush_page_to_ram, local_flush_page_to_ram);
		BTFIXUPCOPY_CALL(flush_sig_insns, local_flush_sig_insns);
		BTFIXUPCOPY_CALL(flush_page_for_dma, local_flush_page_for_dma);
	}
#endif

	if (sparc_cpu_model == sun4d)
		ld_mmu_iounit();
	else
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

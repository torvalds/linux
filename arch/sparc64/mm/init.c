/*  $Id: init.c,v 1.209 2002/02/09 19:49:31 davem Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996-1999 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997-1999 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/initrd.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/cache.h>
#include <linux/sort.h>

#include <asm/head.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/iommu.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/dma.h>
#include <asm/starfire.h>
#include <asm/tlb.h>
#include <asm/spitfire.h>
#include <asm/sections.h>

extern void device_scan(void);

#define MAX_BANKS	32

static struct linux_prom64_registers pavail[MAX_BANKS] __initdata;
static struct linux_prom64_registers pavail_rescan[MAX_BANKS] __initdata;
static int pavail_ents __initdata;
static int pavail_rescan_ents __initdata;

static int cmp_p64(const void *a, const void *b)
{
	const struct linux_prom64_registers *x = a, *y = b;

	if (x->phys_addr > y->phys_addr)
		return 1;
	if (x->phys_addr < y->phys_addr)
		return -1;
	return 0;
}

static void __init read_obp_memory(const char *property,
				   struct linux_prom64_registers *regs,
				   int *num_ents)
{
	int node = prom_finddevice("/memory");
	int prop_size = prom_getproplen(node, property);
	int ents, ret, i;

	ents = prop_size / sizeof(struct linux_prom64_registers);
	if (ents > MAX_BANKS) {
		prom_printf("The machine has more %s property entries than "
			    "this kernel can support (%d).\n",
			    property, MAX_BANKS);
		prom_halt();
	}

	ret = prom_getproperty(node, property, (char *) regs, prop_size);
	if (ret == -1) {
		prom_printf("Couldn't get %s property from /memory.\n");
		prom_halt();
	}

	*num_ents = ents;

	/* Sanitize what we got from the firmware, by page aligning
	 * everything.
	 */
	for (i = 0; i < ents; i++) {
		unsigned long base, size;

		base = regs[i].phys_addr;
		size = regs[i].reg_size;

		size &= PAGE_MASK;
		if (base & ~PAGE_MASK) {
			unsigned long new_base = PAGE_ALIGN(base);

			size -= new_base - base;
			if ((long) size < 0L)
				size = 0UL;
			base = new_base;
		}
		regs[i].phys_addr = base;
		regs[i].reg_size = size;
	}
	sort(regs, ents, sizeof(struct  linux_prom64_registers),
	     cmp_p64, NULL);
}

unsigned long *sparc64_valid_addr_bitmap __read_mostly;

/* Ugly, but necessary... -DaveM */
unsigned long phys_base __read_mostly;
unsigned long kern_base __read_mostly;
unsigned long kern_size __read_mostly;
unsigned long pfn_base __read_mostly;

/* get_new_mmu_context() uses "cache + 1".  */
DEFINE_SPINLOCK(ctx_alloc_lock);
unsigned long tlb_context_cache = CTX_FIRST_VERSION - 1;
#define CTX_BMAP_SLOTS (1UL << (CTX_NR_BITS - 6))
unsigned long mmu_context_bmap[CTX_BMAP_SLOTS];

/* References to special section boundaries */
extern char  _start[], _end[];

/* Initial ramdisk setup */
extern unsigned long sparc_ramdisk_image64;
extern unsigned int sparc_ramdisk_image;
extern unsigned int sparc_ramdisk_size;

struct page *mem_map_zero __read_mostly;

int bigkernel = 0;

/* XXX Tune this... */
#define PGT_CACHE_LOW	25
#define PGT_CACHE_HIGH	50

void check_pgt_cache(void)
{
	preempt_disable();
	if (pgtable_cache_size > PGT_CACHE_HIGH) {
		do {
			if (pgd_quicklist)
				free_pgd_slow(get_pgd_fast());
			if (pte_quicklist[0])
				free_pte_slow(pte_alloc_one_fast(NULL, 0));
			if (pte_quicklist[1])
				free_pte_slow(pte_alloc_one_fast(NULL, 1 << (PAGE_SHIFT + 10)));
		} while (pgtable_cache_size > PGT_CACHE_LOW);
	}
	preempt_enable();
}

#ifdef CONFIG_DEBUG_DCFLUSH
atomic_t dcpage_flushes = ATOMIC_INIT(0);
#ifdef CONFIG_SMP
atomic_t dcpage_flushes_xcall = ATOMIC_INIT(0);
#endif
#endif

__inline__ void flush_dcache_page_impl(struct page *page)
{
#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes);
#endif

#ifdef DCACHE_ALIASING_POSSIBLE
	__flush_dcache_page(page_address(page),
			    ((tlb_type == spitfire) &&
			     page_mapping(page) != NULL));
#else
	if (page_mapping(page) != NULL &&
	    tlb_type == spitfire)
		__flush_icache_page(__pa(page_address(page)));
#endif
}

#define PG_dcache_dirty		PG_arch_1
#define PG_dcache_cpu_shift	24
#define PG_dcache_cpu_mask	(256 - 1)

#if NR_CPUS > 256
#error D-cache dirty tracking and thread_info->cpu need fixing for > 256 cpus
#endif

#define dcache_dirty_cpu(page) \
	(((page)->flags >> PG_dcache_cpu_shift) & PG_dcache_cpu_mask)

static __inline__ void set_dcache_dirty(struct page *page, int this_cpu)
{
	unsigned long mask = this_cpu;
	unsigned long non_cpu_bits;

	non_cpu_bits = ~(PG_dcache_cpu_mask << PG_dcache_cpu_shift);
	mask = (mask << PG_dcache_cpu_shift) | (1UL << PG_dcache_dirty);

	__asm__ __volatile__("1:\n\t"
			     "ldx	[%2], %%g7\n\t"
			     "and	%%g7, %1, %%g1\n\t"
			     "or	%%g1, %0, %%g1\n\t"
			     "casx	[%2], %%g7, %%g1\n\t"
			     "cmp	%%g7, %%g1\n\t"
			     "membar	#StoreLoad | #StoreStore\n\t"
			     "bne,pn	%%xcc, 1b\n\t"
			     " nop"
			     : /* no outputs */
			     : "r" (mask), "r" (non_cpu_bits), "r" (&page->flags)
			     : "g1", "g7");
}

static __inline__ void clear_dcache_dirty_cpu(struct page *page, unsigned long cpu)
{
	unsigned long mask = (1UL << PG_dcache_dirty);

	__asm__ __volatile__("! test_and_clear_dcache_dirty\n"
			     "1:\n\t"
			     "ldx	[%2], %%g7\n\t"
			     "srlx	%%g7, %4, %%g1\n\t"
			     "and	%%g1, %3, %%g1\n\t"
			     "cmp	%%g1, %0\n\t"
			     "bne,pn	%%icc, 2f\n\t"
			     " andn	%%g7, %1, %%g1\n\t"
			     "casx	[%2], %%g7, %%g1\n\t"
			     "cmp	%%g7, %%g1\n\t"
			     "membar	#StoreLoad | #StoreStore\n\t"
			     "bne,pn	%%xcc, 1b\n\t"
			     " nop\n"
			     "2:"
			     : /* no outputs */
			     : "r" (cpu), "r" (mask), "r" (&page->flags),
			       "i" (PG_dcache_cpu_mask),
			       "i" (PG_dcache_cpu_shift)
			     : "g1", "g7");
}

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct page *page;
	unsigned long pfn;
	unsigned long pg_flags;

	pfn = pte_pfn(pte);
	if (pfn_valid(pfn) &&
	    (page = pfn_to_page(pfn), page_mapping(page)) &&
	    ((pg_flags = page->flags) & (1UL << PG_dcache_dirty))) {
		int cpu = ((pg_flags >> PG_dcache_cpu_shift) &
			   PG_dcache_cpu_mask);
		int this_cpu = get_cpu();

		/* This is just to optimize away some function calls
		 * in the SMP case.
		 */
		if (cpu == this_cpu)
			flush_dcache_page_impl(page);
		else
			smp_flush_dcache_page_impl(page, cpu);

		clear_dcache_dirty_cpu(page, cpu);

		put_cpu();
	}
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;
	int this_cpu;

	/* Do not bother with the expensive D-cache flush if it
	 * is merely the zero page.  The 'bigcore' testcase in GDB
	 * causes this case to run millions of times.
	 */
	if (page == ZERO_PAGE(0))
		return;

	this_cpu = get_cpu();

	mapping = page_mapping(page);
	if (mapping && !mapping_mapped(mapping)) {
		int dirty = test_bit(PG_dcache_dirty, &page->flags);
		if (dirty) {
			int dirty_cpu = dcache_dirty_cpu(page);

			if (dirty_cpu == this_cpu)
				goto out;
			smp_flush_dcache_page_impl(page, dirty_cpu);
		}
		set_dcache_dirty(page, this_cpu);
	} else {
		/* We could delay the flush for the !page_mapping
		 * case too.  But that case is for exec env/arg
		 * pages and those are %99 certainly going to get
		 * faulted into the tlb (and thus flushed) anyways.
		 */
		flush_dcache_page_impl(page);
	}

out:
	put_cpu();
}

void __kprobes flush_icache_range(unsigned long start, unsigned long end)
{
	/* Cheetah has coherent I-cache. */
	if (tlb_type == spitfire) {
		unsigned long kaddr;

		for (kaddr = start; kaddr < end; kaddr += PAGE_SIZE)
			__flush_icache_page(__get_phys(kaddr));
	}
}

unsigned long page_to_pfn(struct page *page)
{
	return (unsigned long) ((page - mem_map) + pfn_base);
}

struct page *pfn_to_page(unsigned long pfn)
{
	return (mem_map + (pfn - pfn_base));
}

void show_mem(void)
{
	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n",
	       nr_swap_pages << (PAGE_SHIFT-10));
	printk("%ld pages of RAM\n", num_physpages);
	printk("%d free pages\n", nr_free_pages());
	printk("%d pages in page table cache\n",pgtable_cache_size);
}

void mmu_info(struct seq_file *m)
{
	if (tlb_type == cheetah)
		seq_printf(m, "MMU Type\t: Cheetah\n");
	else if (tlb_type == cheetah_plus)
		seq_printf(m, "MMU Type\t: Cheetah+\n");
	else if (tlb_type == spitfire)
		seq_printf(m, "MMU Type\t: Spitfire\n");
	else
		seq_printf(m, "MMU Type\t: ???\n");

#ifdef CONFIG_DEBUG_DCFLUSH
	seq_printf(m, "DCPageFlushes\t: %d\n",
		   atomic_read(&dcpage_flushes));
#ifdef CONFIG_SMP
	seq_printf(m, "DCPageFlushesXC\t: %d\n",
		   atomic_read(&dcpage_flushes_xcall));
#endif /* CONFIG_SMP */
#endif /* CONFIG_DEBUG_DCFLUSH */
}

struct linux_prom_translation {
	unsigned long virt;
	unsigned long size;
	unsigned long data;
};
static struct linux_prom_translation prom_trans[512] __initdata;

extern unsigned long prom_boot_page;
extern void prom_remap(unsigned long physpage, unsigned long virtpage, int mmu_ihandle);
extern int prom_get_mmu_ihandle(void);
extern void register_prom_callbacks(void);

/* Exported for SMP bootup purposes. */
unsigned long kern_locked_tte_data;

/* Exported for kernel TLB miss handling in ktlb.S */
unsigned long prom_pmd_phys __read_mostly;
unsigned int swapper_pgd_zero __read_mostly;

/* Allocate power-of-2 aligned chunks from the end of the
 * kernel image.  Return physical address.
 */
static inline unsigned long early_alloc_phys(unsigned long size)
{
	unsigned long base;

	BUILD_BUG_ON(size & (size - 1));

	kern_size = (kern_size + (size - 1)) & ~(size - 1);
	base = kern_base + kern_size;
	kern_size += size;

	return base;
}

static inline unsigned long load_phys32(unsigned long pa)
{
	unsigned long val;

	__asm__ __volatile__("lduwa	[%1] %2, %0"
			     : "=&r" (val)
			     : "r" (pa), "i" (ASI_PHYS_USE_EC));

	return val;
}

static inline unsigned long load_phys64(unsigned long pa)
{
	unsigned long val;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=&r" (val)
			     : "r" (pa), "i" (ASI_PHYS_USE_EC));

	return val;
}

static inline void store_phys32(unsigned long pa, unsigned long val)
{
	__asm__ __volatile__("stwa	%0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (pa), "i" (ASI_PHYS_USE_EC));
}

static inline void store_phys64(unsigned long pa, unsigned long val)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (pa), "i" (ASI_PHYS_USE_EC));
}

#define BASE_PAGE_SIZE 8192

/*
 * Translate PROM's mapping we capture at boot time into physical address.
 * The second parameter is only set from prom_callback() invocations.
 */
unsigned long prom_virt_to_phys(unsigned long promva, int *error)
{
	unsigned long pmd_phys = (prom_pmd_phys +
				  ((promva >> 23) & 0x7ff) * sizeof(pmd_t));
	unsigned long pte_phys;
	pmd_t pmd_ent;
	pte_t pte_ent;
	unsigned long base;

	pmd_val(pmd_ent) = load_phys32(pmd_phys);
	if (pmd_none(pmd_ent)) {
		if (error)
			*error = 1;
		return 0;
	}

	pte_phys = (unsigned long)pmd_val(pmd_ent) << 11UL;
	pte_phys += ((promva >> 13) & 0x3ff) * sizeof(pte_t);
	pte_val(pte_ent) = load_phys64(pte_phys);
	if (!pte_present(pte_ent)) {
		if (error)
			*error = 1;
		return 0;
	}
	if (error) {
		*error = 0;
		return pte_val(pte_ent);
	}
	base = pte_val(pte_ent) & _PAGE_PADDR;
	return (base + (promva & (BASE_PAGE_SIZE - 1)));
}

/* The obp translations are saved based on 8k pagesize, since obp can
 * use a mixture of pagesizes. Misses to the LOW_OBP_ADDRESS ->
 * HI_OBP_ADDRESS range are handled in entry.S and do not use the vpte
 * scheme (also, see rant in inherit_locked_prom_mappings()).
 */
static void __init build_obp_range(unsigned long start, unsigned long end, unsigned long data)
{
	unsigned long vaddr;

	for (vaddr = start; vaddr < end; vaddr += BASE_PAGE_SIZE) {
		unsigned long val, pte_phys, pmd_phys;
		pmd_t pmd_ent;
		int i;

		pmd_phys = (prom_pmd_phys +
			    (((vaddr >> 23) & 0x7ff) * sizeof(pmd_t)));
		pmd_val(pmd_ent) = load_phys32(pmd_phys);
		if (pmd_none(pmd_ent)) {
			pte_phys = early_alloc_phys(BASE_PAGE_SIZE);

			for (i = 0; i < BASE_PAGE_SIZE / sizeof(pte_t); i++)
				store_phys64(pte_phys+i*sizeof(pte_t),0);

			pmd_val(pmd_ent) = pte_phys >> 11UL;
			store_phys32(pmd_phys, pmd_val(pmd_ent));
		}

		pte_phys = (unsigned long)pmd_val(pmd_ent) << 11UL;
		pte_phys += (((vaddr >> 13) & 0x3ff) * sizeof(pte_t));

		val = data;

		/* Clear diag TTE bits. */
		if (tlb_type == spitfire)
			val &= ~0x0003fe0000000000UL;

		store_phys64(pte_phys, val | _PAGE_MODIFIED);

		data += BASE_PAGE_SIZE;
	}
}

static inline int in_obp_range(unsigned long vaddr)
{
	return (vaddr >= LOW_OBP_ADDRESS &&
		vaddr < HI_OBP_ADDRESS);
}

#define OBP_PMD_SIZE 2048
static void __init build_obp_pgtable(int prom_trans_ents)
{
	unsigned long i;

	prom_pmd_phys = early_alloc_phys(OBP_PMD_SIZE);
	for (i = 0; i < OBP_PMD_SIZE; i += 4)
		store_phys32(prom_pmd_phys + i, 0);

	for (i = 0; i < prom_trans_ents; i++) {
		unsigned long start, end;

		if (!in_obp_range(prom_trans[i].virt))
			continue;

		start = prom_trans[i].virt;
		end = start + prom_trans[i].size;
		if (end > HI_OBP_ADDRESS)
			end = HI_OBP_ADDRESS;

		build_obp_range(start, end, prom_trans[i].data);
	}
}

/* Read OBP translations property into 'prom_trans[]'.
 * Return the number of entries.
 */
static int __init read_obp_translations(void)
{
	int n, node;

	node = prom_finddevice("/virtual-memory");
	n = prom_getproplen(node, "translations");
	if (unlikely(n == 0 || n == -1)) {
		prom_printf("prom_mappings: Couldn't get size.\n");
		prom_halt();
	}
	if (unlikely(n > sizeof(prom_trans))) {
		prom_printf("prom_mappings: Size %Zd is too big.\n", n);
		prom_halt();
	}

	if ((n = prom_getproperty(node, "translations",
				  (char *)&prom_trans[0],
				  sizeof(prom_trans))) == -1) {
		prom_printf("prom_mappings: Couldn't get property.\n");
		prom_halt();
	}
	n = n / sizeof(struct linux_prom_translation);
	return n;
}

static void __init remap_kernel(void)
{
	unsigned long phys_page, tte_vaddr, tte_data;
	int tlb_ent = sparc64_highest_locked_tlbent();

	tte_vaddr = (unsigned long) KERNBASE;
	phys_page = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	tte_data = (phys_page | (_PAGE_VALID | _PAGE_SZ4MB |
				 _PAGE_CP | _PAGE_CV | _PAGE_P |
				 _PAGE_L | _PAGE_W));

	kern_locked_tte_data = tte_data;

	/* Now lock us into the TLBs via OBP. */
	prom_dtlb_load(tlb_ent, tte_data, tte_vaddr);
	prom_itlb_load(tlb_ent, tte_data, tte_vaddr);
	if (bigkernel) {
		prom_dtlb_load(tlb_ent - 1,
			       tte_data + 0x400000, 
			       tte_vaddr + 0x400000);
		prom_itlb_load(tlb_ent - 1,
			       tte_data + 0x400000, 
			       tte_vaddr + 0x400000);
	}
}

static void __init inherit_prom_mappings(void)
{
	int n;

	n = read_obp_translations();
	build_obp_pgtable(n);

	/* Now fixup OBP's idea about where we really are mapped. */
	prom_printf("Remapping the kernel... ");
	remap_kernel();

	prom_printf("done.\n");

	register_prom_callbacks();
}

/* The OBP specifications for sun4u mark 0xfffffffc00000000 and
 * upwards as reserved for use by the firmware (I wonder if this
 * will be the same on Cheetah...).  We use this virtual address
 * range for the VPTE table mappings of the nucleus so we need
 * to zap them when we enter the PROM.  -DaveM
 */
static void __flush_nucleus_vptes(void)
{
	unsigned long prom_reserved_base = 0xfffffffc00000000UL;
	int i;

	/* Only DTLB must be checked for VPTE entries. */
	if (tlb_type == spitfire) {
		for (i = 0; i < 63; i++) {
			unsigned long tag;

			/* Spitfire Errata #32 workaround */
			/* NOTE: Always runs on spitfire, so no cheetah+
			 *       page size encodings.
			 */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			tag = spitfire_get_dtlb_tag(i);
			if (((tag & ~(PAGE_MASK)) == 0) &&
			    ((tag &  (PAGE_MASK)) >= prom_reserved_base)) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : /* no outputs */
						     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				spitfire_put_dtlb_data(i, 0x0UL);
			}
		}
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		for (i = 0; i < 512; i++) {
			unsigned long tag = cheetah_get_dtlb_tag(i, 2);

			if ((tag & ~PAGE_MASK) == 0 &&
			    (tag & PAGE_MASK) >= prom_reserved_base) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : /* no outputs */
						     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				cheetah_put_dtlb_data(i, 0x0UL, 2);
			}

			if (tlb_type != cheetah_plus)
				continue;

			tag = cheetah_get_dtlb_tag(i, 3);

			if ((tag & ~PAGE_MASK) == 0 &&
			    (tag & PAGE_MASK) >= prom_reserved_base) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : /* no outputs */
						     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				cheetah_put_dtlb_data(i, 0x0UL, 3);
			}
		}
	} else {
		/* Implement me :-) */
		BUG();
	}
}

static int prom_ditlb_set;
struct prom_tlb_entry {
	int		tlb_ent;
	unsigned long	tlb_tag;
	unsigned long	tlb_data;
};
struct prom_tlb_entry prom_itlb[16], prom_dtlb[16];

void prom_world(int enter)
{
	unsigned long pstate;
	int i;

	if (!enter)
		set_fs((mm_segment_t) { get_thread_current_ds() });

	if (!prom_ditlb_set)
		return;

	/* Make sure the following runs atomically. */
	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	if (enter) {
		/* Kick out nucleus VPTEs. */
		__flush_nucleus_vptes();

		/* Install PROM world. */
		for (i = 0; i < 16; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2\n\t"
						     "membar #Sync"
					: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_DMMU));
				if (tlb_type == spitfire)
					spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
							       prom_dtlb[i].tlb_data);
				else if (tlb_type == cheetah || tlb_type == cheetah_plus)
					cheetah_put_ldtlb_data(prom_dtlb[i].tlb_ent,
							       prom_dtlb[i].tlb_data);
			}
			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2\n\t"
						     "membar #Sync"
						     : : "r" (prom_itlb[i].tlb_tag),
						     "r" (TLB_TAG_ACCESS),
						     "i" (ASI_IMMU));
				if (tlb_type == spitfire)
					spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
							       prom_itlb[i].tlb_data);
				else if (tlb_type == cheetah || tlb_type == cheetah_plus)
					cheetah_put_litlb_data(prom_itlb[i].tlb_ent,
							       prom_itlb[i].tlb_data);
			}
		}
	} else {
		for (i = 0; i < 16; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				if (tlb_type == spitfire)
					spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent, 0x0UL);
				else
					cheetah_put_ldtlb_data(prom_dtlb[i].tlb_ent, 0x0UL);
			}
			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : : "r" (TLB_TAG_ACCESS),
						     "i" (ASI_IMMU));
				if (tlb_type == spitfire)
					spitfire_put_itlb_data(prom_itlb[i].tlb_ent, 0x0UL);
				else
					cheetah_put_litlb_data(prom_itlb[i].tlb_ent, 0x0UL);
			}
		}
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

void inherit_locked_prom_mappings(int save_p)
{
	int i;
	int dtlb_seen = 0;
	int itlb_seen = 0;

	/* Fucking losing PROM has more mappings in the TLB, but
	 * it (conveniently) fails to mention any of these in the
	 * translations property.  The only ones that matter are
	 * the locked PROM tlb entries, so we impose the following
	 * irrecovable rule on the PROM, it is allowed 8 locked
	 * entries in the ITLB and 8 in the DTLB.
	 *
	 * Supposedly the upper 16GB of the address space is
	 * reserved for OBP, BUT I WISH THIS WAS DOCUMENTED
	 * SOMEWHERE!!!!!!!!!!!!!!!!!  Furthermore the entire interface
	 * used between the client program and the firmware on sun5
	 * systems to coordinate mmu mappings is also COMPLETELY
	 * UNDOCUMENTED!!!!!! Thanks S(t)un!
	 */
	if (save_p) {
		for (i = 0; i < 16; i++) {
			prom_itlb[i].tlb_ent = -1;
			prom_dtlb[i].tlb_ent = -1;
		}
	}
	if (tlb_type == spitfire) {
		int high = SPITFIRE_HIGHEST_LOCKED_TLBENT - bigkernel;
		for (i = 0; i < high; i++) {
			unsigned long data;

			/* Spitfire Errata #32 workaround */
			/* NOTE: Always runs on spitfire, so no cheetah+
			 *       page size encodings.
			 */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			data = spitfire_get_dtlb_data(i);
			if ((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
				unsigned long tag;

				/* Spitfire Errata #32 workaround */
				/* NOTE: Always runs on spitfire, so no
				 *       cheetah+ page size encodings.
				 */
				__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
						     "flush	%%g6"
						     : /* No outputs */
						     : "r" (0),
						     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

				tag = spitfire_get_dtlb_tag(i);
				if (save_p) {
					prom_dtlb[dtlb_seen].tlb_ent = i;
					prom_dtlb[dtlb_seen].tlb_tag = tag;
					prom_dtlb[dtlb_seen].tlb_data = data;
				}
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				spitfire_put_dtlb_data(i, 0x0UL);

				dtlb_seen++;
				if (dtlb_seen > 15)
					break;
			}
		}

		for (i = 0; i < high; i++) {
			unsigned long data;

			/* Spitfire Errata #32 workaround */
			/* NOTE: Always runs on spitfire, so no
			 *       cheetah+ page size encodings.
			 */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			data = spitfire_get_itlb_data(i);
			if ((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
				unsigned long tag;

				/* Spitfire Errata #32 workaround */
				/* NOTE: Always runs on spitfire, so no
				 *       cheetah+ page size encodings.
				 */
				__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
						     "flush	%%g6"
						     : /* No outputs */
						     : "r" (0),
						     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

				tag = spitfire_get_itlb_tag(i);
				if (save_p) {
					prom_itlb[itlb_seen].tlb_ent = i;
					prom_itlb[itlb_seen].tlb_tag = tag;
					prom_itlb[itlb_seen].tlb_data = data;
				}
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
				spitfire_put_itlb_data(i, 0x0UL);

				itlb_seen++;
				if (itlb_seen > 15)
					break;
			}
		}
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		int high = CHEETAH_HIGHEST_LOCKED_TLBENT - bigkernel;

		for (i = 0; i < high; i++) {
			unsigned long data;

			data = cheetah_get_ldtlb_data(i);
			if ((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
				unsigned long tag;

				tag = cheetah_get_ldtlb_tag(i);
				if (save_p) {
					prom_dtlb[dtlb_seen].tlb_ent = i;
					prom_dtlb[dtlb_seen].tlb_tag = tag;
					prom_dtlb[dtlb_seen].tlb_data = data;
				}
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				cheetah_put_ldtlb_data(i, 0x0UL);

				dtlb_seen++;
				if (dtlb_seen > 15)
					break;
			}
		}

		for (i = 0; i < high; i++) {
			unsigned long data;

			data = cheetah_get_litlb_data(i);
			if ((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
				unsigned long tag;

				tag = cheetah_get_litlb_tag(i);
				if (save_p) {
					prom_itlb[itlb_seen].tlb_ent = i;
					prom_itlb[itlb_seen].tlb_tag = tag;
					prom_itlb[itlb_seen].tlb_data = data;
				}
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
				cheetah_put_litlb_data(i, 0x0UL);

				itlb_seen++;
				if (itlb_seen > 15)
					break;
			}
		}
	} else {
		/* Implement me :-) */
		BUG();
	}
	if (save_p)
		prom_ditlb_set = 1;
}

/* Give PROM back his world, done during reboots... */
void prom_reload_locked(void)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (prom_dtlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2\n\t"
					     "membar #Sync"
				: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
				"i" (ASI_DMMU));
			if (tlb_type == spitfire)
				spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
						       prom_dtlb[i].tlb_data);
			else if (tlb_type == cheetah || tlb_type == cheetah_plus)
				cheetah_put_ldtlb_data(prom_dtlb[i].tlb_ent,
						      prom_dtlb[i].tlb_data);
		}

		if (prom_itlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2\n\t"
					     "membar #Sync"
					     : : "r" (prom_itlb[i].tlb_tag),
					     "r" (TLB_TAG_ACCESS),
					     "i" (ASI_IMMU));
			if (tlb_type == spitfire)
				spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
						       prom_itlb[i].tlb_data);
			else
				cheetah_put_litlb_data(prom_itlb[i].tlb_ent,
						       prom_itlb[i].tlb_data);
		}
	}
}

#ifdef DCACHE_ALIASING_POSSIBLE
void __flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long va;

	if (tlb_type == spitfire) {
		int n = 0;

		for (va = start; va < end; va += 32) {
			spitfire_put_dcache_tag(va & 0x3fe0, 0x0);
			if (++n >= 512)
				break;
		}
	} else {
		start = __pa(start);
		end = __pa(end);
		for (va = start; va < end; va += 32)
			__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
					     "membar #Sync"
					     : /* no outputs */
					     : "r" (va),
					       "i" (ASI_DCACHE_INVALIDATE));
	}
}
#endif /* DCACHE_ALIASING_POSSIBLE */

/* If not locked, zap it. */
void __flush_tlb_all(void)
{
	unsigned long pstate;
	int i;

	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));
	if (tlb_type == spitfire) {
		for (i = 0; i < 64; i++) {
			/* Spitfire Errata #32 workaround */
			/* NOTE: Always runs on spitfire, so no
			 *       cheetah+ page size encodings.
			 */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			if (!(spitfire_get_dtlb_data(i) & _PAGE_L)) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : /* no outputs */
						     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				spitfire_put_dtlb_data(i, 0x0UL);
			}

			/* Spitfire Errata #32 workaround */
			/* NOTE: Always runs on spitfire, so no
			 *       cheetah+ page size encodings.
			 */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			if (!(spitfire_get_itlb_data(i) & _PAGE_L)) {
				__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
						     "membar #Sync"
						     : /* no outputs */
						     : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
				spitfire_put_itlb_data(i, 0x0UL);
			}
		}
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		cheetah_flush_dtlb_all();
		cheetah_flush_itlb_all();
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

/* Caller does TLB context flushing on local CPU if necessary.
 * The caller also ensures that CTX_VALID(mm->context) is false.
 *
 * We must be careful about boundary cases so that we never
 * let the user have CTX 0 (nucleus) or we ever use a CTX
 * version of zero (and thus NO_CONTEXT would not be caught
 * by version mis-match tests in mmu_context.h).
 */
void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx, new_ctx;
	unsigned long orig_pgsz_bits;
	

	spin_lock(&ctx_alloc_lock);
	orig_pgsz_bits = (mm->context.sparc64_ctx_val & CTX_PGSZ_MASK);
	ctx = (tlb_context_cache + 1) & CTX_NR_MASK;
	new_ctx = find_next_zero_bit(mmu_context_bmap, 1 << CTX_NR_BITS, ctx);
	if (new_ctx >= (1 << CTX_NR_BITS)) {
		new_ctx = find_next_zero_bit(mmu_context_bmap, ctx, 1);
		if (new_ctx >= ctx) {
			int i;
			new_ctx = (tlb_context_cache & CTX_VERSION_MASK) +
				CTX_FIRST_VERSION;
			if (new_ctx == 1)
				new_ctx = CTX_FIRST_VERSION;

			/* Don't call memset, for 16 entries that's just
			 * plain silly...
			 */
			mmu_context_bmap[0] = 3;
			mmu_context_bmap[1] = 0;
			mmu_context_bmap[2] = 0;
			mmu_context_bmap[3] = 0;
			for (i = 4; i < CTX_BMAP_SLOTS; i += 4) {
				mmu_context_bmap[i + 0] = 0;
				mmu_context_bmap[i + 1] = 0;
				mmu_context_bmap[i + 2] = 0;
				mmu_context_bmap[i + 3] = 0;
			}
			goto out;
		}
	}
	mmu_context_bmap[new_ctx>>6] |= (1UL << (new_ctx & 63));
	new_ctx |= (tlb_context_cache & CTX_VERSION_MASK);
out:
	tlb_context_cache = new_ctx;
	mm->context.sparc64_ctx_val = new_ctx | orig_pgsz_bits;
	spin_unlock(&ctx_alloc_lock);
}

#ifndef CONFIG_SMP
struct pgtable_cache_struct pgt_quicklists;
#endif

/* OK, we have to color these pages. The page tables are accessed
 * by non-Dcache enabled mapping in the VPTE area by the dtlb_backend.S
 * code, as well as by PAGE_OFFSET range direct-mapped addresses by 
 * other parts of the kernel. By coloring, we make sure that the tlbmiss 
 * fast handlers do not get data from old/garbage dcache lines that 
 * correspond to an old/stale virtual address (user/kernel) that 
 * previously mapped the pagetable page while accessing vpte range 
 * addresses. The idea is that if the vpte color and PAGE_OFFSET range 
 * color is the same, then when the kernel initializes the pagetable 
 * using the later address range, accesses with the first address
 * range will see the newly initialized data rather than the garbage.
 */
#ifdef DCACHE_ALIASING_POSSIBLE
#define DC_ALIAS_SHIFT	1
#else
#define DC_ALIAS_SHIFT	0
#endif
pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	struct page *page;
	unsigned long color;

	{
		pte_t *ptep = pte_alloc_one_fast(mm, address);

		if (ptep)
			return ptep;
	}

	color = VPTE_COLOR(address);
	page = alloc_pages(GFP_KERNEL|__GFP_REPEAT, DC_ALIAS_SHIFT);
	if (page) {
		unsigned long *to_free;
		unsigned long paddr;
		pte_t *pte;

#ifdef DCACHE_ALIASING_POSSIBLE
		set_page_count(page, 1);
		ClearPageCompound(page);

		set_page_count((page + 1), 1);
		ClearPageCompound(page + 1);
#endif
		paddr = (unsigned long) page_address(page);
		memset((char *)paddr, 0, (PAGE_SIZE << DC_ALIAS_SHIFT));

		if (!color) {
			pte = (pte_t *) paddr;
			to_free = (unsigned long *) (paddr + PAGE_SIZE);
		} else {
			pte = (pte_t *) (paddr + PAGE_SIZE);
			to_free = (unsigned long *) paddr;
		}

#ifdef DCACHE_ALIASING_POSSIBLE
		/* Now free the other one up, adjust cache size. */
		preempt_disable();
		*to_free = (unsigned long) pte_quicklist[color ^ 0x1];
		pte_quicklist[color ^ 0x1] = to_free;
		pgtable_cache_size++;
		preempt_enable();
#endif

		return pte;
	}
	return NULL;
}

void sparc_ultra_dump_itlb(void)
{
        int slot;

	if (tlb_type == spitfire) {
		printk ("Contents of itlb: ");
		for (slot = 0; slot < 14; slot++) printk ("    ");
		printk ("%2x:%016lx,%016lx\n",
			0,
			spitfire_get_itlb_tag(0), spitfire_get_itlb_data(0));
		for (slot = 1; slot < 64; slot+=3) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
				slot,
				spitfire_get_itlb_tag(slot), spitfire_get_itlb_data(slot),
				slot+1,
				spitfire_get_itlb_tag(slot+1), spitfire_get_itlb_data(slot+1),
				slot+2,
				spitfire_get_itlb_tag(slot+2), spitfire_get_itlb_data(slot+2));
		}
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		printk ("Contents of itlb0:\n");
		for (slot = 0; slot < 16; slot+=2) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx\n",
				slot,
				cheetah_get_litlb_tag(slot), cheetah_get_litlb_data(slot),
				slot+1,
				cheetah_get_litlb_tag(slot+1), cheetah_get_litlb_data(slot+1));
		}
		printk ("Contents of itlb2:\n");
		for (slot = 0; slot < 128; slot+=2) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx\n",
				slot,
				cheetah_get_itlb_tag(slot), cheetah_get_itlb_data(slot),
				slot+1,
				cheetah_get_itlb_tag(slot+1), cheetah_get_itlb_data(slot+1));
		}
	}
}

void sparc_ultra_dump_dtlb(void)
{
        int slot;

	if (tlb_type == spitfire) {
		printk ("Contents of dtlb: ");
		for (slot = 0; slot < 14; slot++) printk ("    ");
		printk ("%2x:%016lx,%016lx\n", 0,
			spitfire_get_dtlb_tag(0), spitfire_get_dtlb_data(0));
		for (slot = 1; slot < 64; slot+=3) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
				slot,
				spitfire_get_dtlb_tag(slot), spitfire_get_dtlb_data(slot),
				slot+1,
				spitfire_get_dtlb_tag(slot+1), spitfire_get_dtlb_data(slot+1),
				slot+2,
				spitfire_get_dtlb_tag(slot+2), spitfire_get_dtlb_data(slot+2));
		}
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		printk ("Contents of dtlb0:\n");
		for (slot = 0; slot < 16; slot+=2) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx\n",
				slot,
				cheetah_get_ldtlb_tag(slot), cheetah_get_ldtlb_data(slot),
				slot+1,
				cheetah_get_ldtlb_tag(slot+1), cheetah_get_ldtlb_data(slot+1));
		}
		printk ("Contents of dtlb2:\n");
		for (slot = 0; slot < 512; slot+=2) {
			printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx\n",
				slot,
				cheetah_get_dtlb_tag(slot, 2), cheetah_get_dtlb_data(slot, 2),
				slot+1,
				cheetah_get_dtlb_tag(slot+1, 2), cheetah_get_dtlb_data(slot+1, 2));
		}
		if (tlb_type == cheetah_plus) {
			printk ("Contents of dtlb3:\n");
			for (slot = 0; slot < 512; slot+=2) {
				printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx\n",
					slot,
					cheetah_get_dtlb_tag(slot, 3), cheetah_get_dtlb_data(slot, 3),
					slot+1,
					cheetah_get_dtlb_tag(slot+1, 3), cheetah_get_dtlb_data(slot+1, 3));
			}
		}
	}
}

extern unsigned long cmdline_memory_size;

unsigned long __init bootmem_init(unsigned long *pages_avail)
{
	unsigned long bootmap_size, start_pfn, end_pfn;
	unsigned long end_of_phys_memory = 0UL;
	unsigned long bootmap_pfn, bytes_avail, size;
	int i;

#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("bootmem_init: Scan pavail, ");
#endif

	bytes_avail = 0UL;
	for (i = 0; i < pavail_ents; i++) {
		end_of_phys_memory = pavail[i].phys_addr +
			pavail[i].reg_size;
		bytes_avail += pavail[i].reg_size;
		if (cmdline_memory_size) {
			if (bytes_avail > cmdline_memory_size) {
				unsigned long slack = bytes_avail - cmdline_memory_size;

				bytes_avail -= slack;
				end_of_phys_memory -= slack;

				pavail[i].reg_size -= slack;
				if ((long)pavail[i].reg_size <= 0L) {
					pavail[i].phys_addr = 0xdeadbeefUL;
					pavail[i].reg_size = 0UL;
					pavail_ents = i;
				} else {
					pavail[i+1].reg_size = 0Ul;
					pavail[i+1].phys_addr = 0xdeadbeefUL;
					pavail_ents = i + 1;
				}
				break;
			}
		}
	}

	*pages_avail = bytes_avail >> PAGE_SHIFT;

	/* Start with page aligned address of last symbol in kernel
	 * image.  The kernel is hard mapped below PAGE_OFFSET in a
	 * 4MB locked TLB translation.
	 */
	start_pfn = PAGE_ALIGN(kern_base + kern_size) >> PAGE_SHIFT;

	bootmap_pfn = start_pfn;

	end_pfn = end_of_phys_memory >> PAGE_SHIFT;

#ifdef CONFIG_BLK_DEV_INITRD
	/* Now have to check initial ramdisk, so that bootmap does not overwrite it */
	if (sparc_ramdisk_image || sparc_ramdisk_image64) {
		unsigned long ramdisk_image = sparc_ramdisk_image ?
			sparc_ramdisk_image : sparc_ramdisk_image64;
		if (ramdisk_image >= (unsigned long)_end - 2 * PAGE_SIZE)
			ramdisk_image -= KERNBASE;
		initrd_start = ramdisk_image + phys_base;
		initrd_end = initrd_start + sparc_ramdisk_size;
		if (initrd_end > end_of_phys_memory) {
			printk(KERN_CRIT "initrd extends beyond end of memory "
		                 	 "(0x%016lx > 0x%016lx)\ndisabling initrd\n",
			       initrd_end, end_of_phys_memory);
			initrd_start = 0;
		}
		if (initrd_start) {
			if (initrd_start >= (start_pfn << PAGE_SHIFT) &&
			    initrd_start < (start_pfn << PAGE_SHIFT) + 2 * PAGE_SIZE)
				bootmap_pfn = PAGE_ALIGN (initrd_end) >> PAGE_SHIFT;
		}
	}
#endif	
	/* Initialize the boot-time allocator. */
	max_pfn = max_low_pfn = end_pfn;
	min_low_pfn = pfn_base;

#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("init_bootmem(min[%lx], bootmap[%lx], max[%lx])\n",
		    min_low_pfn, bootmap_pfn, max_low_pfn);
#endif
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap_pfn, pfn_base, end_pfn);

	/* Now register the available physical memory with the
	 * allocator.
	 */
	for (i = 0; i < pavail_ents; i++) {
#ifdef CONFIG_DEBUG_BOOTMEM
		prom_printf("free_bootmem(pavail:%d): base[%lx] size[%lx]\n",
			    i, pavail[i].phys_addr, pavail[i].reg_size);
#endif
		free_bootmem(pavail[i].phys_addr, pavail[i].reg_size);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		size = initrd_end - initrd_start;

		/* Resert the initrd image area. */
#ifdef CONFIG_DEBUG_BOOTMEM
		prom_printf("reserve_bootmem(initrd): base[%llx] size[%lx]\n",
			initrd_start, initrd_end);
#endif
		reserve_bootmem(initrd_start, size);
		*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

		initrd_start += PAGE_OFFSET;
		initrd_end += PAGE_OFFSET;
	}
#endif
	/* Reserve the kernel text/data/bss. */
#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("reserve_bootmem(kernel): base[%lx] size[%lx]\n", kern_base, kern_size);
#endif
	reserve_bootmem(kern_base, kern_size);
	*pages_avail -= PAGE_ALIGN(kern_size) >> PAGE_SHIFT;

	/* Reserve the bootmem map.   We do not account for it
	 * in pages_avail because we will release that memory
	 * in free_all_bootmem.
	 */
	size = bootmap_size;
#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("reserve_bootmem(bootmap): base[%lx] size[%lx]\n",
		    (bootmap_pfn << PAGE_SHIFT), size);
#endif
	reserve_bootmem((bootmap_pfn << PAGE_SHIFT), size);
	*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

	return end_pfn;
}

#ifdef CONFIG_DEBUG_PAGEALLOC
static unsigned long kernel_map_range(unsigned long pstart, unsigned long pend, pgprot_t prot)
{
	unsigned long vstart = PAGE_OFFSET + pstart;
	unsigned long vend = PAGE_OFFSET + pend;
	unsigned long alloc_bytes = 0UL;

	if ((vstart & ~PAGE_MASK) || (vend & ~PAGE_MASK)) {
		prom_printf("kernel_map: Unaligned physmem[%lx:%lx]\n",
			    vstart, vend);
		prom_halt();
	}

	while (vstart < vend) {
		unsigned long this_end, paddr = __pa(vstart);
		pgd_t *pgd = pgd_offset_k(vstart);
		pud_t *pud;
		pmd_t *pmd;
		pte_t *pte;

		pud = pud_offset(pgd, vstart);
		if (pud_none(*pud)) {
			pmd_t *new;

			new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
			alloc_bytes += PAGE_SIZE;
			pud_populate(&init_mm, pud, new);
		}

		pmd = pmd_offset(pud, vstart);
		if (!pmd_present(*pmd)) {
			pte_t *new;

			new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
			alloc_bytes += PAGE_SIZE;
			pmd_populate_kernel(&init_mm, pmd, new);
		}

		pte = pte_offset_kernel(pmd, vstart);
		this_end = (vstart + PMD_SIZE) & PMD_MASK;
		if (this_end > vend)
			this_end = vend;

		while (vstart < this_end) {
			pte_val(*pte) = (paddr | pgprot_val(prot));

			vstart += PAGE_SIZE;
			paddr += PAGE_SIZE;
			pte++;
		}
	}

	return alloc_bytes;
}

static struct linux_prom64_registers pall[MAX_BANKS] __initdata;
static int pall_ents __initdata;

extern unsigned int kvmap_linear_patch[1];

static void __init kernel_physical_mapping_init(void)
{
	unsigned long i, mem_alloced = 0UL;

	read_obp_memory("reg", &pall[0], &pall_ents);

	for (i = 0; i < pall_ents; i++) {
		unsigned long phys_start, phys_end;

		phys_start = pall[i].phys_addr;
		phys_end = phys_start + pall[i].reg_size;
		mem_alloced += kernel_map_range(phys_start, phys_end,
						PAGE_KERNEL);
	}

	printk("Allocated %ld bytes for kernel page tables.\n",
	       mem_alloced);

	kvmap_linear_patch[0] = 0x01000000; /* nop */
	flushi(&kvmap_linear_patch[0]);

	__flush_tlb_all();
}

void kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long phys_start = page_to_pfn(page) << PAGE_SHIFT;
	unsigned long phys_end = phys_start + (numpages * PAGE_SIZE);

	kernel_map_range(phys_start, phys_end,
			 (enable ? PAGE_KERNEL : __pgprot(0)));

	/* we should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu.
	 */
	__flush_tlb_kernel_range(PAGE_OFFSET + phys_start,
				 PAGE_OFFSET + phys_end);
}
#endif

unsigned long __init find_ecache_flush_span(unsigned long size)
{
	int i;

	for (i = 0; i < pavail_ents; i++) {
		if (pavail[i].reg_size >= size)
			return pavail[i].phys_addr;
	}

	return ~0UL;
}

/* paging_init() sets up the page tables */

extern void cheetah_ecache_flush_init(void);

static unsigned long last_valid_pfn;
pgd_t swapper_pg_dir[2048];

void __init paging_init(void)
{
	unsigned long end_pfn, pages_avail, shift;
	unsigned long real_end, i;

	/* Find available physical memory... */
	read_obp_memory("available", &pavail[0], &pavail_ents);

	phys_base = 0xffffffffffffffffUL;
	for (i = 0; i < pavail_ents; i++)
		phys_base = min(phys_base, pavail[i].phys_addr);

	pfn_base = phys_base >> PAGE_SHIFT;

	kern_base = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	kern_size = (unsigned long)&_end - (unsigned long)KERNBASE;

	set_bit(0, mmu_context_bmap);

	shift = kern_base + PAGE_OFFSET - ((unsigned long)KERNBASE);

	real_end = (unsigned long)_end;
	if ((real_end > ((unsigned long)KERNBASE + 0x400000)))
		bigkernel = 1;
	if ((real_end > ((unsigned long)KERNBASE + 0x800000))) {
		prom_printf("paging_init: Kernel > 8MB, too large.\n");
		prom_halt();
	}

	/* Set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t)));
	
	memset(swapper_low_pmd_dir, 0, sizeof(swapper_low_pmd_dir));

	/* Now can init the kernel/bad page tables. */
	pud_set(pud_offset(&swapper_pg_dir[0], 0),
		swapper_low_pmd_dir + (shift / sizeof(pgd_t)));
	
	swapper_pgd_zero = pgd_val(swapper_pg_dir[0]);
	
	/* Inherit non-locked OBP mappings. */
	inherit_prom_mappings();
	
	/* Ok, we can use our TLB miss and window trap handlers safely.
	 * We need to do a quick peek here to see if we are on StarFire
	 * or not, so setup_tba can setup the IRQ globals correctly (it
	 * needs to get the hard smp processor id correctly).
	 */
	{
		extern void setup_tba(int);
		setup_tba(this_is_starfire);
	}

	inherit_locked_prom_mappings(1);

	__flush_tlb_all();

	/* Setup bootmem... */
	pages_avail = 0;
	last_valid_pfn = end_pfn = bootmem_init(&pages_avail);

#ifdef CONFIG_DEBUG_PAGEALLOC
	kernel_physical_mapping_init();
#endif

	{
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long zholes_size[MAX_NR_ZONES];
		unsigned long npages;
		int znum;

		for (znum = 0; znum < MAX_NR_ZONES; znum++)
			zones_size[znum] = zholes_size[znum] = 0;

		npages = end_pfn - pfn_base;
		zones_size[ZONE_DMA] = npages;
		zholes_size[ZONE_DMA] = npages - pages_avail;

		free_area_init_node(0, &contig_page_data, zones_size,
				    phys_base >> PAGE_SHIFT, zholes_size);
	}

	device_scan();
}

static void __init taint_real_pages(void)
{
	int i;

	read_obp_memory("available", &pavail_rescan[0], &pavail_rescan_ents);

	/* Find changes discovered in the physmem available rescan and
	 * reserve the lost portions in the bootmem maps.
	 */
	for (i = 0; i < pavail_ents; i++) {
		unsigned long old_start, old_end;

		old_start = pavail[i].phys_addr;
		old_end = old_start +
			pavail[i].reg_size;
		while (old_start < old_end) {
			int n;

			for (n = 0; pavail_rescan_ents; n++) {
				unsigned long new_start, new_end;

				new_start = pavail_rescan[n].phys_addr;
				new_end = new_start +
					pavail_rescan[n].reg_size;

				if (new_start <= old_start &&
				    new_end >= (old_start + PAGE_SIZE)) {
					set_bit(old_start >> 22,
						sparc64_valid_addr_bitmap);
					goto do_next_page;
				}
			}
			reserve_bootmem(old_start, PAGE_SIZE);

		do_next_page:
			old_start += PAGE_SIZE;
		}
	}
}

void __init mem_init(void)
{
	unsigned long codepages, datapages, initpages;
	unsigned long addr, last;
	int i;

	i = last_valid_pfn >> ((22 - PAGE_SHIFT) + 6);
	i += 1;
	sparc64_valid_addr_bitmap = (unsigned long *) alloc_bootmem(i << 3);
	if (sparc64_valid_addr_bitmap == NULL) {
		prom_printf("mem_init: Cannot alloc valid_addr_bitmap.\n");
		prom_halt();
	}
	memset(sparc64_valid_addr_bitmap, 0, i << 3);

	addr = PAGE_OFFSET + kern_base;
	last = PAGE_ALIGN(kern_size) + addr;
	while (addr < last) {
		set_bit(__pa(addr) >> 22, sparc64_valid_addr_bitmap);
		addr += PAGE_SIZE;
	}

	taint_real_pages();

	max_mapnr = last_valid_pfn - pfn_base;
	high_memory = __va(last_valid_pfn << PAGE_SHIFT);

#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("mem_init: Calling free_all_bootmem().\n");
#endif
	totalram_pages = num_physpages = free_all_bootmem() - 1;

	/*
	 * Set up the zero page, mark it reserved, so that page count
	 * is not manipulated when freeing the page from user ptes.
	 */
	mem_map_zero = alloc_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if (mem_map_zero == NULL) {
		prom_printf("paging_init: Cannot alloc zero page.\n");
		prom_halt();
	}
	SetPageReserved(mem_map_zero);

	codepages = (((unsigned long) _etext) - ((unsigned long) _start));
	codepages = PAGE_ALIGN(codepages) >> PAGE_SHIFT;
	datapages = (((unsigned long) _edata) - ((unsigned long) _etext));
	datapages = PAGE_ALIGN(datapages) >> PAGE_SHIFT;
	initpages = (((unsigned long) __init_end) - ((unsigned long) __init_begin));
	initpages = PAGE_ALIGN(initpages) >> PAGE_SHIFT;

	printk("Memory: %uk available (%ldk kernel code, %ldk data, %ldk init) [%016lx,%016lx]\n",
	       nr_free_pages() << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10), 
	       PAGE_OFFSET, (last_valid_pfn << PAGE_SHIFT));

	if (tlb_type == cheetah || tlb_type == cheetah_plus)
		cheetah_ecache_flush_init();
}

void free_initmem(void)
{
	unsigned long addr, initend;

	/*
	 * The init section is aligned to 8k in vmlinux.lds. Page align for >8k pagesizes.
	 */
	addr = PAGE_ALIGN((unsigned long)(__init_begin));
	initend = (unsigned long)(__init_end) & PAGE_MASK;
	for (; addr < initend; addr += PAGE_SIZE) {
		unsigned long page;
		struct page *p;

		page = (addr +
			((unsigned long) __va(kern_base)) -
			((unsigned long) KERNBASE));
		memset((void *)addr, 0xcc, PAGE_SIZE);
		p = virt_to_page(page);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		num_physpages++;
		totalram_pages++;
	}
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		struct page *p = virt_to_page(start);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		num_physpages++;
		totalram_pages++;
	}
}
#endif

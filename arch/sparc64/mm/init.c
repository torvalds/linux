/*  $Id: init.c,v 1.209 2002/02/09 19:49:31 davem Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996-1999 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997-1999 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/module.h>
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
#include <linux/poison.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/cache.h>
#include <linux/sort.h>
#include <linux/percpu.h>
#include <linux/lmb.h>
#include <linux/mmzone.h>

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
#include <asm/tsb.h>
#include <asm/hypervisor.h>
#include <asm/prom.h>
#include <asm/sstate.h>
#include <asm/mdesc.h>
#include <asm/cpudata.h>

#define MAX_PHYS_ADDRESS	(1UL << 42UL)
#define KPTE_BITMAP_CHUNK_SZ	(256UL * 1024UL * 1024UL)
#define KPTE_BITMAP_BYTES	\
	((MAX_PHYS_ADDRESS / KPTE_BITMAP_CHUNK_SZ) / 8)

unsigned long kern_linear_pte_xor[2] __read_mostly;

/* A bitmap, one bit for every 256MB of physical memory.  If the bit
 * is clear, we should use a 4MB page (via kern_linear_pte_xor[0]) else
 * if set we should use a 256MB page (via kern_linear_pte_xor[1]).
 */
unsigned long kpte_linear_bitmap[KPTE_BITMAP_BYTES / sizeof(unsigned long)];

#ifndef CONFIG_DEBUG_PAGEALLOC
/* A special kernel TSB for 4MB and 256MB linear mappings.
 * Space is allocated for this right after the trap table
 * in arch/sparc64/kernel/head.S
 */
extern struct tsb swapper_4m_tsb[KERNEL_TSB4M_NENTRIES];
#endif

#define MAX_BANKS	32

static struct linux_prom64_registers pavail[MAX_BANKS] __initdata;
static int pavail_ents __initdata;

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
		if (size == 0UL) {
			/* If it is empty, simply get rid of it.
			 * This simplifies the logic of the other
			 * functions that process these arrays.
			 */
			memmove(&regs[i], &regs[i + 1],
				(ents - i - 1) * sizeof(regs[0]));
			i--;
			ents--;
			continue;
		}
		regs[i].phys_addr = base;
		regs[i].reg_size = size;
	}

	*num_ents = ents;

	sort(regs, ents, sizeof(struct linux_prom64_registers),
	     cmp_p64, NULL);
}

unsigned long *sparc64_valid_addr_bitmap __read_mostly;

/* Kernel physical address base and size in bytes.  */
unsigned long kern_base __read_mostly;
unsigned long kern_size __read_mostly;

/* Initial ramdisk setup */
extern unsigned long sparc_ramdisk_image64;
extern unsigned int sparc_ramdisk_image;
extern unsigned int sparc_ramdisk_size;

struct page *mem_map_zero __read_mostly;
EXPORT_SYMBOL(mem_map_zero);

unsigned int sparc64_highest_unlocked_tlb_ent __read_mostly;

unsigned long sparc64_kern_pri_context __read_mostly;
unsigned long sparc64_kern_pri_nuc_bits __read_mostly;
unsigned long sparc64_kern_sec_context __read_mostly;

int num_kernel_image_mappings;

#ifdef CONFIG_DEBUG_DCFLUSH
atomic_t dcpage_flushes = ATOMIC_INIT(0);
#ifdef CONFIG_SMP
atomic_t dcpage_flushes_xcall = ATOMIC_INIT(0);
#endif
#endif

inline void flush_dcache_page_impl(struct page *page)
{
	BUG_ON(tlb_type == hypervisor);
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
#define PG_dcache_cpu_shift	32UL
#define PG_dcache_cpu_mask	\
	((1UL<<ilog2(roundup_pow_of_two(NR_CPUS)))-1UL)

#define dcache_dirty_cpu(page) \
	(((page)->flags >> PG_dcache_cpu_shift) & PG_dcache_cpu_mask)

static inline void set_dcache_dirty(struct page *page, int this_cpu)
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

static inline void clear_dcache_dirty_cpu(struct page *page, unsigned long cpu)
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

static inline void tsb_insert(struct tsb *ent, unsigned long tag, unsigned long pte)
{
	unsigned long tsb_addr = (unsigned long) ent;

	if (tlb_type == cheetah_plus || tlb_type == hypervisor)
		tsb_addr = __pa(tsb_addr);

	__tsb_insert(tsb_addr, tag, pte);
}

unsigned long _PAGE_ALL_SZ_BITS __read_mostly;
unsigned long _PAGE_SZBITS __read_mostly;

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct mm_struct *mm;
	struct tsb *tsb;
	unsigned long tag, flags;
	unsigned long tsb_index, tsb_hash_shift;

	if (tlb_type != hypervisor) {
		unsigned long pfn = pte_pfn(pte);
		unsigned long pg_flags;
		struct page *page;

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

	mm = vma->vm_mm;

	tsb_index = MM_TSB_BASE;
	tsb_hash_shift = PAGE_SHIFT;

	spin_lock_irqsave(&mm->context.lock, flags);

#ifdef CONFIG_HUGETLB_PAGE
	if (mm->context.tsb_block[MM_TSB_HUGE].tsb != NULL) {
		if ((tlb_type == hypervisor &&
		     (pte_val(pte) & _PAGE_SZALL_4V) == _PAGE_SZHUGE_4V) ||
		    (tlb_type != hypervisor &&
		     (pte_val(pte) & _PAGE_SZALL_4U) == _PAGE_SZHUGE_4U)) {
			tsb_index = MM_TSB_HUGE;
			tsb_hash_shift = HPAGE_SHIFT;
		}
	}
#endif

	tsb = mm->context.tsb_block[tsb_index].tsb;
	tsb += ((address >> tsb_hash_shift) &
		(mm->context.tsb_block[tsb_index].tsb_nentries - 1UL));
	tag = (address >> 22UL);
	tsb_insert(tsb, tag, pte_val(pte));

	spin_unlock_irqrestore(&mm->context.lock, flags);
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;
	int this_cpu;

	if (tlb_type == hypervisor)
		return;

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
	/* Cheetah and Hypervisor platform cpus have coherent I-cache. */
	if (tlb_type == spitfire) {
		unsigned long kaddr;

		/* This code only runs on Spitfire cpus so this is
		 * why we can assume _PAGE_PADDR_4U.
		 */
		for (kaddr = start; kaddr < end; kaddr += PAGE_SIZE) {
			unsigned long paddr, mask = _PAGE_PADDR_4U;

			if (kaddr >= PAGE_OFFSET)
				paddr = kaddr & mask;
			else {
				pgd_t *pgdp = pgd_offset_k(kaddr);
				pud_t *pudp = pud_offset(pgdp, kaddr);
				pmd_t *pmdp = pmd_offset(pudp, kaddr);
				pte_t *ptep = pte_offset_kernel(pmdp, kaddr);

				paddr = pte_val(*ptep) & mask;
			}
			__flush_icache_page(paddr);
		}
	}
}

void show_mem(void)
{
	unsigned long total = 0, reserved = 0;
	unsigned long shared = 0, cached = 0;
	pg_data_t *pgdat;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	printk(KERN_INFO "Free swap:       %6ldkB\n",
	       nr_swap_pages << (PAGE_SHIFT-10));
	for_each_online_pgdat(pgdat) {
		unsigned long i, flags;

		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}

	printk(KERN_INFO "%lu pages of RAM\n", total);
	printk(KERN_INFO "%lu reserved pages\n", reserved);
	printk(KERN_INFO "%lu pages shared\n", shared);
	printk(KERN_INFO "%lu pages swap cached\n", cached);

	printk(KERN_INFO "%lu pages dirty\n",
	       global_page_state(NR_FILE_DIRTY));
	printk(KERN_INFO "%lu pages writeback\n",
	       global_page_state(NR_WRITEBACK));
	printk(KERN_INFO "%lu pages mapped\n",
	       global_page_state(NR_FILE_MAPPED));
	printk(KERN_INFO "%lu pages slab\n",
		global_page_state(NR_SLAB_RECLAIMABLE) +
		global_page_state(NR_SLAB_UNRECLAIMABLE));
	printk(KERN_INFO "%lu pages pagetables\n",
	       global_page_state(NR_PAGETABLE));
}

void mmu_info(struct seq_file *m)
{
	if (tlb_type == cheetah)
		seq_printf(m, "MMU Type\t: Cheetah\n");
	else if (tlb_type == cheetah_plus)
		seq_printf(m, "MMU Type\t: Cheetah+\n");
	else if (tlb_type == spitfire)
		seq_printf(m, "MMU Type\t: Spitfire\n");
	else if (tlb_type == hypervisor)
		seq_printf(m, "MMU Type\t: Hypervisor (sun4v)\n");
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

/* Exported for kernel TLB miss handling in ktlb.S */
struct linux_prom_translation prom_trans[512] __read_mostly;
unsigned int prom_trans_ents __read_mostly;

/* Exported for SMP bootup purposes. */
unsigned long kern_locked_tte_data;

/* The obp translations are saved based on 8k pagesize, since obp can
 * use a mixture of pagesizes. Misses to the LOW_OBP_ADDRESS ->
 * HI_OBP_ADDRESS range are handled in ktlb.S.
 */
static inline int in_obp_range(unsigned long vaddr)
{
	return (vaddr >= LOW_OBP_ADDRESS &&
		vaddr < HI_OBP_ADDRESS);
}

static int cmp_ptrans(const void *a, const void *b)
{
	const struct linux_prom_translation *x = a, *y = b;

	if (x->virt > y->virt)
		return 1;
	if (x->virt < y->virt)
		return -1;
	return 0;
}

/* Read OBP translations property into 'prom_trans[]'.  */
static void __init read_obp_translations(void)
{
	int n, node, ents, first, last, i;

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

	ents = n;

	sort(prom_trans, ents, sizeof(struct linux_prom_translation),
	     cmp_ptrans, NULL);

	/* Now kick out all the non-OBP entries.  */
	for (i = 0; i < ents; i++) {
		if (in_obp_range(prom_trans[i].virt))
			break;
	}
	first = i;
	for (; i < ents; i++) {
		if (!in_obp_range(prom_trans[i].virt))
			break;
	}
	last = i;

	for (i = 0; i < (last - first); i++) {
		struct linux_prom_translation *src = &prom_trans[i + first];
		struct linux_prom_translation *dest = &prom_trans[i];

		*dest = *src;
	}
	for (; i < ents; i++) {
		struct linux_prom_translation *dest = &prom_trans[i];
		dest->virt = dest->size = dest->data = 0x0UL;
	}

	prom_trans_ents = last - first;

	if (tlb_type == spitfire) {
		/* Clear diag TTE bits. */
		for (i = 0; i < prom_trans_ents; i++)
			prom_trans[i].data &= ~0x0003fe0000000000UL;
	}
}

static void __init hypervisor_tlb_lock(unsigned long vaddr,
				       unsigned long pte,
				       unsigned long mmu)
{
	unsigned long ret = sun4v_mmu_map_perm_addr(vaddr, 0, pte, mmu);

	if (ret != 0) {
		prom_printf("hypervisor_tlb_lock[%lx:%lx:%lx:%lx]: "
			    "errors with %lx\n", vaddr, 0, pte, mmu, ret);
		prom_halt();
	}
}

static unsigned long kern_large_tte(unsigned long paddr);

static void __init remap_kernel(void)
{
	unsigned long phys_page, tte_vaddr, tte_data;
	int i, tlb_ent = sparc64_highest_locked_tlbent();

	tte_vaddr = (unsigned long) KERNBASE;
	phys_page = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	tte_data = kern_large_tte(phys_page);

	kern_locked_tte_data = tte_data;

	/* Now lock us into the TLBs via Hypervisor or OBP. */
	if (tlb_type == hypervisor) {
		for (i = 0; i < num_kernel_image_mappings; i++) {
			hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_DMMU);
			hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_IMMU);
			tte_vaddr += 0x400000;
			tte_data += 0x400000;
		}
	} else {
		for (i = 0; i < num_kernel_image_mappings; i++) {
			prom_dtlb_load(tlb_ent - i, tte_data, tte_vaddr);
			prom_itlb_load(tlb_ent - i, tte_data, tte_vaddr);
			tte_vaddr += 0x400000;
			tte_data += 0x400000;
		}
		sparc64_highest_unlocked_tlb_ent = tlb_ent - i;
	}
	if (tlb_type == cheetah_plus) {
		sparc64_kern_pri_context = (CTX_CHEETAH_PLUS_CTX0 |
					    CTX_CHEETAH_PLUS_NUC);
		sparc64_kern_pri_nuc_bits = CTX_CHEETAH_PLUS_NUC;
		sparc64_kern_sec_context = CTX_CHEETAH_PLUS_CTX0;
	}
}


static void __init inherit_prom_mappings(void)
{
	/* Now fixup OBP's idea about where we really are mapped. */
	printk("Remapping the kernel... ");
	remap_kernel();
	printk("done.\n");
}

void prom_world(int enter)
{
	if (!enter)
		set_fs((mm_segment_t) { get_thread_current_ds() });

	__asm__ __volatile__("flushw");
}

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
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
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

/* get_new_mmu_context() uses "cache + 1".  */
DEFINE_SPINLOCK(ctx_alloc_lock);
unsigned long tlb_context_cache = CTX_FIRST_VERSION - 1;
#define MAX_CTX_NR	(1UL << CTX_NR_BITS)
#define CTX_BMAP_SLOTS	BITS_TO_LONGS(MAX_CTX_NR)
DECLARE_BITMAP(mmu_context_bmap, MAX_CTX_NR);

/* Caller does TLB context flushing on local CPU if necessary.
 * The caller also ensures that CTX_VALID(mm->context) is false.
 *
 * We must be careful about boundary cases so that we never
 * let the user have CTX 0 (nucleus) or we ever use a CTX
 * version of zero (and thus NO_CONTEXT would not be caught
 * by version mis-match tests in mmu_context.h).
 *
 * Always invoked with interrupts disabled.
 */
void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx, new_ctx;
	unsigned long orig_pgsz_bits;
	unsigned long flags;
	int new_version;

	spin_lock_irqsave(&ctx_alloc_lock, flags);
	orig_pgsz_bits = (mm->context.sparc64_ctx_val & CTX_PGSZ_MASK);
	ctx = (tlb_context_cache + 1) & CTX_NR_MASK;
	new_ctx = find_next_zero_bit(mmu_context_bmap, 1 << CTX_NR_BITS, ctx);
	new_version = 0;
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
			new_version = 1;
			goto out;
		}
	}
	mmu_context_bmap[new_ctx>>6] |= (1UL << (new_ctx & 63));
	new_ctx |= (tlb_context_cache & CTX_VERSION_MASK);
out:
	tlb_context_cache = new_ctx;
	mm->context.sparc64_ctx_val = new_ctx | orig_pgsz_bits;
	spin_unlock_irqrestore(&ctx_alloc_lock, flags);

	if (unlikely(new_version))
		smp_new_mmu_context_version();
}

static int numa_enabled = 1;
static int numa_debug;

static int __init early_numa(char *p)
{
	if (!p)
		return 0;

	if (strstr(p, "off"))
		numa_enabled = 0;

	if (strstr(p, "debug"))
		numa_debug = 1;

	return 0;
}
early_param("numa", early_numa);

#define numadbg(f, a...) \
do {	if (numa_debug) \
		printk(KERN_INFO f, ## a); \
} while (0)

static void __init find_ramdisk(unsigned long phys_base)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (sparc_ramdisk_image || sparc_ramdisk_image64) {
		unsigned long ramdisk_image;

		/* Older versions of the bootloader only supported a
		 * 32-bit physical address for the ramdisk image
		 * location, stored at sparc_ramdisk_image.  Newer
		 * SILO versions set sparc_ramdisk_image to zero and
		 * provide a full 64-bit physical address at
		 * sparc_ramdisk_image64.
		 */
		ramdisk_image = sparc_ramdisk_image;
		if (!ramdisk_image)
			ramdisk_image = sparc_ramdisk_image64;

		/* Another bootloader quirk.  The bootloader normalizes
		 * the physical address to KERNBASE, so we have to
		 * factor that back out and add in the lowest valid
		 * physical page address to get the true physical address.
		 */
		ramdisk_image -= KERNBASE;
		ramdisk_image += phys_base;

		numadbg("Found ramdisk at physical address 0x%lx, size %u\n",
			ramdisk_image, sparc_ramdisk_size);

		initrd_start = ramdisk_image;
		initrd_end = ramdisk_image + sparc_ramdisk_size;

		lmb_reserve(initrd_start, initrd_end);

		initrd_start += PAGE_OFFSET;
		initrd_end += PAGE_OFFSET;
	}
#endif
}

struct node_mem_mask {
	unsigned long mask;
	unsigned long val;
	unsigned long bootmem_paddr;
};
static struct node_mem_mask node_masks[MAX_NUMNODES];
static int num_node_masks;

int numa_cpu_lookup_table[NR_CPUS];
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];

#ifdef CONFIG_NEED_MULTIPLE_NODES
static bootmem_data_t plat_node_bdata[MAX_NUMNODES];

struct mdesc_mblock {
	u64	base;
	u64	size;
	u64	offset; /* RA-to-PA */
};
static struct mdesc_mblock *mblocks;
static int num_mblocks;

static unsigned long ra_to_pa(unsigned long addr)
{
	int i;

	for (i = 0; i < num_mblocks; i++) {
		struct mdesc_mblock *m = &mblocks[i];

		if (addr >= m->base &&
		    addr < (m->base + m->size)) {
			addr += m->offset;
			break;
		}
	}
	return addr;
}

static int find_node(unsigned long addr)
{
	int i;

	addr = ra_to_pa(addr);
	for (i = 0; i < num_node_masks; i++) {
		struct node_mem_mask *p = &node_masks[i];

		if ((addr & p->mask) == p->val)
			return i;
	}
	return -1;
}

static unsigned long nid_range(unsigned long start, unsigned long end,
			       int *nid)
{
	*nid = find_node(start);
	start += PAGE_SIZE;
	while (start < end) {
		int n = find_node(start);

		if (n != *nid)
			break;
		start += PAGE_SIZE;
	}

	return start;
}
#else
static unsigned long nid_range(unsigned long start, unsigned long end,
			       int *nid)
{
	*nid = 0;
	return end;
}
#endif

/* This must be invoked after performing all of the necessary
 * add_active_range() calls for 'nid'.  We need to be able to get
 * correct data from get_pfn_range_for_nid().
 */
static void __init allocate_node_data(int nid)
{
	unsigned long paddr, num_pages, start_pfn, end_pfn;
	struct pglist_data *p;

#ifdef CONFIG_NEED_MULTIPLE_NODES
	paddr = lmb_alloc_nid(sizeof(struct pglist_data),
			      SMP_CACHE_BYTES, nid, nid_range);
	if (!paddr) {
		prom_printf("Cannot allocate pglist_data for nid[%d]\n", nid);
		prom_halt();
	}
	NODE_DATA(nid) = __va(paddr);
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->bdata = &plat_node_bdata[nid];
#endif

	p = NODE_DATA(nid);

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
	p->node_start_pfn = start_pfn;
	p->node_spanned_pages = end_pfn - start_pfn;

	if (p->node_spanned_pages) {
		num_pages = bootmem_bootmap_pages(p->node_spanned_pages);

		paddr = lmb_alloc_nid(num_pages << PAGE_SHIFT, PAGE_SIZE, nid,
				      nid_range);
		if (!paddr) {
			prom_printf("Cannot allocate bootmap for nid[%d]\n",
				  nid);
			prom_halt();
		}
		node_masks[nid].bootmem_paddr = paddr;
	}
}

static void init_node_masks_nonnuma(void)
{
	int i;

	numadbg("Initializing tables for non-numa.\n");

	node_masks[0].mask = node_masks[0].val = 0;
	num_node_masks = 1;

	for (i = 0; i < NR_CPUS; i++)
		numa_cpu_lookup_table[i] = 0;

	numa_cpumask_lookup_table[0] = CPU_MASK_ALL;
}

#ifdef CONFIG_NEED_MULTIPLE_NODES
struct pglist_data *node_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(numa_cpumask_lookup_table);
EXPORT_SYMBOL(node_data);

struct mdesc_mlgroup {
	u64	node;
	u64	latency;
	u64	match;
	u64	mask;
};
static struct mdesc_mlgroup *mlgroups;
static int num_mlgroups;

static int scan_pio_for_cfg_handle(struct mdesc_handle *md, u64 pio,
				   u32 cfg_handle)
{
	u64 arc;

	mdesc_for_each_arc(arc, md, pio, MDESC_ARC_TYPE_FWD) {
		u64 target = mdesc_arc_target(md, arc);
		const u64 *val;

		val = mdesc_get_property(md, target,
					 "cfg-handle", NULL);
		if (val && *val == cfg_handle)
			return 0;
	}
	return -ENODEV;
}

static int scan_arcs_for_cfg_handle(struct mdesc_handle *md, u64 grp,
				    u32 cfg_handle)
{
	u64 arc, candidate, best_latency = ~(u64)0;

	candidate = MDESC_NODE_NULL;
	mdesc_for_each_arc(arc, md, grp, MDESC_ARC_TYPE_FWD) {
		u64 target = mdesc_arc_target(md, arc);
		const char *name = mdesc_node_name(md, target);
		const u64 *val;

		if (strcmp(name, "pio-latency-group"))
			continue;

		val = mdesc_get_property(md, target, "latency", NULL);
		if (!val)
			continue;

		if (*val < best_latency) {
			candidate = target;
			best_latency = *val;
		}
	}

	if (candidate == MDESC_NODE_NULL)
		return -ENODEV;

	return scan_pio_for_cfg_handle(md, candidate, cfg_handle);
}

int of_node_to_nid(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;
	struct mdesc_handle *md;
	u32 cfg_handle;
	int count, nid;
	u64 grp;

	if (!mlgroups)
		return -1;

	regs = of_get_property(dp, "reg", NULL);
	if (!regs)
		return -1;

	cfg_handle = (regs->phys_addr >> 32UL) & 0x0fffffff;

	md = mdesc_grab();

	count = 0;
	nid = -1;
	mdesc_for_each_node_by_name(md, grp, "group") {
		if (!scan_arcs_for_cfg_handle(md, grp, cfg_handle)) {
			nid = count;
			break;
		}
		count++;
	}

	mdesc_release(md);

	return nid;
}

static void add_node_ranges(void)
{
	int i;

	for (i = 0; i < lmb.memory.cnt; i++) {
		unsigned long size = lmb_size_bytes(&lmb.memory, i);
		unsigned long start, end;

		start = lmb.memory.region[i].base;
		end = start + size;
		while (start < end) {
			unsigned long this_end;
			int nid;

			this_end = nid_range(start, end, &nid);

			numadbg("Adding active range nid[%d] "
				"start[%lx] end[%lx]\n",
				nid, start, this_end);

			add_active_range(nid,
					 start >> PAGE_SHIFT,
					 this_end >> PAGE_SHIFT);

			start = this_end;
		}
	}
}

static int __init grab_mlgroups(struct mdesc_handle *md)
{
	unsigned long paddr;
	int count = 0;
	u64 node;

	mdesc_for_each_node_by_name(md, node, "memory-latency-group")
		count++;
	if (!count)
		return -ENOENT;

	paddr = lmb_alloc(count * sizeof(struct mdesc_mlgroup),
			  SMP_CACHE_BYTES);
	if (!paddr)
		return -ENOMEM;

	mlgroups = __va(paddr);
	num_mlgroups = count;

	count = 0;
	mdesc_for_each_node_by_name(md, node, "memory-latency-group") {
		struct mdesc_mlgroup *m = &mlgroups[count++];
		const u64 *val;

		m->node = node;

		val = mdesc_get_property(md, node, "latency", NULL);
		m->latency = *val;
		val = mdesc_get_property(md, node, "address-match", NULL);
		m->match = *val;
		val = mdesc_get_property(md, node, "address-mask", NULL);
		m->mask = *val;

		numadbg("MLGROUP[%d]: node[%lx] latency[%lx] "
			"match[%lx] mask[%lx]\n",
			count - 1, m->node, m->latency, m->match, m->mask);
	}

	return 0;
}

static int __init grab_mblocks(struct mdesc_handle *md)
{
	unsigned long paddr;
	int count = 0;
	u64 node;

	mdesc_for_each_node_by_name(md, node, "mblock")
		count++;
	if (!count)
		return -ENOENT;

	paddr = lmb_alloc(count * sizeof(struct mdesc_mblock),
			  SMP_CACHE_BYTES);
	if (!paddr)
		return -ENOMEM;

	mblocks = __va(paddr);
	num_mblocks = count;

	count = 0;
	mdesc_for_each_node_by_name(md, node, "mblock") {
		struct mdesc_mblock *m = &mblocks[count++];
		const u64 *val;

		val = mdesc_get_property(md, node, "base", NULL);
		m->base = *val;
		val = mdesc_get_property(md, node, "size", NULL);
		m->size = *val;
		val = mdesc_get_property(md, node,
					 "address-congruence-offset", NULL);
		m->offset = *val;

		numadbg("MBLOCK[%d]: base[%lx] size[%lx] offset[%lx]\n",
			count - 1, m->base, m->size, m->offset);
	}

	return 0;
}

static void __init numa_parse_mdesc_group_cpus(struct mdesc_handle *md,
					       u64 grp, cpumask_t *mask)
{
	u64 arc;

	cpus_clear(*mask);

	mdesc_for_each_arc(arc, md, grp, MDESC_ARC_TYPE_BACK) {
		u64 target = mdesc_arc_target(md, arc);
		const char *name = mdesc_node_name(md, target);
		const u64 *id;

		if (strcmp(name, "cpu"))
			continue;
		id = mdesc_get_property(md, target, "id", NULL);
		if (*id < NR_CPUS)
			cpu_set(*id, *mask);
	}
}

static struct mdesc_mlgroup * __init find_mlgroup(u64 node)
{
	int i;

	for (i = 0; i < num_mlgroups; i++) {
		struct mdesc_mlgroup *m = &mlgroups[i];
		if (m->node == node)
			return m;
	}
	return NULL;
}

static int __init numa_attach_mlgroup(struct mdesc_handle *md, u64 grp,
				      int index)
{
	struct mdesc_mlgroup *candidate = NULL;
	u64 arc, best_latency = ~(u64)0;
	struct node_mem_mask *n;

	mdesc_for_each_arc(arc, md, grp, MDESC_ARC_TYPE_FWD) {
		u64 target = mdesc_arc_target(md, arc);
		struct mdesc_mlgroup *m = find_mlgroup(target);
		if (!m)
			continue;
		if (m->latency < best_latency) {
			candidate = m;
			best_latency = m->latency;
		}
	}
	if (!candidate)
		return -ENOENT;

	if (num_node_masks != index) {
		printk(KERN_ERR "Inconsistent NUMA state, "
		       "index[%d] != num_node_masks[%d]\n",
		       index, num_node_masks);
		return -EINVAL;
	}

	n = &node_masks[num_node_masks++];

	n->mask = candidate->mask;
	n->val = candidate->match;

	numadbg("NUMA NODE[%d]: mask[%lx] val[%lx] (latency[%lx])\n",
		index, n->mask, n->val, candidate->latency);

	return 0;
}

static int __init numa_parse_mdesc_group(struct mdesc_handle *md, u64 grp,
					 int index)
{
	cpumask_t mask;
	int cpu;

	numa_parse_mdesc_group_cpus(md, grp, &mask);

	for_each_cpu_mask(cpu, mask)
		numa_cpu_lookup_table[cpu] = index;
	numa_cpumask_lookup_table[index] = mask;

	if (numa_debug) {
		printk(KERN_INFO "NUMA GROUP[%d]: cpus [ ", index);
		for_each_cpu_mask(cpu, mask)
			printk("%d ", cpu);
		printk("]\n");
	}

	return numa_attach_mlgroup(md, grp, index);
}

static int __init numa_parse_mdesc(void)
{
	struct mdesc_handle *md = mdesc_grab();
	int i, err, count;
	u64 node;

	node = mdesc_node_by_name(md, MDESC_NODE_NULL, "latency-groups");
	if (node == MDESC_NODE_NULL) {
		mdesc_release(md);
		return -ENOENT;
	}

	err = grab_mblocks(md);
	if (err < 0)
		goto out;

	err = grab_mlgroups(md);
	if (err < 0)
		goto out;

	count = 0;
	mdesc_for_each_node_by_name(md, node, "group") {
		err = numa_parse_mdesc_group(md, node, count);
		if (err < 0)
			break;
		count++;
	}

	add_node_ranges();

	for (i = 0; i < num_node_masks; i++) {
		allocate_node_data(i);
		node_set_online(i);
	}

	err = 0;
out:
	mdesc_release(md);
	return err;
}

static int __init numa_parse_sun4u(void)
{
	return -1;
}

static int __init bootmem_init_numa(void)
{
	int err = -1;

	numadbg("bootmem_init_numa()\n");

	if (numa_enabled) {
		if (tlb_type == hypervisor)
			err = numa_parse_mdesc();
		else
			err = numa_parse_sun4u();
	}
	return err;
}

#else

static int bootmem_init_numa(void)
{
	return -1;
}

#endif

static void __init bootmem_init_nonnuma(void)
{
	unsigned long top_of_ram = lmb_end_of_DRAM();
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned int i;

	numadbg("bootmem_init_nonnuma()\n");

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	init_node_masks_nonnuma();

	for (i = 0; i < lmb.memory.cnt; i++) {
		unsigned long size = lmb_size_bytes(&lmb.memory, i);
		unsigned long start_pfn, end_pfn;

		if (!size)
			continue;

		start_pfn = lmb.memory.region[i].base >> PAGE_SHIFT;
		end_pfn = start_pfn + lmb_size_pages(&lmb.memory, i);
		add_active_range(0, start_pfn, end_pfn);
	}

	allocate_node_data(0);

	node_set_online(0);
}

static void __init reserve_range_in_node(int nid, unsigned long start,
					 unsigned long end)
{
	numadbg("    reserve_range_in_node(nid[%d],start[%lx],end[%lx]\n",
		nid, start, end);
	while (start < end) {
		unsigned long this_end;
		int n;

		this_end = nid_range(start, end, &n);
		if (n == nid) {
			numadbg("      MATCH reserving range [%lx:%lx]\n",
				start, this_end);
			reserve_bootmem_node(NODE_DATA(nid), start,
					     (this_end - start), BOOTMEM_DEFAULT);
		} else
			numadbg("      NO MATCH, advancing start to %lx\n",
				this_end);

		start = this_end;
	}
}

static void __init trim_reserved_in_node(int nid)
{
	int i;

	numadbg("  trim_reserved_in_node(%d)\n", nid);

	for (i = 0; i < lmb.reserved.cnt; i++) {
		unsigned long start = lmb.reserved.region[i].base;
		unsigned long size = lmb_size_bytes(&lmb.reserved, i);
		unsigned long end = start + size;

		reserve_range_in_node(nid, start, end);
	}
}

static void __init bootmem_init_one_node(int nid)
{
	struct pglist_data *p;

	numadbg("bootmem_init_one_node(%d)\n", nid);

	p = NODE_DATA(nid);

	if (p->node_spanned_pages) {
		unsigned long paddr = node_masks[nid].bootmem_paddr;
		unsigned long end_pfn;

		end_pfn = p->node_start_pfn + p->node_spanned_pages;

		numadbg("  init_bootmem_node(%d, %lx, %lx, %lx)\n",
			nid, paddr >> PAGE_SHIFT, p->node_start_pfn, end_pfn);

		init_bootmem_node(p, paddr >> PAGE_SHIFT,
				  p->node_start_pfn, end_pfn);

		numadbg("  free_bootmem_with_active_regions(%d, %lx)\n",
			nid, end_pfn);
		free_bootmem_with_active_regions(nid, end_pfn);

		trim_reserved_in_node(nid);

		numadbg("  sparse_memory_present_with_active_regions(%d)\n",
			nid);
		sparse_memory_present_with_active_regions(nid);
	}
}

static unsigned long __init bootmem_init(unsigned long phys_base)
{
	unsigned long end_pfn;
	int nid;

	end_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn = end_pfn;
	min_low_pfn = (phys_base >> PAGE_SHIFT);

	if (bootmem_init_numa() < 0)
		bootmem_init_nonnuma();

	/* XXX cpu notifier XXX */

	for_each_online_node(nid)
		bootmem_init_one_node(nid);

	sparse_init();

	return end_pfn;
}

static struct linux_prom64_registers pall[MAX_BANKS] __initdata;
static int pall_ents __initdata;

#ifdef CONFIG_DEBUG_PAGEALLOC
static unsigned long __ref kernel_map_range(unsigned long pstart,
					    unsigned long pend, pgprot_t prot)
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

extern unsigned int kvmap_linear_patch[1];
#endif /* CONFIG_DEBUG_PAGEALLOC */

static void __init mark_kpte_bitmap(unsigned long start, unsigned long end)
{
	const unsigned long shift_256MB = 28;
	const unsigned long mask_256MB = ((1UL << shift_256MB) - 1UL);
	const unsigned long size_256MB = (1UL << shift_256MB);

	while (start < end) {
		long remains;

		remains = end - start;
		if (remains < size_256MB)
			break;

		if (start & mask_256MB) {
			start = (start + size_256MB) & ~mask_256MB;
			continue;
		}

		while (remains >= size_256MB) {
			unsigned long index = start >> shift_256MB;

			__set_bit(index, kpte_linear_bitmap);

			start += size_256MB;
			remains -= size_256MB;
		}
	}
}

static void __init init_kpte_bitmap(void)
{
	unsigned long i;

	for (i = 0; i < pall_ents; i++) {
		unsigned long phys_start, phys_end;

		phys_start = pall[i].phys_addr;
		phys_end = phys_start + pall[i].reg_size;

		mark_kpte_bitmap(phys_start, phys_end);
	}
}

static void __init kernel_physical_mapping_init(void)
{
#ifdef CONFIG_DEBUG_PAGEALLOC
	unsigned long i, mem_alloced = 0UL;

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
#endif
}

#ifdef CONFIG_DEBUG_PAGEALLOC
void kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long phys_start = page_to_pfn(page) << PAGE_SHIFT;
	unsigned long phys_end = phys_start + (numpages * PAGE_SIZE);

	kernel_map_range(phys_start, phys_end,
			 (enable ? PAGE_KERNEL : __pgprot(0)));

	flush_tsb_kernel_range(PAGE_OFFSET + phys_start,
			       PAGE_OFFSET + phys_end);

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

static void __init tsb_phys_patch(void)
{
	struct tsb_ldquad_phys_patch_entry *pquad;
	struct tsb_phys_patch_entry *p;

	pquad = &__tsb_ldquad_phys_patch;
	while (pquad < &__tsb_ldquad_phys_patch_end) {
		unsigned long addr = pquad->addr;

		if (tlb_type == hypervisor)
			*(unsigned int *) addr = pquad->sun4v_insn;
		else
			*(unsigned int *) addr = pquad->sun4u_insn;
		wmb();
		__asm__ __volatile__("flush	%0"
				     : /* no outputs */
				     : "r" (addr));

		pquad++;
	}

	p = &__tsb_phys_patch;
	while (p < &__tsb_phys_patch_end) {
		unsigned long addr = p->addr;

		*(unsigned int *) addr = p->insn;
		wmb();
		__asm__ __volatile__("flush	%0"
				     : /* no outputs */
				     : "r" (addr));

		p++;
	}
}

/* Don't mark as init, we give this to the Hypervisor.  */
#ifndef CONFIG_DEBUG_PAGEALLOC
#define NUM_KTSB_DESCR	2
#else
#define NUM_KTSB_DESCR	1
#endif
static struct hv_tsb_descr ktsb_descr[NUM_KTSB_DESCR];
extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static void __init sun4v_ktsb_init(void)
{
	unsigned long ktsb_pa;

	/* First KTSB for PAGE_SIZE mappings.  */
	ktsb_pa = kern_base + ((unsigned long)&swapper_tsb[0] - KERNBASE);

	switch (PAGE_SIZE) {
	case 8 * 1024:
	default:
		ktsb_descr[0].pgsz_idx = HV_PGSZ_IDX_8K;
		ktsb_descr[0].pgsz_mask = HV_PGSZ_MASK_8K;
		break;

	case 64 * 1024:
		ktsb_descr[0].pgsz_idx = HV_PGSZ_IDX_64K;
		ktsb_descr[0].pgsz_mask = HV_PGSZ_MASK_64K;
		break;

	case 512 * 1024:
		ktsb_descr[0].pgsz_idx = HV_PGSZ_IDX_512K;
		ktsb_descr[0].pgsz_mask = HV_PGSZ_MASK_512K;
		break;

	case 4 * 1024 * 1024:
		ktsb_descr[0].pgsz_idx = HV_PGSZ_IDX_4MB;
		ktsb_descr[0].pgsz_mask = HV_PGSZ_MASK_4MB;
		break;
	};

	ktsb_descr[0].assoc = 1;
	ktsb_descr[0].num_ttes = KERNEL_TSB_NENTRIES;
	ktsb_descr[0].ctx_idx = 0;
	ktsb_descr[0].tsb_base = ktsb_pa;
	ktsb_descr[0].resv = 0;

#ifndef CONFIG_DEBUG_PAGEALLOC
	/* Second KTSB for 4MB/256MB mappings.  */
	ktsb_pa = (kern_base +
		   ((unsigned long)&swapper_4m_tsb[0] - KERNBASE));

	ktsb_descr[1].pgsz_idx = HV_PGSZ_IDX_4MB;
	ktsb_descr[1].pgsz_mask = (HV_PGSZ_MASK_4MB |
				   HV_PGSZ_MASK_256MB);
	ktsb_descr[1].assoc = 1;
	ktsb_descr[1].num_ttes = KERNEL_TSB4M_NENTRIES;
	ktsb_descr[1].ctx_idx = 0;
	ktsb_descr[1].tsb_base = ktsb_pa;
	ktsb_descr[1].resv = 0;
#endif
}

void __cpuinit sun4v_ktsb_register(void)
{
	unsigned long pa, ret;

	pa = kern_base + ((unsigned long)&ktsb_descr[0] - KERNBASE);

	ret = sun4v_mmu_tsb_ctx0(NUM_KTSB_DESCR, pa);
	if (ret != 0) {
		prom_printf("hypervisor_mmu_tsb_ctx0[%lx]: "
			    "errors with %lx\n", pa, ret);
		prom_halt();
	}
}

/* paging_init() sets up the page tables */

extern void central_probe(void);

static unsigned long last_valid_pfn;
pgd_t swapper_pg_dir[2048];

static void sun4u_pgprot_init(void);
static void sun4v_pgprot_init(void);

/* Dummy function */
void __init setup_per_cpu_areas(void)
{
}

void __init paging_init(void)
{
	unsigned long end_pfn, shift, phys_base;
	unsigned long real_end, i;

	/* These build time checkes make sure that the dcache_dirty_cpu()
	 * page->flags usage will work.
	 *
	 * When a page gets marked as dcache-dirty, we store the
	 * cpu number starting at bit 32 in the page->flags.  Also,
	 * functions like clear_dcache_dirty_cpu use the cpu mask
	 * in 13-bit signed-immediate instruction fields.
	 */

	/*
	 * Page flags must not reach into upper 32 bits that are used
	 * for the cpu number
	 */
	BUILD_BUG_ON(NR_PAGEFLAGS > 32);

	/*
	 * The bit fields placed in the high range must not reach below
	 * the 32 bit boundary. Otherwise we cannot place the cpu field
	 * at the 32 bit boundary.
	 */
	BUILD_BUG_ON(SECTIONS_WIDTH + NODES_WIDTH + ZONES_WIDTH +
		ilog2(roundup_pow_of_two(NR_CPUS)) > 32);

	BUILD_BUG_ON(NR_CPUS > 4096);

	kern_base = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	kern_size = (unsigned long)&_end - (unsigned long)KERNBASE;

	sstate_booting();

	/* Invalidate both kernel TSBs.  */
	memset(swapper_tsb, 0x40, sizeof(swapper_tsb));
#ifndef CONFIG_DEBUG_PAGEALLOC
	memset(swapper_4m_tsb, 0x40, sizeof(swapper_4m_tsb));
#endif

	if (tlb_type == hypervisor)
		sun4v_pgprot_init();
	else
		sun4u_pgprot_init();

	if (tlb_type == cheetah_plus ||
	    tlb_type == hypervisor)
		tsb_phys_patch();

	if (tlb_type == hypervisor) {
		sun4v_patch_tlb_handlers();
		sun4v_ktsb_init();
	}

	lmb_init();

	/* Find available physical memory...
	 *
	 * Read it twice in order to work around a bug in openfirmware.
	 * The call to grab this table itself can cause openfirmware to
	 * allocate memory, which in turn can take away some space from
	 * the list of available memory.  Reading it twice makes sure
	 * we really do get the final value.
	 */
	read_obp_translations();
	read_obp_memory("reg", &pall[0], &pall_ents);
	read_obp_memory("available", &pavail[0], &pavail_ents);
	read_obp_memory("available", &pavail[0], &pavail_ents);

	phys_base = 0xffffffffffffffffUL;
	for (i = 0; i < pavail_ents; i++) {
		phys_base = min(phys_base, pavail[i].phys_addr);
		lmb_add(pavail[i].phys_addr, pavail[i].reg_size);
	}

	lmb_reserve(kern_base, kern_size);

	find_ramdisk(phys_base);

	if (cmdline_memory_size)
		lmb_enforce_memory_limit(phys_base + cmdline_memory_size);

	lmb_analyze();
	lmb_dump_all();

	set_bit(0, mmu_context_bmap);

	shift = kern_base + PAGE_OFFSET - ((unsigned long)KERNBASE);

	real_end = (unsigned long)_end;
	num_kernel_image_mappings = DIV_ROUND_UP(real_end - KERNBASE, 1 << 22);
	printk("Kernel: Using %d locked TLB entries for main kernel image.\n",
	       num_kernel_image_mappings);

	/* Set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t)));
	
	memset(swapper_low_pmd_dir, 0, sizeof(swapper_low_pmd_dir));

	/* Now can init the kernel/bad page tables. */
	pud_set(pud_offset(&swapper_pg_dir[0], 0),
		swapper_low_pmd_dir + (shift / sizeof(pgd_t)));
	
	inherit_prom_mappings();
	
	init_kpte_bitmap();

	/* Ok, we can use our TLB miss and window trap handlers safely.  */
	setup_tba();

	__flush_tlb_all();

	if (tlb_type == hypervisor)
		sun4v_ktsb_register();

	/* We must setup the per-cpu areas before we pull in the
	 * PROM and the MDESC.  The code there fills in cpu and
	 * other information into per-cpu data structures.
	 */
	real_setup_per_cpu_areas();

	prom_build_devicetree();

	if (tlb_type == hypervisor)
		sun4v_mdesc_init();

	/* Setup bootmem... */
	last_valid_pfn = end_pfn = bootmem_init(phys_base);

#ifndef CONFIG_NEED_MULTIPLE_NODES
	max_mapnr = last_valid_pfn;
#endif
	kernel_physical_mapping_init();

	{
		unsigned long max_zone_pfns[MAX_NR_ZONES];

		memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

		max_zone_pfns[ZONE_NORMAL] = end_pfn;

		free_area_init_nodes(max_zone_pfns);
	}

	printk("Booting Linux...\n");

	central_probe();
	cpu_probe();
}

int __init page_in_phys_avail(unsigned long paddr)
{
	int i;

	paddr &= PAGE_MASK;

	for (i = 0; i < pavail_ents; i++) {
		unsigned long start, end;

		start = pavail[i].phys_addr;
		end = start + pavail[i].reg_size;

		if (paddr >= start && paddr < end)
			return 1;
	}
	if (paddr >= kern_base && paddr < (kern_base + kern_size))
		return 1;
#ifdef CONFIG_BLK_DEV_INITRD
	if (paddr >= __pa(initrd_start) &&
	    paddr < __pa(PAGE_ALIGN(initrd_end)))
		return 1;
#endif

	return 0;
}

static struct linux_prom64_registers pavail_rescan[MAX_BANKS] __initdata;
static int pavail_rescan_ents __initdata;

/* Certain OBP calls, such as fetching "available" properties, can
 * claim physical memory.  So, along with initializing the valid
 * address bitmap, what we do here is refetch the physical available
 * memory list again, and make sure it provides at least as much
 * memory as 'pavail' does.
 */
static void setup_valid_addr_bitmap_from_pavail(void)
{
	int i;

	read_obp_memory("available", &pavail_rescan[0], &pavail_rescan_ents);

	for (i = 0; i < pavail_ents; i++) {
		unsigned long old_start, old_end;

		old_start = pavail[i].phys_addr;
		old_end = old_start + pavail[i].reg_size;
		while (old_start < old_end) {
			int n;

			for (n = 0; n < pavail_rescan_ents; n++) {
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

			prom_printf("mem_init: Lost memory in pavail\n");
			prom_printf("mem_init: OLD start[%lx] size[%lx]\n",
				    pavail[i].phys_addr,
				    pavail[i].reg_size);
			prom_printf("mem_init: NEW start[%lx] size[%lx]\n",
				    pavail_rescan[i].phys_addr,
				    pavail_rescan[i].reg_size);
			prom_printf("mem_init: Cannot continue, aborting.\n");
			prom_halt();

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

	setup_valid_addr_bitmap_from_pavail();

	high_memory = __va(last_valid_pfn << PAGE_SHIFT);

#ifdef CONFIG_NEED_MULTIPLE_NODES
	for_each_online_node(i) {
		if (NODE_DATA(i)->node_spanned_pages != 0) {
			totalram_pages +=
				free_all_bootmem_node(NODE_DATA(i));
		}
	}
#else
	totalram_pages = free_all_bootmem();
#endif

	/* We subtract one to account for the mem_map_zero page
	 * allocated below.
	 */
	totalram_pages -= 1;
	num_physpages = totalram_pages;

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

	printk("Memory: %luk available (%ldk kernel code, %ldk data, %ldk init) [%016lx,%016lx]\n",
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
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		p = virt_to_page(page);

		ClearPageReserved(p);
		init_page_count(p);
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
		init_page_count(p);
		__free_page(p);
		num_physpages++;
		totalram_pages++;
	}
}
#endif

#define _PAGE_CACHE_4U	(_PAGE_CP_4U | _PAGE_CV_4U)
#define _PAGE_CACHE_4V	(_PAGE_CP_4V | _PAGE_CV_4V)
#define __DIRTY_BITS_4U	 (_PAGE_MODIFIED_4U | _PAGE_WRITE_4U | _PAGE_W_4U)
#define __DIRTY_BITS_4V	 (_PAGE_MODIFIED_4V | _PAGE_WRITE_4V | _PAGE_W_4V)
#define __ACCESS_BITS_4U (_PAGE_ACCESSED_4U | _PAGE_READ_4U | _PAGE_R)
#define __ACCESS_BITS_4V (_PAGE_ACCESSED_4V | _PAGE_READ_4V | _PAGE_R)

pgprot_t PAGE_KERNEL __read_mostly;
EXPORT_SYMBOL(PAGE_KERNEL);

pgprot_t PAGE_KERNEL_LOCKED __read_mostly;
pgprot_t PAGE_COPY __read_mostly;

pgprot_t PAGE_SHARED __read_mostly;
EXPORT_SYMBOL(PAGE_SHARED);

pgprot_t PAGE_EXEC __read_mostly;
unsigned long pg_iobits __read_mostly;

unsigned long _PAGE_IE __read_mostly;
EXPORT_SYMBOL(_PAGE_IE);

unsigned long _PAGE_E __read_mostly;
EXPORT_SYMBOL(_PAGE_E);

unsigned long _PAGE_CACHE __read_mostly;
EXPORT_SYMBOL(_PAGE_CACHE);

#ifdef CONFIG_SPARSEMEM_VMEMMAP

#define VMEMMAP_CHUNK_SHIFT	22
#define VMEMMAP_CHUNK		(1UL << VMEMMAP_CHUNK_SHIFT)
#define VMEMMAP_CHUNK_MASK	~(VMEMMAP_CHUNK - 1UL)
#define VMEMMAP_ALIGN(x)	(((x)+VMEMMAP_CHUNK-1UL)&VMEMMAP_CHUNK_MASK)

#define VMEMMAP_SIZE	((((1UL << MAX_PHYSADDR_BITS) >> PAGE_SHIFT) * \
			  sizeof(struct page *)) >> VMEMMAP_CHUNK_SHIFT)
unsigned long vmemmap_table[VMEMMAP_SIZE];

int __meminit vmemmap_populate(struct page *start, unsigned long nr, int node)
{
	unsigned long vstart = (unsigned long) start;
	unsigned long vend = (unsigned long) (start + nr);
	unsigned long phys_start = (vstart - VMEMMAP_BASE);
	unsigned long phys_end = (vend - VMEMMAP_BASE);
	unsigned long addr = phys_start & VMEMMAP_CHUNK_MASK;
	unsigned long end = VMEMMAP_ALIGN(phys_end);
	unsigned long pte_base;

	pte_base = (_PAGE_VALID | _PAGE_SZ4MB_4U |
		    _PAGE_CP_4U | _PAGE_CV_4U |
		    _PAGE_P_4U | _PAGE_W_4U);
	if (tlb_type == hypervisor)
		pte_base = (_PAGE_VALID | _PAGE_SZ4MB_4V |
			    _PAGE_CP_4V | _PAGE_CV_4V |
			    _PAGE_P_4V | _PAGE_W_4V);

	for (; addr < end; addr += VMEMMAP_CHUNK) {
		unsigned long *vmem_pp =
			vmemmap_table + (addr >> VMEMMAP_CHUNK_SHIFT);
		void *block;

		if (!(*vmem_pp & _PAGE_VALID)) {
			block = vmemmap_alloc_block(1UL << 22, node);
			if (!block)
				return -ENOMEM;

			*vmem_pp = pte_base | __pa(block);

			printk(KERN_INFO "[%p-%p] page_structs=%lu "
			       "node=%d entry=%lu/%lu\n", start, block, nr,
			       node,
			       addr >> VMEMMAP_CHUNK_SHIFT,
			       VMEMMAP_SIZE >> VMEMMAP_CHUNK_SHIFT);
		}
	}
	return 0;
}
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

static void prot_init_common(unsigned long page_none,
			     unsigned long page_shared,
			     unsigned long page_copy,
			     unsigned long page_readonly,
			     unsigned long page_exec_bit)
{
	PAGE_COPY = __pgprot(page_copy);
	PAGE_SHARED = __pgprot(page_shared);

	protection_map[0x0] = __pgprot(page_none);
	protection_map[0x1] = __pgprot(page_readonly & ~page_exec_bit);
	protection_map[0x2] = __pgprot(page_copy & ~page_exec_bit);
	protection_map[0x3] = __pgprot(page_copy & ~page_exec_bit);
	protection_map[0x4] = __pgprot(page_readonly);
	protection_map[0x5] = __pgprot(page_readonly);
	protection_map[0x6] = __pgprot(page_copy);
	protection_map[0x7] = __pgprot(page_copy);
	protection_map[0x8] = __pgprot(page_none);
	protection_map[0x9] = __pgprot(page_readonly & ~page_exec_bit);
	protection_map[0xa] = __pgprot(page_shared & ~page_exec_bit);
	protection_map[0xb] = __pgprot(page_shared & ~page_exec_bit);
	protection_map[0xc] = __pgprot(page_readonly);
	protection_map[0xd] = __pgprot(page_readonly);
	protection_map[0xe] = __pgprot(page_shared);
	protection_map[0xf] = __pgprot(page_shared);
}

static void __init sun4u_pgprot_init(void)
{
	unsigned long page_none, page_shared, page_copy, page_readonly;
	unsigned long page_exec_bit;

	PAGE_KERNEL = __pgprot (_PAGE_PRESENT_4U | _PAGE_VALID |
				_PAGE_CACHE_4U | _PAGE_P_4U |
				__ACCESS_BITS_4U | __DIRTY_BITS_4U |
				_PAGE_EXEC_4U);
	PAGE_KERNEL_LOCKED = __pgprot (_PAGE_PRESENT_4U | _PAGE_VALID |
				       _PAGE_CACHE_4U | _PAGE_P_4U |
				       __ACCESS_BITS_4U | __DIRTY_BITS_4U |
				       _PAGE_EXEC_4U | _PAGE_L_4U);
	PAGE_EXEC = __pgprot(_PAGE_EXEC_4U);

	_PAGE_IE = _PAGE_IE_4U;
	_PAGE_E = _PAGE_E_4U;
	_PAGE_CACHE = _PAGE_CACHE_4U;

	pg_iobits = (_PAGE_VALID | _PAGE_PRESENT_4U | __DIRTY_BITS_4U |
		     __ACCESS_BITS_4U | _PAGE_E_4U);

#ifdef CONFIG_DEBUG_PAGEALLOC
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZBITS_4U) ^
		0xfffff80000000000;
#else
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4U) ^
		0xfffff80000000000;
#endif
	kern_linear_pte_xor[0] |= (_PAGE_CP_4U | _PAGE_CV_4U |
				   _PAGE_P_4U | _PAGE_W_4U);

	/* XXX Should use 256MB on Panther. XXX */
	kern_linear_pte_xor[1] = kern_linear_pte_xor[0];

	_PAGE_SZBITS = _PAGE_SZBITS_4U;
	_PAGE_ALL_SZ_BITS =  (_PAGE_SZ4MB_4U | _PAGE_SZ512K_4U |
			      _PAGE_SZ64K_4U | _PAGE_SZ8K_4U |
			      _PAGE_SZ32MB_4U | _PAGE_SZ256MB_4U);


	page_none = _PAGE_PRESENT_4U | _PAGE_ACCESSED_4U | _PAGE_CACHE_4U;
	page_shared = (_PAGE_VALID | _PAGE_PRESENT_4U | _PAGE_CACHE_4U |
		       __ACCESS_BITS_4U | _PAGE_WRITE_4U | _PAGE_EXEC_4U);
	page_copy   = (_PAGE_VALID | _PAGE_PRESENT_4U | _PAGE_CACHE_4U |
		       __ACCESS_BITS_4U | _PAGE_EXEC_4U);
	page_readonly   = (_PAGE_VALID | _PAGE_PRESENT_4U | _PAGE_CACHE_4U |
			   __ACCESS_BITS_4U | _PAGE_EXEC_4U);

	page_exec_bit = _PAGE_EXEC_4U;

	prot_init_common(page_none, page_shared, page_copy, page_readonly,
			 page_exec_bit);
}

static void __init sun4v_pgprot_init(void)
{
	unsigned long page_none, page_shared, page_copy, page_readonly;
	unsigned long page_exec_bit;

	PAGE_KERNEL = __pgprot (_PAGE_PRESENT_4V | _PAGE_VALID |
				_PAGE_CACHE_4V | _PAGE_P_4V |
				__ACCESS_BITS_4V | __DIRTY_BITS_4V |
				_PAGE_EXEC_4V);
	PAGE_KERNEL_LOCKED = PAGE_KERNEL;
	PAGE_EXEC = __pgprot(_PAGE_EXEC_4V);

	_PAGE_IE = _PAGE_IE_4V;
	_PAGE_E = _PAGE_E_4V;
	_PAGE_CACHE = _PAGE_CACHE_4V;

#ifdef CONFIG_DEBUG_PAGEALLOC
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZBITS_4V) ^
		0xfffff80000000000;
#else
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4V) ^
		0xfffff80000000000;
#endif
	kern_linear_pte_xor[0] |= (_PAGE_CP_4V | _PAGE_CV_4V |
				   _PAGE_P_4V | _PAGE_W_4V);

#ifdef CONFIG_DEBUG_PAGEALLOC
	kern_linear_pte_xor[1] = (_PAGE_VALID | _PAGE_SZBITS_4V) ^
		0xfffff80000000000;
#else
	kern_linear_pte_xor[1] = (_PAGE_VALID | _PAGE_SZ256MB_4V) ^
		0xfffff80000000000;
#endif
	kern_linear_pte_xor[1] |= (_PAGE_CP_4V | _PAGE_CV_4V |
				   _PAGE_P_4V | _PAGE_W_4V);

	pg_iobits = (_PAGE_VALID | _PAGE_PRESENT_4V | __DIRTY_BITS_4V |
		     __ACCESS_BITS_4V | _PAGE_E_4V);

	_PAGE_SZBITS = _PAGE_SZBITS_4V;
	_PAGE_ALL_SZ_BITS = (_PAGE_SZ16GB_4V | _PAGE_SZ2GB_4V |
			     _PAGE_SZ256MB_4V | _PAGE_SZ32MB_4V |
			     _PAGE_SZ4MB_4V | _PAGE_SZ512K_4V |
			     _PAGE_SZ64K_4V | _PAGE_SZ8K_4V);

	page_none = _PAGE_PRESENT_4V | _PAGE_ACCESSED_4V | _PAGE_CACHE_4V;
	page_shared = (_PAGE_VALID | _PAGE_PRESENT_4V | _PAGE_CACHE_4V |
		       __ACCESS_BITS_4V | _PAGE_WRITE_4V | _PAGE_EXEC_4V);
	page_copy   = (_PAGE_VALID | _PAGE_PRESENT_4V | _PAGE_CACHE_4V |
		       __ACCESS_BITS_4V | _PAGE_EXEC_4V);
	page_readonly = (_PAGE_VALID | _PAGE_PRESENT_4V | _PAGE_CACHE_4V |
			 __ACCESS_BITS_4V | _PAGE_EXEC_4V);

	page_exec_bit = _PAGE_EXEC_4V;

	prot_init_common(page_none, page_shared, page_copy, page_readonly,
			 page_exec_bit);
}

unsigned long pte_sz_bits(unsigned long sz)
{
	if (tlb_type == hypervisor) {
		switch (sz) {
		case 8 * 1024:
		default:
			return _PAGE_SZ8K_4V;
		case 64 * 1024:
			return _PAGE_SZ64K_4V;
		case 512 * 1024:
			return _PAGE_SZ512K_4V;
		case 4 * 1024 * 1024:
			return _PAGE_SZ4MB_4V;
		};
	} else {
		switch (sz) {
		case 8 * 1024:
		default:
			return _PAGE_SZ8K_4U;
		case 64 * 1024:
			return _PAGE_SZ64K_4U;
		case 512 * 1024:
			return _PAGE_SZ512K_4U;
		case 4 * 1024 * 1024:
			return _PAGE_SZ4MB_4U;
		};
	}
}

pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space, unsigned long page_size)
{
	pte_t pte;

	pte_val(pte)  = page | pgprot_val(pgprot_noncached(prot));
	pte_val(pte) |= (((unsigned long)space) << 32);
	pte_val(pte) |= pte_sz_bits(page_size);

	return pte;
}

static unsigned long kern_large_tte(unsigned long paddr)
{
	unsigned long val;

	val = (_PAGE_VALID | _PAGE_SZ4MB_4U |
	       _PAGE_CP_4U | _PAGE_CV_4U | _PAGE_P_4U |
	       _PAGE_EXEC_4U | _PAGE_L_4U | _PAGE_W_4U);
	if (tlb_type == hypervisor)
		val = (_PAGE_VALID | _PAGE_SZ4MB_4V |
		       _PAGE_CP_4V | _PAGE_CV_4V | _PAGE_P_4V |
		       _PAGE_EXEC_4V | _PAGE_W_4V);

	return val | paddr;
}

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
	if (tlb_type == hypervisor) {
		sun4v_mmu_demap_all();
	} else if (tlb_type == spitfire) {
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

			if (!(spitfire_get_dtlb_data(i) & _PAGE_L_4U)) {
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

			if (!(spitfire_get_itlb_data(i) & _PAGE_L_4U)) {
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

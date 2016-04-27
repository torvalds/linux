/*
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
#include <linux/initrd.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/poison.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/cache.h>
#include <linux/sort.h>
#include <linux/ioport.h>
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/gfp.h>

#include <asm/head.h>
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
#include <asm/mdesc.h>
#include <asm/cpudata.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include "init_64.h"

unsigned long kern_linear_pte_xor[4] __read_mostly;
static unsigned long page_cache4v_flag;

/* A bitmap, two bits for every 256MB of physical memory.  These two
 * bits determine what page size we use for kernel linear
 * translations.  They form an index into kern_linear_pte_xor[].  The
 * value in the indexed slot is XOR'd with the TLB miss virtual
 * address to form the resulting TTE.  The mapping is:
 *
 *	0	==>	4MB
 *	1	==>	256MB
 *	2	==>	2GB
 *	3	==>	16GB
 *
 * All sun4v chips support 256MB pages.  Only SPARC-T4 and later
 * support 2GB pages, and hopefully future cpus will support the 16GB
 * pages as well.  For slots 2 and 3, we encode a 256MB TTE xor there
 * if these larger page sizes are not supported by the cpu.
 *
 * It would be nice to determine this from the machine description
 * 'cpu' properties, but we need to have this table setup before the
 * MDESC is initialized.
 */

#ifndef CONFIG_DEBUG_PAGEALLOC
/* A special kernel TSB for 4MB, 256MB, 2GB and 16GB linear mappings.
 * Space is allocated for this right after the trap table in
 * arch/sparc64/kernel/head.S
 */
extern struct tsb swapper_4m_tsb[KERNEL_TSB4M_NENTRIES];
#endif
extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static unsigned long cpu_pgsz_mask;

#define MAX_BANKS	1024

static struct linux_prom64_registers pavail[MAX_BANKS];
static int pavail_ents;

u64 numa_latency[MAX_NUMNODES][MAX_NUMNODES];

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
	phandle node = prom_finddevice("/memory");
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
		prom_printf("Couldn't get %s property from /memory.\n",
				property);
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

static void flush_dcache(unsigned long pfn)
{
	struct page *page;

	page = pfn_to_page(pfn);
	if (page) {
		unsigned long pg_flags;

		pg_flags = page->flags;
		if (pg_flags & (1UL << PG_dcache_dirty)) {
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
}

/* mm->context.lock must be held */
static void __update_mmu_tsb_insert(struct mm_struct *mm, unsigned long tsb_index,
				    unsigned long tsb_hash_shift, unsigned long address,
				    unsigned long tte)
{
	struct tsb *tsb = mm->context.tsb_block[tsb_index].tsb;
	unsigned long tag;

	if (unlikely(!tsb))
		return;

	tsb += ((address >> tsb_hash_shift) &
		(mm->context.tsb_block[tsb_index].tsb_nentries - 1UL));
	tag = (address >> 22UL);
	tsb_insert(tsb, tag, tte);
}

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
static inline bool is_hugetlb_pte(pte_t pte)
{
	if ((tlb_type == hypervisor &&
	     (pte_val(pte) & _PAGE_SZALL_4V) == _PAGE_SZHUGE_4V) ||
	    (tlb_type != hypervisor &&
	     (pte_val(pte) & _PAGE_SZALL_4U) == _PAGE_SZHUGE_4U))
		return true;
	return false;
}
#endif

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t *ptep)
{
	struct mm_struct *mm;
	unsigned long flags;
	pte_t pte = *ptep;

	if (tlb_type != hypervisor) {
		unsigned long pfn = pte_pfn(pte);

		if (pfn_valid(pfn))
			flush_dcache(pfn);
	}

	mm = vma->vm_mm;

	/* Don't insert a non-valid PTE into the TSB, we'll deadlock.  */
	if (!pte_accessible(mm, pte))
		return;

	spin_lock_irqsave(&mm->context.lock, flags);

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
	if (mm->context.huge_pte_count && is_hugetlb_pte(pte))
		__update_mmu_tsb_insert(mm, MM_TSB_HUGE, REAL_HPAGE_SHIFT,
					address, pte_val(pte));
	else
#endif
		__update_mmu_tsb_insert(mm, MM_TSB_BASE, PAGE_SHIFT,
					address, pte_val(pte));

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
EXPORT_SYMBOL(flush_dcache_page);

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
EXPORT_SYMBOL(flush_icache_range);

void mmu_info(struct seq_file *m)
{
	static const char *pgsz_strings[] = {
		"8K", "64K", "512K", "4MB", "32MB",
		"256MB", "2GB", "16GB",
	};
	int i, printed;

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

	seq_printf(m, "MMU PGSZs\t: ");
	printed = 0;
	for (i = 0; i < ARRAY_SIZE(pgsz_strings); i++) {
		if (cpu_pgsz_mask & (1UL << i)) {
			seq_printf(m, "%s%s",
				   printed ? "," : "", pgsz_strings[i]);
			printed++;
		}
	}
	seq_putc(m, '\n');

#ifdef CONFIG_DEBUG_DCFLUSH
	seq_printf(m, "DCPageFlushes\t: %d\n",
		   atomic_read(&dcpage_flushes));
#ifdef CONFIG_SMP
	seq_printf(m, "DCPageFlushesXC\t: %d\n",
		   atomic_read(&dcpage_flushes_xcall));
#endif /* CONFIG_SMP */
#endif /* CONFIG_DEBUG_DCFLUSH */
}

struct linux_prom_translation prom_trans[512] __read_mostly;
unsigned int prom_trans_ents __read_mostly;

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
		prom_printf("prom_mappings: Size %d is too big.\n", n);
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

	/* Force execute bit on.  */
	for (i = 0; i < prom_trans_ents; i++)
		prom_trans[i].data |= (tlb_type == hypervisor ?
				       _PAGE_EXEC_4V : _PAGE_EXEC_4U);
}

static void __init hypervisor_tlb_lock(unsigned long vaddr,
				       unsigned long pte,
				       unsigned long mmu)
{
	unsigned long ret = sun4v_mmu_map_perm_addr(vaddr, 0, pte, mmu);

	if (ret != 0) {
		prom_printf("hypervisor_tlb_lock[%lx:%x:%lx:%lx]: "
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
	phys_page = (prom_boot_mapping_phys_low >> ILOG2_4MB) << ILOG2_4MB;
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
		set_fs(get_fs());

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
EXPORT_SYMBOL(__flush_dcache_range);

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
	int new_version;

	spin_lock(&ctx_alloc_lock);
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
	spin_unlock(&ctx_alloc_lock);

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

		memblock_reserve(initrd_start, sparc_ramdisk_size);

		initrd_start += PAGE_OFFSET;
		initrd_end += PAGE_OFFSET;
	}
#endif
}

struct node_mem_mask {
	unsigned long mask;
	unsigned long val;
};
static struct node_mem_mask node_masks[MAX_NUMNODES];
static int num_node_masks;

#ifdef CONFIG_NEED_MULTIPLE_NODES

int numa_cpu_lookup_table[NR_CPUS];
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];

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
	/* The following condition has been observed on LDOM guests.*/
	WARN_ONCE(1, "find_node: A physical address doesn't match a NUMA node"
		" rule. Some physical memory will be owned by node 0.");
	return 0;
}

static u64 memblock_nid_range(u64 start, u64 end, int *nid)
{
	*nid = find_node(start);
	start += PAGE_SIZE;
	while (start < end) {
		int n = find_node(start);

		if (n != *nid)
			break;
		start += PAGE_SIZE;
	}

	if (start > end)
		start = end;

	return start;
}
#endif

/* This must be invoked after performing all of the necessary
 * memblock_set_node() calls for 'nid'.  We need to be able to get
 * correct data from get_pfn_range_for_nid().
 */
static void __init allocate_node_data(int nid)
{
	struct pglist_data *p;
	unsigned long start_pfn, end_pfn;
#ifdef CONFIG_NEED_MULTIPLE_NODES
	unsigned long paddr;

	paddr = memblock_alloc_try_nid(sizeof(struct pglist_data), SMP_CACHE_BYTES, nid);
	if (!paddr) {
		prom_printf("Cannot allocate pglist_data for nid[%d]\n", nid);
		prom_halt();
	}
	NODE_DATA(nid) = __va(paddr);
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->node_id = nid;
#endif

	p = NODE_DATA(nid);

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
	p->node_start_pfn = start_pfn;
	p->node_spanned_pages = end_pfn - start_pfn;
}

static void init_node_masks_nonnuma(void)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int i;
#endif

	numadbg("Initializing tables for non-numa.\n");

	node_masks[0].mask = node_masks[0].val = 0;
	num_node_masks = 1;

#ifdef CONFIG_NEED_MULTIPLE_NODES
	for (i = 0; i < NR_CPUS; i++)
		numa_cpu_lookup_table[i] = 0;

	cpumask_setall(&numa_cpumask_lookup_table[0]);
#endif
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

	/* This is the right thing to do on currently supported
	 * SUN4U NUMA platforms as well, as the PCI controller does
	 * not sit behind any particular memory controller.
	 */
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

static void __init add_node_ranges(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		unsigned long size = reg->size;
		unsigned long start, end;

		start = reg->base;
		end = start + size;
		while (start < end) {
			unsigned long this_end;
			int nid;

			this_end = memblock_nid_range(start, end, &nid);

			numadbg("Setting memblock NUMA node nid[%d] "
				"start[%lx] end[%lx]\n",
				nid, start, this_end);

			memblock_set_node(start, this_end - start,
					  &memblock.memory, nid);
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

	paddr = memblock_alloc(count * sizeof(struct mdesc_mlgroup),
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

		numadbg("MLGROUP[%d]: node[%llx] latency[%llx] "
			"match[%llx] mask[%llx]\n",
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

	paddr = memblock_alloc(count * sizeof(struct mdesc_mblock),
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

		/* The address-congruence-offset property is optional.
		 * Explicity zero it be identifty this.
		 */
		if (val)
			m->offset = *val;
		else
			m->offset = 0UL;

		numadbg("MBLOCK[%d]: base[%llx] size[%llx] offset[%llx]\n",
			count - 1, m->base, m->size, m->offset);
	}

	return 0;
}

static void __init numa_parse_mdesc_group_cpus(struct mdesc_handle *md,
					       u64 grp, cpumask_t *mask)
{
	u64 arc;

	cpumask_clear(mask);

	mdesc_for_each_arc(arc, md, grp, MDESC_ARC_TYPE_BACK) {
		u64 target = mdesc_arc_target(md, arc);
		const char *name = mdesc_node_name(md, target);
		const u64 *id;

		if (strcmp(name, "cpu"))
			continue;
		id = mdesc_get_property(md, target, "id", NULL);
		if (*id < nr_cpu_ids)
			cpumask_set_cpu(*id, mask);
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

int __node_distance(int from, int to)
{
	if ((from >= MAX_NUMNODES) || (to >= MAX_NUMNODES)) {
		pr_warn("Returning default NUMA distance value for %d->%d\n",
			from, to);
		return (from == to) ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	}
	return numa_latency[from][to];
}

static int find_best_numa_node_for_mlgroup(struct mdesc_mlgroup *grp)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		struct node_mem_mask *n = &node_masks[i];

		if ((grp->mask == n->mask) && (grp->match == n->val))
			break;
	}
	return i;
}

static void find_numa_latencies_for_group(struct mdesc_handle *md, u64 grp,
					  int index)
{
	u64 arc;

	mdesc_for_each_arc(arc, md, grp, MDESC_ARC_TYPE_FWD) {
		int tnode;
		u64 target = mdesc_arc_target(md, arc);
		struct mdesc_mlgroup *m = find_mlgroup(target);

		if (!m)
			continue;
		tnode = find_best_numa_node_for_mlgroup(m);
		if (tnode == MAX_NUMNODES)
			continue;
		numa_latency[index][tnode] = m->latency;
	}
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

	numadbg("NUMA NODE[%d]: mask[%lx] val[%lx] (latency[%llx])\n",
		index, n->mask, n->val, candidate->latency);

	return 0;
}

static int __init numa_parse_mdesc_group(struct mdesc_handle *md, u64 grp,
					 int index)
{
	cpumask_t mask;
	int cpu;

	numa_parse_mdesc_group_cpus(md, grp, &mask);

	for_each_cpu(cpu, &mask)
		numa_cpu_lookup_table[cpu] = index;
	cpumask_copy(&numa_cpumask_lookup_table[index], &mask);

	if (numa_debug) {
		printk(KERN_INFO "NUMA GROUP[%d]: cpus [ ", index);
		for_each_cpu(cpu, &mask)
			printk("%d ", cpu);
		printk("]\n");
	}

	return numa_attach_mlgroup(md, grp, index);
}

static int __init numa_parse_mdesc(void)
{
	struct mdesc_handle *md = mdesc_grab();
	int i, j, err, count;
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

	count = 0;
	mdesc_for_each_node_by_name(md, node, "group") {
		find_numa_latencies_for_group(md, node, count);
		count++;
	}

	/* Normalize numa latency matrix according to ACPI SLIT spec. */
	for (i = 0; i < MAX_NUMNODES; i++) {
		u64 self_latency = numa_latency[i][i];

		for (j = 0; j < MAX_NUMNODES; j++) {
			numa_latency[i][j] =
				(numa_latency[i][j] * LOCAL_DISTANCE) /
				self_latency;
		}
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

static int __init numa_parse_jbus(void)
{
	unsigned long cpu, index;

	/* NUMA node id is encoded in bits 36 and higher, and there is
	 * a 1-to-1 mapping from CPU ID to NUMA node ID.
	 */
	index = 0;
	for_each_present_cpu(cpu) {
		numa_cpu_lookup_table[cpu] = index;
		cpumask_copy(&numa_cpumask_lookup_table[index], cpumask_of(cpu));
		node_masks[index].mask = ~((1UL << 36UL) - 1UL);
		node_masks[index].val = cpu << 36UL;

		index++;
	}
	num_node_masks = index;

	add_node_ranges();

	for (index = 0; index < num_node_masks; index++) {
		allocate_node_data(index);
		node_set_online(index);
	}

	return 0;
}

static int __init numa_parse_sun4u(void)
{
	if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		unsigned long ver;

		__asm__ ("rdpr %%ver, %0" : "=r" (ver));
		if ((ver >> 32UL) == __JALAPENO_ID ||
		    (ver >> 32UL) == __SERRANO_ID)
			return numa_parse_jbus();
	}
	return -1;
}

static int __init bootmem_init_numa(void)
{
	int i, j;
	int err = -1;

	numadbg("bootmem_init_numa()\n");

	/* Some sane defaults for numa latency values */
	for (i = 0; i < MAX_NUMNODES; i++) {
		for (j = 0; j < MAX_NUMNODES; j++)
			numa_latency[i][j] = (i == j) ?
				LOCAL_DISTANCE : REMOTE_DISTANCE;
	}

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
	unsigned long top_of_ram = memblock_end_of_DRAM();
	unsigned long total_ram = memblock_phys_mem_size();

	numadbg("bootmem_init_nonnuma()\n");

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	init_node_masks_nonnuma();
	memblock_set_node(0, (phys_addr_t)ULLONG_MAX, &memblock.memory, 0);
	allocate_node_data(0);
	node_set_online(0);
}

static unsigned long __init bootmem_init(unsigned long phys_base)
{
	unsigned long end_pfn;

	end_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn = end_pfn;
	min_low_pfn = (phys_base >> PAGE_SHIFT);

	if (bootmem_init_numa() < 0)
		bootmem_init_nonnuma();

	/* Dump memblock with node info. */
	memblock_dump_all();

	/* XXX cpu notifier XXX */

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

	return end_pfn;
}

static struct linux_prom64_registers pall[MAX_BANKS] __initdata;
static int pall_ents __initdata;

static unsigned long max_phys_bits = 40;

bool kern_addr_valid(unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if ((long)addr < 0L) {
		unsigned long pa = __pa(addr);

		if ((addr >> max_phys_bits) != 0UL)
			return false;

		return pfn_valid(pa >> PAGE_SHIFT);
	}

	if (addr >= (unsigned long) KERNBASE &&
	    addr < (unsigned long)&_end)
		return true;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd))
		return 0;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud))
		return 0;

	if (pud_large(*pud))
		return pfn_valid(pud_pfn(*pud));

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return pfn_valid(pmd_pfn(*pmd));

	pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte))
		return 0;

	return pfn_valid(pte_pfn(*pte));
}
EXPORT_SYMBOL(kern_addr_valid);

static unsigned long __ref kernel_map_hugepud(unsigned long vstart,
					      unsigned long vend,
					      pud_t *pud)
{
	const unsigned long mask16gb = (1UL << 34) - 1UL;
	u64 pte_val = vstart;

	/* Each PUD is 8GB */
	if ((vstart & mask16gb) ||
	    (vend - vstart <= mask16gb)) {
		pte_val ^= kern_linear_pte_xor[2];
		pud_val(*pud) = pte_val | _PAGE_PUD_HUGE;

		return vstart + PUD_SIZE;
	}

	pte_val ^= kern_linear_pte_xor[3];
	pte_val |= _PAGE_PUD_HUGE;

	vend = vstart + mask16gb + 1UL;
	while (vstart < vend) {
		pud_val(*pud) = pte_val;

		pte_val += PUD_SIZE;
		vstart += PUD_SIZE;
		pud++;
	}
	return vstart;
}

static bool kernel_can_map_hugepud(unsigned long vstart, unsigned long vend,
				   bool guard)
{
	if (guard && !(vstart & ~PUD_MASK) && (vend - vstart) >= PUD_SIZE)
		return true;

	return false;
}

static unsigned long __ref kernel_map_hugepmd(unsigned long vstart,
					      unsigned long vend,
					      pmd_t *pmd)
{
	const unsigned long mask256mb = (1UL << 28) - 1UL;
	const unsigned long mask2gb = (1UL << 31) - 1UL;
	u64 pte_val = vstart;

	/* Each PMD is 8MB */
	if ((vstart & mask256mb) ||
	    (vend - vstart <= mask256mb)) {
		pte_val ^= kern_linear_pte_xor[0];
		pmd_val(*pmd) = pte_val | _PAGE_PMD_HUGE;

		return vstart + PMD_SIZE;
	}

	if ((vstart & mask2gb) ||
	    (vend - vstart <= mask2gb)) {
		pte_val ^= kern_linear_pte_xor[1];
		pte_val |= _PAGE_PMD_HUGE;
		vend = vstart + mask256mb + 1UL;
	} else {
		pte_val ^= kern_linear_pte_xor[2];
		pte_val |= _PAGE_PMD_HUGE;
		vend = vstart + mask2gb + 1UL;
	}

	while (vstart < vend) {
		pmd_val(*pmd) = pte_val;

		pte_val += PMD_SIZE;
		vstart += PMD_SIZE;
		pmd++;
	}

	return vstart;
}

static bool kernel_can_map_hugepmd(unsigned long vstart, unsigned long vend,
				   bool guard)
{
	if (guard && !(vstart & ~PMD_MASK) && (vend - vstart) >= PMD_SIZE)
		return true;

	return false;
}

static unsigned long __ref kernel_map_range(unsigned long pstart,
					    unsigned long pend, pgprot_t prot,
					    bool use_huge)
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

		if (pgd_none(*pgd)) {
			pud_t *new;

			new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
			alloc_bytes += PAGE_SIZE;
			pgd_populate(&init_mm, pgd, new);
		}
		pud = pud_offset(pgd, vstart);
		if (pud_none(*pud)) {
			pmd_t *new;

			if (kernel_can_map_hugepud(vstart, vend, use_huge)) {
				vstart = kernel_map_hugepud(vstart, vend, pud);
				continue;
			}
			new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
			alloc_bytes += PAGE_SIZE;
			pud_populate(&init_mm, pud, new);
		}

		pmd = pmd_offset(pud, vstart);
		if (pmd_none(*pmd)) {
			pte_t *new;

			if (kernel_can_map_hugepmd(vstart, vend, use_huge)) {
				vstart = kernel_map_hugepmd(vstart, vend, pmd);
				continue;
			}
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

static void __init flush_all_kernel_tsbs(void)
{
	int i;

	for (i = 0; i < KERNEL_TSB_NENTRIES; i++) {
		struct tsb *ent = &swapper_tsb[i];

		ent->tag = (1UL << TSB_TAG_INVALID_BIT);
	}
#ifndef CONFIG_DEBUG_PAGEALLOC
	for (i = 0; i < KERNEL_TSB4M_NENTRIES; i++) {
		struct tsb *ent = &swapper_4m_tsb[i];

		ent->tag = (1UL << TSB_TAG_INVALID_BIT);
	}
#endif
}

extern unsigned int kvmap_linear_patch[1];

static void __init kernel_physical_mapping_init(void)
{
	unsigned long i, mem_alloced = 0UL;
	bool use_huge = true;

#ifdef CONFIG_DEBUG_PAGEALLOC
	use_huge = false;
#endif
	for (i = 0; i < pall_ents; i++) {
		unsigned long phys_start, phys_end;

		phys_start = pall[i].phys_addr;
		phys_end = phys_start + pall[i].reg_size;

		mem_alloced += kernel_map_range(phys_start, phys_end,
						PAGE_KERNEL, use_huge);
	}

	printk("Allocated %ld bytes for kernel page tables.\n",
	       mem_alloced);

	kvmap_linear_patch[0] = 0x01000000; /* nop */
	flushi(&kvmap_linear_patch[0]);

	flush_all_kernel_tsbs();

	__flush_tlb_all();
}

#ifdef CONFIG_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long phys_start = page_to_pfn(page) << PAGE_SHIFT;
	unsigned long phys_end = phys_start + (numpages * PAGE_SIZE);

	kernel_map_range(phys_start, phys_end,
			 (enable ? PAGE_KERNEL : __pgprot(0)), false);

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

unsigned long PAGE_OFFSET;
EXPORT_SYMBOL(PAGE_OFFSET);

unsigned long VMALLOC_END   = 0x0000010000000000UL;
EXPORT_SYMBOL(VMALLOC_END);

unsigned long sparc64_va_hole_top =    0xfffff80000000000UL;
unsigned long sparc64_va_hole_bottom = 0x0000080000000000UL;

static void __init setup_page_offset(void)
{
	if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		/* Cheetah/Panther support a full 64-bit virtual
		 * address, so we can use all that our page tables
		 * support.
		 */
		sparc64_va_hole_top =    0xfff0000000000000UL;
		sparc64_va_hole_bottom = 0x0010000000000000UL;

		max_phys_bits = 42;
	} else if (tlb_type == hypervisor) {
		switch (sun4v_chip_type) {
		case SUN4V_CHIP_NIAGARA1:
		case SUN4V_CHIP_NIAGARA2:
			/* T1 and T2 support 48-bit virtual addresses.  */
			sparc64_va_hole_top =    0xffff800000000000UL;
			sparc64_va_hole_bottom = 0x0000800000000000UL;

			max_phys_bits = 39;
			break;
		case SUN4V_CHIP_NIAGARA3:
			/* T3 supports 48-bit virtual addresses.  */
			sparc64_va_hole_top =    0xffff800000000000UL;
			sparc64_va_hole_bottom = 0x0000800000000000UL;

			max_phys_bits = 43;
			break;
		case SUN4V_CHIP_NIAGARA4:
		case SUN4V_CHIP_NIAGARA5:
		case SUN4V_CHIP_SPARC64X:
		case SUN4V_CHIP_SPARC_M6:
			/* T4 and later support 52-bit virtual addresses.  */
			sparc64_va_hole_top =    0xfff8000000000000UL;
			sparc64_va_hole_bottom = 0x0008000000000000UL;
			max_phys_bits = 47;
			break;
		case SUN4V_CHIP_SPARC_M7:
		default:
			/* M7 and later support 52-bit virtual addresses.  */
			sparc64_va_hole_top =    0xfff8000000000000UL;
			sparc64_va_hole_bottom = 0x0008000000000000UL;
			max_phys_bits = 49;
			break;
		}
	}

	if (max_phys_bits > MAX_PHYS_ADDRESS_BITS) {
		prom_printf("MAX_PHYS_ADDRESS_BITS is too small, need %lu\n",
			    max_phys_bits);
		prom_halt();
	}

	PAGE_OFFSET = sparc64_va_hole_top;
	VMALLOC_END = ((sparc64_va_hole_bottom >> 1) +
		       (sparc64_va_hole_bottom >> 2));

	pr_info("MM: PAGE_OFFSET is 0x%016lx (max_phys_bits == %lu)\n",
		PAGE_OFFSET, max_phys_bits);
	pr_info("MM: VMALLOC [0x%016lx --> 0x%016lx]\n",
		VMALLOC_START, VMALLOC_END);
	pr_info("MM: VMEMMAP [0x%016lx --> 0x%016lx]\n",
		VMEMMAP_BASE, VMEMMAP_BASE << 1);
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

/* The swapper TSBs are loaded with a base sequence of:
 *
 *	sethi	%uhi(SYMBOL), REG1
 *	sethi	%hi(SYMBOL), REG2
 *	or	REG1, %ulo(SYMBOL), REG1
 *	or	REG2, %lo(SYMBOL), REG2
 *	sllx	REG1, 32, REG1
 *	or	REG1, REG2, REG1
 *
 * When we use physical addressing for the TSB accesses, we patch the
 * first four instructions in the above sequence.
 */

static void patch_one_ktsb_phys(unsigned int *start, unsigned int *end, unsigned long pa)
{
	unsigned long high_bits, low_bits;

	high_bits = (pa >> 32) & 0xffffffff;
	low_bits = (pa >> 0) & 0xffffffff;

	while (start < end) {
		unsigned int *ia = (unsigned int *)(unsigned long)*start;

		ia[0] = (ia[0] & ~0x3fffff) | (high_bits >> 10);
		__asm__ __volatile__("flush	%0" : : "r" (ia));

		ia[1] = (ia[1] & ~0x3fffff) | (low_bits >> 10);
		__asm__ __volatile__("flush	%0" : : "r" (ia + 1));

		ia[2] = (ia[2] & ~0x1fff) | (high_bits & 0x3ff);
		__asm__ __volatile__("flush	%0" : : "r" (ia + 2));

		ia[3] = (ia[3] & ~0x1fff) | (low_bits & 0x3ff);
		__asm__ __volatile__("flush	%0" : : "r" (ia + 3));

		start++;
	}
}

static void ktsb_phys_patch(void)
{
	extern unsigned int __swapper_tsb_phys_patch;
	extern unsigned int __swapper_tsb_phys_patch_end;
	unsigned long ktsb_pa;

	ktsb_pa = kern_base + ((unsigned long)&swapper_tsb[0] - KERNBASE);
	patch_one_ktsb_phys(&__swapper_tsb_phys_patch,
			    &__swapper_tsb_phys_patch_end, ktsb_pa);
#ifndef CONFIG_DEBUG_PAGEALLOC
	{
	extern unsigned int __swapper_4m_tsb_phys_patch;
	extern unsigned int __swapper_4m_tsb_phys_patch_end;
	ktsb_pa = (kern_base +
		   ((unsigned long)&swapper_4m_tsb[0] - KERNBASE));
	patch_one_ktsb_phys(&__swapper_4m_tsb_phys_patch,
			    &__swapper_4m_tsb_phys_patch_end, ktsb_pa);
	}
#endif
}

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
	}

	ktsb_descr[0].assoc = 1;
	ktsb_descr[0].num_ttes = KERNEL_TSB_NENTRIES;
	ktsb_descr[0].ctx_idx = 0;
	ktsb_descr[0].tsb_base = ktsb_pa;
	ktsb_descr[0].resv = 0;

#ifndef CONFIG_DEBUG_PAGEALLOC
	/* Second KTSB for 4MB/256MB/2GB/16GB mappings.  */
	ktsb_pa = (kern_base +
		   ((unsigned long)&swapper_4m_tsb[0] - KERNBASE));

	ktsb_descr[1].pgsz_idx = HV_PGSZ_IDX_4MB;
	ktsb_descr[1].pgsz_mask = ((HV_PGSZ_MASK_4MB |
				    HV_PGSZ_MASK_256MB |
				    HV_PGSZ_MASK_2GB |
				    HV_PGSZ_MASK_16GB) &
				   cpu_pgsz_mask);
	ktsb_descr[1].assoc = 1;
	ktsb_descr[1].num_ttes = KERNEL_TSB4M_NENTRIES;
	ktsb_descr[1].ctx_idx = 0;
	ktsb_descr[1].tsb_base = ktsb_pa;
	ktsb_descr[1].resv = 0;
#endif
}

void sun4v_ktsb_register(void)
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

static void __init sun4u_linear_pte_xor_finalize(void)
{
#ifndef CONFIG_DEBUG_PAGEALLOC
	/* This is where we would add Panther support for
	 * 32MB and 256MB pages.
	 */
#endif
}

static void __init sun4v_linear_pte_xor_finalize(void)
{
	unsigned long pagecv_flag;

	/* Bit 9 of TTE is no longer CV bit on M7 processor and it instead
	 * enables MCD error. Do not set bit 9 on M7 processor.
	 */
	switch (sun4v_chip_type) {
	case SUN4V_CHIP_SPARC_M7:
		pagecv_flag = 0x00;
		break;
	default:
		pagecv_flag = _PAGE_CV_4V;
		break;
	}
#ifndef CONFIG_DEBUG_PAGEALLOC
	if (cpu_pgsz_mask & HV_PGSZ_MASK_256MB) {
		kern_linear_pte_xor[1] = (_PAGE_VALID | _PAGE_SZ256MB_4V) ^
			PAGE_OFFSET;
		kern_linear_pte_xor[1] |= (_PAGE_CP_4V | pagecv_flag |
					   _PAGE_P_4V | _PAGE_W_4V);
	} else {
		kern_linear_pte_xor[1] = kern_linear_pte_xor[0];
	}

	if (cpu_pgsz_mask & HV_PGSZ_MASK_2GB) {
		kern_linear_pte_xor[2] = (_PAGE_VALID | _PAGE_SZ2GB_4V) ^
			PAGE_OFFSET;
		kern_linear_pte_xor[2] |= (_PAGE_CP_4V | pagecv_flag |
					   _PAGE_P_4V | _PAGE_W_4V);
	} else {
		kern_linear_pte_xor[2] = kern_linear_pte_xor[1];
	}

	if (cpu_pgsz_mask & HV_PGSZ_MASK_16GB) {
		kern_linear_pte_xor[3] = (_PAGE_VALID | _PAGE_SZ16GB_4V) ^
			PAGE_OFFSET;
		kern_linear_pte_xor[3] |= (_PAGE_CP_4V | pagecv_flag |
					   _PAGE_P_4V | _PAGE_W_4V);
	} else {
		kern_linear_pte_xor[3] = kern_linear_pte_xor[2];
	}
#endif
}

/* paging_init() sets up the page tables */

static unsigned long last_valid_pfn;

static void sun4u_pgprot_init(void);
static void sun4v_pgprot_init(void);

static phys_addr_t __init available_memory(void)
{
	phys_addr_t available = 0ULL;
	phys_addr_t pa_start, pa_end;
	u64 i;

	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &pa_start,
				&pa_end, NULL)
		available = available + (pa_end  - pa_start);

	return available;
}

#define _PAGE_CACHE_4U	(_PAGE_CP_4U | _PAGE_CV_4U)
#define _PAGE_CACHE_4V	(_PAGE_CP_4V | _PAGE_CV_4V)
#define __DIRTY_BITS_4U	 (_PAGE_MODIFIED_4U | _PAGE_WRITE_4U | _PAGE_W_4U)
#define __DIRTY_BITS_4V	 (_PAGE_MODIFIED_4V | _PAGE_WRITE_4V | _PAGE_W_4V)
#define __ACCESS_BITS_4U (_PAGE_ACCESSED_4U | _PAGE_READ_4U | _PAGE_R)
#define __ACCESS_BITS_4V (_PAGE_ACCESSED_4V | _PAGE_READ_4V | _PAGE_R)

/* We need to exclude reserved regions. This exclusion will include
 * vmlinux and initrd. To be more precise the initrd size could be used to
 * compute a new lower limit because it is freed later during initialization.
 */
static void __init reduce_memory(phys_addr_t limit_ram)
{
	phys_addr_t avail_ram = available_memory();
	phys_addr_t pa_start, pa_end;
	u64 i;

	if (limit_ram >= avail_ram)
		return;

	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &pa_start,
				&pa_end, NULL) {
		phys_addr_t region_size = pa_end - pa_start;
		phys_addr_t clip_start = pa_start;

		avail_ram = avail_ram - region_size;
		/* Are we consuming too much? */
		if (avail_ram < limit_ram) {
			phys_addr_t give_back = limit_ram - avail_ram;

			region_size = region_size - give_back;
			clip_start = clip_start + give_back;
		}

		memblock_remove(clip_start, region_size);

		if (avail_ram <= limit_ram)
			break;
		i = 0UL;
	}
}

void __init paging_init(void)
{
	unsigned long end_pfn, shift, phys_base;
	unsigned long real_end, i;
	int node;

	setup_page_offset();

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

	kern_base = (prom_boot_mapping_phys_low >> ILOG2_4MB) << ILOG2_4MB;
	kern_size = (unsigned long)&_end - (unsigned long)KERNBASE;

	/* Invalidate both kernel TSBs.  */
	memset(swapper_tsb, 0x40, sizeof(swapper_tsb));
#ifndef CONFIG_DEBUG_PAGEALLOC
	memset(swapper_4m_tsb, 0x40, sizeof(swapper_4m_tsb));
#endif

	/* TTE.cv bit on sparc v9 occupies the same position as TTE.mcde
	 * bit on M7 processor. This is a conflicting usage of the same
	 * bit. Enabling TTE.cv on M7 would turn on Memory Corruption
	 * Detection error on all pages and this will lead to problems
	 * later. Kernel does not run with MCD enabled and hence rest
	 * of the required steps to fully configure memory corruption
	 * detection are not taken. We need to ensure TTE.mcde is not
	 * set on M7 processor. Compute the value of cacheability
	 * flag for use later taking this into consideration.
	 */
	switch (sun4v_chip_type) {
	case SUN4V_CHIP_SPARC_M7:
		page_cache4v_flag = _PAGE_CP_4V;
		break;
	default:
		page_cache4v_flag = _PAGE_CACHE_4V;
		break;
	}

	if (tlb_type == hypervisor)
		sun4v_pgprot_init();
	else
		sun4u_pgprot_init();

	if (tlb_type == cheetah_plus ||
	    tlb_type == hypervisor) {
		tsb_phys_patch();
		ktsb_phys_patch();
	}

	if (tlb_type == hypervisor)
		sun4v_patch_tlb_handlers();

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
		memblock_add(pavail[i].phys_addr, pavail[i].reg_size);
	}

	memblock_reserve(kern_base, kern_size);

	find_ramdisk(phys_base);

	if (cmdline_memory_size)
		reduce_memory(cmdline_memory_size);

	memblock_allow_resize();
	memblock_dump_all();

	set_bit(0, mmu_context_bmap);

	shift = kern_base + PAGE_OFFSET - ((unsigned long)KERNBASE);

	real_end = (unsigned long)_end;
	num_kernel_image_mappings = DIV_ROUND_UP(real_end - KERNBASE, 1 << ILOG2_4MB);
	printk("Kernel: Using %d locked TLB entries for main kernel image.\n",
	       num_kernel_image_mappings);

	/* Set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t)));
	
	memset(swapper_pg_dir, 0, sizeof(swapper_pg_dir));

	inherit_prom_mappings();
	
	/* Ok, we can use our TLB miss and window trap handlers safely.  */
	setup_tba();

	__flush_tlb_all();

	prom_build_devicetree();
	of_populate_present_mask();
#ifndef CONFIG_SMP
	of_fill_in_cpu_data();
#endif

	if (tlb_type == hypervisor) {
		sun4v_mdesc_init();
		mdesc_populate_present_mask(cpu_all_mask);
#ifndef CONFIG_SMP
		mdesc_fill_in_cpu_data(cpu_all_mask);
#endif
		mdesc_get_page_sizes(cpu_all_mask, &cpu_pgsz_mask);

		sun4v_linear_pte_xor_finalize();

		sun4v_ktsb_init();
		sun4v_ktsb_register();
	} else {
		unsigned long impl, ver;

		cpu_pgsz_mask = (HV_PGSZ_MASK_8K | HV_PGSZ_MASK_64K |
				 HV_PGSZ_MASK_512K | HV_PGSZ_MASK_4MB);

		__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
		impl = ((ver >> 32) & 0xffff);
		if (impl == PANTHER_IMPL)
			cpu_pgsz_mask |= (HV_PGSZ_MASK_32MB |
					  HV_PGSZ_MASK_256MB);

		sun4u_linear_pte_xor_finalize();
	}

	/* Flush the TLBs and the 4M TSB so that the updated linear
	 * pte XOR settings are realized for all mappings.
	 */
	__flush_tlb_all();
#ifndef CONFIG_DEBUG_PAGEALLOC
	memset(swapper_4m_tsb, 0x40, sizeof(swapper_4m_tsb));
#endif
	__flush_tlb_all();

	/* Setup bootmem... */
	last_valid_pfn = end_pfn = bootmem_init(phys_base);

	/* Once the OF device tree and MDESC have been setup, we know
	 * the list of possible cpus.  Therefore we can allocate the
	 * IRQ stacks.
	 */
	for_each_possible_cpu(i) {
		node = cpu_to_node(i);

		softirq_stack[i] = __alloc_bootmem_node(NODE_DATA(node),
							THREAD_SIZE,
							THREAD_SIZE, 0);
		hardirq_stack[i] = __alloc_bootmem_node(NODE_DATA(node),
							THREAD_SIZE,
							THREAD_SIZE, 0);
	}

	kernel_physical_mapping_init();

	{
		unsigned long max_zone_pfns[MAX_NR_ZONES];

		memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

		max_zone_pfns[ZONE_NORMAL] = end_pfn;

		free_area_init_nodes(max_zone_pfns);
	}

	printk("Booting Linux...\n");
}

int page_in_phys_avail(unsigned long paddr)
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

static void __init register_page_bootmem_info(void)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int i;

	for_each_online_node(i)
		if (NODE_DATA(i)->node_spanned_pages)
			register_page_bootmem_info_node(NODE_DATA(i));
#endif
}
void __init mem_init(void)
{
	high_memory = __va(last_valid_pfn << PAGE_SHIFT);

	register_page_bootmem_info();
	free_all_bootmem();

	/*
	 * Set up the zero page, mark it reserved, so that page count
	 * is not manipulated when freeing the page from user ptes.
	 */
	mem_map_zero = alloc_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if (mem_map_zero == NULL) {
		prom_printf("paging_init: Cannot alloc zero page.\n");
		prom_halt();
	}
	mark_page_reserved(mem_map_zero);

	mem_init_print_info(NULL);

	if (tlb_type == cheetah || tlb_type == cheetah_plus)
		cheetah_ecache_flush_init();
}

void free_initmem(void)
{
	unsigned long addr, initend;
	int do_free = 1;

	/* If the physical memory maps were trimmed by kernel command
	 * line options, don't even try freeing this initmem stuff up.
	 * The kernel image could have been in the trimmed out region
	 * and if so the freeing below will free invalid page structs.
	 */
	if (cmdline_memory_size)
		do_free = 0;

	/*
	 * The init section is aligned to 8k in vmlinux.lds. Page align for >8k pagesizes.
	 */
	addr = PAGE_ALIGN((unsigned long)(__init_begin));
	initend = (unsigned long)(__init_end) & PAGE_MASK;
	for (; addr < initend; addr += PAGE_SIZE) {
		unsigned long page;

		page = (addr +
			((unsigned long) __va(kern_base)) -
			((unsigned long) KERNBASE));
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);

		if (do_free)
			free_reserved_page(virt_to_page(page));
	}
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

pgprot_t PAGE_KERNEL __read_mostly;
EXPORT_SYMBOL(PAGE_KERNEL);

pgprot_t PAGE_KERNEL_LOCKED __read_mostly;
pgprot_t PAGE_COPY __read_mostly;

pgprot_t PAGE_SHARED __read_mostly;
EXPORT_SYMBOL(PAGE_SHARED);

unsigned long pg_iobits __read_mostly;

unsigned long _PAGE_IE __read_mostly;
EXPORT_SYMBOL(_PAGE_IE);

unsigned long _PAGE_E __read_mostly;
EXPORT_SYMBOL(_PAGE_E);

unsigned long _PAGE_CACHE __read_mostly;
EXPORT_SYMBOL(_PAGE_CACHE);

#ifdef CONFIG_SPARSEMEM_VMEMMAP
int __meminit vmemmap_populate(unsigned long vstart, unsigned long vend,
			       int node)
{
	unsigned long pte_base;

	pte_base = (_PAGE_VALID | _PAGE_SZ4MB_4U |
		    _PAGE_CP_4U | _PAGE_CV_4U |
		    _PAGE_P_4U | _PAGE_W_4U);
	if (tlb_type == hypervisor)
		pte_base = (_PAGE_VALID | _PAGE_SZ4MB_4V |
			    page_cache4v_flag | _PAGE_P_4V | _PAGE_W_4V);

	pte_base |= _PAGE_PMD_HUGE;

	vstart = vstart & PMD_MASK;
	vend = ALIGN(vend, PMD_SIZE);
	for (; vstart < vend; vstart += PMD_SIZE) {
		pgd_t *pgd = pgd_offset_k(vstart);
		unsigned long pte;
		pud_t *pud;
		pmd_t *pmd;

		if (pgd_none(*pgd)) {
			pud_t *new = vmemmap_alloc_block(PAGE_SIZE, node);

			if (!new)
				return -ENOMEM;
			pgd_populate(&init_mm, pgd, new);
		}

		pud = pud_offset(pgd, vstart);
		if (pud_none(*pud)) {
			pmd_t *new = vmemmap_alloc_block(PAGE_SIZE, node);

			if (!new)
				return -ENOMEM;
			pud_populate(&init_mm, pud, new);
		}

		pmd = pmd_offset(pud, vstart);

		pte = pmd_val(*pmd);
		if (!(pte & _PAGE_VALID)) {
			void *block = vmemmap_alloc_block(PMD_SIZE, node);

			if (!block)
				return -ENOMEM;

			pmd_val(*pmd) = pte_base | __pa(block);
		}
	}

	return 0;
}

void vmemmap_free(unsigned long start, unsigned long end)
{
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
	int i;

	PAGE_KERNEL = __pgprot (_PAGE_PRESENT_4U | _PAGE_VALID |
				_PAGE_CACHE_4U | _PAGE_P_4U |
				__ACCESS_BITS_4U | __DIRTY_BITS_4U |
				_PAGE_EXEC_4U);
	PAGE_KERNEL_LOCKED = __pgprot (_PAGE_PRESENT_4U | _PAGE_VALID |
				       _PAGE_CACHE_4U | _PAGE_P_4U |
				       __ACCESS_BITS_4U | __DIRTY_BITS_4U |
				       _PAGE_EXEC_4U | _PAGE_L_4U);

	_PAGE_IE = _PAGE_IE_4U;
	_PAGE_E = _PAGE_E_4U;
	_PAGE_CACHE = _PAGE_CACHE_4U;

	pg_iobits = (_PAGE_VALID | _PAGE_PRESENT_4U | __DIRTY_BITS_4U |
		     __ACCESS_BITS_4U | _PAGE_E_4U);

#ifdef CONFIG_DEBUG_PAGEALLOC
	kern_linear_pte_xor[0] = _PAGE_VALID ^ PAGE_OFFSET;
#else
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4U) ^
		PAGE_OFFSET;
#endif
	kern_linear_pte_xor[0] |= (_PAGE_CP_4U | _PAGE_CV_4U |
				   _PAGE_P_4U | _PAGE_W_4U);

	for (i = 1; i < 4; i++)
		kern_linear_pte_xor[i] = kern_linear_pte_xor[0];

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
	int i;

	PAGE_KERNEL = __pgprot (_PAGE_PRESENT_4V | _PAGE_VALID |
				page_cache4v_flag | _PAGE_P_4V |
				__ACCESS_BITS_4V | __DIRTY_BITS_4V |
				_PAGE_EXEC_4V);
	PAGE_KERNEL_LOCKED = PAGE_KERNEL;

	_PAGE_IE = _PAGE_IE_4V;
	_PAGE_E = _PAGE_E_4V;
	_PAGE_CACHE = page_cache4v_flag;

#ifdef CONFIG_DEBUG_PAGEALLOC
	kern_linear_pte_xor[0] = _PAGE_VALID ^ PAGE_OFFSET;
#else
	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4V) ^
		PAGE_OFFSET;
#endif
	kern_linear_pte_xor[0] |= (page_cache4v_flag | _PAGE_P_4V |
				   _PAGE_W_4V);

	for (i = 1; i < 4; i++)
		kern_linear_pte_xor[i] = kern_linear_pte_xor[0];

	pg_iobits = (_PAGE_VALID | _PAGE_PRESENT_4V | __DIRTY_BITS_4V |
		     __ACCESS_BITS_4V | _PAGE_E_4V);

	_PAGE_ALL_SZ_BITS = (_PAGE_SZ16GB_4V | _PAGE_SZ2GB_4V |
			     _PAGE_SZ256MB_4V | _PAGE_SZ32MB_4V |
			     _PAGE_SZ4MB_4V | _PAGE_SZ512K_4V |
			     _PAGE_SZ64K_4V | _PAGE_SZ8K_4V);

	page_none = _PAGE_PRESENT_4V | _PAGE_ACCESSED_4V | page_cache4v_flag;
	page_shared = (_PAGE_VALID | _PAGE_PRESENT_4V | page_cache4v_flag |
		       __ACCESS_BITS_4V | _PAGE_WRITE_4V | _PAGE_EXEC_4V);
	page_copy   = (_PAGE_VALID | _PAGE_PRESENT_4V | page_cache4v_flag |
		       __ACCESS_BITS_4V | _PAGE_EXEC_4V);
	page_readonly = (_PAGE_VALID | _PAGE_PRESENT_4V | page_cache4v_flag |
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
		}
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
		}
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
		       page_cache4v_flag | _PAGE_P_4V |
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

pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
			    unsigned long address)
{
	struct page *page = alloc_page(GFP_KERNEL | __GFP_NOTRACK |
				       __GFP_REPEAT | __GFP_ZERO);
	pte_t *pte = NULL;

	if (page)
		pte = (pte_t *) page_address(page);

	return pte;
}

pgtable_t pte_alloc_one(struct mm_struct *mm,
			unsigned long address)
{
	struct page *page = alloc_page(GFP_KERNEL | __GFP_NOTRACK |
				       __GFP_REPEAT | __GFP_ZERO);
	if (!page)
		return NULL;
	if (!pgtable_page_ctor(page)) {
		free_hot_cold_page(page, 0);
		return NULL;
	}
	return (pte_t *) page_address(page);
}

void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static void __pte_free(pgtable_t pte)
{
	struct page *page = virt_to_page(pte);

	pgtable_page_dtor(page);
	__free_page(page);
}

void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	__pte_free(pte);
}

void pgtable_free(void *table, bool is_page)
{
	if (is_page)
		__pte_free(table);
	else
		kmem_cache_free(pgtable_cache, table);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
			  pmd_t *pmd)
{
	unsigned long pte, flags;
	struct mm_struct *mm;
	pmd_t entry = *pmd;

	if (!pmd_large(entry) || !pmd_young(entry))
		return;

	pte = pmd_val(entry);

	/* Don't insert a non-valid PMD into the TSB, we'll deadlock.  */
	if (!(pte & _PAGE_VALID))
		return;

	/* We are fabricating 8MB pages using 4MB real hw pages.  */
	pte |= (addr & (1UL << REAL_HPAGE_SHIFT));

	mm = vma->vm_mm;

	spin_lock_irqsave(&mm->context.lock, flags);

	if (mm->context.tsb_block[MM_TSB_HUGE].tsb != NULL)
		__update_mmu_tsb_insert(mm, MM_TSB_HUGE, REAL_HPAGE_SHIFT,
					addr, pte);

	spin_unlock_irqrestore(&mm->context.lock, flags);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
static void context_reload(void *__data)
{
	struct mm_struct *mm = __data;

	if (mm == current->mm)
		load_secondary_context(mm);
}

void hugetlb_setup(struct pt_regs *regs)
{
	struct mm_struct *mm = current->mm;
	struct tsb_config *tp;

	if (faulthandler_disabled() || !mm) {
		const struct exception_table_entry *entry;

		entry = search_exception_tables(regs->tpc);
		if (entry) {
			regs->tpc = entry->fixup;
			regs->tnpc = regs->tpc + 4;
			return;
		}
		pr_alert("Unexpected HugeTLB setup in atomic context.\n");
		die_if_kernel("HugeTSB in atomic", regs);
	}

	tp = &mm->context.tsb_block[MM_TSB_HUGE];
	if (likely(tp->tsb == NULL))
		tsb_grow(mm, MM_TSB_HUGE, 0);

	tsb_context_switch(mm);
	smp_tsb_sync(mm);

	/* On UltraSPARC-III+ and later, configure the second half of
	 * the Data-TLB for huge pages.
	 */
	if (tlb_type == cheetah_plus) {
		unsigned long ctx;

		spin_lock(&ctx_alloc_lock);
		ctx = mm->context.sparc64_ctx_val;
		ctx &= ~CTX_PGSZ_MASK;
		ctx |= CTX_PGSZ_BASE << CTX_PGSZ0_SHIFT;
		ctx |= CTX_PGSZ_HUGE << CTX_PGSZ1_SHIFT;

		if (ctx != mm->context.sparc64_ctx_val) {
			/* When changing the page size fields, we
			 * must perform a context flush so that no
			 * stale entries match.  This flush must
			 * occur with the original context register
			 * settings.
			 */
			do_flush_tlb_mm(mm);

			/* Reload the context register of all processors
			 * also executing in this address space.
			 */
			mm->context.sparc64_ctx_val = ctx;
			on_each_cpu(context_reload, mm, 0);
		}
		spin_unlock(&ctx_alloc_lock);
	}
}
#endif

static struct resource code_resource = {
	.name	= "Kernel code",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource data_resource = {
	.name	= "Kernel data",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static inline resource_size_t compute_kern_paddr(void *addr)
{
	return (resource_size_t) (addr - KERNBASE + kern_base);
}

static void __init kernel_lds_init(void)
{
	code_resource.start = compute_kern_paddr(_text);
	code_resource.end   = compute_kern_paddr(_etext - 1);
	data_resource.start = compute_kern_paddr(_etext);
	data_resource.end   = compute_kern_paddr(_edata - 1);
	bss_resource.start  = compute_kern_paddr(__bss_start);
	bss_resource.end    = compute_kern_paddr(_end - 1);
}

static int __init report_memory(void)
{
	int i;
	struct resource *res;

	kernel_lds_init();

	for (i = 0; i < pavail_ents; i++) {
		res = kzalloc(sizeof(struct resource), GFP_KERNEL);

		if (!res) {
			pr_warn("Failed to allocate source.\n");
			break;
		}

		res->name = "System RAM";
		res->start = pavail[i].phys_addr;
		res->end = pavail[i].phys_addr + pavail[i].reg_size - 1;
		res->flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM;

		if (insert_resource(&iomem_resource, res) < 0) {
			pr_warn("Resource insertion failed.\n");
			break;
		}

		insert_resource(res, &code_resource);
		insert_resource(res, &data_resource);
		insert_resource(res, &bss_resource);
	}

	return 0;
}
arch_initcall(report_memory);

#ifdef CONFIG_SMP
#define do_flush_tlb_kernel_range	smp_flush_tlb_kernel_range
#else
#define do_flush_tlb_kernel_range	__flush_tlb_kernel_range
#endif

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	if (start < HI_OBP_ADDRESS && end > LOW_OBP_ADDRESS) {
		if (start < LOW_OBP_ADDRESS) {
			flush_tsb_kernel_range(start, LOW_OBP_ADDRESS);
			do_flush_tlb_kernel_range(start, LOW_OBP_ADDRESS);
		}
		if (end > HI_OBP_ADDRESS) {
			flush_tsb_kernel_range(HI_OBP_ADDRESS, end);
			do_flush_tlb_kernel_range(HI_OBP_ADDRESS, end);
		}
	} else {
		flush_tsb_kernel_range(start, end);
		do_flush_tlb_kernel_range(start, end);
	}
}

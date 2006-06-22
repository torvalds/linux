/*  $Id: init.c,v 1.209 2002/02/09 19:49:31 davem Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996-1999 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997-1999 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
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
#include <asm/tsb.h>
#include <asm/hypervisor.h>
#include <asm/prom.h>

extern void device_scan(void);

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

/* A special kernel TSB for 4MB and 256MB linear mappings.  */
struct tsb swapper_4m_tsb[KERNEL_TSB4M_NENTRIES];

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

	for (i = 0; i < ents; i++) {
		if (regs[i].reg_size == 0UL) {
			int j;

			for (j = i; j < ents - 1; j++) {
				regs[j].phys_addr =
					regs[j+1].phys_addr;
				regs[j].reg_size =
					regs[j+1].reg_size;
			}

			ents--;
			i--;
		}
	}

	*num_ents = ents;

	sort(regs, ents, sizeof(struct linux_prom64_registers),
	     cmp_p64, NULL);
}

unsigned long *sparc64_valid_addr_bitmap __read_mostly;

/* Kernel physical address base and size in bytes.  */
unsigned long kern_base __read_mostly;
unsigned long kern_size __read_mostly;

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

unsigned int sparc64_highest_unlocked_tlb_ent __read_mostly;

unsigned long sparc64_kern_pri_context __read_mostly;
unsigned long sparc64_kern_pri_nuc_bits __read_mostly;
unsigned long sparc64_kern_sec_context __read_mostly;

int bigkernel = 0;

kmem_cache_t *pgtable_cache __read_mostly;

static void zero_ctor(void *addr, kmem_cache_t *cache, unsigned long flags)
{
	clear_page(addr);
}

extern void tsb_cache_init(void);

void pgtable_cache_init(void)
{
	pgtable_cache = kmem_cache_create("pgtable_cache",
					  PAGE_SIZE, PAGE_SIZE,
					  SLAB_HWCACHE_ALIGN |
					  SLAB_MUST_HWCACHE_ALIGN,
					  zero_ctor,
					  NULL);
	if (!pgtable_cache) {
		prom_printf("Could not create pgtable_cache\n");
		prom_halt();
	}
	tsb_cache_init();
}

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
#define PG_dcache_cpu_shift	24UL
#define PG_dcache_cpu_mask	(256UL - 1UL)

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

		for (kaddr = start; kaddr < end; kaddr += PAGE_SIZE)
			__flush_icache_page(__get_phys(kaddr));
	}
}

void show_mem(void)
{
	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n",
	       nr_swap_pages << (PAGE_SHIFT-10));
	printk("%ld pages of RAM\n", num_physpages);
	printk("%d free pages\n", nr_free_pages());
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
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");
	register unsigned long arg1 asm("%o1");
	register unsigned long arg2 asm("%o2");
	register unsigned long arg3 asm("%o3");

	func = HV_FAST_MMU_MAP_PERM_ADDR;
	arg0 = vaddr;
	arg1 = 0;
	arg2 = pte;
	arg3 = mmu;
	__asm__ __volatile__("ta	0x80"
			     : "=&r" (func), "=&r" (arg0),
			       "=&r" (arg1), "=&r" (arg2),
			       "=&r" (arg3)
			     : "0" (func), "1" (arg0), "2" (arg1),
			       "3" (arg2), "4" (arg3));
	if (arg0 != 0) {
		prom_printf("hypervisor_tlb_lock[%lx:%lx:%lx:%lx]: "
			    "errors with %lx\n", vaddr, 0, pte, mmu, arg0);
		prom_halt();
	}
}

static unsigned long kern_large_tte(unsigned long paddr);

static void __init remap_kernel(void)
{
	unsigned long phys_page, tte_vaddr, tte_data;
	int tlb_ent = sparc64_highest_locked_tlbent();

	tte_vaddr = (unsigned long) KERNBASE;
	phys_page = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	tte_data = kern_large_tte(phys_page);

	kern_locked_tte_data = tte_data;

	/* Now lock us into the TLBs via Hypervisor or OBP. */
	if (tlb_type == hypervisor) {
		hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_DMMU);
		hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_IMMU);
		if (bigkernel) {
			tte_vaddr += 0x400000;
			tte_data += 0x400000;
			hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_DMMU);
			hypervisor_tlb_lock(tte_vaddr, tte_data, HV_MMU_IMMU);
		}
	} else {
		prom_dtlb_load(tlb_ent, tte_data, tte_vaddr);
		prom_itlb_load(tlb_ent, tte_data, tte_vaddr);
		if (bigkernel) {
			tlb_ent -= 1;
			prom_dtlb_load(tlb_ent,
				       tte_data + 0x400000, 
				       tte_vaddr + 0x400000);
			prom_itlb_load(tlb_ent,
				       tte_data + 0x400000, 
				       tte_vaddr + 0x400000);
		}
		sparc64_highest_unlocked_tlb_ent = tlb_ent - 1;
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
	read_obp_translations();

	/* Now fixup OBP's idea about where we really are mapped. */
	prom_printf("Remapping the kernel... ");
	remap_kernel();
	prom_printf("done.\n");
}

void prom_world(int enter)
{
	if (!enter)
		set_fs((mm_segment_t) { get_thread_current_ds() });

	__asm__ __volatile__("flushw");
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
#endif /* DCACHE_ALIASING_POSSIBLE */

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

/* Find a free area for the bootmem map, avoiding the kernel image
 * and the initial ramdisk.
 */
static unsigned long __init choose_bootmap_pfn(unsigned long start_pfn,
					       unsigned long end_pfn)
{
	unsigned long avoid_start, avoid_end, bootmap_size;
	int i;

	bootmap_size = ((end_pfn - start_pfn) + 7) / 8;
	bootmap_size = ALIGN(bootmap_size, sizeof(long));

	avoid_start = avoid_end = 0;
#ifdef CONFIG_BLK_DEV_INITRD
	avoid_start = initrd_start;
	avoid_end = PAGE_ALIGN(initrd_end);
#endif

#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("choose_bootmap_pfn: kern[%lx:%lx] avoid[%lx:%lx]\n",
		    kern_base, PAGE_ALIGN(kern_base + kern_size),
		    avoid_start, avoid_end);
#endif
	for (i = 0; i < pavail_ents; i++) {
		unsigned long start, end;

		start = pavail[i].phys_addr;
		end = start + pavail[i].reg_size;

		while (start < end) {
			if (start >= kern_base &&
			    start < PAGE_ALIGN(kern_base + kern_size)) {
				start = PAGE_ALIGN(kern_base + kern_size);
				continue;
			}
			if (start >= avoid_start && start < avoid_end) {
				start = avoid_end;
				continue;
			}

			if ((end - start) < bootmap_size)
				break;

			if (start < kern_base &&
			    (start + bootmap_size) > kern_base) {
				start = PAGE_ALIGN(kern_base + kern_size);
				continue;
			}

			if (start < avoid_start &&
			    (start + bootmap_size) > avoid_start) {
				start = avoid_end;
				continue;
			}

			/* OK, it doesn't overlap anything, use it.  */
#ifdef CONFIG_DEBUG_BOOTMEM
			prom_printf("choose_bootmap_pfn: Using %lx [%lx]\n",
				    start >> PAGE_SHIFT, start);
#endif
			return start >> PAGE_SHIFT;
		}
	}

	prom_printf("Cannot find free area for bootmap, aborting.\n");
	prom_halt();
}

static unsigned long __init bootmem_init(unsigned long *pages_avail,
					 unsigned long phys_base)
{
	unsigned long bootmap_size, end_pfn;
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
			initrd_end = 0;
		}
	}
#endif	
	/* Initialize the boot-time allocator. */
	max_pfn = max_low_pfn = end_pfn;
	min_low_pfn = (phys_base >> PAGE_SHIFT);

	bootmap_pfn = choose_bootmap_pfn(min_low_pfn, end_pfn);

#ifdef CONFIG_DEBUG_BOOTMEM
	prom_printf("init_bootmem(min[%lx], bootmap[%lx], max[%lx])\n",
		    min_low_pfn, bootmap_pfn, max_low_pfn);
#endif
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap_pfn,
					 min_low_pfn, end_pfn);

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

	for (i = 0; i < pavail_ents; i++) {
		unsigned long start_pfn, end_pfn;

		start_pfn = pavail[i].phys_addr >> PAGE_SHIFT;
		end_pfn = (start_pfn + (pavail[i].reg_size >> PAGE_SHIFT));
#ifdef CONFIG_DEBUG_BOOTMEM
		prom_printf("memory_present(0, %lx, %lx)\n",
			    start_pfn, end_pfn);
#endif
		memory_present(0, start_pfn, end_pfn);
	}

	sparse_init();

	return end_pfn;
}

static struct linux_prom64_registers pall[MAX_BANKS] __initdata;
static int pall_ents __initdata;

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

static void __init kernel_physical_mapping_init(void)
{
	unsigned long i;
#ifdef CONFIG_DEBUG_PAGEALLOC
	unsigned long mem_alloced = 0UL;
#endif

	read_obp_memory("reg", &pall[0], &pall_ents);

	for (i = 0; i < pall_ents; i++) {
		unsigned long phys_start, phys_end;

		phys_start = pall[i].phys_addr;
		phys_end = phys_start + pall[i].reg_size;

		mark_kpte_bitmap(phys_start, phys_end);

#ifdef CONFIG_DEBUG_PAGEALLOC
		mem_alloced += kernel_map_range(phys_start, phys_end,
						PAGE_KERNEL);
#endif
	}

#ifdef CONFIG_DEBUG_PAGEALLOC
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
static struct hv_tsb_descr ktsb_descr[2];
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
}

void __cpuinit sun4v_ktsb_register(void)
{
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");
	register unsigned long arg1 asm("%o1");
	unsigned long pa;

	pa = kern_base + ((unsigned long)&ktsb_descr[0] - KERNBASE);

	func = HV_FAST_MMU_TSB_CTX0;
	arg0 = 2;
	arg1 = pa;
	__asm__ __volatile__("ta	%6"
			     : "=&r" (func), "=&r" (arg0), "=&r" (arg1)
			     : "0" (func), "1" (arg0), "2" (arg1),
			       "i" (HV_FAST_TRAP));
}

/* paging_init() sets up the page tables */

extern void cheetah_ecache_flush_init(void);
extern void sun4v_patch_tlb_handlers(void);

static unsigned long last_valid_pfn;
pgd_t swapper_pg_dir[2048];

static void sun4u_pgprot_init(void);
static void sun4v_pgprot_init(void);

void __init paging_init(void)
{
	unsigned long end_pfn, pages_avail, shift, phys_base;
	unsigned long real_end, i;

	kern_base = (prom_boot_mapping_phys_low >> 22UL) << 22UL;
	kern_size = (unsigned long)&_end - (unsigned long)KERNBASE;

	/* Invalidate both kernel TSBs.  */
	memset(swapper_tsb, 0x40, sizeof(swapper_tsb));
	memset(swapper_4m_tsb, 0x40, sizeof(swapper_4m_tsb));

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

	/* Find available physical memory... */
	read_obp_memory("available", &pavail[0], &pavail_ents);

	phys_base = 0xffffffffffffffffUL;
	for (i = 0; i < pavail_ents; i++)
		phys_base = min(phys_base, pavail[i].phys_addr);

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
	
	inherit_prom_mappings();
	
	/* Ok, we can use our TLB miss and window trap handlers safely.  */
	setup_tba();

	__flush_tlb_all();

	if (tlb_type == hypervisor)
		sun4v_ktsb_register();

	/* Setup bootmem... */
	pages_avail = 0;
	last_valid_pfn = end_pfn = bootmem_init(&pages_avail, phys_base);

	max_mapnr = last_valid_pfn;

	kernel_physical_mapping_init();

	prom_build_devicetree();

	{
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long zholes_size[MAX_NR_ZONES];
		int znum;

		for (znum = 0; znum < MAX_NR_ZONES; znum++)
			zones_size[znum] = zholes_size[znum] = 0;

		zones_size[ZONE_DMA] = end_pfn;
		zholes_size[ZONE_DMA] = end_pfn - pages_avail;

		free_area_init_node(0, &contig_page_data, zones_size,
				    __pa(PAGE_OFFSET) >> PAGE_SHIFT,
				    zholes_size);
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

unsigned long _PAGE_E __read_mostly;
EXPORT_SYMBOL(_PAGE_E);

unsigned long _PAGE_CACHE __read_mostly;
EXPORT_SYMBOL(_PAGE_CACHE);

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

	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4U) ^
		0xfffff80000000000;
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

	kern_linear_pte_xor[0] = (_PAGE_VALID | _PAGE_SZ4MB_4V) ^
		0xfffff80000000000;
	kern_linear_pte_xor[0] |= (_PAGE_CP_4V | _PAGE_CV_4V |
				   _PAGE_P_4V | _PAGE_W_4V);

	kern_linear_pte_xor[1] = (_PAGE_VALID | _PAGE_SZ256MB_4V) ^
		0xfffff80000000000;
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

/*
 * Translate PROM's mapping we capture at boot time into physical address.
 * The second parameter is only set from prom_callback() invocations.
 */
unsigned long prom_virt_to_phys(unsigned long promva, int *error)
{
	unsigned long mask;
	int i;

	mask = _PAGE_PADDR_4U;
	if (tlb_type == hypervisor)
		mask = _PAGE_PADDR_4V;

	for (i = 0; i < prom_trans_ents; i++) {
		struct linux_prom_translation *p = &prom_trans[i];

		if (promva >= p->virt &&
		    promva < (p->virt + p->size)) {
			unsigned long base = p->data & mask;

			if (error)
				*error = 0;
			return base + (promva & (8192 - 1));
		}
	}
	if (error)
		*error = 1;
	return 0UL;
}

/* XXX We should kill off this ugly thing at so me point. XXX */
unsigned long sun4u_get_pte(unsigned long addr)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long mask = _PAGE_PADDR_4U;

	if (tlb_type == hypervisor)
		mask = _PAGE_PADDR_4V;

	if (addr >= PAGE_OFFSET)
		return addr & mask;

	if ((addr >= LOW_OBP_ADDRESS) && (addr < HI_OBP_ADDRESS))
		return prom_virt_to_phys(addr, NULL);

	pgdp = pgd_offset_k(addr);
	pudp = pud_offset(pgdp, addr);
	pmdp = pmd_offset(pudp, addr);
	ptep = pte_offset_kernel(pmdp, addr);

	return pte_val(*ptep) & mask;
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

#ifdef CONFIG_MEMORY_HOTPLUG

void online_page(struct page *page)
{
	ClearPageReserved(page);
	init_page_count(page);
	__free_page(page);
	totalram_pages++;
	num_physpages++;
}

int remove_memory(u64 start, u64 size)
{
	return -EINVAL;
}

#endif /* CONFIG_MEMORY_HOTPLUG */

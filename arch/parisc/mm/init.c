// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/parisc/mm/init.c
 *
 *  Copyright (C) 1995	Linus Torvalds
 *  Copyright 1999 SuSE GmbH
 *    changed by Philipp Rumpf
 *  Copyright 1999 Philipp Rumpf (prumpf@tux.org)
 *  Copyright 2004 Randolph Chung (tausq@debian.org)
 *  Copyright 2006-2007 Helge Deller (deller@gmx.de)
 *
 */


#include <linux/module.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/swap.h>
#include <linux/unistd.h>
#include <linux/nodemask.h>	/* for node_online_map */
#include <linux/pagemap.h>	/* for release_pages */
#include <linux/compat.h>

#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/pdc_chassis.h>
#include <asm/mmzone.h>
#include <asm/sections.h>
#include <asm/msgbuf.h>
#include <asm/sparsemem.h>

extern int  data_start;
extern void parisc_kernel_start(void);	/* Kernel entry point in head.S */

#if CONFIG_PGTABLE_LEVELS == 3
/* NOTE: This layout exactly conforms to the hybrid L2/L3 page table layout
 * with the first pmd adjacent to the pgd and below it. gcc doesn't actually
 * guarantee that global objects will be laid out in memory in the same order
 * as the order of declaration, so put these in different sections and use
 * the linker script to order them. */
pmd_t pmd0[PTRS_PER_PMD] __section(".data..vm0.pmd") __attribute__ ((aligned(PAGE_SIZE)));
#endif

pgd_t swapper_pg_dir[PTRS_PER_PGD] __section(".data..vm0.pgd") __attribute__ ((aligned(PAGE_SIZE)));
pte_t pg0[PT_INITIAL * PTRS_PER_PTE] __section(".data..vm0.pte") __attribute__ ((aligned(PAGE_SIZE)));

static struct resource data_resource = {
	.name	= "Kernel data",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
};

static struct resource pdcdata_resource = {
	.name	= "PDC data (Page Zero)",
	.start	= 0,
	.end	= 0x9ff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource sysram_resources[MAX_PHYSMEM_RANGES] __ro_after_init;

/* The following array is initialized from the firmware specific
 * information retrieved in kernel/inventory.c.
 */

physmem_range_t pmem_ranges[MAX_PHYSMEM_RANGES] __initdata;
int npmem_ranges __initdata;

#ifdef CONFIG_64BIT
#define MAX_MEM         (1UL << MAX_PHYSMEM_BITS)
#else /* !CONFIG_64BIT */
#define MAX_MEM         (3584U*1024U*1024U)
#endif /* !CONFIG_64BIT */

static unsigned long mem_limit __read_mostly = MAX_MEM;

static void __init mem_limit_func(void)
{
	char *cp, *end;
	unsigned long limit;

	/* We need this before __setup() functions are called */

	limit = MAX_MEM;
	for (cp = boot_command_line; *cp; ) {
		if (memcmp(cp, "mem=", 4) == 0) {
			cp += 4;
			limit = memparse(cp, &end);
			if (end != cp)
				break;
			cp = end;
		} else {
			while (*cp != ' ' && *cp)
				++cp;
			while (*cp == ' ')
				++cp;
		}
	}

	if (limit < mem_limit)
		mem_limit = limit;
}

#define MAX_GAP (0x40000000UL >> PAGE_SHIFT)

static void __init setup_bootmem(void)
{
	unsigned long mem_max;
#ifndef CONFIG_SPARSEMEM
	physmem_range_t pmem_holes[MAX_PHYSMEM_RANGES - 1];
	int npmem_holes;
#endif
	int i, sysram_resource_count;

	disable_sr_hashing(); /* Turn off space register hashing */

	/*
	 * Sort the ranges. Since the number of ranges is typically
	 * small, and performance is not an issue here, just do
	 * a simple insertion sort.
	 */

	for (i = 1; i < npmem_ranges; i++) {
		int j;

		for (j = i; j > 0; j--) {
			physmem_range_t tmp;

			if (pmem_ranges[j-1].start_pfn <
			    pmem_ranges[j].start_pfn) {

				break;
			}
			tmp = pmem_ranges[j-1];
			pmem_ranges[j-1] = pmem_ranges[j];
			pmem_ranges[j] = tmp;
		}
	}

#ifndef CONFIG_SPARSEMEM
	/*
	 * Throw out ranges that are too far apart (controlled by
	 * MAX_GAP).
	 */

	for (i = 1; i < npmem_ranges; i++) {
		if (pmem_ranges[i].start_pfn -
			(pmem_ranges[i-1].start_pfn +
			 pmem_ranges[i-1].pages) > MAX_GAP) {
			npmem_ranges = i;
			printk("Large gap in memory detected (%ld pages). "
			       "Consider turning on CONFIG_SPARSEMEM\n",
			       pmem_ranges[i].start_pfn -
			       (pmem_ranges[i-1].start_pfn +
			        pmem_ranges[i-1].pages));
			break;
		}
	}
#endif

	/* Print the memory ranges */
	pr_info("Memory Ranges:\n");

	for (i = 0; i < npmem_ranges; i++) {
		struct resource *res = &sysram_resources[i];
		unsigned long start;
		unsigned long size;

		size = (pmem_ranges[i].pages << PAGE_SHIFT);
		start = (pmem_ranges[i].start_pfn << PAGE_SHIFT);
		pr_info("%2d) Start 0x%016lx End 0x%016lx Size %6ld MB\n",
			i, start, start + (size - 1), size >> 20);

		/* request memory resource */
		res->name = "System RAM";
		res->start = start;
		res->end = start + size - 1;
		res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);
	}

	sysram_resource_count = npmem_ranges;

	/*
	 * For 32 bit kernels we limit the amount of memory we can
	 * support, in order to preserve enough kernel address space
	 * for other purposes. For 64 bit kernels we don't normally
	 * limit the memory, but this mechanism can be used to
	 * artificially limit the amount of memory (and it is written
	 * to work with multiple memory ranges).
	 */

	mem_limit_func();       /* check for "mem=" argument */

	mem_max = 0;
	for (i = 0; i < npmem_ranges; i++) {
		unsigned long rsize;

		rsize = pmem_ranges[i].pages << PAGE_SHIFT;
		if ((mem_max + rsize) > mem_limit) {
			printk(KERN_WARNING "Memory truncated to %ld MB\n", mem_limit >> 20);
			if (mem_max == mem_limit)
				npmem_ranges = i;
			else {
				pmem_ranges[i].pages =   (mem_limit >> PAGE_SHIFT)
						       - (mem_max >> PAGE_SHIFT);
				npmem_ranges = i + 1;
				mem_max = mem_limit;
			}
			break;
		}
		mem_max += rsize;
	}

	printk(KERN_INFO "Total Memory: %ld MB\n",mem_max >> 20);

#ifndef CONFIG_SPARSEMEM
	/* Merge the ranges, keeping track of the holes */
	{
		unsigned long end_pfn;
		unsigned long hole_pages;

		npmem_holes = 0;
		end_pfn = pmem_ranges[0].start_pfn + pmem_ranges[0].pages;
		for (i = 1; i < npmem_ranges; i++) {

			hole_pages = pmem_ranges[i].start_pfn - end_pfn;
			if (hole_pages) {
				pmem_holes[npmem_holes].start_pfn = end_pfn;
				pmem_holes[npmem_holes++].pages = hole_pages;
				end_pfn += hole_pages;
			}
			end_pfn += pmem_ranges[i].pages;
		}

		pmem_ranges[0].pages = end_pfn - pmem_ranges[0].start_pfn;
		npmem_ranges = 1;
	}
#endif

	/*
	 * Initialize and free the full range of memory in each range.
	 */

	max_pfn = 0;
	for (i = 0; i < npmem_ranges; i++) {
		unsigned long start_pfn;
		unsigned long npages;
		unsigned long start;
		unsigned long size;

		start_pfn = pmem_ranges[i].start_pfn;
		npages = pmem_ranges[i].pages;

		start = start_pfn << PAGE_SHIFT;
		size = npages << PAGE_SHIFT;

		/* add system RAM memblock */
		memblock_add(start, size);

		if ((start_pfn + npages) > max_pfn)
			max_pfn = start_pfn + npages;
	}

	/*
	 * We can't use memblock top-down allocations because we only
	 * created the initial mapping up to KERNEL_INITIAL_SIZE in
	 * the assembly bootup code.
	 */
	memblock_set_bottom_up(true);

	/* IOMMU is always used to access "high mem" on those boxes
	 * that can support enough mem that a PCI device couldn't
	 * directly DMA to any physical addresses.
	 * ISA DMA support will need to revisit this.
	 */
	max_low_pfn = max_pfn;

	/* reserve PAGE0 pdc memory, kernel text/data/bss & bootmap */

#define PDC_CONSOLE_IO_IODC_SIZE 32768

	memblock_reserve(0UL, (unsigned long)(PAGE0->mem_free +
				PDC_CONSOLE_IO_IODC_SIZE));
	memblock_reserve(__pa(KERNEL_BINARY_TEXT_START),
			(unsigned long)(_end - KERNEL_BINARY_TEXT_START));

#ifndef CONFIG_SPARSEMEM

	/* reserve the holes */

	for (i = 0; i < npmem_holes; i++) {
		memblock_reserve((pmem_holes[i].start_pfn << PAGE_SHIFT),
				(pmem_holes[i].pages << PAGE_SHIFT));
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		printk(KERN_INFO "initrd: %08lx-%08lx\n", initrd_start, initrd_end);
		if (__pa(initrd_start) < mem_max) {
			unsigned long initrd_reserve;

			if (__pa(initrd_end) > mem_max) {
				initrd_reserve = mem_max - __pa(initrd_start);
			} else {
				initrd_reserve = initrd_end - initrd_start;
			}
			initrd_below_start_ok = 1;
			printk(KERN_INFO "initrd: reserving %08lx-%08lx (mem_max %08lx)\n", __pa(initrd_start), __pa(initrd_start) + initrd_reserve, mem_max);

			memblock_reserve(__pa(initrd_start), initrd_reserve);
		}
	}
#endif

	data_resource.start =  virt_to_phys(&data_start);
	data_resource.end = virt_to_phys(_end) - 1;
	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(&data_start)-1;

	/* We don't know which region the kernel will be in, so try
	 * all of them.
	 */
	for (i = 0; i < sysram_resource_count; i++) {
		struct resource *res = &sysram_resources[i];
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
	}
	request_resource(&sysram_resources[0], &pdcdata_resource);

	/* Initialize Page Deallocation Table (PDT) and check for bad memory. */
	pdc_pdt_init();

	memblock_allow_resize();
	memblock_dump_all();
}

static bool kernel_set_to_readonly;

static void __init map_pages(unsigned long start_vaddr,
			     unsigned long start_paddr, unsigned long size,
			     pgprot_t pgprot, int force)
{
	pmd_t *pmd;
	pte_t *pg_table;
	unsigned long end_paddr;
	unsigned long start_pmd;
	unsigned long start_pte;
	unsigned long tmp1;
	unsigned long tmp2;
	unsigned long address;
	unsigned long vaddr;
	unsigned long ro_start;
	unsigned long ro_end;
	unsigned long kernel_start, kernel_end;

	ro_start = __pa((unsigned long)_text);
	ro_end   = __pa((unsigned long)&data_start);
	kernel_start = __pa((unsigned long)&__init_begin);
	kernel_end  = __pa((unsigned long)&_end);

	end_paddr = start_paddr + size;

	/* for 2-level configuration PTRS_PER_PMD is 0 so start_pmd will be 0 */
	start_pmd = ((start_vaddr >> PMD_SHIFT) & (PTRS_PER_PMD - 1));
	start_pte = ((start_vaddr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));

	address = start_paddr;
	vaddr = start_vaddr;
	while (address < end_paddr) {
		pgd_t *pgd = pgd_offset_k(vaddr);
		p4d_t *p4d = p4d_offset(pgd, vaddr);
		pud_t *pud = pud_offset(p4d, vaddr);

#if CONFIG_PGTABLE_LEVELS == 3
		if (pud_none(*pud)) {
			pmd = memblock_alloc(PAGE_SIZE << PMD_ORDER,
					     PAGE_SIZE << PMD_ORDER);
			if (!pmd)
				panic("pmd allocation failed.\n");
			pud_populate(NULL, pud, pmd);
		}
#endif

		pmd = pmd_offset(pud, vaddr);
		for (tmp1 = start_pmd; tmp1 < PTRS_PER_PMD; tmp1++, pmd++) {
			if (pmd_none(*pmd)) {
				pg_table = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
				if (!pg_table)
					panic("page table allocation failed\n");
				pmd_populate_kernel(NULL, pmd, pg_table);
			}

			pg_table = pte_offset_kernel(pmd, vaddr);
			for (tmp2 = start_pte; tmp2 < PTRS_PER_PTE; tmp2++, pg_table++) {
				pte_t pte;
				pgprot_t prot;
				bool huge = false;

				if (force) {
					prot = pgprot;
				} else if (address < kernel_start || address >= kernel_end) {
					/* outside kernel memory */
					prot = PAGE_KERNEL;
				} else if (!kernel_set_to_readonly) {
					/* still initializing, allow writing to RO memory */
					prot = PAGE_KERNEL_RWX;
					huge = true;
				} else if (address >= ro_start) {
					/* Code (ro) and Data areas */
					prot = (address < ro_end) ?
						PAGE_KERNEL_EXEC : PAGE_KERNEL;
					huge = true;
				} else {
					prot = PAGE_KERNEL;
				}

				pte = __mk_pte(address, prot);
				if (huge)
					pte = pte_mkhuge(pte);

				if (address >= end_paddr)
					break;

				set_pte(pg_table, pte);

				address += PAGE_SIZE;
				vaddr += PAGE_SIZE;
			}
			start_pte = 0;

			if (address >= end_paddr)
			    break;
		}
		start_pmd = 0;
	}
}

void __init set_kernel_text_rw(int enable_read_write)
{
	unsigned long start = (unsigned long) __init_begin;
	unsigned long end   = (unsigned long) &data_start;

	map_pages(start, __pa(start), end-start,
		PAGE_KERNEL_RWX, enable_read_write ? 1:0);

	/* force the kernel to see the new page table entries */
	flush_cache_all();
	flush_tlb_all();
}

void __ref free_initmem(void)
{
	unsigned long init_begin = (unsigned long)__init_begin;
	unsigned long init_end = (unsigned long)__init_end;
	unsigned long kernel_end  = (unsigned long)&_end;

	/* Remap kernel text and data, but do not touch init section yet. */
	kernel_set_to_readonly = true;
	map_pages(init_end, __pa(init_end), kernel_end - init_end,
		  PAGE_KERNEL, 0);

	/* The init text pages are marked R-X.  We have to
	 * flush the icache and mark them RW-
	 *
	 * This is tricky, because map_pages is in the init section.
	 * Do a dummy remap of the data section first (the data
	 * section is already PAGE_KERNEL) to pull in the TLB entries
	 * for map_kernel */
	map_pages(init_begin, __pa(init_begin), init_end - init_begin,
		  PAGE_KERNEL_RWX, 1);
	/* now remap at PAGE_KERNEL since the TLB is pre-primed to execute
	 * map_pages */
	map_pages(init_begin, __pa(init_begin), init_end - init_begin,
		  PAGE_KERNEL, 1);

	/* force the kernel to see the new TLB entries */
	__flush_tlb_range(0, init_begin, kernel_end);

	/* finally dump all the instructions which were cached, since the
	 * pages are no-longer executable */
	flush_icache_range(init_begin, init_end);
	
	free_initmem_default(POISON_FREE_INITMEM);

	/* set up a new led state on systems shipped LED State panel */
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_BCOMPLETE);
}


#ifdef CONFIG_STRICT_KERNEL_RWX
void mark_rodata_ro(void)
{
	/* rodata memory was already mapped with KERNEL_RO access rights by
           pagetable_init() and map_pages(). No need to do additional stuff here */
	unsigned long roai_size = __end_ro_after_init - __start_ro_after_init;

	pr_info("Write protected read-only-after-init data: %luk\n", roai_size >> 10);
}
#endif


/*
 * Just an arbitrary offset to serve as a "hole" between mapping areas
 * (between top of physical memory and a potential pcxl dma mapping
 * area, and below the vmalloc mapping area).
 *
 * The current 32K value just means that there will be a 32K "hole"
 * between mapping areas. That means that  any out-of-bounds memory
 * accesses will hopefully be caught. The vmalloc() routines leaves
 * a hole of 4kB between each vmalloced area for the same reason.
 */

 /* Leave room for gateway page expansion */
#if KERNEL_MAP_START < GATEWAY_PAGE_SIZE
#error KERNEL_MAP_START is in gateway reserved region
#endif
#define MAP_START (KERNEL_MAP_START)

#define VM_MAP_OFFSET  (32*1024)
#define SET_MAP_OFFSET(x) ((void *)(((unsigned long)(x) + VM_MAP_OFFSET) \
				     & ~(VM_MAP_OFFSET-1)))

void *parisc_vmalloc_start __ro_after_init;
EXPORT_SYMBOL(parisc_vmalloc_start);

#ifdef CONFIG_PA11
unsigned long pcxl_dma_start __ro_after_init;
#endif

void __init mem_init(void)
{
	/* Do sanity checks on IPC (compat) structures */
	BUILD_BUG_ON(sizeof(struct ipc64_perm) != 48);
#ifndef CONFIG_64BIT
	BUILD_BUG_ON(sizeof(struct semid64_ds) != 80);
	BUILD_BUG_ON(sizeof(struct msqid64_ds) != 104);
	BUILD_BUG_ON(sizeof(struct shmid64_ds) != 104);
#endif
#ifdef CONFIG_COMPAT
	BUILD_BUG_ON(sizeof(struct compat_ipc64_perm) != sizeof(struct ipc64_perm));
	BUILD_BUG_ON(sizeof(struct compat_semid64_ds) != 80);
	BUILD_BUG_ON(sizeof(struct compat_msqid64_ds) != 104);
	BUILD_BUG_ON(sizeof(struct compat_shmid64_ds) != 104);
#endif

	/* Do sanity checks on page table constants */
	BUILD_BUG_ON(PTE_ENTRY_SIZE != sizeof(pte_t));
	BUILD_BUG_ON(PMD_ENTRY_SIZE != sizeof(pmd_t));
	BUILD_BUG_ON(PGD_ENTRY_SIZE != sizeof(pgd_t));
	BUILD_BUG_ON(PAGE_SHIFT + BITS_PER_PTE + BITS_PER_PMD + BITS_PER_PGD
			> BITS_PER_LONG);

	high_memory = __va((max_pfn << PAGE_SHIFT));
	set_max_mapnr(max_low_pfn);
	memblock_free_all();

#ifdef CONFIG_PA11
	if (boot_cpu_data.cpu_type == pcxl2 || boot_cpu_data.cpu_type == pcxl) {
		pcxl_dma_start = (unsigned long)SET_MAP_OFFSET(MAP_START);
		parisc_vmalloc_start = SET_MAP_OFFSET(pcxl_dma_start
						+ PCXL_DMA_MAP_SIZE);
	} else
#endif
		parisc_vmalloc_start = SET_MAP_OFFSET(MAP_START);

	mem_init_print_info(NULL);

#if 0
	/*
	 * Do not expose the virtual kernel memory layout to userspace.
	 * But keep code for debugging purposes.
	 */
	printk("virtual kernel memory layout:\n"
	       "     vmalloc : 0x%px - 0x%px   (%4ld MB)\n"
	       "     fixmap  : 0x%px - 0x%px   (%4ld kB)\n"
	       "     memory  : 0x%px - 0x%px   (%4ld MB)\n"
	       "       .init : 0x%px - 0x%px   (%4ld kB)\n"
	       "       .data : 0x%px - 0x%px   (%4ld kB)\n"
	       "       .text : 0x%px - 0x%px   (%4ld kB)\n",

	       (void*)VMALLOC_START, (void*)VMALLOC_END,
	       (VMALLOC_END - VMALLOC_START) >> 20,

	       (void *)FIXMAP_START, (void *)(FIXMAP_START + FIXMAP_SIZE),
	       (unsigned long)(FIXMAP_SIZE / 1024),

	       __va(0), high_memory,
	       ((unsigned long)high_memory - (unsigned long)__va(0)) >> 20,

	       __init_begin, __init_end,
	       ((unsigned long)__init_end - (unsigned long)__init_begin) >> 10,

	       _etext, _edata,
	       ((unsigned long)_edata - (unsigned long)_etext) >> 10,

	       _text, _etext,
	       ((unsigned long)_etext - (unsigned long)_text) >> 10);
#endif
}

unsigned long *empty_zero_page __ro_after_init;
EXPORT_SYMBOL(empty_zero_page);

/*
 * pagetable_init() sets up the page tables
 *
 * Note that gateway_init() places the Linux gateway page at page 0.
 * Since gateway pages cannot be dereferenced this has the desirable
 * side effect of trapping those pesky NULL-reference errors in the
 * kernel.
 */
static void __init pagetable_init(void)
{
	int range;

	/* Map each physical memory range to its kernel vaddr */

	for (range = 0; range < npmem_ranges; range++) {
		unsigned long start_paddr;
		unsigned long end_paddr;
		unsigned long size;

		start_paddr = pmem_ranges[range].start_pfn << PAGE_SHIFT;
		size = pmem_ranges[range].pages << PAGE_SHIFT;
		end_paddr = start_paddr + size;

		map_pages((unsigned long)__va(start_paddr), start_paddr,
			  size, PAGE_KERNEL, 0);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_end && initrd_end > mem_limit) {
		printk(KERN_INFO "initrd: mapping %08lx-%08lx\n", initrd_start, initrd_end);
		map_pages(initrd_start, __pa(initrd_start),
			  initrd_end - initrd_start, PAGE_KERNEL, 0);
	}
#endif

	empty_zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!empty_zero_page)
		panic("zero page allocation failed.\n");

}

static void __init gateway_init(void)
{
	unsigned long linux_gateway_page_addr;
	/* FIXME: This is 'const' in order to trick the compiler
	   into not treating it as DP-relative data. */
	extern void * const linux_gateway_page;

	linux_gateway_page_addr = LINUX_GATEWAY_ADDR & PAGE_MASK;

	/*
	 * Setup Linux Gateway page.
	 *
	 * The Linux gateway page will reside in kernel space (on virtual
	 * page 0), so it doesn't need to be aliased into user space.
	 */

	map_pages(linux_gateway_page_addr, __pa(&linux_gateway_page),
		  PAGE_SIZE, PAGE_GATEWAY, 1);
}

static void __init parisc_bootmem_free(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0, };

	max_zone_pfn[0] = memblock_end_of_DRAM();

	free_area_init(max_zone_pfn);
}

void __init paging_init(void)
{
	setup_bootmem();
	pagetable_init();
	gateway_init();
	flush_cache_all_local(); /* start with known state */
	flush_tlb_all_local(NULL);

	sparse_init();
	parisc_bootmem_free();
}

#ifdef CONFIG_PA20

/*
 * Currently, all PA20 chips have 18 bit protection IDs, which is the
 * limiting factor (space ids are 32 bits).
 */

#define NR_SPACE_IDS 262144

#else

/*
 * Currently we have a one-to-one relationship between space IDs and
 * protection IDs. Older parisc chips (PCXS, PCXT, PCXL, PCXL2) only
 * support 15 bit protection IDs, so that is the limiting factor.
 * PCXT' has 18 bit protection IDs, but only 16 bit spaceids, so it's
 * probably not worth the effort for a special case here.
 */

#define NR_SPACE_IDS 32768

#endif  /* !CONFIG_PA20 */

#define RECYCLE_THRESHOLD (NR_SPACE_IDS / 2)
#define SID_ARRAY_SIZE  (NR_SPACE_IDS / (8 * sizeof(long)))

static unsigned long space_id[SID_ARRAY_SIZE] = { 1 }; /* disallow space 0 */
static unsigned long dirty_space_id[SID_ARRAY_SIZE];
static unsigned long space_id_index;
static unsigned long free_space_ids = NR_SPACE_IDS - 1;
static unsigned long dirty_space_ids = 0;

static DEFINE_SPINLOCK(sid_lock);

unsigned long alloc_sid(void)
{
	unsigned long index;

	spin_lock(&sid_lock);

	if (free_space_ids == 0) {
		if (dirty_space_ids != 0) {
			spin_unlock(&sid_lock);
			flush_tlb_all(); /* flush_tlb_all() calls recycle_sids() */
			spin_lock(&sid_lock);
		}
		BUG_ON(free_space_ids == 0);
	}

	free_space_ids--;

	index = find_next_zero_bit(space_id, NR_SPACE_IDS, space_id_index);
	space_id[BIT_WORD(index)] |= BIT_MASK(index);
	space_id_index = index;

	spin_unlock(&sid_lock);

	return index << SPACEID_SHIFT;
}

void free_sid(unsigned long spaceid)
{
	unsigned long index = spaceid >> SPACEID_SHIFT;
	unsigned long *dirty_space_offset, mask;

	dirty_space_offset = &dirty_space_id[BIT_WORD(index)];
	mask = BIT_MASK(index);

	spin_lock(&sid_lock);

	BUG_ON(*dirty_space_offset & mask); /* attempt to free space id twice */

	*dirty_space_offset |= mask;
	dirty_space_ids++;

	spin_unlock(&sid_lock);
}


#ifdef CONFIG_SMP
static void get_dirty_sids(unsigned long *ndirtyptr,unsigned long *dirty_array)
{
	int i;

	/* NOTE: sid_lock must be held upon entry */

	*ndirtyptr = dirty_space_ids;
	if (dirty_space_ids != 0) {
	    for (i = 0; i < SID_ARRAY_SIZE; i++) {
		dirty_array[i] = dirty_space_id[i];
		dirty_space_id[i] = 0;
	    }
	    dirty_space_ids = 0;
	}

	return;
}

static void recycle_sids(unsigned long ndirty,unsigned long *dirty_array)
{
	int i;

	/* NOTE: sid_lock must be held upon entry */

	if (ndirty != 0) {
		for (i = 0; i < SID_ARRAY_SIZE; i++) {
			space_id[i] ^= dirty_array[i];
		}

		free_space_ids += ndirty;
		space_id_index = 0;
	}
}

#else /* CONFIG_SMP */

static void recycle_sids(void)
{
	int i;

	/* NOTE: sid_lock must be held upon entry */

	if (dirty_space_ids != 0) {
		for (i = 0; i < SID_ARRAY_SIZE; i++) {
			space_id[i] ^= dirty_space_id[i];
			dirty_space_id[i] = 0;
		}

		free_space_ids += dirty_space_ids;
		dirty_space_ids = 0;
		space_id_index = 0;
	}
}
#endif

/*
 * flush_tlb_all() calls recycle_sids(), since whenever the entire tlb is
 * purged, we can safely reuse the space ids that were released but
 * not flushed from the tlb.
 */

#ifdef CONFIG_SMP

static unsigned long recycle_ndirty;
static unsigned long recycle_dirty_array[SID_ARRAY_SIZE];
static unsigned int recycle_inuse;

void flush_tlb_all(void)
{
	int do_recycle;

	__inc_irq_stat(irq_tlb_count);
	do_recycle = 0;
	spin_lock(&sid_lock);
	if (dirty_space_ids > RECYCLE_THRESHOLD) {
	    BUG_ON(recycle_inuse);  /* FIXME: Use a semaphore/wait queue here */
	    get_dirty_sids(&recycle_ndirty,recycle_dirty_array);
	    recycle_inuse++;
	    do_recycle++;
	}
	spin_unlock(&sid_lock);
	on_each_cpu(flush_tlb_all_local, NULL, 1);
	if (do_recycle) {
	    spin_lock(&sid_lock);
	    recycle_sids(recycle_ndirty,recycle_dirty_array);
	    recycle_inuse = 0;
	    spin_unlock(&sid_lock);
	}
}
#else
void flush_tlb_all(void)
{
	__inc_irq_stat(irq_tlb_count);
	spin_lock(&sid_lock);
	flush_tlb_all_local(NULL);
	recycle_sids();
	spin_unlock(&sid_lock);
}
#endif

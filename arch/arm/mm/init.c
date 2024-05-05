// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2005 Russell King
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/dma-map-ops.h>
#include <linux/sizes.h>
#include <linux/stop_machine.h>
#include <linux/swiotlb.h>
#include <linux/execmem.h>

#include <asm/cp15.h>
#include <asm/mach-types.h>
#include <asm/memblock.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/set_memory.h>
#include <asm/system_info.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
#include <asm/ptdump.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"

#ifdef CONFIG_CPU_CP15_MMU
unsigned long __init __clear_cr(unsigned long mask)
{
	cr_alignment = cr_alignment & ~mask;
	return cr_alignment;
}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
static int __init parse_tag_initrd(const struct tag *tag)
{
	pr_warn("ATAG_INITRD is deprecated; "
		"please update your bootloader.\n");
	phys_initrd_start = __virt_to_phys(tag->u.initrd.start);
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD, parse_tag_initrd);

static int __init parse_tag_initrd2(const struct tag *tag)
{
	phys_initrd_start = tag->u.initrd.start;
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD2, parse_tag_initrd2);
#endif

static void __init find_limits(unsigned long *min, unsigned long *max_low,
			       unsigned long *max_high)
{
	*max_low = PFN_DOWN(memblock_get_current_limit());
	*min = PFN_UP(memblock_start_of_DRAM());
	*max_high = PFN_DOWN(memblock_end_of_DRAM());
}

#ifdef CONFIG_ZONE_DMA

phys_addr_t arm_dma_zone_size __read_mostly;
EXPORT_SYMBOL(arm_dma_zone_size);

/*
 * The DMA mask corresponding to the maximum bus address allocatable
 * using GFP_DMA.  The default here places no restriction on DMA
 * allocations.  This must be the smallest DMA mask in the system,
 * so a successful GFP_DMA allocation will always satisfy this.
 */
phys_addr_t arm_dma_limit;
unsigned long arm_dma_pfn_limit;
#endif

void __init setup_dma_zone(const struct machine_desc *mdesc)
{
#ifdef CONFIG_ZONE_DMA
	if (mdesc->dma_zone_size) {
		arm_dma_zone_size = mdesc->dma_zone_size;
		arm_dma_limit = PHYS_OFFSET + arm_dma_zone_size - 1;
	} else
		arm_dma_limit = 0xffffffff;
	arm_dma_pfn_limit = arm_dma_limit >> PAGE_SHIFT;
#endif
}

static void __init zone_sizes_init(unsigned long min, unsigned long max_low,
	unsigned long max_high)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

#ifdef CONFIG_ZONE_DMA
	max_zone_pfn[ZONE_DMA] = min(arm_dma_pfn_limit, max_low);
#endif
	max_zone_pfn[ZONE_NORMAL] = max_low;
#ifdef CONFIG_HIGHMEM
	max_zone_pfn[ZONE_HIGHMEM] = max_high;
#endif
	free_area_init(max_zone_pfn);
}

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
int pfn_valid(unsigned long pfn)
{
	phys_addr_t addr = __pfn_to_phys(pfn);
	unsigned long pageblock_size = PAGE_SIZE * pageblock_nr_pages;

	if (__phys_to_pfn(addr) != pfn)
		return 0;

	/*
	 * If address less than pageblock_size bytes away from a present
	 * memory chunk there still will be a memory map entry for it
	 * because we round freed memory map to the pageblock boundaries.
	 */
	if (memblock_overlaps_region(&memblock.memory,
				     ALIGN_DOWN(addr, pageblock_size),
				     pageblock_size))
		return 1;

	return 0;
}
EXPORT_SYMBOL(pfn_valid);
#endif

static bool arm_memblock_steal_permitted = true;

phys_addr_t __init arm_memblock_steal(phys_addr_t size, phys_addr_t align)
{
	phys_addr_t phys;

	BUG_ON(!arm_memblock_steal_permitted);

	phys = memblock_phys_alloc(size, align);
	if (!phys)
		panic("Failed to steal %pa bytes at %pS\n",
		      &size, (void *)_RET_IP_);

	memblock_phys_free(phys, size);
	memblock_remove(phys, size);

	return phys;
}

#ifdef CONFIG_CPU_ICACHE_MISMATCH_WORKAROUND
void check_cpu_icache_size(int cpuid)
{
	u32 size, ctr;

	asm("mrc p15, 0, %0, c0, c0, 1" : "=r" (ctr));

	size = 1 << ((ctr & 0xf) + 2);
	if (cpuid != 0 && icache_size != size)
		pr_info("CPU%u: detected I-Cache line size mismatch, workaround enabled\n",
			cpuid);
	if (icache_size > size)
		icache_size = size;
}
#endif

void __init arm_memblock_init(const struct machine_desc *mdesc)
{
	/* Register the kernel text, kernel data and initrd with memblock. */
	memblock_reserve(__pa(KERNEL_START), KERNEL_END - KERNEL_START);

	reserve_initrd_mem();

	arm_mm_memblock_reserve();

	/* reserve any platform specific memblock areas */
	if (mdesc->reserve)
		mdesc->reserve();

	early_init_fdt_scan_reserved_mem();

	/* reserve memory for DMA contiguous allocations */
	dma_contiguous_reserve(arm_dma_limit);

	arm_memblock_steal_permitted = false;
	memblock_dump_all();
}

void __init bootmem_init(void)
{
	memblock_allow_resize();

	find_limits(&min_low_pfn, &max_low_pfn, &max_pfn);

	early_memtest((phys_addr_t)min_low_pfn << PAGE_SHIFT,
		      (phys_addr_t)max_low_pfn << PAGE_SHIFT);

	/*
	 * sparse_init() tries to allocate memory from memblock, so must be
	 * done after the fixed reservations
	 */
	sparse_init();

	/*
	 * Now free the memory - free_area_init needs
	 * the sparse mem_map arrays initialized by sparse_init()
	 * for memmap_init_zone(), otherwise all PFNs are invalid.
	 */
	zone_sizes_init(min_low_pfn, max_low_pfn, max_pfn);
}

/*
 * Poison init memory with an undefined instruction (ARM) or a branch to an
 * undefined instruction (Thumb).
 */
static inline void poison_init_mem(void *s, size_t count)
{
	u32 *p = (u32 *)s;
	for (; count != 0; count -= 4)
		*p++ = 0xe7fddef0;
}

static void __init free_highpages(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long max_low = max_low_pfn;
	phys_addr_t range_start, range_end;
	u64 i;

	/* set highmem page free */
	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE,
				&range_start, &range_end, NULL) {
		unsigned long start = PFN_UP(range_start);
		unsigned long end = PFN_DOWN(range_end);

		/* Ignore complete lowmem entries */
		if (end <= max_low)
			continue;

		/* Truncate partial highmem entries */
		if (start < max_low)
			start = max_low;

		for (; start < end; start++)
			free_highmem_page(pfn_to_page(start));
	}
#endif
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
#ifdef CONFIG_ARM_LPAE
	swiotlb_init(max_pfn > arm_dma_pfn_limit, SWIOTLB_VERBOSE);
#endif

	set_max_mapnr(pfn_to_page(max_pfn) - mem_map);

	/* this will put all unused low memory onto the freelists */
	memblock_free_all();

#ifdef CONFIG_SA1111
	/* now that our DMA memory is actually so designated, we can free it */
	free_reserved_area(__va(PHYS_OFFSET), swapper_pg_dir, -1, NULL);
#endif

	free_highpages();

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#ifdef CONFIG_MMU
	BUILD_BUG_ON(TASK_SIZE				> MODULES_VADDR);
	BUG_ON(TASK_SIZE 				> MODULES_VADDR);
#endif

#ifdef CONFIG_HIGHMEM
	BUILD_BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE > PAGE_OFFSET);
	BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE	> PAGE_OFFSET);
#endif
}

#ifdef CONFIG_STRICT_KERNEL_RWX
struct section_perm {
	const char *name;
	unsigned long start;
	unsigned long end;
	pmdval_t mask;
	pmdval_t prot;
	pmdval_t clear;
};

/* First section-aligned location at or after __start_rodata. */
extern char __start_rodata_section_aligned[];

static struct section_perm nx_perms[] = {
	/* Make pages tables, etc before _stext RW (set NX). */
	{
		.name	= "pre-text NX",
		.start	= PAGE_OFFSET,
		.end	= (unsigned long)_stext,
		.mask	= ~PMD_SECT_XN,
		.prot	= PMD_SECT_XN,
	},
	/* Make init RW (set NX). */
	{
		.name	= "init NX",
		.start	= (unsigned long)__init_begin,
		.end	= (unsigned long)_sdata,
		.mask	= ~PMD_SECT_XN,
		.prot	= PMD_SECT_XN,
	},
	/* Make rodata NX (set RO in ro_perms below). */
	{
		.name	= "rodata NX",
		.start  = (unsigned long)__start_rodata_section_aligned,
		.end    = (unsigned long)__init_begin,
		.mask   = ~PMD_SECT_XN,
		.prot   = PMD_SECT_XN,
	},
};

static struct section_perm ro_perms[] = {
	/* Make kernel code and rodata RX (set RO). */
	{
		.name	= "text/rodata RO",
		.start  = (unsigned long)_stext,
		.end    = (unsigned long)__init_begin,
#ifdef CONFIG_ARM_LPAE
		.mask   = ~(L_PMD_SECT_RDONLY | PMD_SECT_AP2),
		.prot   = L_PMD_SECT_RDONLY | PMD_SECT_AP2,
#else
		.mask   = ~(PMD_SECT_APX | PMD_SECT_AP_WRITE),
		.prot   = PMD_SECT_APX | PMD_SECT_AP_WRITE,
		.clear  = PMD_SECT_AP_WRITE,
#endif
	},
};

/*
 * Updates section permissions only for the current mm (sections are
 * copied into each mm). During startup, this is the init_mm. Is only
 * safe to be called with preemption disabled, as under stop_machine().
 */
static inline void section_update(unsigned long addr, pmdval_t mask,
				  pmdval_t prot, struct mm_struct *mm)
{
	pmd_t *pmd;

	pmd = pmd_offset(pud_offset(p4d_offset(pgd_offset(mm, addr), addr), addr), addr);

#ifdef CONFIG_ARM_LPAE
	pmd[0] = __pmd((pmd_val(pmd[0]) & mask) | prot);
#else
	if (addr & SECTION_SIZE)
		pmd[1] = __pmd((pmd_val(pmd[1]) & mask) | prot);
	else
		pmd[0] = __pmd((pmd_val(pmd[0]) & mask) | prot);
#endif
	flush_pmd_entry(pmd);
	local_flush_tlb_kernel_range(addr, addr + SECTION_SIZE);
}

/* Make sure extended page tables are in use. */
static inline bool arch_has_strict_perms(void)
{
	if (cpu_architecture() < CPU_ARCH_ARMv6)
		return false;

	return !!(get_cr() & CR_XP);
}

static void set_section_perms(struct section_perm *perms, int n, bool set,
			      struct mm_struct *mm)
{
	size_t i;
	unsigned long addr;

	if (!arch_has_strict_perms())
		return;

	for (i = 0; i < n; i++) {
		if (!IS_ALIGNED(perms[i].start, SECTION_SIZE) ||
		    !IS_ALIGNED(perms[i].end, SECTION_SIZE)) {
			pr_err("BUG: %s section %lx-%lx not aligned to %lx\n",
				perms[i].name, perms[i].start, perms[i].end,
				SECTION_SIZE);
			continue;
		}

		for (addr = perms[i].start;
		     addr < perms[i].end;
		     addr += SECTION_SIZE)
			section_update(addr, perms[i].mask,
				set ? perms[i].prot : perms[i].clear, mm);
	}

}

/*
 * update_sections_early intended to be called only through stop_machine
 * framework and executed by only one CPU while all other CPUs will spin and
 * wait, so no locking is required in this function.
 */
static void update_sections_early(struct section_perm perms[], int n)
{
	struct task_struct *t, *s;

	for_each_process(t) {
		if (t->flags & PF_KTHREAD)
			continue;
		for_each_thread(t, s)
			if (s->mm)
				set_section_perms(perms, n, true, s->mm);
	}
	set_section_perms(perms, n, true, current->active_mm);
	set_section_perms(perms, n, true, &init_mm);
}

static int __fix_kernmem_perms(void *unused)
{
	update_sections_early(nx_perms, ARRAY_SIZE(nx_perms));
	return 0;
}

static void fix_kernmem_perms(void)
{
	stop_machine(__fix_kernmem_perms, NULL, NULL);
}

static int __mark_rodata_ro(void *unused)
{
	update_sections_early(ro_perms, ARRAY_SIZE(ro_perms));
	return 0;
}

void mark_rodata_ro(void)
{
	stop_machine(__mark_rodata_ro, NULL, NULL);
	arm_debug_checkwx();
}

#else
static inline void fix_kernmem_perms(void) { }
#endif /* CONFIG_STRICT_KERNEL_RWX */

void free_initmem(void)
{
	fix_kernmem_perms();

	poison_init_mem(__init_begin, __init_end - __init_begin);
	if (!machine_is_integrator() && !machine_is_cintegrator())
		free_initmem_default(-1);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start == initrd_start)
		start = round_down(start, PAGE_SIZE);
	if (end == initrd_end)
		end = round_up(end, PAGE_SIZE);

	poison_init_mem((void *)start, PAGE_ALIGN(end) - start);
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

#ifdef CONFIG_EXECMEM

#ifdef CONFIG_XIP_KERNEL
/*
 * The XIP kernel text is mapped in the module area for modules and
 * some other stuff to work without any indirect relocations.
 * MODULES_VADDR is redefined here and not in asm/memory.h to avoid
 * recompiling the whole kernel when CONFIG_XIP_KERNEL is turned on/off.
 */
#undef MODULES_VADDR
#define MODULES_VADDR	(((unsigned long)_exiprom + ~PMD_MASK) & PMD_MASK)
#endif

#ifdef CONFIG_MMU
static struct execmem_info execmem_info __ro_after_init;

struct execmem_info __init *execmem_arch_setup(void)
{
	unsigned long fallback_start = 0, fallback_end = 0;

	if (IS_ENABLED(CONFIG_ARM_MODULE_PLTS)) {
		fallback_start = VMALLOC_START;
		fallback_end = VMALLOC_END;
	}

	execmem_info = (struct execmem_info){
		.ranges = {
			[EXECMEM_DEFAULT] = {
				.start	= MODULES_VADDR,
				.end	= MODULES_END,
				.pgprot	= PAGE_KERNEL_EXEC,
				.alignment = 1,
				.fallback_start	= fallback_start,
				.fallback_end	= fallback_end,
			},
		},
	};

	return &execmem_info;
}
#endif /* CONFIG_MMU */

#endif /* CONFIG_EXECMEM */

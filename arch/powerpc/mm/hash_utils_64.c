/*
 * PowerPC64 port by Mike Corrigan and Dave Engebretsen
 *   {mikejc|engebret}@us.ibm.com
 *
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * 
 *    Module name: htab.c
 *
 *    Description:
 *      PowerPC Hashed Page Table functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG
#undef DEBUG_LOW

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/abs_addr.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/eeh.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/spu.h>
#include <asm/udbg.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

#ifdef DEBUG_LOW
#define DBG_LOW(fmt...) udbg_printf(fmt)
#else
#define DBG_LOW(fmt...)
#endif

#define KB (1024)
#define MB (1024*KB)
#define GB (1024L*MB)

/*
 * Note:  pte   --> Linux PTE
 *        HPTE  --> PowerPC Hashed Page Table Entry
 *
 * Execution context:
 *   htab_initialize is called with the MMU off (of course), but
 *   the kernel has been copied down to zero so it can directly
 *   reference global data.  At this point it is very difficult
 *   to print debug info.
 *
 */

#ifdef CONFIG_U3_DART
extern unsigned long dart_tablebase;
#endif /* CONFIG_U3_DART */

static unsigned long _SDR1;
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];

struct hash_pte *htab_address;
unsigned long htab_size_bytes;
unsigned long htab_hash_mask;
EXPORT_SYMBOL_GPL(htab_hash_mask);
int mmu_linear_psize = MMU_PAGE_4K;
int mmu_virtual_psize = MMU_PAGE_4K;
int mmu_vmalloc_psize = MMU_PAGE_4K;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
int mmu_vmemmap_psize = MMU_PAGE_4K;
#endif
int mmu_io_psize = MMU_PAGE_4K;
int mmu_kernel_ssize = MMU_SEGSIZE_256M;
int mmu_highuser_ssize = MMU_SEGSIZE_256M;
u16 mmu_slb_size = 64;
EXPORT_SYMBOL_GPL(mmu_slb_size);
#ifdef CONFIG_HUGETLB_PAGE
unsigned int HPAGE_SHIFT;
#endif
#ifdef CONFIG_PPC_64K_PAGES
int mmu_ci_restrictions;
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
static u8 *linear_map_hash_slots;
static unsigned long linear_map_hash_count;
static DEFINE_SPINLOCK(linear_map_hash_lock);
#endif /* CONFIG_DEBUG_PAGEALLOC */

/* There are definitions of page sizes arrays to be used when none
 * is provided by the firmware.
 */

/* Pre-POWER4 CPUs (4k pages only)
 */
static struct mmu_psize_def mmu_psize_defaults_old[] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.sllp	= 0,
		.penc	= 0,
		.avpnm	= 0,
		.tlbiel = 0,
	},
};

/* POWER4, GPUL, POWER5
 *
 * Support for 16Mb large pages
 */
static struct mmu_psize_def mmu_psize_defaults_gp[] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.sllp	= 0,
		.penc	= 0,
		.avpnm	= 0,
		.tlbiel = 1,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
		.sllp	= SLB_VSID_L,
		.penc	= 0,
		.avpnm	= 0x1UL,
		.tlbiel = 0,
	},
};

static unsigned long htab_convert_pte_flags(unsigned long pteflags)
{
	unsigned long rflags = pteflags & 0x1fa;

	/* _PAGE_EXEC -> NOEXEC */
	if ((pteflags & _PAGE_EXEC) == 0)
		rflags |= HPTE_R_N;

	/* PP bits. PAGE_USER is already PP bit 0x2, so we only
	 * need to add in 0x1 if it's a read-only user page
	 */
	if ((pteflags & _PAGE_USER) && !((pteflags & _PAGE_RW) &&
					 (pteflags & _PAGE_DIRTY)))
		rflags |= 1;

	/* Always add C */
	return rflags | HPTE_R_C;
}

int htab_bolt_mapping(unsigned long vstart, unsigned long vend,
		      unsigned long pstart, unsigned long prot,
		      int psize, int ssize)
{
	unsigned long vaddr, paddr;
	unsigned int step, shift;
	int ret = 0;

	shift = mmu_psize_defs[psize].shift;
	step = 1 << shift;

	prot = htab_convert_pte_flags(prot);

	DBG("htab_bolt_mapping(%lx..%lx -> %lx (%lx,%d,%d)\n",
	    vstart, vend, pstart, prot, psize, ssize);

	for (vaddr = vstart, paddr = pstart; vaddr < vend;
	     vaddr += step, paddr += step) {
		unsigned long hash, hpteg;
		unsigned long vsid = get_kernel_vsid(vaddr, ssize);
		unsigned long va = hpt_va(vaddr, vsid, ssize);
		unsigned long tprot = prot;

		/* Make kernel text executable */
		if (overlaps_kernel_text(vaddr, vaddr + step))
			tprot &= ~HPTE_R_N;

		hash = hpt_hash(va, shift, ssize);
		hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

		BUG_ON(!ppc_md.hpte_insert);
		ret = ppc_md.hpte_insert(hpteg, va, paddr, tprot,
					 HPTE_V_BOLTED, psize, ssize);

		if (ret < 0)
			break;
#ifdef CONFIG_DEBUG_PAGEALLOC
		if ((paddr >> PAGE_SHIFT) < linear_map_hash_count)
			linear_map_hash_slots[paddr >> PAGE_SHIFT] = ret | 0x80;
#endif /* CONFIG_DEBUG_PAGEALLOC */
	}
	return ret < 0 ? ret : 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static int htab_remove_mapping(unsigned long vstart, unsigned long vend,
		      int psize, int ssize)
{
	unsigned long vaddr;
	unsigned int step, shift;

	shift = mmu_psize_defs[psize].shift;
	step = 1 << shift;

	if (!ppc_md.hpte_removebolted) {
		printk(KERN_WARNING "Platform doesn't implement "
				"hpte_removebolted\n");
		return -EINVAL;
	}

	for (vaddr = vstart; vaddr < vend; vaddr += step)
		ppc_md.hpte_removebolted(vaddr, psize, ssize);

	return 0;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

static int __init htab_dt_scan_seg_sizes(unsigned long node,
					 const char *uname, int depth,
					 void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	u32 *prop;
	unsigned long size = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = (u32 *)of_get_flat_dt_prop(node, "ibm,processor-segment-sizes",
					  &size);
	if (prop == NULL)
		return 0;
	for (; size >= 4; size -= 4, ++prop) {
		if (prop[0] == 40) {
			DBG("1T segment support detected\n");
			cur_cpu_spec->cpu_features |= CPU_FTR_1T_SEGMENT;
			return 1;
		}
	}
	cur_cpu_spec->cpu_features &= ~CPU_FTR_NO_SLBIE_B;
	return 0;
}

static void __init htab_init_seg_sizes(void)
{
	of_scan_flat_dt(htab_dt_scan_seg_sizes, NULL);
}

static int __init htab_dt_scan_page_sizes(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	u32 *prop;
	unsigned long size = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = (u32 *)of_get_flat_dt_prop(node,
					  "ibm,segment-page-sizes", &size);
	if (prop != NULL) {
		DBG("Page sizes from device-tree:\n");
		size /= 4;
		cur_cpu_spec->cpu_features &= ~(CPU_FTR_16M_PAGE);
		while(size > 0) {
			unsigned int shift = prop[0];
			unsigned int slbenc = prop[1];
			unsigned int lpnum = prop[2];
			unsigned int lpenc = 0;
			struct mmu_psize_def *def;
			int idx = -1;

			size -= 3; prop += 3;
			while(size > 0 && lpnum) {
				if (prop[0] == shift)
					lpenc = prop[1];
				prop += 2; size -= 2;
				lpnum--;
			}
			switch(shift) {
			case 0xc:
				idx = MMU_PAGE_4K;
				break;
			case 0x10:
				idx = MMU_PAGE_64K;
				break;
			case 0x14:
				idx = MMU_PAGE_1M;
				break;
			case 0x18:
				idx = MMU_PAGE_16M;
				cur_cpu_spec->cpu_features |= CPU_FTR_16M_PAGE;
				break;
			case 0x22:
				idx = MMU_PAGE_16G;
				break;
			}
			if (idx < 0)
				continue;
			def = &mmu_psize_defs[idx];
			def->shift = shift;
			if (shift <= 23)
				def->avpnm = 0;
			else
				def->avpnm = (1 << (shift - 23)) - 1;
			def->sllp = slbenc;
			def->penc = lpenc;
			/* We don't know for sure what's up with tlbiel, so
			 * for now we only set it for 4K and 64K pages
			 */
			if (idx == MMU_PAGE_4K || idx == MMU_PAGE_64K)
				def->tlbiel = 1;
			else
				def->tlbiel = 0;

			DBG(" %d: shift=%02x, sllp=%04lx, avpnm=%08lx, "
			    "tlbiel=%d, penc=%d\n",
			    idx, shift, def->sllp, def->avpnm, def->tlbiel,
			    def->penc);
		}
		return 1;
	}
	return 0;
}

#ifdef CONFIG_HUGETLB_PAGE
/* Scan for 16G memory blocks that have been set aside for huge pages
 * and reserve those blocks for 16G huge pages.
 */
static int __init htab_dt_scan_hugepage_blocks(unsigned long node,
					const char *uname, int depth,
					void *data) {
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	unsigned long *addr_prop;
	u32 *page_count_prop;
	unsigned int expected_pages;
	long unsigned int phys_addr;
	long unsigned int block_size;

	/* We are scanning "memory" nodes only */
	if (type == NULL || strcmp(type, "memory") != 0)
		return 0;

	/* This property is the log base 2 of the number of virtual pages that
	 * will represent this memory block. */
	page_count_prop = of_get_flat_dt_prop(node, "ibm,expected#pages", NULL);
	if (page_count_prop == NULL)
		return 0;
	expected_pages = (1 << page_count_prop[0]);
	addr_prop = of_get_flat_dt_prop(node, "reg", NULL);
	if (addr_prop == NULL)
		return 0;
	phys_addr = addr_prop[0];
	block_size = addr_prop[1];
	if (block_size != (16 * GB))
		return 0;
	printk(KERN_INFO "Huge page(16GB) memory: "
			"addr = 0x%lX size = 0x%lX pages = %d\n",
			phys_addr, block_size, expected_pages);
	if (phys_addr + (16 * GB) <= memblock_end_of_DRAM()) {
		memblock_reserve(phys_addr, block_size * expected_pages);
		add_gpage(phys_addr, block_size, expected_pages);
	}
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE */

static void __init htab_init_page_sizes(void)
{
	int rc;

	/* Default to 4K pages only */
	memcpy(mmu_psize_defs, mmu_psize_defaults_old,
	       sizeof(mmu_psize_defaults_old));

	/*
	 * Try to find the available page sizes in the device-tree
	 */
	rc = of_scan_flat_dt(htab_dt_scan_page_sizes, NULL);
	if (rc != 0)  /* Found */
		goto found;

	/*
	 * Not in the device-tree, let's fallback on known size
	 * list for 16M capable GP & GR
	 */
	if (cpu_has_feature(CPU_FTR_16M_PAGE))
		memcpy(mmu_psize_defs, mmu_psize_defaults_gp,
		       sizeof(mmu_psize_defaults_gp));
 found:
#ifndef CONFIG_DEBUG_PAGEALLOC
	/*
	 * Pick a size for the linear mapping. Currently, we only support
	 * 16M, 1M and 4K which is the default
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		mmu_linear_psize = MMU_PAGE_16M;
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		mmu_linear_psize = MMU_PAGE_1M;
#endif /* CONFIG_DEBUG_PAGEALLOC */

#ifdef CONFIG_PPC_64K_PAGES
	/*
	 * Pick a size for the ordinary pages. Default is 4K, we support
	 * 64K for user mappings and vmalloc if supported by the processor.
	 * We only use 64k for ioremap if the processor
	 * (and firmware) support cache-inhibited large pages.
	 * If not, we use 4k and set mmu_ci_restrictions so that
	 * hash_page knows to switch processes that use cache-inhibited
	 * mappings to 4k pages.
	 */
	if (mmu_psize_defs[MMU_PAGE_64K].shift) {
		mmu_virtual_psize = MMU_PAGE_64K;
		mmu_vmalloc_psize = MMU_PAGE_64K;
		if (mmu_linear_psize == MMU_PAGE_4K)
			mmu_linear_psize = MMU_PAGE_64K;
		if (cpu_has_feature(CPU_FTR_CI_LARGE_PAGE)) {
			/*
			 * Don't use 64k pages for ioremap on pSeries, since
			 * that would stop us accessing the HEA ethernet.
			 */
			if (!machine_is(pseries))
				mmu_io_psize = MMU_PAGE_64K;
		} else
			mmu_ci_restrictions = 1;
	}
#endif /* CONFIG_PPC_64K_PAGES */

#ifdef CONFIG_SPARSEMEM_VMEMMAP
	/* We try to use 16M pages for vmemmap if that is supported
	 * and we have at least 1G of RAM at boot
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift &&
	    memblock_phys_mem_size() >= 0x40000000)
		mmu_vmemmap_psize = MMU_PAGE_16M;
	else if (mmu_psize_defs[MMU_PAGE_64K].shift)
		mmu_vmemmap_psize = MMU_PAGE_64K;
	else
		mmu_vmemmap_psize = MMU_PAGE_4K;
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

	printk(KERN_DEBUG "Page orders: linear mapping = %d, "
	       "virtual = %d, io = %d"
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	       ", vmemmap = %d"
#endif
	       "\n",
	       mmu_psize_defs[mmu_linear_psize].shift,
	       mmu_psize_defs[mmu_virtual_psize].shift,
	       mmu_psize_defs[mmu_io_psize].shift
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	       ,mmu_psize_defs[mmu_vmemmap_psize].shift
#endif
	       );

#ifdef CONFIG_HUGETLB_PAGE
	/* Reserve 16G huge page memory sections for huge pages */
	of_scan_flat_dt(htab_dt_scan_hugepage_blocks, NULL);
#endif /* CONFIG_HUGETLB_PAGE */
}

static int __init htab_dt_scan_pftsize(unsigned long node,
				       const char *uname, int depth,
				       void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	u32 *prop;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = (u32 *)of_get_flat_dt_prop(node, "ibm,pft-size", NULL);
	if (prop != NULL) {
		/* pft_size[0] is the NUMA CEC cookie */
		ppc64_pft_size = prop[1];
		return 1;
	}
	return 0;
}

static unsigned long __init htab_get_table_size(void)
{
	unsigned long mem_size, rnd_mem_size, pteg_count, psize;

	/* If hash size isn't already provided by the platform, we try to
	 * retrieve it from the device-tree. If it's not there neither, we
	 * calculate it now based on the total RAM size
	 */
	if (ppc64_pft_size == 0)
		of_scan_flat_dt(htab_dt_scan_pftsize, NULL);
	if (ppc64_pft_size)
		return 1UL << ppc64_pft_size;

	/* round mem_size up to next power of 2 */
	mem_size = memblock_phys_mem_size();
	rnd_mem_size = 1UL << __ilog2(mem_size);
	if (rnd_mem_size < mem_size)
		rnd_mem_size <<= 1;

	/* # pages / 2 */
	psize = mmu_psize_defs[mmu_virtual_psize].shift;
	pteg_count = max(rnd_mem_size >> (psize + 1), 1UL << 11);

	return pteg_count << 7;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void create_section_mapping(unsigned long start, unsigned long end)
{
	BUG_ON(htab_bolt_mapping(start, end, __pa(start),
				 pgprot_val(PAGE_KERNEL), mmu_linear_psize,
				 mmu_kernel_ssize));
}

int remove_section_mapping(unsigned long start, unsigned long end)
{
	return htab_remove_mapping(start, end, mmu_linear_psize,
			mmu_kernel_ssize);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

static inline void make_bl(unsigned int *insn_addr, void *func)
{
	unsigned long funcp = *((unsigned long *)func);
	int offset = funcp - (unsigned long)insn_addr;

	*insn_addr = (unsigned int)(0x48000001 | (offset & 0x03fffffc));
	flush_icache_range((unsigned long)insn_addr, 4+
			   (unsigned long)insn_addr);
}

static void __init htab_finish_init(void)
{
	extern unsigned int *htab_call_hpte_insert1;
	extern unsigned int *htab_call_hpte_insert2;
	extern unsigned int *htab_call_hpte_remove;
	extern unsigned int *htab_call_hpte_updatepp;

#ifdef CONFIG_PPC_HAS_HASH_64K
	extern unsigned int *ht64_call_hpte_insert1;
	extern unsigned int *ht64_call_hpte_insert2;
	extern unsigned int *ht64_call_hpte_remove;
	extern unsigned int *ht64_call_hpte_updatepp;

	make_bl(ht64_call_hpte_insert1, ppc_md.hpte_insert);
	make_bl(ht64_call_hpte_insert2, ppc_md.hpte_insert);
	make_bl(ht64_call_hpte_remove, ppc_md.hpte_remove);
	make_bl(ht64_call_hpte_updatepp, ppc_md.hpte_updatepp);
#endif /* CONFIG_PPC_HAS_HASH_64K */

	make_bl(htab_call_hpte_insert1, ppc_md.hpte_insert);
	make_bl(htab_call_hpte_insert2, ppc_md.hpte_insert);
	make_bl(htab_call_hpte_remove, ppc_md.hpte_remove);
	make_bl(htab_call_hpte_updatepp, ppc_md.hpte_updatepp);
}

static void __init htab_initialize(void)
{
	unsigned long table;
	unsigned long pteg_count;
	unsigned long prot;
	unsigned long base = 0, size = 0, limit;
	int i;

	DBG(" -> htab_initialize()\n");

	/* Initialize segment sizes */
	htab_init_seg_sizes();

	/* Initialize page sizes */
	htab_init_page_sizes();

	if (cpu_has_feature(CPU_FTR_1T_SEGMENT)) {
		mmu_kernel_ssize = MMU_SEGSIZE_1T;
		mmu_highuser_ssize = MMU_SEGSIZE_1T;
		printk(KERN_INFO "Using 1TB segments\n");
	}

	/*
	 * Calculate the required size of the htab.  We want the number of
	 * PTEGs to equal one half the number of real pages.
	 */ 
	htab_size_bytes = htab_get_table_size();
	pteg_count = htab_size_bytes >> 7;

	htab_hash_mask = pteg_count - 1;

	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		/* Using a hypervisor which owns the htab */
		htab_address = NULL;
		_SDR1 = 0; 
	} else {
		/* Find storage for the HPT.  Must be contiguous in
		 * the absolute address space. On cell we want it to be
		 * in the first 2 Gig so we can use it for IOMMU hacks.
		 */
		if (machine_is(cell))
			limit = 0x80000000;
		else
			limit = 0;

		table = memblock_alloc_base(htab_size_bytes, htab_size_bytes, limit);

		DBG("Hash table allocated at %lx, size: %lx\n", table,
		    htab_size_bytes);

		htab_address = abs_to_virt(table);

		/* htab absolute addr + encoded htabsize */
		_SDR1 = table + __ilog2(pteg_count) - 11;

		/* Initialize the HPT with no entries */
		memset((void *)table, 0, htab_size_bytes);

		/* Set SDR1 */
		mtspr(SPRN_SDR1, _SDR1);
	}

	prot = pgprot_val(PAGE_KERNEL);

#ifdef CONFIG_DEBUG_PAGEALLOC
	linear_map_hash_count = memblock_end_of_DRAM() >> PAGE_SHIFT;
	linear_map_hash_slots = __va(memblock_alloc_base(linear_map_hash_count,
						    1, memblock.rmo_size));
	memset(linear_map_hash_slots, 0, linear_map_hash_count);
#endif /* CONFIG_DEBUG_PAGEALLOC */

	/* On U3 based machines, we need to reserve the DART area and
	 * _NOT_ map it to avoid cache paradoxes as it's remapped non
	 * cacheable later on
	 */

	/* create bolted the linear mapping in the hash table */
	for (i=0; i < memblock.memory.cnt; i++) {
		base = (unsigned long)__va(memblock.memory.region[i].base);
		size = memblock.memory.region[i].size;

		DBG("creating mapping for region: %lx..%lx (prot: %lx)\n",
		    base, size, prot);

#ifdef CONFIG_U3_DART
		/* Do not map the DART space. Fortunately, it will be aligned
		 * in such a way that it will not cross two memblock regions and
		 * will fit within a single 16Mb page.
		 * The DART space is assumed to be a full 16Mb region even if
		 * we only use 2Mb of that space. We will use more of it later
		 * for AGP GART. We have to use a full 16Mb large page.
		 */
		DBG("DART base: %lx\n", dart_tablebase);

		if (dart_tablebase != 0 && dart_tablebase >= base
		    && dart_tablebase < (base + size)) {
			unsigned long dart_table_end = dart_tablebase + 16 * MB;
			if (base != dart_tablebase)
				BUG_ON(htab_bolt_mapping(base, dart_tablebase,
							__pa(base), prot,
							mmu_linear_psize,
							mmu_kernel_ssize));
			if ((base + size) > dart_table_end)
				BUG_ON(htab_bolt_mapping(dart_tablebase+16*MB,
							base + size,
							__pa(dart_table_end),
							 prot,
							 mmu_linear_psize,
							 mmu_kernel_ssize));
			continue;
		}
#endif /* CONFIG_U3_DART */
		BUG_ON(htab_bolt_mapping(base, base + size, __pa(base),
				prot, mmu_linear_psize, mmu_kernel_ssize));
       }

	/*
	 * If we have a memory_limit and we've allocated TCEs then we need to
	 * explicitly map the TCE area at the top of RAM. We also cope with the
	 * case that the TCEs start below memory_limit.
	 * tce_alloc_start/end are 16MB aligned so the mapping should work
	 * for either 4K or 16MB pages.
	 */
	if (tce_alloc_start) {
		tce_alloc_start = (unsigned long)__va(tce_alloc_start);
		tce_alloc_end = (unsigned long)__va(tce_alloc_end);

		if (base + size >= tce_alloc_start)
			tce_alloc_start = base + size + 1;

		BUG_ON(htab_bolt_mapping(tce_alloc_start, tce_alloc_end,
					 __pa(tce_alloc_start), prot,
					 mmu_linear_psize, mmu_kernel_ssize));
	}

	htab_finish_init();

	DBG(" <- htab_initialize()\n");
}
#undef KB
#undef MB

void __init early_init_mmu(void)
{
	/* Setup initial STAB address in the PACA */
	get_paca()->stab_real = __pa((u64)&initial_stab);
	get_paca()->stab_addr = (u64)&initial_stab;

	/* Initialize the MMU Hash table and create the linear mapping
	 * of memory. Has to be done before stab/slb initialization as
	 * this is currently where the page size encoding is obtained
	 */
	htab_initialize();

	/* Initialize stab / SLB management except on iSeries
	 */
	if (cpu_has_feature(CPU_FTR_SLB))
		slb_initialize();
	else if (!firmware_has_feature(FW_FEATURE_ISERIES))
		stab_initialize(get_paca()->stab_real);
}

#ifdef CONFIG_SMP
void __cpuinit early_init_mmu_secondary(void)
{
	/* Initialize hash table for that CPU */
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		mtspr(SPRN_SDR1, _SDR1);

	/* Initialize STAB/SLB. We use a virtual address as it works
	 * in real mode on pSeries and we want a virutal address on
	 * iSeries anyway
	 */
	if (cpu_has_feature(CPU_FTR_SLB))
		slb_initialize();
	else
		stab_initialize(get_paca()->stab_addr);
}
#endif /* CONFIG_SMP */

/*
 * Called by asm hashtable.S for doing lazy icache flush
 */
unsigned int hash_page_do_lazy_icache(unsigned int pp, pte_t pte, int trap)
{
	struct page *page;

	if (!pfn_valid(pte_pfn(pte)))
		return pp;

	page = pte_page(pte);

	/* page is dirty */
	if (!test_bit(PG_arch_1, &page->flags) && !PageReserved(page)) {
		if (trap == 0x400) {
			flush_dcache_icache_page(page);
			set_bit(PG_arch_1, &page->flags);
		} else
			pp |= HPTE_R_N;
	}
	return pp;
}

#ifdef CONFIG_PPC_MM_SLICES
unsigned int get_paca_psize(unsigned long addr)
{
	unsigned long index, slices;

	if (addr < SLICE_LOW_TOP) {
		slices = get_paca()->context.low_slices_psize;
		index = GET_LOW_SLICE_INDEX(addr);
	} else {
		slices = get_paca()->context.high_slices_psize;
		index = GET_HIGH_SLICE_INDEX(addr);
	}
	return (slices >> (index * 4)) & 0xF;
}

#else
unsigned int get_paca_psize(unsigned long addr)
{
	return get_paca()->context.user_psize;
}
#endif

/*
 * Demote a segment to using 4k pages.
 * For now this makes the whole process use 4k pages.
 */
#ifdef CONFIG_PPC_64K_PAGES
void demote_segment_4k(struct mm_struct *mm, unsigned long addr)
{
	if (get_slice_psize(mm, addr) == MMU_PAGE_4K)
		return;
	slice_set_range_psize(mm, addr, 1, MMU_PAGE_4K);
#ifdef CONFIG_SPU_BASE
	spu_flush_all_slbs(mm);
#endif
	if (get_paca_psize(addr) != MMU_PAGE_4K) {
		get_paca()->context = mm->context;
		slb_flush_and_rebolt();
	}
}
#endif /* CONFIG_PPC_64K_PAGES */

#ifdef CONFIG_PPC_SUBPAGE_PROT
/*
 * This looks up a 2-bit protection code for a 4k subpage of a 64k page.
 * Userspace sets the subpage permissions using the subpage_prot system call.
 *
 * Result is 0: full permissions, _PAGE_RW: read-only,
 * _PAGE_USER or _PAGE_USER|_PAGE_RW: no access.
 */
static int subpage_protection(struct mm_struct *mm, unsigned long ea)
{
	struct subpage_prot_table *spt = &mm->context.spt;
	u32 spp = 0;
	u32 **sbpm, *sbpp;

	if (ea >= spt->maxaddr)
		return 0;
	if (ea < 0x100000000) {
		/* addresses below 4GB use spt->low_prot */
		sbpm = spt->low_prot;
	} else {
		sbpm = spt->protptrs[ea >> SBP_L3_SHIFT];
		if (!sbpm)
			return 0;
	}
	sbpp = sbpm[(ea >> SBP_L2_SHIFT) & (SBP_L2_COUNT - 1)];
	if (!sbpp)
		return 0;
	spp = sbpp[(ea >> PAGE_SHIFT) & (SBP_L1_COUNT - 1)];

	/* extract 2-bit bitfield for this 4k subpage */
	spp >>= 30 - 2 * ((ea >> 12) & 0xf);

	/* turn 0,1,2,3 into combination of _PAGE_USER and _PAGE_RW */
	spp = ((spp & 2) ? _PAGE_USER : 0) | ((spp & 1) ? _PAGE_RW : 0);
	return spp;
}

#else /* CONFIG_PPC_SUBPAGE_PROT */
static inline int subpage_protection(struct mm_struct *mm, unsigned long ea)
{
	return 0;
}
#endif

void hash_failure_debug(unsigned long ea, unsigned long access,
			unsigned long vsid, unsigned long trap,
			int ssize, int psize, unsigned long pte)
{
	if (!printk_ratelimit())
		return;
	pr_info("mm: Hashing failure ! EA=0x%lx access=0x%lx current=%s\n",
		ea, access, current->comm);
	pr_info("    trap=0x%lx vsid=0x%lx ssize=%d psize=%d pte=0x%lx\n",
		trap, vsid, ssize, psize, pte);
}

/* Result code is:
 *  0 - handled
 *  1 - normal page fault
 * -1 - critical hash insertion error
 * -2 - access not permitted by subpage protection mechanism
 */
int hash_page(unsigned long ea, unsigned long access, unsigned long trap)
{
	pgd_t *pgdir;
	unsigned long vsid;
	struct mm_struct *mm;
	pte_t *ptep;
	unsigned hugeshift;
	const struct cpumask *tmp;
	int rc, user_region = 0, local = 0;
	int psize, ssize;

	DBG_LOW("hash_page(ea=%016lx, access=%lx, trap=%lx\n",
		ea, access, trap);

	if ((ea & ~REGION_MASK) >= PGTABLE_RANGE) {
		DBG_LOW(" out of pgtable range !\n");
 		return 1;
	}

	/* Get region & vsid */
 	switch (REGION_ID(ea)) {
	case USER_REGION_ID:
		user_region = 1;
		mm = current->mm;
		if (! mm) {
			DBG_LOW(" user region with no mm !\n");
			return 1;
		}
		psize = get_slice_psize(mm, ea);
		ssize = user_segment_size(ea);
		vsid = get_vsid(mm->context.id, ea, ssize);
		break;
	case VMALLOC_REGION_ID:
		mm = &init_mm;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		if (ea < VMALLOC_END)
			psize = mmu_vmalloc_psize;
		else
			psize = mmu_io_psize;
		ssize = mmu_kernel_ssize;
		break;
	default:
		/* Not a valid range
		 * Send the problem up to do_page_fault 
		 */
		return 1;
	}
	DBG_LOW(" mm=%p, mm->pgdir=%p, vsid=%016lx\n", mm, mm->pgd, vsid);

	/* Get pgdir */
	pgdir = mm->pgd;
	if (pgdir == NULL)
		return 1;

	/* Check CPU locality */
	tmp = cpumask_of(smp_processor_id());
	if (user_region && cpumask_equal(mm_cpumask(mm), tmp))
		local = 1;

#ifndef CONFIG_PPC_64K_PAGES
	/* If we use 4K pages and our psize is not 4K, then we might
	 * be hitting a special driver mapping, and need to align the
	 * address before we fetch the PTE.
	 *
	 * It could also be a hugepage mapping, in which case this is
	 * not necessary, but it's not harmful, either.
	 */
	if (psize != MMU_PAGE_4K)
		ea &= ~((1ul << mmu_psize_defs[psize].shift) - 1);
#endif /* CONFIG_PPC_64K_PAGES */

	/* Get PTE and page size from page tables */
	ptep = find_linux_pte_or_hugepte(pgdir, ea, &hugeshift);
	if (ptep == NULL || !pte_present(*ptep)) {
		DBG_LOW(" no PTE !\n");
		return 1;
	}

	/* Add _PAGE_PRESENT to the required access perm */
	access |= _PAGE_PRESENT;

	/* Pre-check access permissions (will be re-checked atomically
	 * in __hash_page_XX but this pre-check is a fast path
	 */
	if (access & ~pte_val(*ptep)) {
		DBG_LOW(" no access !\n");
		return 1;
	}

#ifdef CONFIG_HUGETLB_PAGE
	if (hugeshift)
		return __hash_page_huge(ea, access, vsid, ptep, trap, local,
					ssize, hugeshift, psize);
#endif /* CONFIG_HUGETLB_PAGE */

#ifndef CONFIG_PPC_64K_PAGES
	DBG_LOW(" i-pte: %016lx\n", pte_val(*ptep));
#else
	DBG_LOW(" i-pte: %016lx %016lx\n", pte_val(*ptep),
		pte_val(*(ptep + PTRS_PER_PTE)));
#endif
	/* Do actual hashing */
#ifdef CONFIG_PPC_64K_PAGES
	/* If _PAGE_4K_PFN is set, make sure this is a 4k segment */
	if ((pte_val(*ptep) & _PAGE_4K_PFN) && psize == MMU_PAGE_64K) {
		demote_segment_4k(mm, ea);
		psize = MMU_PAGE_4K;
	}

	/* If this PTE is non-cacheable and we have restrictions on
	 * using non cacheable large pages, then we switch to 4k
	 */
	if (mmu_ci_restrictions && psize == MMU_PAGE_64K &&
	    (pte_val(*ptep) & _PAGE_NO_CACHE)) {
		if (user_region) {
			demote_segment_4k(mm, ea);
			psize = MMU_PAGE_4K;
		} else if (ea < VMALLOC_END) {
			/*
			 * some driver did a non-cacheable mapping
			 * in vmalloc space, so switch vmalloc
			 * to 4k pages
			 */
			printk(KERN_ALERT "Reducing vmalloc segment "
			       "to 4kB pages because of "
			       "non-cacheable mapping\n");
			psize = mmu_vmalloc_psize = MMU_PAGE_4K;
#ifdef CONFIG_SPU_BASE
			spu_flush_all_slbs(mm);
#endif
		}
	}
	if (user_region) {
		if (psize != get_paca_psize(ea)) {
			get_paca()->context = mm->context;
			slb_flush_and_rebolt();
		}
	} else if (get_paca()->vmalloc_sllp !=
		   mmu_psize_defs[mmu_vmalloc_psize].sllp) {
		get_paca()->vmalloc_sllp =
			mmu_psize_defs[mmu_vmalloc_psize].sllp;
		slb_vmalloc_update();
	}
#endif /* CONFIG_PPC_64K_PAGES */

#ifdef CONFIG_PPC_HAS_HASH_64K
	if (psize == MMU_PAGE_64K)
		rc = __hash_page_64K(ea, access, vsid, ptep, trap, local, ssize);
	else
#endif /* CONFIG_PPC_HAS_HASH_64K */
	{
		int spp = subpage_protection(mm, ea);
		if (access & spp)
			rc = -2;
		else
			rc = __hash_page_4K(ea, access, vsid, ptep, trap,
					    local, ssize, spp);
	}

	/* Dump some info in case of hash insertion failure, they should
	 * never happen so it is really useful to know if/when they do
	 */
	if (rc == -1)
		hash_failure_debug(ea, access, vsid, trap, ssize, psize,
				   pte_val(*ptep));
#ifndef CONFIG_PPC_64K_PAGES
	DBG_LOW(" o-pte: %016lx\n", pte_val(*ptep));
#else
	DBG_LOW(" o-pte: %016lx %016lx\n", pte_val(*ptep),
		pte_val(*(ptep + PTRS_PER_PTE)));
#endif
	DBG_LOW(" -> rc=%d\n", rc);
	return rc;
}
EXPORT_SYMBOL_GPL(hash_page);

void hash_preload(struct mm_struct *mm, unsigned long ea,
		  unsigned long access, unsigned long trap)
{
	unsigned long vsid;
	void *pgdir;
	pte_t *ptep;
	unsigned long flags;
	int rc, ssize, local = 0;

	BUG_ON(REGION_ID(ea) != USER_REGION_ID);

#ifdef CONFIG_PPC_MM_SLICES
	/* We only prefault standard pages for now */
	if (unlikely(get_slice_psize(mm, ea) != mm->context.user_psize))
		return;
#endif

	DBG_LOW("hash_preload(mm=%p, mm->pgdir=%p, ea=%016lx, access=%lx,"
		" trap=%lx\n", mm, mm->pgd, ea, access, trap);

	/* Get Linux PTE if available */
	pgdir = mm->pgd;
	if (pgdir == NULL)
		return;
	ptep = find_linux_pte(pgdir, ea);
	if (!ptep)
		return;

#ifdef CONFIG_PPC_64K_PAGES
	/* If either _PAGE_4K_PFN or _PAGE_NO_CACHE is set (and we are on
	 * a 64K kernel), then we don't preload, hash_page() will take
	 * care of it once we actually try to access the page.
	 * That way we don't have to duplicate all of the logic for segment
	 * page size demotion here
	 */
	if (pte_val(*ptep) & (_PAGE_4K_PFN | _PAGE_NO_CACHE))
		return;
#endif /* CONFIG_PPC_64K_PAGES */

	/* Get VSID */
	ssize = user_segment_size(ea);
	vsid = get_vsid(mm->context.id, ea, ssize);

	/* Hash doesn't like irqs */
	local_irq_save(flags);

	/* Is that local to this CPU ? */
	if (cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		local = 1;

	/* Hash it in */
#ifdef CONFIG_PPC_HAS_HASH_64K
	if (mm->context.user_psize == MMU_PAGE_64K)
		rc = __hash_page_64K(ea, access, vsid, ptep, trap, local, ssize);
	else
#endif /* CONFIG_PPC_HAS_HASH_64K */
		rc = __hash_page_4K(ea, access, vsid, ptep, trap, local, ssize,
				    subpage_protection(pgdir, ea));

	/* Dump some info in case of hash insertion failure, they should
	 * never happen so it is really useful to know if/when they do
	 */
	if (rc == -1)
		hash_failure_debug(ea, access, vsid, trap, ssize,
				   mm->context.user_psize, pte_val(*ptep));

	local_irq_restore(flags);
}

/* WARNING: This is called from hash_low_64.S, if you change this prototype,
 *          do not forget to update the assembly call site !
 */
void flush_hash_page(unsigned long va, real_pte_t pte, int psize, int ssize,
		     int local)
{
	unsigned long hash, index, shift, hidx, slot;

	DBG_LOW("flush_hash_page(va=%016lx)\n", va);
	pte_iterate_hashed_subpages(pte, psize, va, index, shift) {
		hash = hpt_hash(va, shift, ssize);
		hidx = __rpte_to_hidx(pte, index);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;
		DBG_LOW(" sub %ld: hash=%lx, hidx=%lx\n", index, slot, hidx);
		ppc_md.hpte_invalidate(slot, va, psize, ssize, local);
	} pte_iterate_hashed_end();
}

void flush_hash_range(unsigned long number, int local)
{
	if (ppc_md.flush_hash_range)
		ppc_md.flush_hash_range(number, local);
	else {
		int i;
		struct ppc64_tlb_batch *batch =
			&__get_cpu_var(ppc64_tlb_batch);

		for (i = 0; i < number; i++)
			flush_hash_page(batch->vaddr[i], batch->pte[i],
					batch->psize, batch->ssize, local);
	}
}

/*
 * low_hash_fault is called when we the low level hash code failed
 * to instert a PTE due to an hypervisor error
 */
void low_hash_fault(struct pt_regs *regs, unsigned long address, int rc)
{
	if (user_mode(regs)) {
#ifdef CONFIG_PPC_SUBPAGE_PROT
		if (rc == -2)
			_exception(SIGSEGV, regs, SEGV_ACCERR, address);
		else
#endif
			_exception(SIGBUS, regs, BUS_ADRERR, address);
	} else
		bad_page_fault(regs, address, SIGBUS);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
static void kernel_map_linear_page(unsigned long vaddr, unsigned long lmi)
{
	unsigned long hash, hpteg;
	unsigned long vsid = get_kernel_vsid(vaddr, mmu_kernel_ssize);
	unsigned long va = hpt_va(vaddr, vsid, mmu_kernel_ssize);
	unsigned long mode = htab_convert_pte_flags(PAGE_KERNEL);
	int ret;

	hash = hpt_hash(va, PAGE_SHIFT, mmu_kernel_ssize);
	hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

	ret = ppc_md.hpte_insert(hpteg, va, __pa(vaddr),
				 mode, HPTE_V_BOLTED,
				 mmu_linear_psize, mmu_kernel_ssize);
	BUG_ON (ret < 0);
	spin_lock(&linear_map_hash_lock);
	BUG_ON(linear_map_hash_slots[lmi] & 0x80);
	linear_map_hash_slots[lmi] = ret | 0x80;
	spin_unlock(&linear_map_hash_lock);
}

static void kernel_unmap_linear_page(unsigned long vaddr, unsigned long lmi)
{
	unsigned long hash, hidx, slot;
	unsigned long vsid = get_kernel_vsid(vaddr, mmu_kernel_ssize);
	unsigned long va = hpt_va(vaddr, vsid, mmu_kernel_ssize);

	hash = hpt_hash(va, PAGE_SHIFT, mmu_kernel_ssize);
	spin_lock(&linear_map_hash_lock);
	BUG_ON(!(linear_map_hash_slots[lmi] & 0x80));
	hidx = linear_map_hash_slots[lmi] & 0x7f;
	linear_map_hash_slots[lmi] = 0;
	spin_unlock(&linear_map_hash_lock);
	if (hidx & _PTEIDX_SECONDARY)
		hash = ~hash;
	slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	slot += hidx & _PTEIDX_GROUP_IX;
	ppc_md.hpte_invalidate(slot, va, mmu_linear_psize, mmu_kernel_ssize, 0);
}

void kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long flags, vaddr, lmi;
	int i;

	local_irq_save(flags);
	for (i = 0; i < numpages; i++, page++) {
		vaddr = (unsigned long)page_address(page);
		lmi = __pa(vaddr) >> PAGE_SHIFT;
		if (lmi >= linear_map_hash_count)
			continue;
		if (enable)
			kernel_map_linear_page(vaddr, lmi);
		else
			kernel_unmap_linear_page(vaddr, lmi);
	}
	local_irq_restore(flags);
}
#endif /* CONFIG_DEBUG_PAGEALLOC */

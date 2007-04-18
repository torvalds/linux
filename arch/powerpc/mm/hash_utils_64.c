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

#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/lmb.h>
#include <asm/abs_addr.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/eeh.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/cputable.h>
#include <asm/abs_addr.h>
#include <asm/sections.h>

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

hpte_t *htab_address;
unsigned long htab_size_bytes;
unsigned long htab_hash_mask;
int mmu_linear_psize = MMU_PAGE_4K;
int mmu_virtual_psize = MMU_PAGE_4K;
int mmu_vmalloc_psize = MMU_PAGE_4K;
int mmu_io_psize = MMU_PAGE_4K;
#ifdef CONFIG_HUGETLB_PAGE
int mmu_huge_psize = MMU_PAGE_16M;
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
struct mmu_psize_def mmu_psize_defaults_old[] = {
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
struct mmu_psize_def mmu_psize_defaults_gp[] = {
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


int htab_bolt_mapping(unsigned long vstart, unsigned long vend,
		      unsigned long pstart, unsigned long mode, int psize)
{
	unsigned long vaddr, paddr;
	unsigned int step, shift;
	unsigned long tmp_mode;
	int ret = 0;

	shift = mmu_psize_defs[psize].shift;
	step = 1 << shift;

	for (vaddr = vstart, paddr = pstart; vaddr < vend;
	     vaddr += step, paddr += step) {
		unsigned long hash, hpteg;
		unsigned long vsid = get_kernel_vsid(vaddr);
		unsigned long va = (vsid << 28) | (vaddr & 0x0fffffff);

		tmp_mode = mode;
		
		/* Make non-kernel text non-executable */
		if (!in_kernel_text(vaddr))
			tmp_mode = mode | HPTE_R_N;

		hash = hpt_hash(va, shift);
		hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

		DBG("htab_bolt_mapping: calling %p\n", ppc_md.hpte_insert);

		BUG_ON(!ppc_md.hpte_insert);
		ret = ppc_md.hpte_insert(hpteg, va, paddr,
				tmp_mode, HPTE_V_BOLTED, psize);

		if (ret < 0)
			break;
#ifdef CONFIG_DEBUG_PAGEALLOC
		if ((paddr >> PAGE_SHIFT) < linear_map_hash_count)
			linear_map_hash_slots[paddr >> PAGE_SHIFT] = ret | 0x80;
#endif /* CONFIG_DEBUG_PAGEALLOC */
	}
	return ret < 0 ? ret : 0;
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

			DBG(" %d: shift=%02x, sllp=%04x, avpnm=%08x, "
			    "tlbiel=%d, penc=%d\n",
			    idx, shift, def->sllp, def->avpnm, def->tlbiel,
			    def->penc);
		}
		return 1;
	}
	return 0;
}


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
		if (cpu_has_feature(CPU_FTR_CI_LARGE_PAGE))
			mmu_io_psize = MMU_PAGE_64K;
		else
			mmu_ci_restrictions = 1;
	}
#endif /* CONFIG_PPC_64K_PAGES */

	printk(KERN_DEBUG "Page orders: linear mapping = %d, "
	       "virtual = %d, io = %d\n",
	       mmu_psize_defs[mmu_linear_psize].shift,
	       mmu_psize_defs[mmu_virtual_psize].shift,
	       mmu_psize_defs[mmu_io_psize].shift);

#ifdef CONFIG_HUGETLB_PAGE
	/* Init large page size. Currently, we pick 16M or 1M depending
	 * on what is available
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		mmu_huge_psize = MMU_PAGE_16M;
	/* With 4k/4level pagetables, we can't (for now) cope with a
	 * huge page size < PMD_SIZE */
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		mmu_huge_psize = MMU_PAGE_1M;

	/* Calculate HPAGE_SHIFT and sanity check it */
	if (mmu_psize_defs[mmu_huge_psize].shift > MIN_HUGEPTE_SHIFT &&
	    mmu_psize_defs[mmu_huge_psize].shift < SID_SHIFT)
		HPAGE_SHIFT = mmu_psize_defs[mmu_huge_psize].shift;
	else
		HPAGE_SHIFT = 0; /* No huge pages dude ! */
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
	unsigned long mem_size, rnd_mem_size, pteg_count;

	/* If hash size isn't already provided by the platform, we try to
	 * retrieve it from the device-tree. If it's not there neither, we
	 * calculate it now based on the total RAM size
	 */
	if (ppc64_pft_size == 0)
		of_scan_flat_dt(htab_dt_scan_pftsize, NULL);
	if (ppc64_pft_size)
		return 1UL << ppc64_pft_size;

	/* round mem_size up to next power of 2 */
	mem_size = lmb_phys_mem_size();
	rnd_mem_size = 1UL << __ilog2(mem_size);
	if (rnd_mem_size < mem_size)
		rnd_mem_size <<= 1;

	/* # pages / 2 */
	pteg_count = max(rnd_mem_size >> (12 + 1), 1UL << 11);

	return pteg_count << 7;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void create_section_mapping(unsigned long start, unsigned long end)
{
		BUG_ON(htab_bolt_mapping(start, end, __pa(start),
			_PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_COHERENT | PP_RWXX,
			mmu_linear_psize));
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

#ifdef CONFIG_PPC_64K_PAGES
	extern unsigned int *ht64_call_hpte_insert1;
	extern unsigned int *ht64_call_hpte_insert2;
	extern unsigned int *ht64_call_hpte_remove;
	extern unsigned int *ht64_call_hpte_updatepp;

	make_bl(ht64_call_hpte_insert1, ppc_md.hpte_insert);
	make_bl(ht64_call_hpte_insert2, ppc_md.hpte_insert);
	make_bl(ht64_call_hpte_remove, ppc_md.hpte_remove);
	make_bl(ht64_call_hpte_updatepp, ppc_md.hpte_updatepp);
#endif /* CONFIG_PPC_64K_PAGES */

	make_bl(htab_call_hpte_insert1, ppc_md.hpte_insert);
	make_bl(htab_call_hpte_insert2, ppc_md.hpte_insert);
	make_bl(htab_call_hpte_remove, ppc_md.hpte_remove);
	make_bl(htab_call_hpte_updatepp, ppc_md.hpte_updatepp);
}

void __init htab_initialize(void)
{
	unsigned long table;
	unsigned long pteg_count;
	unsigned long mode_rw;
	unsigned long base = 0, size = 0;
	int i;

	extern unsigned long tce_alloc_start, tce_alloc_end;

	DBG(" -> htab_initialize()\n");

	/* Initialize page sizes */
	htab_init_page_sizes();

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
		 * the absolute address space.
		 */
		table = lmb_alloc(htab_size_bytes, htab_size_bytes);

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

	mode_rw = _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_COHERENT | PP_RWXX;

#ifdef CONFIG_DEBUG_PAGEALLOC
	linear_map_hash_count = lmb_end_of_DRAM() >> PAGE_SHIFT;
	linear_map_hash_slots = __va(lmb_alloc_base(linear_map_hash_count,
						    1, lmb.rmo_size));
	memset(linear_map_hash_slots, 0, linear_map_hash_count);
#endif /* CONFIG_DEBUG_PAGEALLOC */

	/* On U3 based machines, we need to reserve the DART area and
	 * _NOT_ map it to avoid cache paradoxes as it's remapped non
	 * cacheable later on
	 */

	/* create bolted the linear mapping in the hash table */
	for (i=0; i < lmb.memory.cnt; i++) {
		base = (unsigned long)__va(lmb.memory.region[i].base);
		size = lmb.memory.region[i].size;

		DBG("creating mapping for region: %lx : %lx\n", base, size);

#ifdef CONFIG_U3_DART
		/* Do not map the DART space. Fortunately, it will be aligned
		 * in such a way that it will not cross two lmb regions and
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
							__pa(base), mode_rw,
							mmu_linear_psize));
			if ((base + size) > dart_table_end)
				BUG_ON(htab_bolt_mapping(dart_tablebase+16*MB,
							base + size,
							__pa(dart_table_end),
							 mode_rw,
							 mmu_linear_psize));
			continue;
		}
#endif /* CONFIG_U3_DART */
		BUG_ON(htab_bolt_mapping(base, base + size, __pa(base),
					mode_rw, mmu_linear_psize));
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
					 __pa(tce_alloc_start), mode_rw,
					 mmu_linear_psize));
	}

	htab_finish_init();

	DBG(" <- htab_initialize()\n");
}
#undef KB
#undef MB

void htab_initialize_secondary(void)
{
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		mtspr(SPRN_SDR1, _SDR1);
}

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
			__flush_dcache_icache(page_address(page));
			set_bit(PG_arch_1, &page->flags);
		} else
			pp |= HPTE_R_N;
	}
	return pp;
}

/*
 * Demote a segment to using 4k pages.
 * For now this makes the whole process use 4k pages.
 */
void demote_segment_4k(struct mm_struct *mm, unsigned long addr)
{
#ifdef CONFIG_PPC_64K_PAGES
	if (mm->context.user_psize == MMU_PAGE_4K)
		return;
	mm->context.user_psize = MMU_PAGE_4K;
	mm->context.sllp = SLB_VSID_USER | mmu_psize_defs[MMU_PAGE_4K].sllp;
	get_paca()->context = mm->context;
	slb_flush_and_rebolt();
#ifdef CONFIG_SPE_BASE
	spu_flush_all_slbs(mm);
#endif
#endif
}

EXPORT_SYMBOL_GPL(demote_segment_4k);

/* Result code is:
 *  0 - handled
 *  1 - normal page fault
 * -1 - critical hash insertion error
 */
int hash_page(unsigned long ea, unsigned long access, unsigned long trap)
{
	void *pgdir;
	unsigned long vsid;
	struct mm_struct *mm;
	pte_t *ptep;
	cpumask_t tmp;
	int rc, user_region = 0, local = 0;
	int psize;

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
		vsid = get_vsid(mm->context.id, ea);
		psize = mm->context.user_psize;
		break;
	case VMALLOC_REGION_ID:
		mm = &init_mm;
		vsid = get_kernel_vsid(ea);
		if (ea < VMALLOC_END)
			psize = mmu_vmalloc_psize;
		else
			psize = mmu_io_psize;
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
	tmp = cpumask_of_cpu(smp_processor_id());
	if (user_region && cpus_equal(mm->cpu_vm_mask, tmp))
		local = 1;

	/* Handle hugepage regions */
	if (unlikely(in_hugepage_area(mm->context, ea))) {
		DBG_LOW(" -> huge page !\n");
		return hash_huge_page(mm, access, ea, vsid, local, trap);
	}

	/* Get PTE and page size from page tables */
	ptep = find_linux_pte(pgdir, ea);
	if (ptep == NULL || !pte_present(*ptep)) {
		DBG_LOW(" no PTE !\n");
		return 1;
	}

#ifndef CONFIG_PPC_64K_PAGES
	DBG_LOW(" i-pte: %016lx\n", pte_val(*ptep));
#else
	DBG_LOW(" i-pte: %016lx %016lx\n", pte_val(*ptep),
		pte_val(*(ptep + PTRS_PER_PTE)));
#endif
	/* Pre-check access permissions (will be re-checked atomically
	 * in __hash_page_XX but this pre-check is a fast path
	 */
	if (access & ~pte_val(*ptep)) {
		DBG_LOW(" no access !\n");
		return 1;
	}

	/* Do actual hashing */
#ifndef CONFIG_PPC_64K_PAGES
	rc = __hash_page_4K(ea, access, vsid, ptep, trap, local);
#else
	/* If _PAGE_4K_PFN is set, make sure this is a 4k segment */
	if (pte_val(*ptep) & _PAGE_4K_PFN) {
		demote_segment_4k(mm, ea);
		psize = MMU_PAGE_4K;
	}

	if (mmu_ci_restrictions) {
		/* If this PTE is non-cacheable, switch to 4k */
		if (psize == MMU_PAGE_64K &&
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
			}
#ifdef CONFIG_SPE_BASE
			spu_flush_all_slbs(mm);
#endif
		}
		if (user_region) {
			if (psize != get_paca()->context.user_psize) {
				get_paca()->context = mm->context;
				slb_flush_and_rebolt();
			}
		} else if (get_paca()->vmalloc_sllp !=
			   mmu_psize_defs[mmu_vmalloc_psize].sllp) {
			get_paca()->vmalloc_sllp =
				mmu_psize_defs[mmu_vmalloc_psize].sllp;
			slb_flush_and_rebolt();
		}
	}
	if (psize == MMU_PAGE_64K)
		rc = __hash_page_64K(ea, access, vsid, ptep, trap, local);
	else
		rc = __hash_page_4K(ea, access, vsid, ptep, trap, local);
#endif /* CONFIG_PPC_64K_PAGES */

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
	cpumask_t mask;
	unsigned long flags;
	int local = 0;

	/* We don't want huge pages prefaulted for now
	 */
	if (unlikely(in_hugepage_area(mm->context, ea)))
		return;

	DBG_LOW("hash_preload(mm=%p, mm->pgdir=%p, ea=%016lx, access=%lx,"
		" trap=%lx\n", mm, mm->pgd, ea, access, trap);

	/* Get PTE, VSID, access mask */
	pgdir = mm->pgd;
	if (pgdir == NULL)
		return;
	ptep = find_linux_pte(pgdir, ea);
	if (!ptep)
		return;
	vsid = get_vsid(mm->context.id, ea);

	/* Hash it in */
	local_irq_save(flags);
	mask = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(mm->cpu_vm_mask, mask))
		local = 1;
#ifndef CONFIG_PPC_64K_PAGES
	__hash_page_4K(ea, access, vsid, ptep, trap, local);
#else
	if (mmu_ci_restrictions) {
		/* If this PTE is non-cacheable, switch to 4k */
		if (mm->context.user_psize == MMU_PAGE_64K &&
		    (pte_val(*ptep) & _PAGE_NO_CACHE))
			demote_segment_4k(mm, ea);
	}
	if (mm->context.user_psize == MMU_PAGE_64K)
		__hash_page_64K(ea, access, vsid, ptep, trap, local);
	else
		__hash_page_4K(ea, access, vsid, ptep, trap, local);
#endif /* CONFIG_PPC_64K_PAGES */
	local_irq_restore(flags);
}

void flush_hash_page(unsigned long va, real_pte_t pte, int psize, int local)
{
	unsigned long hash, index, shift, hidx, slot;

	DBG_LOW("flush_hash_page(va=%016x)\n", va);
	pte_iterate_hashed_subpages(pte, psize, va, index, shift) {
		hash = hpt_hash(va, shift);
		hidx = __rpte_to_hidx(pte, index);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;
		DBG_LOW(" sub %d: hash=%x, hidx=%x\n", index, slot, hidx);
		ppc_md.hpte_invalidate(slot, va, psize, local);
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
					batch->psize, local);
	}
}

/*
 * low_hash_fault is called when we the low level hash code failed
 * to instert a PTE due to an hypervisor error
 */
void low_hash_fault(struct pt_regs *regs, unsigned long address)
{
	if (user_mode(regs)) {
		siginfo_t info;

		info.si_signo = SIGBUS;
		info.si_errno = 0;
		info.si_code = BUS_ADRERR;
		info.si_addr = (void __user *)address;
		force_sig_info(SIGBUS, &info, current);
		return;
	}
	bad_page_fault(regs, address, SIGBUS);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
static void kernel_map_linear_page(unsigned long vaddr, unsigned long lmi)
{
	unsigned long hash, hpteg, vsid = get_kernel_vsid(vaddr);
	unsigned long va = (vsid << 28) | (vaddr & 0x0fffffff);
	unsigned long mode = _PAGE_ACCESSED | _PAGE_DIRTY |
		_PAGE_COHERENT | PP_RWXX | HPTE_R_N;
	int ret;

	hash = hpt_hash(va, PAGE_SHIFT);
	hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

	ret = ppc_md.hpte_insert(hpteg, va, __pa(vaddr),
				 mode, HPTE_V_BOLTED, mmu_linear_psize);
	BUG_ON (ret < 0);
	spin_lock(&linear_map_hash_lock);
	BUG_ON(linear_map_hash_slots[lmi] & 0x80);
	linear_map_hash_slots[lmi] = ret | 0x80;
	spin_unlock(&linear_map_hash_lock);
}

static void kernel_unmap_linear_page(unsigned long vaddr, unsigned long lmi)
{
	unsigned long hash, hidx, slot, vsid = get_kernel_vsid(vaddr);
	unsigned long va = (vsid << 28) | (vaddr & 0x0fffffff);

	hash = hpt_hash(va, PAGE_SHIFT);
	spin_lock(&linear_map_hash_lock);
	BUG_ON(!(linear_map_hash_slots[lmi] & 0x80));
	hidx = linear_map_hash_slots[lmi] & 0x7f;
	linear_map_hash_slots[lmi] = 0;
	spin_unlock(&linear_map_hash_lock);
	if (hidx & _PTEIDX_SECONDARY)
		hash = ~hash;
	slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	slot += hidx & _PTEIDX_GROUP_IX;
	ppc_md.hpte_invalidate(slot, va, mmu_linear_psize, 0);
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

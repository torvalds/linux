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

#define pr_fmt(fmt) "hash-mmu: " fmt
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/sched/mm.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/memblock.h>
#include <linux/context_tracking.h>
#include <linux/libfdt.h>
#include <linux/pkeys.h>

#include <asm/debugfs.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <linux/uaccess.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/eeh.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/copro.h>
#include <asm/udbg.h>
#include <asm/code-patching.h>
#include <asm/fadump.h>
#include <asm/firmware.h>
#include <asm/tm.h>
#include <asm/trace.h>
#include <asm/ps3.h>
#include <asm/pte-walk.h>
#include <asm/asm-prototypes.h>

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

static unsigned long _SDR1;
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];
EXPORT_SYMBOL_GPL(mmu_psize_defs);

u8 hpte_page_sizes[1 << LP_BITS];
EXPORT_SYMBOL_GPL(hpte_page_sizes);

struct hash_pte *htab_address;
unsigned long htab_size_bytes;
unsigned long htab_hash_mask;
EXPORT_SYMBOL_GPL(htab_hash_mask);
int mmu_linear_psize = MMU_PAGE_4K;
EXPORT_SYMBOL_GPL(mmu_linear_psize);
int mmu_virtual_psize = MMU_PAGE_4K;
int mmu_vmalloc_psize = MMU_PAGE_4K;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
int mmu_vmemmap_psize = MMU_PAGE_4K;
#endif
int mmu_io_psize = MMU_PAGE_4K;
int mmu_kernel_ssize = MMU_SEGSIZE_256M;
EXPORT_SYMBOL_GPL(mmu_kernel_ssize);
int mmu_highuser_ssize = MMU_SEGSIZE_256M;
u16 mmu_slb_size = 64;
EXPORT_SYMBOL_GPL(mmu_slb_size);
#ifdef CONFIG_PPC_64K_PAGES
int mmu_ci_restrictions;
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
static u8 *linear_map_hash_slots;
static unsigned long linear_map_hash_count;
static DEFINE_SPINLOCK(linear_map_hash_lock);
#endif /* CONFIG_DEBUG_PAGEALLOC */
struct mmu_hash_ops mmu_hash_ops;
EXPORT_SYMBOL(mmu_hash_ops);

/* There are definitions of page sizes arrays to be used when none
 * is provided by the firmware.
 */

/*
 * Fallback (4k pages only)
 */
static struct mmu_psize_def mmu_psize_defaults[] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.sllp	= 0,
		.penc   = {[MMU_PAGE_4K] = 0, [1 ... MMU_PAGE_COUNT - 1] = -1},
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
		.penc   = {[MMU_PAGE_4K] = 0, [1 ... MMU_PAGE_COUNT - 1] = -1},
		.avpnm	= 0,
		.tlbiel = 1,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
		.sllp	= SLB_VSID_L,
		.penc   = {[0 ... MMU_PAGE_16M - 1] = -1, [MMU_PAGE_16M] = 0,
			    [MMU_PAGE_16M + 1 ... MMU_PAGE_COUNT - 1] = -1 },
		.avpnm	= 0x1UL,
		.tlbiel = 0,
	},
};

/*
 * 'R' and 'C' update notes:
 *  - Under pHyp or KVM, the updatepp path will not set C, thus it *will*
 *     create writeable HPTEs without C set, because the hcall H_PROTECT
 *     that we use in that case will not update C
 *  - The above is however not a problem, because we also don't do that
 *     fancy "no flush" variant of eviction and we use H_REMOVE which will
 *     do the right thing and thus we don't have the race I described earlier
 *
 *    - Under bare metal,  we do have the race, so we need R and C set
 *    - We make sure R is always set and never lost
 *    - C is _PAGE_DIRTY, and *should* always be set for a writeable mapping
 */
unsigned long htab_convert_pte_flags(unsigned long pteflags)
{
	unsigned long rflags = 0;

	/* _PAGE_EXEC -> NOEXEC */
	if ((pteflags & _PAGE_EXEC) == 0)
		rflags |= HPTE_R_N;
	/*
	 * PPP bits:
	 * Linux uses slb key 0 for kernel and 1 for user.
	 * kernel RW areas are mapped with PPP=0b000
	 * User area is mapped with PPP=0b010 for read/write
	 * or PPP=0b011 for read-only (including writeable but clean pages).
	 */
	if (pteflags & _PAGE_PRIVILEGED) {
		/*
		 * Kernel read only mapped with ppp bits 0b110
		 */
		if (!(pteflags & _PAGE_WRITE)) {
			if (mmu_has_feature(MMU_FTR_KERNEL_RO))
				rflags |= (HPTE_R_PP0 | 0x2);
			else
				rflags |= 0x3;
		}
	} else {
		if (pteflags & _PAGE_RWX)
			rflags |= 0x2;
		if (!((pteflags & _PAGE_WRITE) && (pteflags & _PAGE_DIRTY)))
			rflags |= 0x1;
	}
	/*
	 * We can't allow hardware to update hpte bits. Hence always
	 * set 'R' bit and set 'C' if it is a write fault
	 */
	rflags |=  HPTE_R_R;

	if (pteflags & _PAGE_DIRTY)
		rflags |= HPTE_R_C;
	/*
	 * Add in WIG bits
	 */

	if ((pteflags & _PAGE_CACHE_CTL) == _PAGE_TOLERANT)
		rflags |= HPTE_R_I;
	else if ((pteflags & _PAGE_CACHE_CTL) == _PAGE_NON_IDEMPOTENT)
		rflags |= (HPTE_R_I | HPTE_R_G);
	else if ((pteflags & _PAGE_CACHE_CTL) == _PAGE_SAO)
		rflags |= (HPTE_R_W | HPTE_R_I | HPTE_R_M);
	else
		/*
		 * Add memory coherence if cache inhibited is not set
		 */
		rflags |= HPTE_R_M;

	rflags |= pte_to_hpte_pkey_bits(pteflags);
	return rflags;
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
		unsigned long vpn  = hpt_vpn(vaddr, vsid, ssize);
		unsigned long tprot = prot;

		/*
		 * If we hit a bad address return error.
		 */
		if (!vsid)
			return -1;
		/* Make kernel text executable */
		if (overlaps_kernel_text(vaddr, vaddr + step))
			tprot &= ~HPTE_R_N;

		/* Make kvm guest trampolines executable */
		if (overlaps_kvm_tmp(vaddr, vaddr + step))
			tprot &= ~HPTE_R_N;

		/*
		 * If relocatable, check if it overlaps interrupt vectors that
		 * are copied down to real 0. For relocatable kernel
		 * (e.g. kdump case) we copy interrupt vectors down to real
		 * address 0. Mark that region as executable. This is
		 * because on p8 system with relocation on exception feature
		 * enabled, exceptions are raised with MMU (IR=DR=1) ON. Hence
		 * in order to execute the interrupt handlers in virtual
		 * mode the vector region need to be marked as executable.
		 */
		if ((PHYSICAL_START > MEMORY_START) &&
			overlaps_interrupt_vector_text(vaddr, vaddr + step))
				tprot &= ~HPTE_R_N;

		hash = hpt_hash(vpn, shift, ssize);
		hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

		BUG_ON(!mmu_hash_ops.hpte_insert);
		ret = mmu_hash_ops.hpte_insert(hpteg, vpn, paddr, tprot,
					       HPTE_V_BOLTED, psize, psize,
					       ssize);

		if (ret < 0)
			break;

#ifdef CONFIG_DEBUG_PAGEALLOC
		if (debug_pagealloc_enabled() &&
			(paddr >> PAGE_SHIFT) < linear_map_hash_count)
			linear_map_hash_slots[paddr >> PAGE_SHIFT] = ret | 0x80;
#endif /* CONFIG_DEBUG_PAGEALLOC */
	}
	return ret < 0 ? ret : 0;
}

int htab_remove_mapping(unsigned long vstart, unsigned long vend,
		      int psize, int ssize)
{
	unsigned long vaddr;
	unsigned int step, shift;
	int rc;
	int ret = 0;

	shift = mmu_psize_defs[psize].shift;
	step = 1 << shift;

	if (!mmu_hash_ops.hpte_removebolted)
		return -ENODEV;

	for (vaddr = vstart; vaddr < vend; vaddr += step) {
		rc = mmu_hash_ops.hpte_removebolted(vaddr, psize, ssize);
		if (rc == -ENOENT) {
			ret = -ENOENT;
			continue;
		}
		if (rc < 0)
			return rc;
	}

	return ret;
}

static bool disable_1tb_segments = false;

static int __init parse_disable_1tb_segments(char *p)
{
	disable_1tb_segments = true;
	return 0;
}
early_param("disable_1tb_segments", parse_disable_1tb_segments);

static int __init htab_dt_scan_seg_sizes(unsigned long node,
					 const char *uname, int depth,
					 void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *prop;
	int size = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "ibm,processor-segment-sizes", &size);
	if (prop == NULL)
		return 0;
	for (; size >= 4; size -= 4, ++prop) {
		if (be32_to_cpu(prop[0]) == 40) {
			DBG("1T segment support detected\n");

			if (disable_1tb_segments) {
				DBG("1T segments disabled by command line\n");
				break;
			}

			cur_cpu_spec->mmu_features |= MMU_FTR_1T_SEGMENT;
			return 1;
		}
	}
	cur_cpu_spec->mmu_features &= ~MMU_FTR_NO_SLBIE_B;
	return 0;
}

static int __init get_idx_from_shift(unsigned int shift)
{
	int idx = -1;

	switch (shift) {
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
		break;
	case 0x22:
		idx = MMU_PAGE_16G;
		break;
	}
	return idx;
}

static int __init htab_dt_scan_page_sizes(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *prop;
	int size = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "ibm,segment-page-sizes", &size);
	if (!prop)
		return 0;

	pr_info("Page sizes from device-tree:\n");
	size /= 4;
	cur_cpu_spec->mmu_features &= ~(MMU_FTR_16M_PAGE);
	while(size > 0) {
		unsigned int base_shift = be32_to_cpu(prop[0]);
		unsigned int slbenc = be32_to_cpu(prop[1]);
		unsigned int lpnum = be32_to_cpu(prop[2]);
		struct mmu_psize_def *def;
		int idx, base_idx;

		size -= 3; prop += 3;
		base_idx = get_idx_from_shift(base_shift);
		if (base_idx < 0) {
			/* skip the pte encoding also */
			prop += lpnum * 2; size -= lpnum * 2;
			continue;
		}
		def = &mmu_psize_defs[base_idx];
		if (base_idx == MMU_PAGE_16M)
			cur_cpu_spec->mmu_features |= MMU_FTR_16M_PAGE;

		def->shift = base_shift;
		if (base_shift <= 23)
			def->avpnm = 0;
		else
			def->avpnm = (1 << (base_shift - 23)) - 1;
		def->sllp = slbenc;
		/*
		 * We don't know for sure what's up with tlbiel, so
		 * for now we only set it for 4K and 64K pages
		 */
		if (base_idx == MMU_PAGE_4K || base_idx == MMU_PAGE_64K)
			def->tlbiel = 1;
		else
			def->tlbiel = 0;

		while (size > 0 && lpnum) {
			unsigned int shift = be32_to_cpu(prop[0]);
			int penc  = be32_to_cpu(prop[1]);

			prop += 2; size -= 2;
			lpnum--;

			idx = get_idx_from_shift(shift);
			if (idx < 0)
				continue;

			if (penc == -1)
				pr_err("Invalid penc for base_shift=%d "
				       "shift=%d\n", base_shift, shift);

			def->penc[idx] = penc;
			pr_info("base_shift=%d: shift=%d, sllp=0x%04lx,"
				" avpnm=0x%08lx, tlbiel=%d, penc=%d\n",
				base_shift, shift, def->sllp,
				def->avpnm, def->tlbiel, def->penc[idx]);
		}
	}

	return 1;
}

#ifdef CONFIG_HUGETLB_PAGE
/* Scan for 16G memory blocks that have been set aside for huge pages
 * and reserve those blocks for 16G huge pages.
 */
static int __init htab_dt_scan_hugepage_blocks(unsigned long node,
					const char *uname, int depth,
					void *data) {
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be64 *addr_prop;
	const __be32 *page_count_prop;
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
	expected_pages = (1 << be32_to_cpu(page_count_prop[0]));
	addr_prop = of_get_flat_dt_prop(node, "reg", NULL);
	if (addr_prop == NULL)
		return 0;
	phys_addr = be64_to_cpu(addr_prop[0]);
	block_size = be64_to_cpu(addr_prop[1]);
	if (block_size != (16 * GB))
		return 0;
	printk(KERN_INFO "Huge page(16GB) memory: "
			"addr = 0x%lX size = 0x%lX pages = %d\n",
			phys_addr, block_size, expected_pages);
	if (phys_addr + block_size * expected_pages <= memblock_end_of_DRAM()) {
		memblock_reserve(phys_addr, block_size * expected_pages);
		pseries_add_gpage(phys_addr, block_size, expected_pages);
	}
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE */

static void mmu_psize_set_default_penc(void)
{
	int bpsize, apsize;
	for (bpsize = 0; bpsize < MMU_PAGE_COUNT; bpsize++)
		for (apsize = 0; apsize < MMU_PAGE_COUNT; apsize++)
			mmu_psize_defs[bpsize].penc[apsize] = -1;
}

#ifdef CONFIG_PPC_64K_PAGES

static bool might_have_hea(void)
{
	/*
	 * The HEA ethernet adapter requires awareness of the
	 * GX bus. Without that awareness we can easily assume
	 * we will never see an HEA ethernet device.
	 */
#ifdef CONFIG_IBMEBUS
	return !cpu_has_feature(CPU_FTR_ARCH_207S) &&
		firmware_has_feature(FW_FEATURE_SPLPAR);
#else
	return false;
#endif
}

#endif /* #ifdef CONFIG_PPC_64K_PAGES */

static void __init htab_scan_page_sizes(void)
{
	int rc;

	/* se the invalid penc to -1 */
	mmu_psize_set_default_penc();

	/* Default to 4K pages only */
	memcpy(mmu_psize_defs, mmu_psize_defaults,
	       sizeof(mmu_psize_defaults));

	/*
	 * Try to find the available page sizes in the device-tree
	 */
	rc = of_scan_flat_dt(htab_dt_scan_page_sizes, NULL);
	if (rc == 0 && early_mmu_has_feature(MMU_FTR_16M_PAGE)) {
		/*
		 * Nothing in the device-tree, but the CPU supports 16M pages,
		 * so let's fallback on a known size list for 16M capable CPUs.
		 */
		memcpy(mmu_psize_defs, mmu_psize_defaults_gp,
		       sizeof(mmu_psize_defaults_gp));
	}

#ifdef CONFIG_HUGETLB_PAGE
	if (!hugetlb_disabled) {
		/* Reserve 16G huge page memory sections for huge pages */
		of_scan_flat_dt(htab_dt_scan_hugepage_blocks, NULL);
	}
#endif /* CONFIG_HUGETLB_PAGE */
}

/*
 * Fill in the hpte_page_sizes[] array.
 * We go through the mmu_psize_defs[] array looking for all the
 * supported base/actual page size combinations.  Each combination
 * has a unique pagesize encoding (penc) value in the low bits of
 * the LP field of the HPTE.  For actual page sizes less than 1MB,
 * some of the upper LP bits are used for RPN bits, meaning that
 * we need to fill in several entries in hpte_page_sizes[].
 *
 * In diagrammatic form, with r = RPN bits and z = page size bits:
 *        PTE LP     actual page size
 *    rrrr rrrz		>=8KB
 *    rrrr rrzz		>=16KB
 *    rrrr rzzz		>=32KB
 *    rrrr zzzz		>=64KB
 *    ...
 *
 * The zzzz bits are implementation-specific but are chosen so that
 * no encoding for a larger page size uses the same value in its
 * low-order N bits as the encoding for the 2^(12+N) byte page size
 * (if it exists).
 */
static void init_hpte_page_sizes(void)
{
	long int ap, bp;
	long int shift, penc;

	for (bp = 0; bp < MMU_PAGE_COUNT; ++bp) {
		if (!mmu_psize_defs[bp].shift)
			continue;	/* not a supported page size */
		for (ap = bp; ap < MMU_PAGE_COUNT; ++ap) {
			penc = mmu_psize_defs[bp].penc[ap];
			if (penc == -1 || !mmu_psize_defs[ap].shift)
				continue;
			shift = mmu_psize_defs[ap].shift - LP_SHIFT;
			if (shift <= 0)
				continue;	/* should never happen */
			/*
			 * For page sizes less than 1MB, this loop
			 * replicates the entry for all possible values
			 * of the rrrr bits.
			 */
			while (penc < (1 << LP_BITS)) {
				hpte_page_sizes[penc] = (ap << 4) | bp;
				penc += 1 << shift;
			}
		}
	}
}

static void __init htab_init_page_sizes(void)
{
	init_hpte_page_sizes();

	if (!debug_pagealloc_enabled()) {
		/*
		 * Pick a size for the linear mapping. Currently, we only
		 * support 16M, 1M and 4K which is the default
		 */
		if (mmu_psize_defs[MMU_PAGE_16M].shift)
			mmu_linear_psize = MMU_PAGE_16M;
		else if (mmu_psize_defs[MMU_PAGE_1M].shift)
			mmu_linear_psize = MMU_PAGE_1M;
	}

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
		if (mmu_has_feature(MMU_FTR_CI_LARGE_PAGE)) {
			/*
			 * When running on pSeries using 64k pages for ioremap
			 * would stop us accessing the HEA ethernet. So if we
			 * have the chance of ever seeing one, stay at 4k.
			 */
			if (!might_have_hea())
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
}

static int __init htab_dt_scan_pftsize(unsigned long node,
				       const char *uname, int depth,
				       void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *prop;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "ibm,pft-size", NULL);
	if (prop != NULL) {
		/* pft_size[0] is the NUMA CEC cookie */
		ppc64_pft_size = be32_to_cpu(prop[1]);
		return 1;
	}
	return 0;
}

unsigned htab_shift_for_mem_size(unsigned long mem_size)
{
	unsigned memshift = __ilog2(mem_size);
	unsigned pshift = mmu_psize_defs[mmu_virtual_psize].shift;
	unsigned pteg_shift;

	/* round mem_size up to next power of 2 */
	if ((1UL << memshift) < mem_size)
		memshift += 1;

	/* aim for 2 pages / pteg */
	pteg_shift = memshift - (pshift + 1);

	/*
	 * 2^11 PTEGS of 128 bytes each, ie. 2^18 bytes is the minimum htab
	 * size permitted by the architecture.
	 */
	return max(pteg_shift + 7, 18U);
}

static unsigned long __init htab_get_table_size(void)
{
	/* If hash size isn't already provided by the platform, we try to
	 * retrieve it from the device-tree. If it's not there neither, we
	 * calculate it now based on the total RAM size
	 */
	if (ppc64_pft_size == 0)
		of_scan_flat_dt(htab_dt_scan_pftsize, NULL);
	if (ppc64_pft_size)
		return 1UL << ppc64_pft_size;

	return 1UL << htab_shift_for_mem_size(memblock_phys_mem_size());
}

#ifdef CONFIG_MEMORY_HOTPLUG
void resize_hpt_for_hotplug(unsigned long new_mem_size)
{
	unsigned target_hpt_shift;

	if (!mmu_hash_ops.resize_hpt)
		return;

	target_hpt_shift = htab_shift_for_mem_size(new_mem_size);

	/*
	 * To avoid lots of HPT resizes if memory size is fluctuating
	 * across a boundary, we deliberately have some hysterisis
	 * here: we immediately increase the HPT size if the target
	 * shift exceeds the current shift, but we won't attempt to
	 * reduce unless the target shift is at least 2 below the
	 * current shift
	 */
	if ((target_hpt_shift > ppc64_pft_size)
	    || (target_hpt_shift < (ppc64_pft_size - 1))) {
		int rc;

		rc = mmu_hash_ops.resize_hpt(target_hpt_shift);
		if (rc && (rc != -ENODEV))
			printk(KERN_WARNING
			       "Unable to resize hash page table to target order %d: %d\n",
			       target_hpt_shift, rc);
	}
}

int hash__create_section_mapping(unsigned long start, unsigned long end, int nid)
{
	int rc = htab_bolt_mapping(start, end, __pa(start),
				   pgprot_val(PAGE_KERNEL), mmu_linear_psize,
				   mmu_kernel_ssize);

	if (rc < 0) {
		int rc2 = htab_remove_mapping(start, end, mmu_linear_psize,
					      mmu_kernel_ssize);
		BUG_ON(rc2 && (rc2 != -ENOENT));
	}
	return rc;
}

int hash__remove_section_mapping(unsigned long start, unsigned long end)
{
	int rc = htab_remove_mapping(start, end, mmu_linear_psize,
				     mmu_kernel_ssize);
	WARN_ON(rc < 0);
	return rc;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

static void __init hash_init_partition_table(phys_addr_t hash_table,
					     unsigned long htab_size)
{
	mmu_partition_table_init();

	/*
	 * PS field (VRMA page size) is not used for LPID 0, hence set to 0.
	 * For now, UPRT is 0 and we have no segment table.
	 */
	htab_size =  __ilog2(htab_size) - 18;
	mmu_partition_table_set_entry(0, hash_table | htab_size, 0);
	pr_info("Partition table %p\n", partition_tb);
}

static void __init htab_initialize(void)
{
	unsigned long table;
	unsigned long pteg_count;
	unsigned long prot;
	unsigned long base = 0, size = 0;
	struct memblock_region *reg;

	DBG(" -> htab_initialize()\n");

	if (mmu_has_feature(MMU_FTR_1T_SEGMENT)) {
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

	if (firmware_has_feature(FW_FEATURE_LPAR) ||
	    firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		/* Using a hypervisor which owns the htab */
		htab_address = NULL;
		_SDR1 = 0; 
		/*
		 * On POWER9, we need to do a H_REGISTER_PROC_TBL hcall
		 * to inform the hypervisor that we wish to use the HPT.
		 */
		if (cpu_has_feature(CPU_FTR_ARCH_300))
			register_process_table(0, 0, 0);
#ifdef CONFIG_FA_DUMP
		/*
		 * If firmware assisted dump is active firmware preserves
		 * the contents of htab along with entire partition memory.
		 * Clear the htab if firmware assisted dump is active so
		 * that we dont end up using old mappings.
		 */
		if (is_fadump_active() && mmu_hash_ops.hpte_clear_all)
			mmu_hash_ops.hpte_clear_all();
#endif
	} else {
		unsigned long limit = MEMBLOCK_ALLOC_ANYWHERE;

#ifdef CONFIG_PPC_CELL
		/*
		 * Cell may require the hash table down low when using the
		 * Axon IOMMU in order to fit the dynamic region over it, see
		 * comments in cell/iommu.c
		 */
		if (fdt_subnode_offset(initial_boot_params, 0, "axon") > 0) {
			limit = 0x80000000;
			pr_info("Hash table forced below 2G for Axon IOMMU\n");
		}
#endif /* CONFIG_PPC_CELL */

		table = memblock_alloc_base(htab_size_bytes, htab_size_bytes,
					    limit);

		DBG("Hash table allocated at %lx, size: %lx\n", table,
		    htab_size_bytes);

		htab_address = __va(table);

		/* htab absolute addr + encoded htabsize */
		_SDR1 = table + __ilog2(htab_size_bytes) - 18;

		/* Initialize the HPT with no entries */
		memset((void *)table, 0, htab_size_bytes);

		if (!cpu_has_feature(CPU_FTR_ARCH_300))
			/* Set SDR1 */
			mtspr(SPRN_SDR1, _SDR1);
		else
			hash_init_partition_table(table, htab_size_bytes);
	}

	prot = pgprot_val(PAGE_KERNEL);

#ifdef CONFIG_DEBUG_PAGEALLOC
	if (debug_pagealloc_enabled()) {
		linear_map_hash_count = memblock_end_of_DRAM() >> PAGE_SHIFT;
		linear_map_hash_slots = __va(memblock_alloc_base(
				linear_map_hash_count, 1, ppc64_rma_size));
		memset(linear_map_hash_slots, 0, linear_map_hash_count);
	}
#endif /* CONFIG_DEBUG_PAGEALLOC */

	/* create bolted the linear mapping in the hash table */
	for_each_memblock(memory, reg) {
		base = (unsigned long)__va(reg->base);
		size = reg->size;

		DBG("creating mapping for region: %lx..%lx (prot: %lx)\n",
		    base, size, prot);

		BUG_ON(htab_bolt_mapping(base, base + size, __pa(base),
				prot, mmu_linear_psize, mmu_kernel_ssize));
	}
	memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);

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


	DBG(" <- htab_initialize()\n");
}
#undef KB
#undef MB

void __init hash__early_init_devtree(void)
{
	/* Initialize segment sizes */
	of_scan_flat_dt(htab_dt_scan_seg_sizes, NULL);

	/* Initialize page sizes */
	htab_scan_page_sizes();
}

void __init hash__early_init_mmu(void)
{
#ifndef CONFIG_PPC_64K_PAGES
	/*
	 * We have code in __hash_page_4K() and elsewhere, which assumes it can
	 * do the following:
	 *   new_pte |= (slot << H_PAGE_F_GIX_SHIFT) & (H_PAGE_F_SECOND | H_PAGE_F_GIX);
	 *
	 * Where the slot number is between 0-15, and values of 8-15 indicate
	 * the secondary bucket. For that code to work H_PAGE_F_SECOND and
	 * H_PAGE_F_GIX must occupy four contiguous bits in the PTE, and
	 * H_PAGE_F_SECOND must be placed above H_PAGE_F_GIX. Assert that here
	 * with a BUILD_BUG_ON().
	 */
	BUILD_BUG_ON(H_PAGE_F_SECOND != (1ul  << (H_PAGE_F_GIX_SHIFT + 3)));
#endif /* CONFIG_PPC_64K_PAGES */

	htab_init_page_sizes();

	/*
	 * initialize page table size
	 */
	__pte_frag_nr = H_PTE_FRAG_NR;
	__pte_frag_size_shift = H_PTE_FRAG_SIZE_SHIFT;
	__pmd_frag_nr = H_PMD_FRAG_NR;
	__pmd_frag_size_shift = H_PMD_FRAG_SIZE_SHIFT;

	__pte_index_size = H_PTE_INDEX_SIZE;
	__pmd_index_size = H_PMD_INDEX_SIZE;
	__pud_index_size = H_PUD_INDEX_SIZE;
	__pgd_index_size = H_PGD_INDEX_SIZE;
	__pud_cache_index = H_PUD_CACHE_INDEX;
	__pte_table_size = H_PTE_TABLE_SIZE;
	__pmd_table_size = H_PMD_TABLE_SIZE;
	__pud_table_size = H_PUD_TABLE_SIZE;
	__pgd_table_size = H_PGD_TABLE_SIZE;
	/*
	 * 4k use hugepd format, so for hash set then to
	 * zero
	 */
	__pmd_val_bits = HASH_PMD_VAL_BITS;
	__pud_val_bits = HASH_PUD_VAL_BITS;
	__pgd_val_bits = HASH_PGD_VAL_BITS;

	__kernel_virt_start = H_KERN_VIRT_START;
	__kernel_virt_size = H_KERN_VIRT_SIZE;
	__vmalloc_start = H_VMALLOC_START;
	__vmalloc_end = H_VMALLOC_END;
	__kernel_io_start = H_KERN_IO_START;
	vmemmap = (struct page *)H_VMEMMAP_BASE;
	ioremap_bot = IOREMAP_BASE;

#ifdef CONFIG_PCI
	pci_io_base = ISA_IO_BASE;
#endif

	/* Select appropriate backend */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1))
		ps3_early_mm_init();
	else if (firmware_has_feature(FW_FEATURE_LPAR))
		hpte_init_pseries();
	else if (IS_ENABLED(CONFIG_PPC_NATIVE))
		hpte_init_native();

	if (!mmu_hash_ops.hpte_insert)
		panic("hash__early_init_mmu: No MMU hash ops defined!\n");

	/* Initialize the MMU Hash table and create the linear mapping
	 * of memory. Has to be done before SLB initialization as this is
	 * currently where the page size encoding is obtained.
	 */
	htab_initialize();

	pr_info("Initializing hash mmu with SLB\n");
	/* Initialize SLB management */
	slb_initialize();

	if (cpu_has_feature(CPU_FTR_ARCH_206)
			&& cpu_has_feature(CPU_FTR_HVMODE))
		tlbiel_all();
}

#ifdef CONFIG_SMP
void hash__early_init_mmu_secondary(void)
{
	/* Initialize hash table for that CPU */
	if (!firmware_has_feature(FW_FEATURE_LPAR)) {

		if (!cpu_has_feature(CPU_FTR_ARCH_300))
			mtspr(SPRN_SDR1, _SDR1);
		else
			mtspr(SPRN_PTCR,
			      __pa(partition_tb) | (PATB_SIZE_SHIFT - 12));
	}
	/* Initialize SLB */
	slb_initialize();

	if (cpu_has_feature(CPU_FTR_ARCH_206)
			&& cpu_has_feature(CPU_FTR_HVMODE))
		tlbiel_all();
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
static unsigned int get_paca_psize(unsigned long addr)
{
	unsigned char *psizes;
	unsigned long index, mask_index;

	if (addr < SLICE_LOW_TOP) {
		psizes = get_paca()->mm_ctx_low_slices_psize;
		index = GET_LOW_SLICE_INDEX(addr);
	} else {
		psizes = get_paca()->mm_ctx_high_slices_psize;
		index = GET_HIGH_SLICE_INDEX(addr);
	}
	mask_index = index & 0x1;
	return (psizes[index >> 1] >> (mask_index * 4)) & 0xF;
}

#else
unsigned int get_paca_psize(unsigned long addr)
{
	return get_paca()->mm_ctx_user_psize;
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
	copro_flush_all_slbs(mm);
	if ((get_paca_psize(addr) != MMU_PAGE_4K) && (current->mm == mm)) {

		copy_mm_to_paca(mm);
		slb_flush_and_restore_bolted();
	}
}
#endif /* CONFIG_PPC_64K_PAGES */

#ifdef CONFIG_PPC_SUBPAGE_PROT
/*
 * This looks up a 2-bit protection code for a 4k subpage of a 64k page.
 * Userspace sets the subpage permissions using the subpage_prot system call.
 *
 * Result is 0: full permissions, _PAGE_RW: read-only,
 * _PAGE_RWX: no access.
 */
static int subpage_protection(struct mm_struct *mm, unsigned long ea)
{
	struct subpage_prot_table *spt = &mm->context.spt;
	u32 spp = 0;
	u32 **sbpm, *sbpp;

	if (ea >= spt->maxaddr)
		return 0;
	if (ea < 0x100000000UL) {
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

	/*
	 * 0 -> full premission
	 * 1 -> Read only
	 * 2 -> no access.
	 * We return the flag that need to be cleared.
	 */
	spp = ((spp & 2) ? _PAGE_RWX : 0) | ((spp & 1) ? _PAGE_WRITE : 0);
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
			int ssize, int psize, int lpsize, unsigned long pte)
{
	if (!printk_ratelimit())
		return;
	pr_info("mm: Hashing failure ! EA=0x%lx access=0x%lx current=%s\n",
		ea, access, current->comm);
	pr_info("    trap=0x%lx vsid=0x%lx ssize=%d base psize=%d psize %d pte=0x%lx\n",
		trap, vsid, ssize, psize, lpsize, pte);
}

static void check_paca_psize(unsigned long ea, struct mm_struct *mm,
			     int psize, bool user_region)
{
	if (user_region) {
		if (psize != get_paca_psize(ea)) {
			copy_mm_to_paca(mm);
			slb_flush_and_restore_bolted();
		}
	} else if (get_paca()->vmalloc_sllp !=
		   mmu_psize_defs[mmu_vmalloc_psize].sllp) {
		get_paca()->vmalloc_sllp =
			mmu_psize_defs[mmu_vmalloc_psize].sllp;
		slb_vmalloc_update();
	}
}

/* Result code is:
 *  0 - handled
 *  1 - normal page fault
 * -1 - critical hash insertion error
 * -2 - access not permitted by subpage protection mechanism
 */
int hash_page_mm(struct mm_struct *mm, unsigned long ea,
		 unsigned long access, unsigned long trap,
		 unsigned long flags)
{
	bool is_thp;
	enum ctx_state prev_state = exception_enter();
	pgd_t *pgdir;
	unsigned long vsid;
	pte_t *ptep;
	unsigned hugeshift;
	int rc, user_region = 0;
	int psize, ssize;

	DBG_LOW("hash_page(ea=%016lx, access=%lx, trap=%lx\n",
		ea, access, trap);
	trace_hash_fault(ea, access, trap);

	/* Get region & vsid */
 	switch (REGION_ID(ea)) {
	case USER_REGION_ID:
		user_region = 1;
		if (! mm) {
			DBG_LOW(" user region with no mm !\n");
			rc = 1;
			goto bail;
		}
		psize = get_slice_psize(mm, ea);
		ssize = user_segment_size(ea);
		vsid = get_user_vsid(&mm->context, ea, ssize);
		break;
	case VMALLOC_REGION_ID:
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
		rc = 1;
		goto bail;
	}
	DBG_LOW(" mm=%p, mm->pgdir=%p, vsid=%016lx\n", mm, mm->pgd, vsid);

	/* Bad address. */
	if (!vsid) {
		DBG_LOW("Bad address!\n");
		rc = 1;
		goto bail;
	}
	/* Get pgdir */
	pgdir = mm->pgd;
	if (pgdir == NULL) {
		rc = 1;
		goto bail;
	}

	/* Check CPU locality */
	if (user_region && mm_is_thread_local(mm))
		flags |= HPTE_LOCAL_UPDATE;

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
	ptep = find_linux_pte(pgdir, ea, &is_thp, &hugeshift);
	if (ptep == NULL || !pte_present(*ptep)) {
		DBG_LOW(" no PTE !\n");
		rc = 1;
		goto bail;
	}

	/* Add _PAGE_PRESENT to the required access perm */
	access |= _PAGE_PRESENT;

	/* Pre-check access permissions (will be re-checked atomically
	 * in __hash_page_XX but this pre-check is a fast path
	 */
	if (!check_pte_access(access, pte_val(*ptep))) {
		DBG_LOW(" no access !\n");
		rc = 1;
		goto bail;
	}

	if (hugeshift) {
		if (is_thp)
			rc = __hash_page_thp(ea, access, vsid, (pmd_t *)ptep,
					     trap, flags, ssize, psize);
#ifdef CONFIG_HUGETLB_PAGE
		else
			rc = __hash_page_huge(ea, access, vsid, ptep, trap,
					      flags, ssize, hugeshift, psize);
#else
		else {
			/*
			 * if we have hugeshift, and is not transhuge with
			 * hugetlb disabled, something is really wrong.
			 */
			rc = 1;
			WARN_ON(1);
		}
#endif
		if (current->mm == mm)
			check_paca_psize(ea, mm, psize, user_region);

		goto bail;
	}

#ifndef CONFIG_PPC_64K_PAGES
	DBG_LOW(" i-pte: %016lx\n", pte_val(*ptep));
#else
	DBG_LOW(" i-pte: %016lx %016lx\n", pte_val(*ptep),
		pte_val(*(ptep + PTRS_PER_PTE)));
#endif
	/* Do actual hashing */
#ifdef CONFIG_PPC_64K_PAGES
	/* If H_PAGE_4K_PFN is set, make sure this is a 4k segment */
	if ((pte_val(*ptep) & H_PAGE_4K_PFN) && psize == MMU_PAGE_64K) {
		demote_segment_4k(mm, ea);
		psize = MMU_PAGE_4K;
	}

	/* If this PTE is non-cacheable and we have restrictions on
	 * using non cacheable large pages, then we switch to 4k
	 */
	if (mmu_ci_restrictions && psize == MMU_PAGE_64K && pte_ci(*ptep)) {
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
			copro_flush_all_slbs(mm);
		}
	}

#endif /* CONFIG_PPC_64K_PAGES */

	if (current->mm == mm)
		check_paca_psize(ea, mm, psize, user_region);

#ifdef CONFIG_PPC_64K_PAGES
	if (psize == MMU_PAGE_64K)
		rc = __hash_page_64K(ea, access, vsid, ptep, trap,
				     flags, ssize);
	else
#endif /* CONFIG_PPC_64K_PAGES */
	{
		int spp = subpage_protection(mm, ea);
		if (access & spp)
			rc = -2;
		else
			rc = __hash_page_4K(ea, access, vsid, ptep, trap,
					    flags, ssize, spp);
	}

	/* Dump some info in case of hash insertion failure, they should
	 * never happen so it is really useful to know if/when they do
	 */
	if (rc == -1)
		hash_failure_debug(ea, access, vsid, trap, ssize, psize,
				   psize, pte_val(*ptep));
#ifndef CONFIG_PPC_64K_PAGES
	DBG_LOW(" o-pte: %016lx\n", pte_val(*ptep));
#else
	DBG_LOW(" o-pte: %016lx %016lx\n", pte_val(*ptep),
		pte_val(*(ptep + PTRS_PER_PTE)));
#endif
	DBG_LOW(" -> rc=%d\n", rc);

bail:
	exception_exit(prev_state);
	return rc;
}
EXPORT_SYMBOL_GPL(hash_page_mm);

int hash_page(unsigned long ea, unsigned long access, unsigned long trap,
	      unsigned long dsisr)
{
	unsigned long flags = 0;
	struct mm_struct *mm = current->mm;

	if (REGION_ID(ea) == VMALLOC_REGION_ID)
		mm = &init_mm;

	if (dsisr & DSISR_NOHPTE)
		flags |= HPTE_NOHPTE_UPDATE;

	return hash_page_mm(mm, ea, access, trap, flags);
}
EXPORT_SYMBOL_GPL(hash_page);

int __hash_page(unsigned long ea, unsigned long msr, unsigned long trap,
		unsigned long dsisr)
{
	unsigned long access = _PAGE_PRESENT | _PAGE_READ;
	unsigned long flags = 0;
	struct mm_struct *mm = current->mm;

	if (REGION_ID(ea) == VMALLOC_REGION_ID)
		mm = &init_mm;

	if (dsisr & DSISR_NOHPTE)
		flags |= HPTE_NOHPTE_UPDATE;

	if (dsisr & DSISR_ISSTORE)
		access |= _PAGE_WRITE;
	/*
	 * We set _PAGE_PRIVILEGED only when
	 * kernel mode access kernel space.
	 *
	 * _PAGE_PRIVILEGED is NOT set
	 * 1) when kernel mode access user space
	 * 2) user space access kernel space.
	 */
	access |= _PAGE_PRIVILEGED;
	if ((msr & MSR_PR) || (REGION_ID(ea) == USER_REGION_ID))
		access &= ~_PAGE_PRIVILEGED;

	if (trap == 0x400)
		access |= _PAGE_EXEC;

	return hash_page_mm(mm, ea, access, trap, flags);
}

#ifdef CONFIG_PPC_MM_SLICES
static bool should_hash_preload(struct mm_struct *mm, unsigned long ea)
{
	int psize = get_slice_psize(mm, ea);

	/* We only prefault standard pages for now */
	if (unlikely(psize != mm->context.user_psize))
		return false;

	/*
	 * Don't prefault if subpage protection is enabled for the EA.
	 */
	if (unlikely((psize == MMU_PAGE_4K) && subpage_protection(mm, ea)))
		return false;

	return true;
}
#else
static bool should_hash_preload(struct mm_struct *mm, unsigned long ea)
{
	return true;
}
#endif

void hash_preload(struct mm_struct *mm, unsigned long ea,
		  bool is_exec, unsigned long trap)
{
	int hugepage_shift;
	unsigned long vsid;
	pgd_t *pgdir;
	pte_t *ptep;
	unsigned long flags;
	int rc, ssize, update_flags = 0;
	unsigned long access = _PAGE_PRESENT | _PAGE_READ | (is_exec ? _PAGE_EXEC : 0);

	BUG_ON(REGION_ID(ea) != USER_REGION_ID);

	if (!should_hash_preload(mm, ea))
		return;

	DBG_LOW("hash_preload(mm=%p, mm->pgdir=%p, ea=%016lx, access=%lx,"
		" trap=%lx\n", mm, mm->pgd, ea, access, trap);

	/* Get Linux PTE if available */
	pgdir = mm->pgd;
	if (pgdir == NULL)
		return;

	/* Get VSID */
	ssize = user_segment_size(ea);
	vsid = get_user_vsid(&mm->context, ea, ssize);
	if (!vsid)
		return;
	/*
	 * Hash doesn't like irqs. Walking linux page table with irq disabled
	 * saves us from holding multiple locks.
	 */
	local_irq_save(flags);

	/*
	 * THP pages use update_mmu_cache_pmd. We don't do
	 * hash preload there. Hence can ignore THP here
	 */
	ptep = find_current_mm_pte(pgdir, ea, NULL, &hugepage_shift);
	if (!ptep)
		goto out_exit;

	WARN_ON(hugepage_shift);
#ifdef CONFIG_PPC_64K_PAGES
	/* If either H_PAGE_4K_PFN or cache inhibited is set (and we are on
	 * a 64K kernel), then we don't preload, hash_page() will take
	 * care of it once we actually try to access the page.
	 * That way we don't have to duplicate all of the logic for segment
	 * page size demotion here
	 */
	if ((pte_val(*ptep) & H_PAGE_4K_PFN) || pte_ci(*ptep))
		goto out_exit;
#endif /* CONFIG_PPC_64K_PAGES */

	/* Is that local to this CPU ? */
	if (mm_is_thread_local(mm))
		update_flags |= HPTE_LOCAL_UPDATE;

	/* Hash it in */
#ifdef CONFIG_PPC_64K_PAGES
	if (mm->context.user_psize == MMU_PAGE_64K)
		rc = __hash_page_64K(ea, access, vsid, ptep, trap,
				     update_flags, ssize);
	else
#endif /* CONFIG_PPC_64K_PAGES */
		rc = __hash_page_4K(ea, access, vsid, ptep, trap, update_flags,
				    ssize, subpage_protection(mm, ea));

	/* Dump some info in case of hash insertion failure, they should
	 * never happen so it is really useful to know if/when they do
	 */
	if (rc == -1)
		hash_failure_debug(ea, access, vsid, trap, ssize,
				   mm->context.user_psize,
				   mm->context.user_psize,
				   pte_val(*ptep));
out_exit:
	local_irq_restore(flags);
}

#ifdef CONFIG_PPC_MEM_KEYS
/*
 * Return the protection key associated with the given address and the
 * mm_struct.
 */
u16 get_mm_addr_key(struct mm_struct *mm, unsigned long address)
{
	pte_t *ptep;
	u16 pkey = 0;
	unsigned long flags;

	if (!mm || !mm->pgd)
		return 0;

	local_irq_save(flags);
	ptep = find_linux_pte(mm->pgd, address, NULL, NULL);
	if (ptep)
		pkey = pte_to_pkey_bits(pte_val(READ_ONCE(*ptep)));
	local_irq_restore(flags);

	return pkey;
}
#endif /* CONFIG_PPC_MEM_KEYS */

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static inline void tm_flush_hash_page(int local)
{
	/*
	 * Transactions are not aborted by tlbiel, only tlbie. Without, syncing a
	 * page back to a block device w/PIO could pick up transactional data
	 * (bad!) so we force an abort here. Before the sync the page will be
	 * made read-only, which will flush_hash_page. BIG ISSUE here: if the
	 * kernel uses a page from userspace without unmapping it first, it may
	 * see the speculated version.
	 */
	if (local && cpu_has_feature(CPU_FTR_TM) && current->thread.regs &&
	    MSR_TM_ACTIVE(current->thread.regs->msr)) {
		tm_enable();
		tm_abort(TM_CAUSE_TLBI);
	}
}
#else
static inline void tm_flush_hash_page(int local)
{
}
#endif

/*
 * Return the global hash slot, corresponding to the given PTE, which contains
 * the HPTE.
 */
unsigned long pte_get_hash_gslot(unsigned long vpn, unsigned long shift,
		int ssize, real_pte_t rpte, unsigned int subpg_index)
{
	unsigned long hash, gslot, hidx;

	hash = hpt_hash(vpn, shift, ssize);
	hidx = __rpte_to_hidx(rpte, subpg_index);
	if (hidx & _PTEIDX_SECONDARY)
		hash = ~hash;
	gslot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	gslot += hidx & _PTEIDX_GROUP_IX;
	return gslot;
}

/* WARNING: This is called from hash_low_64.S, if you change this prototype,
 *          do not forget to update the assembly call site !
 */
void flush_hash_page(unsigned long vpn, real_pte_t pte, int psize, int ssize,
		     unsigned long flags)
{
	unsigned long index, shift, gslot;
	int local = flags & HPTE_LOCAL_UPDATE;

	DBG_LOW("flush_hash_page(vpn=%016lx)\n", vpn);
	pte_iterate_hashed_subpages(pte, psize, vpn, index, shift) {
		gslot = pte_get_hash_gslot(vpn, shift, ssize, pte, index);
		DBG_LOW(" sub %ld: gslot=%lx\n", index, gslot);
		/*
		 * We use same base page size and actual psize, because we don't
		 * use these functions for hugepage
		 */
		mmu_hash_ops.hpte_invalidate(gslot, vpn, psize, psize,
					     ssize, local);
	} pte_iterate_hashed_end();

	tm_flush_hash_page(local);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void flush_hash_hugepage(unsigned long vsid, unsigned long addr,
			 pmd_t *pmdp, unsigned int psize, int ssize,
			 unsigned long flags)
{
	int i, max_hpte_count, valid;
	unsigned long s_addr;
	unsigned char *hpte_slot_array;
	unsigned long hidx, shift, vpn, hash, slot;
	int local = flags & HPTE_LOCAL_UPDATE;

	s_addr = addr & HPAGE_PMD_MASK;
	hpte_slot_array = get_hpte_slot_array(pmdp);
	/*
	 * IF we try to do a HUGE PTE update after a withdraw is done.
	 * we will find the below NULL. This happens when we do
	 * split_huge_page_pmd
	 */
	if (!hpte_slot_array)
		return;

	if (mmu_hash_ops.hugepage_invalidate) {
		mmu_hash_ops.hugepage_invalidate(vsid, s_addr, hpte_slot_array,
						 psize, ssize, local);
		goto tm_abort;
	}
	/*
	 * No bluk hpte removal support, invalidate each entry
	 */
	shift = mmu_psize_defs[psize].shift;
	max_hpte_count = HPAGE_PMD_SIZE >> shift;
	for (i = 0; i < max_hpte_count; i++) {
		/*
		 * 8 bits per each hpte entries
		 * 000| [ secondary group (one bit) | hidx (3 bits) | valid bit]
		 */
		valid = hpte_valid(hpte_slot_array, i);
		if (!valid)
			continue;
		hidx =  hpte_hash_index(hpte_slot_array, i);

		/* get the vpn */
		addr = s_addr + (i * (1ul << shift));
		vpn = hpt_vpn(addr, vsid, ssize);
		hash = hpt_hash(vpn, shift, ssize);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;

		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;
		mmu_hash_ops.hpte_invalidate(slot, vpn, psize,
					     MMU_PAGE_16M, ssize, local);
	}
tm_abort:
	tm_flush_hash_page(local);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

void flush_hash_range(unsigned long number, int local)
{
	if (mmu_hash_ops.flush_hash_range)
		mmu_hash_ops.flush_hash_range(number, local);
	else {
		int i;
		struct ppc64_tlb_batch *batch =
			this_cpu_ptr(&ppc64_tlb_batch);

		for (i = 0; i < number; i++)
			flush_hash_page(batch->vpn[i], batch->pte[i],
					batch->psize, batch->ssize, local);
	}
}

/*
 * low_hash_fault is called when we the low level hash code failed
 * to instert a PTE due to an hypervisor error
 */
void low_hash_fault(struct pt_regs *regs, unsigned long address, int rc)
{
	enum ctx_state prev_state = exception_enter();

	if (user_mode(regs)) {
#ifdef CONFIG_PPC_SUBPAGE_PROT
		if (rc == -2)
			_exception(SIGSEGV, regs, SEGV_ACCERR, address);
		else
#endif
			_exception(SIGBUS, regs, BUS_ADRERR, address);
	} else
		bad_page_fault(regs, address, SIGBUS);

	exception_exit(prev_state);
}

long hpte_insert_repeating(unsigned long hash, unsigned long vpn,
			   unsigned long pa, unsigned long rflags,
			   unsigned long vflags, int psize, int ssize)
{
	unsigned long hpte_group;
	long slot;

repeat:
	hpte_group = (hash & htab_hash_mask) * HPTES_PER_GROUP;

	/* Insert into the hash table, primary slot */
	slot = mmu_hash_ops.hpte_insert(hpte_group, vpn, pa, rflags, vflags,
					psize, psize, ssize);

	/* Primary is full, try the secondary */
	if (unlikely(slot == -1)) {
		hpte_group = (~hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot = mmu_hash_ops.hpte_insert(hpte_group, vpn, pa, rflags,
						vflags | HPTE_V_SECONDARY,
						psize, psize, ssize);
		if (slot == -1) {
			if (mftb() & 0x1)
				hpte_group = (hash & htab_hash_mask) *
						HPTES_PER_GROUP;

			mmu_hash_ops.hpte_remove(hpte_group);
			goto repeat;
		}
	}

	return slot;
}

#ifdef CONFIG_DEBUG_PAGEALLOC
static void kernel_map_linear_page(unsigned long vaddr, unsigned long lmi)
{
	unsigned long hash;
	unsigned long vsid = get_kernel_vsid(vaddr, mmu_kernel_ssize);
	unsigned long vpn = hpt_vpn(vaddr, vsid, mmu_kernel_ssize);
	unsigned long mode = htab_convert_pte_flags(pgprot_val(PAGE_KERNEL));
	long ret;

	hash = hpt_hash(vpn, PAGE_SHIFT, mmu_kernel_ssize);

	/* Don't create HPTE entries for bad address */
	if (!vsid)
		return;

	ret = hpte_insert_repeating(hash, vpn, __pa(vaddr), mode,
				    HPTE_V_BOLTED,
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
	unsigned long vpn = hpt_vpn(vaddr, vsid, mmu_kernel_ssize);

	hash = hpt_hash(vpn, PAGE_SHIFT, mmu_kernel_ssize);
	spin_lock(&linear_map_hash_lock);
	BUG_ON(!(linear_map_hash_slots[lmi] & 0x80));
	hidx = linear_map_hash_slots[lmi] & 0x7f;
	linear_map_hash_slots[lmi] = 0;
	spin_unlock(&linear_map_hash_lock);
	if (hidx & _PTEIDX_SECONDARY)
		hash = ~hash;
	slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	slot += hidx & _PTEIDX_GROUP_IX;
	mmu_hash_ops.hpte_invalidate(slot, vpn, mmu_linear_psize,
				     mmu_linear_psize,
				     mmu_kernel_ssize, 0);
}

void __kernel_map_pages(struct page *page, int numpages, int enable)
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

void hash__setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);

	/*
	 * On virtualized systems the first entry is our RMA region aka VRMA,
	 * non-virtualized 64-bit hash MMU systems don't have a limitation
	 * on real mode access.
	 *
	 * For guests on platforms before POWER9, we clamp the it limit to 1G
	 * to avoid some funky things such as RTAS bugs etc...
	 */
	if (!early_cpu_has_feature(CPU_FTR_HVMODE)) {
		ppc64_rma_size = first_memblock_size;
		if (!early_cpu_has_feature(CPU_FTR_ARCH_300))
			ppc64_rma_size = min_t(u64, ppc64_rma_size, 0x40000000);

		/* Finally limit subsequent allocations */
		memblock_set_current_limit(ppc64_rma_size);
	} else {
		ppc64_rma_size = ULONG_MAX;
	}
}

#ifdef CONFIG_DEBUG_FS

static int hpt_order_get(void *data, u64 *val)
{
	*val = ppc64_pft_size;
	return 0;
}

static int hpt_order_set(void *data, u64 val)
{
	if (!mmu_hash_ops.resize_hpt)
		return -ENODEV;

	return mmu_hash_ops.resize_hpt(val);
}

DEFINE_SIMPLE_ATTRIBUTE(fops_hpt_order, hpt_order_get, hpt_order_set, "%llu\n");

static int __init hash64_debugfs(void)
{
	if (!debugfs_create_file("hpt_order", 0600, powerpc_debugfs_root,
				 NULL, &fops_hpt_order)) {
		pr_err("lpar: unable to create hpt_order debugsfs file\n");
	}

	return 0;
}
machine_device_initcall(pseries, hash64_debugfs);
#endif /* CONFIG_DEBUG_FS */

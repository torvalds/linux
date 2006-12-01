/*
 *  linux/arch/arm/mm/mmu.c
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mman.h>
#include <linux/nodemask.h>

#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/tlb.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

extern void _stext, _etext, __data_start, _end;
extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;

/*
 * The pmd table for the upper-most set of pages.
 */
pmd_t *top_pmd;

#define CPOLICY_UNCACHED	0
#define CPOLICY_BUFFERED	1
#define CPOLICY_WRITETHROUGH	2
#define CPOLICY_WRITEBACK	3
#define CPOLICY_WRITEALLOC	4

static unsigned int cachepolicy __initdata = CPOLICY_WRITEBACK;
static unsigned int ecc_mask __initdata = 0;
pgprot_t pgprot_kernel;

EXPORT_SYMBOL(pgprot_kernel);

struct cachepolicy {
	const char	policy[16];
	unsigned int	cr_mask;
	unsigned int	pmd;
	unsigned int	pte;
};

static struct cachepolicy cache_policies[] __initdata = {
	{
		.policy		= "uncached",
		.cr_mask	= CR_W|CR_C,
		.pmd		= PMD_SECT_UNCACHED,
		.pte		= 0,
	}, {
		.policy		= "buffered",
		.cr_mask	= CR_C,
		.pmd		= PMD_SECT_BUFFERED,
		.pte		= PTE_BUFFERABLE,
	}, {
		.policy		= "writethrough",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WT,
		.pte		= PTE_CACHEABLE,
	}, {
		.policy		= "writeback",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WB,
		.pte		= PTE_BUFFERABLE|PTE_CACHEABLE,
	}, {
		.policy		= "writealloc",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WBWA,
		.pte		= PTE_BUFFERABLE|PTE_CACHEABLE,
	}
};

/*
 * These are useful for identifing cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static void __init early_cachepolicy(char **p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_policies); i++) {
		int len = strlen(cache_policies[i].policy);

		if (memcmp(*p, cache_policies[i].policy, len) == 0) {
			cachepolicy = i;
			cr_alignment &= ~cache_policies[i].cr_mask;
			cr_no_alignment &= ~cache_policies[i].cr_mask;
			*p += len;
			break;
		}
	}
	if (i == ARRAY_SIZE(cache_policies))
		printk(KERN_ERR "ERROR: unknown or unsupported cache policy\n");
	flush_cache_all();
	set_cr(cr_alignment);
}
__early_param("cachepolicy=", early_cachepolicy);

static void __init early_nocache(char **__unused)
{
	char *p = "buffered";
	printk(KERN_WARNING "nocache is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}
__early_param("nocache", early_nocache);

static void __init early_nowrite(char **__unused)
{
	char *p = "uncached";
	printk(KERN_WARNING "nowb is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}
__early_param("nowb", early_nowrite);

static void __init early_ecc(char **p)
{
	if (memcmp(*p, "on", 2) == 0) {
		ecc_mask = PMD_PROTECTION;
		*p += 2;
	} else if (memcmp(*p, "off", 3) == 0) {
		ecc_mask = 0;
		*p += 3;
	}
}
__early_param("ecc=", early_ecc);

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~CR_A;
	cr_no_alignment &= ~CR_A;
	set_cr(cr_alignment);
	return 1;
}
__setup("noalign", noalign_setup);

struct mem_types {
	unsigned int	prot_pte;
	unsigned int	prot_l1;
	unsigned int	prot_sect;
	unsigned int	domain;
};

static struct mem_types mem_types[] __initdata = {
	[MT_DEVICE] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_WRITE,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_UNCACHED |
				PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_IO,
	},
	[MT_CACHECLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MINICLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_MINICACHE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_LOW_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_EXEC,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_HIGH_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_USER | L_PTE_EXEC,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_MEMORY] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_ROM] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_IXP2000_DEVICE] = { /* IXP2400 requires XCB=101 for on-chip I/O */
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_WRITE,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_UNCACHED |
				PMD_SECT_AP_WRITE | PMD_SECT_BUFFERABLE |
				PMD_SECT_TEX(1),
		.domain    = DOMAIN_IO,
	},
	[MT_NONSHARED_DEVICE] = {
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_NONSHARED_DEV |
				PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_IO,
	}
};

/*
 * Adjust the PMD section entries according to the CPU in use.
 */
static void __init build_mem_type_table(void)
{
	struct cachepolicy *cp;
	unsigned int cr = get_cr();
	unsigned int user_pgprot, kern_pgprot;
	int cpu_arch = cpu_architecture();
	int i;

#if defined(CONFIG_CPU_DCACHE_DISABLE)
	if (cachepolicy > CPOLICY_BUFFERED)
		cachepolicy = CPOLICY_BUFFERED;
#elif defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
	if (cachepolicy > CPOLICY_WRITETHROUGH)
		cachepolicy = CPOLICY_WRITETHROUGH;
#endif
	if (cpu_arch < CPU_ARCH_ARMv5) {
		if (cachepolicy >= CPOLICY_WRITEALLOC)
			cachepolicy = CPOLICY_WRITEBACK;
		ecc_mask = 0;
	}

	/*
	 * Xscale must not have PMD bit 4 set for section mappings.
	 */
	if (cpu_is_xscale())
		for (i = 0; i < ARRAY_SIZE(mem_types); i++)
			mem_types[i].prot_sect &= ~PMD_BIT4;

	/*
	 * ARMv5 and lower, excluding Xscale, bit 4 must be set for
	 * page tables.
	 */
	if (cpu_arch < CPU_ARCH_ARMv6 && !cpu_is_xscale())
		for (i = 0; i < ARRAY_SIZE(mem_types); i++)
			if (mem_types[i].prot_l1)
				mem_types[i].prot_l1 |= PMD_BIT4;

	cp = &cache_policies[cachepolicy];
	kern_pgprot = user_pgprot = cp->pte;

	/*
	 * Enable CPU-specific coherency if supported.
	 * (Only available on XSC3 at the moment.)
	 */
	if (arch_is_coherent()) {
		if (cpu_is_xsc3()) {
			mem_types[MT_MEMORY].prot_sect |= PMD_SECT_S;
			mem_types[MT_MEMORY].prot_pte |= L_PTE_SHARED;
		}
	}

	/*
	 * ARMv6 and above have extended page tables.
	 */
	if (cpu_arch >= CPU_ARCH_ARMv6 && (cr & CR_XP)) {
		/*
		 * bit 4 becomes XN which we must clear for the
		 * kernel memory mapping.
		 */
		mem_types[MT_MEMORY].prot_sect &= ~PMD_SECT_XN;
		mem_types[MT_ROM].prot_sect &= ~PMD_SECT_XN;

		/*
		 * Mark cache clean areas and XIP ROM read only
		 * from SVC mode and no access from userspace.
		 */
		mem_types[MT_ROM].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_MINICLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;

		/*
		 * Mark the device area as "shared device"
		 */
		mem_types[MT_DEVICE].prot_pte |= L_PTE_BUFFERABLE;
		mem_types[MT_DEVICE].prot_sect |= PMD_SECT_BUFFERED;

		/*
		 * User pages need to be mapped with the ASID
		 * (iow, non-global)
		 */
		user_pgprot |= L_PTE_ASID;

#ifdef CONFIG_SMP
		/*
		 * Mark memory with the "shared" attribute for SMP systems
		 */
		user_pgprot |= L_PTE_SHARED;
		kern_pgprot |= L_PTE_SHARED;
		mem_types[MT_MEMORY].prot_sect |= PMD_SECT_S;
#endif
	}

	for (i = 0; i < 16; i++) {
		unsigned long v = pgprot_val(protection_map[i]);
		v = (v & ~(L_PTE_BUFFERABLE|L_PTE_CACHEABLE)) | user_pgprot;
		protection_map[i] = __pgprot(v);
	}

	mem_types[MT_LOW_VECTORS].prot_pte |= kern_pgprot;
	mem_types[MT_HIGH_VECTORS].prot_pte |= kern_pgprot;

	if (cpu_arch >= CPU_ARCH_ARMv5) {
#ifndef CONFIG_SMP
		/*
		 * Only use write-through for non-SMP systems
		 */
		mem_types[MT_LOW_VECTORS].prot_pte &= ~L_PTE_BUFFERABLE;
		mem_types[MT_HIGH_VECTORS].prot_pte &= ~L_PTE_BUFFERABLE;
#endif
	} else {
		mem_types[MT_MINICLEAN].prot_sect &= ~PMD_SECT_TEX(1);
	}

	pgprot_kernel = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG |
				 L_PTE_DIRTY | L_PTE_WRITE |
				 L_PTE_EXEC | kern_pgprot);

	mem_types[MT_LOW_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_HIGH_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_MEMORY].prot_sect |= ecc_mask | cp->pmd;
	mem_types[MT_ROM].prot_sect |= cp->pmd;

	switch (cp->pmd) {
	case PMD_SECT_WT:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WT;
		break;
	case PMD_SECT_WB:
	case PMD_SECT_WBWA:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WB;
		break;
	}
	printk("Memory policy: ECC %sabled, Data cache %s\n",
		ecc_mask ? "en" : "dis", cp->policy);
}

#define vectors_base()	(vectors_high() ? 0xffff0000 : 0)

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  This operates on half-
 * pgdir entry increments.
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t *pmdp = pmd_off_k(virt);

	if (virt & (1 << 20))
		pmdp++;

	*pmdp = __pmd(phys | prot);
	flush_pmd_entry(pmdp);
}

/*
 * Create a SUPER SECTION PGD between VIRT and PHYS with protection PROT
 */
static inline void
alloc_init_supersection(unsigned long virt, unsigned long phys, int prot)
{
	int i;

	for (i = 0; i < 16; i += 1) {
		alloc_init_section(virt, phys, prot | PMD_SECT_SUPER);

		virt += (PGDIR_SIZE / 2);
	}
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  Note that due to the
 * way we map the PTEs, we must allocate two PTE_SIZE'd
 * blocks - one for the Linux pte table, and one for
 * the hardware pte table.
 */
static inline void
alloc_init_page(unsigned long virt, unsigned long phys, unsigned int prot_l1, pgprot_t prot)
{
	pmd_t *pmdp = pmd_off_k(virt);
	pte_t *ptep;

	if (pmd_none(*pmdp)) {
		ptep = alloc_bootmem_low_pages(2 * PTRS_PER_PTE *
					       sizeof(pte_t));

		__pmd_populate(pmdp, __pa(ptep) | prot_l1);
	}
	ptep = pte_offset_kernel(pmdp, virt);

	set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
}

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections and
 * supersections.
 */
void __init create_mapping(struct map_desc *md)
{
	unsigned long virt, length;
	int prot_sect, prot_l1, domain;
	pgprot_t prot_pte;
	unsigned long off = (u32)__pfn_to_phys(md->pfn);

	if (md->virtual != vectors_base() && md->virtual < TASK_SIZE) {
		printk(KERN_WARNING "BUG: not creating mapping for "
		       "0x%08llx at 0x%08lx in user region\n",
		       __pfn_to_phys((u64)md->pfn), md->virtual);
		return;
	}

	if ((md->type == MT_DEVICE || md->type == MT_ROM) &&
	    md->virtual >= PAGE_OFFSET && md->virtual < VMALLOC_END) {
		printk(KERN_WARNING "BUG: mapping for 0x%08llx at 0x%08lx "
		       "overlaps vmalloc space\n",
		       __pfn_to_phys((u64)md->pfn), md->virtual);
	}

	domain	  = mem_types[md->type].domain;
	prot_pte  = __pgprot(mem_types[md->type].prot_pte);
	prot_l1   = mem_types[md->type].prot_l1 | PMD_DOMAIN(domain);
	prot_sect = mem_types[md->type].prot_sect | PMD_DOMAIN(domain);

	/*
	 * Catch 36-bit addresses
	 */
	if(md->pfn >= 0x100000) {
		if(domain) {
			printk(KERN_ERR "MM: invalid domain in supersection "
				"mapping for 0x%08llx at 0x%08lx\n",
				__pfn_to_phys((u64)md->pfn), md->virtual);
			return;
		}
		if((md->virtual | md->length | __pfn_to_phys(md->pfn))
			& ~SUPERSECTION_MASK) {
			printk(KERN_ERR "MM: cannot create mapping for "
				"0x%08llx at 0x%08lx invalid alignment\n",
				__pfn_to_phys((u64)md->pfn), md->virtual);
			return;
		}

		/*
		 * Shift bits [35:32] of address into bits [23:20] of PMD
		 * (See ARMv6 spec).
		 */
		off |= (((md->pfn >> (32 - PAGE_SHIFT)) & 0xF) << 20);
	}

	virt   = md->virtual;
	off   -= virt;
	length = md->length;

	if (mem_types[md->type].prot_l1 == 0 &&
	    (virt & 0xfffff || (virt + off) & 0xfffff || (virt + length) & 0xfffff)) {
		printk(KERN_WARNING "BUG: map for 0x%08lx at 0x%08lx can not "
		       "be mapped using pages, ignoring.\n",
		       __pfn_to_phys(md->pfn), md->virtual);
		return;
	}

	while ((virt & 0xfffff || (virt + off) & 0xfffff) && length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	/* N.B.	ARMv6 supersections are only defined to work with domain 0.
	 *	Since domain assignments can in fact be arbitrary, the
	 *	'domain == 0' check below is required to insure that ARMv6
	 *	supersections are only allocated for domain 0 regardless
	 *	of the actual domain assignments in use.
	 */
	if ((cpu_architecture() >= CPU_ARCH_ARMv6 || cpu_is_xsc3())
		&& domain == 0) {
		/*
		 * Align to supersection boundary if !high pages.
		 * High pages have already been checked for proper
		 * alignment above and they will fail the SUPSERSECTION_MASK
		 * check because of the way the address is encoded into
		 * offset.
		 */
		if (md->pfn <= 0x100000) {
			while ((virt & ~SUPERSECTION_MASK ||
			        (virt + off) & ~SUPERSECTION_MASK) &&
				length >= (PGDIR_SIZE / 2)) {
				alloc_init_section(virt, virt + off, prot_sect);

				virt   += (PGDIR_SIZE / 2);
				length -= (PGDIR_SIZE / 2);
			}
		}

		while (length >= SUPERSECTION_SIZE) {
			alloc_init_supersection(virt, virt + off, prot_sect);

			virt   += SUPERSECTION_SIZE;
			length -= SUPERSECTION_SIZE;
		}
	}

	/*
	 * A section mapping covers half a "pgdir" entry.
	 */
	while (length >= (PGDIR_SIZE / 2)) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += (PGDIR_SIZE / 2);
		length -= (PGDIR_SIZE / 2);
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		create_mapping(io_desc + i);
}

static inline void prepare_page_table(struct meminfo *mi)
{
	unsigned long addr;

	/*
	 * Clear out all the mappings below the kernel image.
	 */
	for (addr = 0; addr < MODULE_START; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

#ifdef CONFIG_XIP_KERNEL
	/* The XIP kernel is mapped in the module area -- skip over it */
	addr = ((unsigned long)&_etext + PGDIR_SIZE - 1) & PGDIR_MASK;
#endif
	for ( ; addr < PAGE_OFFSET; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

	/*
	 * Clear out all the kernel space mappings, except for the first
	 * memory bank, up to the end of the vmalloc region.
	 */
	for (addr = __phys_to_virt(mi->bank[0].start + mi->bank[0].size);
	     addr < VMALLOC_END; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));
}

/*
 * Reserve the various regions of node 0
 */
void __init reserve_node_zero(pg_data_t *pgdat)
{
	unsigned long res_size = 0;

	/*
	 * Register the kernel text and data with bootmem.
	 * Note that this can only be in node 0.
	 */
#ifdef CONFIG_XIP_KERNEL
	reserve_bootmem_node(pgdat, __pa(&__data_start), &_end - &__data_start);
#else
	reserve_bootmem_node(pgdat, __pa(&_stext), &_end - &_stext);
#endif

	/*
	 * Reserve the page tables.  These are already in use,
	 * and can only be in node 0.
	 */
	reserve_bootmem_node(pgdat, __pa(swapper_pg_dir),
			     PTRS_PER_PGD * sizeof(pgd_t));

	/*
	 * Hmm... This should go elsewhere, but we really really need to
	 * stop things allocating the low memory; ideally we need a better
	 * implementation of GFP_DMA which does not assume that DMA-able
	 * memory starts at zero.
	 */
	if (machine_is_integrator() || machine_is_cintegrator())
		res_size = __pa(swapper_pg_dir) - PHYS_OFFSET;

	/*
	 * These should likewise go elsewhere.  They pre-reserve the
	 * screen memory region at the start of main system memory.
	 */
	if (machine_is_edb7211())
		res_size = 0x00020000;
	if (machine_is_p720t())
		res_size = 0x00014000;

#ifdef CONFIG_SA1111
	/*
	 * Because of the SA1111 DMA bug, we want to preserve our
	 * precious DMA-able memory...
	 */
	res_size = __pa(swapper_pg_dir) - PHYS_OFFSET;
#endif
	if (res_size)
		reserve_bootmem_node(pgdat, PHYS_OFFSET, res_size);
}

/*
 * Set up device the mappings.  Since we clear out the page tables for all
 * mappings above VMALLOC_END, we will remove any debug device mappings.
 * This means you have to be careful how you debug this function, or any
 * called function.  This means you can't use any function or debugging
 * method which may touch any device, otherwise the kernel _will_ crash.
 */
static void __init devicemaps_init(struct machine_desc *mdesc)
{
	struct map_desc map;
	unsigned long addr;
	void *vectors;

	/*
	 * Allocate the vector page early.
	 */
	vectors = alloc_bootmem_low_pages(PAGE_SIZE);
	BUG_ON(!vectors);

	for (addr = VMALLOC_END; addr; addr += PGDIR_SIZE)
		pmd_clear(pmd_off_k(addr));

	/*
	 * Map the kernel if it is XIP.
	 * It is always first in the modulearea.
	 */
#ifdef CONFIG_XIP_KERNEL
	map.pfn = __phys_to_pfn(CONFIG_XIP_PHYS_ADDR & SECTION_MASK);
	map.virtual = MODULE_START;
	map.length = ((unsigned long)&_etext - map.virtual + ~SECTION_MASK) & SECTION_MASK;
	map.type = MT_ROM;
	create_mapping(&map);
#endif

	/*
	 * Map the cache flushing regions.
	 */
#ifdef FLUSH_BASE
	map.pfn = __phys_to_pfn(FLUSH_BASE_PHYS);
	map.virtual = FLUSH_BASE;
	map.length = SZ_1M;
	map.type = MT_CACHECLEAN;
	create_mapping(&map);
#endif
#ifdef FLUSH_BASE_MINICACHE
	map.pfn = __phys_to_pfn(FLUSH_BASE_PHYS + SZ_1M);
	map.virtual = FLUSH_BASE_MINICACHE;
	map.length = SZ_1M;
	map.type = MT_MINICLEAN;
	create_mapping(&map);
#endif

	/*
	 * Create a mapping for the machine vectors at the high-vectors
	 * location (0xffff0000).  If we aren't using high-vectors, also
	 * create a mapping at the low-vectors virtual address.
	 */
	map.pfn = __phys_to_pfn(virt_to_phys(vectors));
	map.virtual = 0xffff0000;
	map.length = PAGE_SIZE;
	map.type = MT_HIGH_VECTORS;
	create_mapping(&map);

	if (!vectors_high()) {
		map.virtual = 0;
		map.type = MT_LOW_VECTORS;
		create_mapping(&map);
	}

	/*
	 * Ask the machine support to map in the statically mapped devices.
	 */
	if (mdesc->map_io)
		mdesc->map_io();

	/*
	 * Finally flush the caches and tlb to ensure that we're in a
	 * consistent state wrt the writebuffer.  This also ensures that
	 * any write-allocated cache lines in the vector page are written
	 * back.  After this point, we can start to touch devices again.
	 */
	local_flush_tlb_all();
	flush_cache_all();
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(struct meminfo *mi, struct machine_desc *mdesc)
{
	void *zero_page;

	build_mem_type_table();
	prepare_page_table(mi);
	bootmem_init(mi);
	devicemaps_init(mdesc);

	top_pmd = pmd_off_k(0xffff0000);

	/*
	 * allocate the zero page.  Note that we count on this going ok.
	 */
	zero_page = alloc_bootmem_low_pages(PAGE_SIZE);
	memzero(zero_page, PAGE_SIZE);
	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	unsigned long base_pmdval;
	pgd_t *pgd;
	int i;

	if (current->mm && current->mm->pgd)
		pgd = current->mm->pgd;
	else
		pgd = init_mm.pgd;

	base_pmdval = PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | PMD_TYPE_SECT;
	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale())
		base_pmdval |= PMD_BIT4;

	for (i = 0; i < FIRST_USER_PGD_NR + USER_PTRS_PER_PGD; i++, pgd++) {
		unsigned long pmdval = (i << PGDIR_SHIFT) | base_pmdval;
		pmd_t *pmd;

		pmd = pmd_off(pgd, i << PGDIR_SHIFT);
		pmd[0] = __pmd(pmdval);
		pmd[1] = __pmd(pmdval + (1 << (PGDIR_SHIFT - 1)));
		flush_pmd_entry(pmd);
	}
}
